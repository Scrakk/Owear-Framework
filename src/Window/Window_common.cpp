// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/Window_common.cpp — lógica común de Window (3 plataformas).
//
// F3.2: outbox — todos los _apply/_event se encolan y salen en UN solo
//       eval por tick del main loop (crítico durante resize storms).
// F3.4: BeginCloseFlow — veto nativo → aviso JS+SDK con requestId →
//       ventana de 300 ms para responder → destroy.
//
#include "Window_p.hpp"
#include "../Bridge/Dispatcher.hpp"
#include "../Core/App.hpp"
#include "../Control/ControlServer.hpp"
#include "../Core/Log.hpp"
#include "ow/detail/minjson.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>

namespace ow {

namespace {

/// Envuelve JS arbitrario para recibir el resultado como JSON string.
std::string WrapEval(const std::string& js) {
    return "(function(){ try { return JSON.stringify((" + js + ")); } catch(e) { "
           "return JSON.stringify({owError: String(e)}); } })()";
}

int CloseVetoTimeoutMs() {
    if (const char* v = std::getenv("OW_CLOSE_TIMEOUT_MS")) {
        int n = atoi(v);
        if (n > 0) return n;
    }
    return 1000; // default: 1 s para responder desde JS
}

} // namespace

// ── ciclo de vida ────────────────────────────────────────────────────────────

Window::Impl::Impl(Window* self, const WindowOptions& opts)
    : self(self), opts(opts) {}

void Window::Impl::InitCommon() {
    webview = CreateWebviewBackend();
    if (!webview) {
        log::Error("window", "no se pudo crear el backend de webview");
        return;
    }

    // PCreate crea la ventana nativa Y llama webview->Create(parent).
    if (!PCreate()) {
        log::Error("window", "PCreate falló");
        return;
    }

    // Scripts de inicio: corren en orden, ANTES de cualquier script de página.
    webview->InjectInitScript(BuildBridgeScript());
    webview->InjectInitScript("window.__owWindowId=" + std::to_string(id) + ";");

    webview->SetMessageHandler([this](std::string_view text) {
        HandleWebViewMessage(text);
    });

    if (!opts.url.empty()) webview->LoadURL(opts.url);
    if (opts.show) PShow();
}

// ── mensajes del WebView ─────────────────────────────────────────────────────

void Window::Impl::HandleWebViewMessage(std::string_view text) {
    bridge::Message msg;
    if (!bridge::DecodeMessage(text, msg)) {
        log::Warn("bridge", "mensaje inválido desde JS");
        return;
    }

    if (msg.type == bridge::MsgType::Invoke) {
        if (msg.module == "ow-window") {
            HandleInternalInvoke(msg);
            return;
        }

        ow_request_t req{};
        req.json = msg.json.c_str();
        req.json_len = static_cast<uint32_t>(msg.json.size());
        req.bin = msg.bin.empty() ? nullptr : msg.bin.data();
        req.bin_len = static_cast<uint32_t>(msg.bin.size());

        ow_response_t res{};
        Dispatcher::Get().Execute(msg.window == 0 ? id : msg.window,
                                  msg.module, msg.method, &req, &res);

        std::string resultJson;
        bool ok = res.status == 0;
        if (!ok) {
            json::Object errObj;
            errObj.emplace_back("message",
                                json::Value(res.error ? std::string(res.error) : "error"));
            resultJson = json::Value(std::move(errObj)).Serialize();
        } else {
            resultJson.assign(res.json, res.json_len);
        }
        // Apply DIRECTO (no batched): los awaits de promesas en el renderer
        // dependen de esta resolución inmediata; batchearla puede deadlockear
        // cuando el propio evaluate espera la promesa.
        std::string js = "window.__ow && window.__ow._apply(" +
                         std::to_string(msg.id) + ',' + (ok ? "true" : "false") + ',' +
                         json::JsLiteral(resultJson) + ")";
        if (webview) webview->EvalJS(js);
        return;
    }

    if (msg.type == bridge::MsgType::Event) {
        // IPC dirigido: to != 0 y distinta de esta ventana → enrutar al destino
        if (msg.to != 0 && msg.to != id) {
            auto it = LiveWindows().find(msg.to);
            if (it != LiveWindows().end())
                it->second->EmitToJS(msg.name, msg.json);
            return;
        }
        FireEvent(msg.name, msg.json);
    }
}

void Window::Impl::HandleInternalInvoke(const bridge::Message& msg) {
    auto respond = [&](bool ok, std::string json) {
        EnqueueOp({OpKind::Apply, std::to_string(msg.id), ok ? "true" : "false", json});
        ScheduleFlush(this);
    };
    auto parsed = json::Parse(std::string_view(msg.json));
    json::Value args = parsed.value ? std::move(*parsed.value) : json::Value(nullptr);

    if (msg.method == "beginMoveDrag") {
        PBeginMoveDrag();
        respond(true, "null");
    } else if (msg.method == "beginResizeDrag") {
        std::string edge = "bottom-right";
        if (const json::Value* v = args.Find("0"); v && v->IsString()) edge = v->AsString();
        PBeginResizeDrag(edge);
        respond(true, "null");
    } else {
        respond(false, std::string("{\"message\":\"ow-window: función desconocida ") +
                           msg.method + "\"}");
    }
}

// ── eventos ──────────────────────────────────────────────────────────────────

void Window::Impl::FireEvent(const std::string& name, std::string_view payloadJson) {
    auto it = listeners.find(name);
    if (it == listeners.end()) return;
    for (auto& l : it->second) l.fn(payloadJson);
}

ListenerId Window::On(const std::string& name, std::function<void(EventPayload)> cb) {
    auto& ls = impl_->listeners[name];
    ListenerId id = impl_->nextListenerId++;
    ls.push_back({id, std::move(cb)});
    return id;
}

void Window::Off(ListenerId id) {
    for (auto& [name, vec] : impl_->listeners)
        vec.erase(std::remove_if(vec.begin(), vec.end(),
                                 [id](const auto& l) { return l.id == id; }),
                  vec.end());
}

void Window::EmitToJS(const std::string& name, std::string_view jsonPayload) {
    impl_->EnqueueOp({Impl::OpKind::Event, std::to_string(impl_->id),
                      std::string(name), std::string(jsonPayload)});
    Impl::ScheduleFlush(impl_.get());
}

// ── F3.2 outbox ──────────────────────────────────────────────────────────────

void Window::Impl::EmitPlatformEvent(Impl* impl, const std::string& name,
                                     std::string_view payloadJson) {
    // listeners nativos inmediatos (sin batch: son C++ locales)
    impl->FireEvent(name, payloadJson);
    // JS vía outbox
    impl->EnqueueOp({Impl::OpKind::Event, std::to_string(impl->id),
                     name, std::string(payloadJson)});
    Impl::ScheduleFlush(impl);
}

void Window::Impl::EnqueueOp(Op op) {
    std::lock_guard lock(outboxMu);
    outbox.push_back(std::move(op));
}

void Window::Impl::ScheduleFlush(Impl* impl) {
    {
        std::lock_guard lock(impl->outboxMu);
        if (impl->outboxScheduled) return;
        impl->outboxScheduled = true;
    }
    auto aliveWeak = std::weak_ptr<std::atomic<bool>>(impl->alive);
    App::Post([aliveWeak, impl] {
        if (auto a = aliveWeak.lock(); a && a->load())
            FlushOutbox(impl);
    });
}

void Window::Impl::FlushOutbox(Impl* impl) {
    if (!impl->webview) return;
    std::vector<Op> batch;
    {
        std::lock_guard lock(impl->outboxMu);
        impl->outboxScheduled = false;
        batch.swap(impl->outbox);
    }
    if (batch.empty()) return;

    // un eval con __ow._batch([[kind,...],...])
    std::string script = "window.__ow && window.__ow._batch([";
    for (size_t i = 0; i < batch.size(); ++i) {
        const Op& op = batch[i];
        if (i) script += ',';
        if (op.kind == OpKind::Apply) {
            script += "[\"a\"," + op.a + ',' + op.b + ',' +
                      json::JsLiteral(op.c) + ']';
        } else {
            script += "[\"e\"," + op.a + ',' + json::JsLiteral(op.b) + ',' +
                      json::JsLiteral(op.c) + ']';
        }
    }
    script += "])";
    impl->webview->EvalJS(script);
}

// ── F3.4 flujo de cierre con veto JS ─────────────────────────────────────────

bool Window::Impl::BeginCloseFlow() {
    // ya hay una petición en vuelo → el timeout decidirá
    if (jsCloseRequestId != 0) return false;

    jsCloseRequestId = ++jsCloseSeq;
    jsCloseResponded = false;

    // payload único con requestId: nativo (veto) + JS + SDK
    json::Object payload;
    payload.emplace_back("requestId",
                         json::Value(static_cast<int64_t>(jsCloseRequestId)));
    std::string pj = json::Value(std::move(payload)).Serialize();

    FireEvent("closeRequested", pj); // nativos pueden marcar closeRequestVeto
    if (closeRequestVeto) {
        jsCloseRequestId = 0;
        return false;
    }
    EnqueueOp({OpKind::Event, std::to_string(id), "closeRequested", pj});
    ScheduleFlush(this);

    // timer de seguridad: sin respuesta → cerrar igualmente
    auto aliveWeak = std::weak_ptr<std::atomic<bool>>(alive);
    internal::PlatformDelay(CloseVetoTimeoutMs(), [this, aliveWeak] {
        if (auto a = aliveWeak.lock(); a && a->load()) CloseTimerFired(this);
    });
    return false; // el cierre real lo decide RespondJsClose o el timer
}

void Window::Impl::CloseTimerFired(Window::Impl* impl) {
    if (!impl->jsCloseResponded && impl->jsCloseRequestId != 0) {
        log::Debug("window", "close sin respuesta JS → cerrando (timeout)");
        impl->self->Destroy();
    }
}

void Window::Impl::RespondJsClose(uint64_t requestId, bool allow) {
    if (requestId != jsCloseRequestId || jsCloseResponded) return;
    jsCloseResponded = true;
    jsCloseRequestId = 0;
    if (allow) self->Destroy();
    // allow=false → cancelado; la ventana sigue viva
}

// ── delegación pública común ─────────────────────────────────────────────────

Window::Window(const WindowOptions& options) : impl_(new Impl(this, options)) {
    static std::atomic<uint32_t> s_next{1};
    impl_->id = s_next.fetch_add(1);
    impl_->InitCommon();
}

WindowId Window::Id() const { return impl_->id; }
void Window::Show() { impl_->PShow(); }
void Window::Hide() { impl_->PHide(); }
void Window::Focus() { impl_->PFocus(); }
void Window::Minimize() { impl_->PMinimize(); }
void Window::Maximize() { impl_->PMaximize(); }
void Window::Unmaximize() { impl_->PUnmaximize(); }
void Window::Restore() { impl_->PRestore(); }
void Window::SetFullScreen(bool e) { impl_->PSetFullScreen(e); }
bool Window::IsMaximized() const { return impl_->PIsMaximized(); }
bool Window::IsMinimized() const { return impl_->PIsMinimized(); }
bool Window::IsFullScreen() const { return impl_->PIsFullScreen(); }
Window::Bounds Window::GetBounds() const { return impl_->PGetBounds(); }
void Window::SetBounds(const Bounds& b) { impl_->PSetBounds(b); }
void Window::Center() { impl_->PCenter(); }
void Window::SetTitle(const std::string& t) { impl_->PSetTitle(t); }
std::string Window::Title() const { return impl_->PGetTitle(); }
void Window::SetTitleBarStyle(TitleBarStyle s) {
    impl_->opts.titleBarStyle = s;
    impl_->PApplyTitleBar();
}
void Window::SetTitleBarOverlay(const TitleBarOverlay& o) {
    impl_->opts.titleBarOverlay = o;
    impl_->PApplyTitleBar();
}
void* Window::NativeHandle() const {
    return impl_->webview ? impl_->webview->NativeWidget() : nullptr;
}

void Window::Close() { impl_->BeginCloseFlow(); }
void Window::Destroy() { impl_->PDestroy(); }

void Window::LoadURL(const std::string& url) {
    impl_->opts.url = url;
    impl_->webview->LoadURL(url);
}

void Window::EvalJS(const std::string& js,
                    std::function<void(std::string_view)> callback) {
    impl_->webview->EvalJS(WrapEval(js),
                           [callback](std::string_view result, bool ok) {
                               if (callback) callback(ok ? result : std::string_view("null"));
                           });
}

} // namespace ow
