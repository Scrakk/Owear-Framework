// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Control/ControlServer.cpp — lógica común de comandos.
// Transporte por plataforma en ControlServer_<platform>.cpp.
//
#include "ControlServer.hpp"

#include "../Core/App.hpp"
#include "../Window/Window_p.hpp"
#include "../Core/Log.hpp"
#include "../Runtime/NodeManager.hpp"
#include "ow/App.h"
#include "ow/Window.h"
#include "ow/detail/minjson.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ow {

using V = json::Value;

uint32_t CurrentPid() {
#ifdef _WIN32
    return static_cast<uint32_t>(GetCurrentProcessId());
#else
    return static_cast<uint32_t>(getpid());
#endif
}

std::map<WindowId, Window*>& LiveWindows() {
    static std::map<WindowId, Window*> m;
    return m;
}

// ControlServer::Get() se define por plataforma (ControlServer_<plat>.cpp)
// porque devuelve la subclase concreta con el transporte.

void ControlServer::SendLine(uint64_t clientId, std::string_view line) {
    PlatformSend(clientId, line);
}

void ControlServer::SendResponse(uint64_t clientId, uint64_t id, bool ok,
                                 std::string_view resultJson, std::string_view error) {
    std::string out = "{\"id\":" + std::to_string(id) + ",\"ok\":";
    out += ok ? "true" : "false";
    if (ok) {
        out += ",\"result\":";
        out.append(resultJson.empty() ? "null" : resultJson);
    } else {
        out += ",\"error\":";
        out += json::Value(std::string(error)).Serialize();
    }
    out += "}";
    SendLine(clientId, out);
}

void ControlServer::BroadcastEvent(const std::string& name, std::string_view paramsJson) {
    std::string line = "{\"event\":";
    line += json::Value(name).Serialize();
    line += ",\"params\":";
    line.append(paramsJson.empty() ? "null" : paramsJson);
    line += "}";
    SendLine(0, line);
}

void ControlServer::HandleClientDisconnected(uint64_t) {}

void ControlServer::WireWindowEvents(WindowId id, Window* w) {
    auto forward = [this, id](const std::string& name) {
        return [this, id, name](std::string_view payload) {
            json::Object params;
            params.emplace_back("windowId", json::Value(static_cast<int64_t>(id)));
            params.emplace_back("name", json::Value(name));
            params.emplace_back(
                "payload",
                json::Value(json::Value(nullptr))); // placeholder reemplazado abajo
            // parsea payload para incrustarlo tal cual
            json::Value pv = json::Value(nullptr);
            if (auto p = json::Parse(payload); p.value) pv = std::move(*p.value);
            params.back().second = std::move(pv);
            BroadcastEvent("window.event",
                           json::Value(std::move(params)).Serialize());
        };
    };

    w->On("resize", forward("resize"));
    w->On("move", forward("move"));
    w->On("focus", forward("focus"));
    w->On("blur", forward("blur"));
    w->On("maximize", forward("maximize"));
    w->On("unmaximize", forward("unmaximize"));
    w->On("enterFullScreen", forward("enterFullScreen"));
    w->On("leaveFullScreen", forward("leaveFullScreen"));
    // navegación (F-next)
    w->On("navigationStarted", forward("navigationStarted"));
    w->On("loadCommitted", forward("loadCommitted"));
    w->On("didFinishLoad", forward("didFinishLoad"));
    w->On("didFailLoad", forward("didFailLoad"));
    w->On("pageTitleUpdated", forward("pageTitleUpdated"));
    // F3.4: closeRequested se reenvía al SDK con requestId; el kernel
    // decide con window.respondCloseRequest o el timeout de 300 ms.
    w->On("closed", [this, id](std::string_view) {
        auto it = LiveWindows().find(id);
        if (it == LiveWindows().end()) return;
        Window* dead = it->second;
        LiveWindows().erase(it);
        json::Object params;
        params.emplace_back("windowId", json::Value(static_cast<int64_t>(id)));
        params.emplace_back("name", json::Value("closed"));
        params.emplace_back("payload", json::Value(nullptr));
        BroadcastEvent("window.event", json::Value(std::move(params)).Serialize());
        // destruye el objeto C++ fuera del signal handler de GTK
        App::Post([dead] { delete dead; });
    });
}

bool ControlServer::Start() {
    if (started_) return true;
    if (!PlatformListen()) return false;
    started_ = true;
    return true;
}

void ControlServer::Stop() {
    if (!started_) return;
    PlatformStop();
    started_ = false;
}

std::string ControlServer::SocketPath() const { return socketPath_; }

bool ControlServer::HandleCommand(uint64_t clientId, uint64_t id,
                                  const std::string& cmd,
                                  std::string_view paramsJson,
                                  std::string& resultJson, std::string& error) {
    auto parsed = json::Parse(paramsJson);
    V params = parsed.value ? std::move(*parsed.value) : V(json::Object{});

    auto getWindow = [&](Window** out) -> bool {
        const V* wid = params.IsArray() ? (params.AsArray().empty() ? nullptr : &params.AsArray()[0])
                                        : params.Find("windowId");
        if (!wid || !wid->IsNumber()) { error = "windowId requerido"; return false; }
        auto it = LiveWindows().find(static_cast<WindowId>(wid->AsInt()));
        if (it == LiveWindows().end()) { error = "ventana no encontrada"; return false; }
        *out = it->second;
        return true;
    };

    if (cmd == "app.info") {
        json::Object o;
        o.emplace_back("pid", V(CurrentPid()));
        o.emplace_back("version", V(OW_VERSION_STRING));
        o.emplace_back("socket", V(socketPath_));
        resultJson = V(std::move(o)).Serialize();
        return true;
    }

    if (cmd == "app.quit") {
        App::Quit(0);
        resultJson = "null";
        return true;
    }

    if (cmd == "node.ensure") {
        std::string range = "latest";
        if (const V* r = params.Find("range"); r && r->IsString()) range = r->AsString();
        auto node = NodeManager::Ensure(range);
        if (node.IsErr()) { error = node.Error(); return false; }
        json::Object o;
        o.emplace_back("path", V(node.Value().string()));
        resultJson = V(std::move(o)).Serialize();
        return true;
    }

    if (cmd == "window.create") {
        WindowOptions opts;
        if (const V* t = params.Find("title"); t && t->IsString()) opts.title = t->AsString();
        if (const V* v = params.Find("width"); v && v->IsNumber()) opts.width = (int)v->AsInt();
        if (const V* v = params.Find("height"); v && v->IsNumber()) opts.height = (int)v->AsInt();
        if (const V* v = params.Find("x"); v && v->IsNumber()) opts.center = false;
        if (const V* v = params.Find("resizable"); v && v->IsBool()) opts.resizable = v->AsBool();
        if (const V* v = params.Find("frameless"); v && v->IsBool()) opts.frameless = v->AsBool();
        if (opts.frameless) opts.titleBarStyle = TitleBarStyle::Hidden;
        if (const V* v = params.Find("titleBarStyle"); v && v->IsString()) {
            std::string s = v->AsString();
            if (s == "hidden") opts.titleBarStyle = TitleBarStyle::Hidden;
            else if (s == "custom") opts.titleBarStyle = TitleBarStyle::Custom;
            else opts.titleBarStyle = TitleBarStyle::Default;
        }
        if (const V* v = params.Find("url"); v && v->IsString()) opts.url = v->AsString();

        auto* win = new Window(opts);
        WindowId wid = win->Id();
        LiveWindows()[wid] = win;
        WireWindowEvents(wid, win);

        json::Object o;
        o.emplace_back("windowId", V(static_cast<int64_t>(wid)));
        resultJson = V(std::move(o)).Serialize();
        return true;
    }

    Window* w = nullptr;
    if (cmd.rfind("window.", 0) == 0 && cmd != "window.create") {
        if (!getWindow(&w)) return false;
    }

    if (cmd == "window.close") { w->Close(); resultJson = "null"; return true; }
    if (cmd == "window.destroy") {
        // destroy dispara 'closed' → limpia LiveWindows
        w->Destroy();
        resultJson = "null";
        return true;
    }
    if (cmd == "window.show") { w->Show(); resultJson = "null"; return true; }
    if (cmd == "window.hide") { w->Hide(); resultJson = "null"; return true; }
    if (cmd == "window.focus") { w->Focus(); resultJson = "null"; return true; }
    if (cmd == "window.minimize") { w->Minimize(); resultJson = "null"; return true; }
    if (cmd == "window.maximize") {
        bool target = true;
        if (const V* v = params.Find("enabled"); v && v->IsBool()) target = v->AsBool();
        target ? w->Maximize() : w->Unmaximize();
        resultJson = "null";
        return true;
    }
    if (cmd == "window.unmaximize") { w->Unmaximize(); resultJson = "null"; return true; }
    if (cmd == "window.setFullScreen") {
        bool enabled = false;
        if (const V* v = params.Find("enabled"); v && v->IsBool()) enabled = v->AsBool();
        w->SetFullScreen(enabled);
        resultJson = "null";
        return true;
    }
    if (cmd == "window.isMaximized") {
        resultJson = w->IsMaximized() ? "true" : "false";
        return true;
    }
    if (cmd == "window.isMinimized") {
        resultJson = w->IsMinimized() ? "true" : "false";
        return true;
    }
    if (cmd == "window.getBounds") {
        auto b = w->GetBounds();
        json::Object o;
        o.emplace_back("x", V(b.x));
        o.emplace_back("y", V(b.y));
        o.emplace_back("width", V(b.w));
        o.emplace_back("height", V(b.h));
        resultJson = V(std::move(o)).Serialize();
        return true;
    }
    if (cmd == "window.setBounds") {
        Window::Bounds b{};
        if (const V* v = params.Find("x"); v && v->IsNumber()) b.x = (int)v->AsInt();
        if (const V* v = params.Find("y"); v && v->IsNumber()) b.y = (int)v->AsInt();
        if (const V* v = params.Find("width"); v && v->IsNumber()) b.w = (int)v->AsInt();
        if (const V* v = params.Find("height"); v && v->IsNumber()) b.h = (int)v->AsInt();
        w->SetBounds(b);
        resultJson = "null";
        return true;
    }
    if (cmd == "window.setTitle") {
        const V* t = params.Find("title");
        if (!t || !t->IsString()) { error = "title requerido"; return false; }
        w->SetTitle(t->AsString());
        resultJson = "null";
        return true;
    }
    if (cmd == "window.loadURL") {
        const V* u = params.Find("url");
        if (!u || !u->IsString()) { error = "url requerida"; return false; }
        w->LoadURL(u->AsString());
        resultJson = "null";
        return true;
    }
    if (cmd == "window.respondCloseRequest") {
        const V* rid = params.Find("requestId");
        const V* allow = params.Find("allow");
        if (!rid || !rid->IsNumber() || !allow || !allow->IsBool()) {
            error = "requestId y allow requeridos";
            return false;
        }
        w->impl()->RespondJsClose(static_cast<uint64_t>(rid->AsInt()),
                                  allow->AsBool());
        resultJson = "null";
        return true;
    }
    if (cmd == "window.eval") {
        const V* js = params.Find("js");
        if (!js || !js->IsString()) { error = "js requerido"; return false; }
        // Respuesta asíncrona: se envía con el mismo id cuando llegue el callback
        w->EvalJS(js->AsString(), [this, clientId, id](std::string_view result) {
            bool ok = result.find("owError") == std::string::npos;
            SendResponse(clientId, id, ok, result, ok ? "" : "eval falló");
        });
        return true; // respuesta ya enviada (o pendiente) — evita doble send
    }

    error = "comando desconocido: " + cmd;
    return false;
}

void ControlServer::HandleLine(uint64_t clientId, std::string_view line) {
    if (line.empty()) return;
    auto parsed = json::Parse(line);
    if (!parsed.value || !parsed.value->IsObject()) {
        log::Warn("control", "línea inválida: " + std::string(line.substr(0, 120)));
        return;
    }
    const V& msg = *parsed.value;
    uint64_t reqId = 0;
    std::string cmd;
    std::string paramsJson = "{}";
    if (const V* v = msg.Find("id"); v && v->IsNumber()) reqId = (uint64_t)v->AsInt();
    if (const V* v = msg.Find("cmd"); v && v->IsString()) cmd = v->AsString();
    if (const V* v = msg.Find("params"); v) paramsJson = v->Serialize();

    if (cmd.empty()) return;

    // Eventos JS→nativo vía SDK (sin id)
    if (cmd == "event.emit") {
        // reenvía al renderer destino si trae windowId
        BroadcastEvent("sdk.event", paramsJson);
        return;
    }

    std::string resultJson;
    std::string error;
    bool ok = HandleCommand(clientId, reqId, cmd, paramsJson, resultJson, error);

    // window.eval responde async (evita duplicar)
    if (ok && cmd == "window.eval") return;

    SendResponse(clientId, reqId, ok, resultJson, error);
}

} // namespace ow
