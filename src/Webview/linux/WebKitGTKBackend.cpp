// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Webview/linux/WebKitGTKBackend.cpp — backend WebKitGTK 4.1.
// Todo en main thread (GTK).
//
// Schemes registrados por instancia:
//   app://    → archivos del directorio de assets (dist/)
//   ow-shm:// → regiones de memoria compartida SIN copia (F3)
//   ow-sync://→ canal síncrono invoke (XHR bloqueante, escape hatch F3)
//
#include "../IWebviewBackend.hpp"
#include "../../Bridge/Dispatcher.hpp"
#include "../../Bridge/Shm.hpp"
#include "ow/Bridge/Codec.h"
#include "../../Core/Log.hpp"
#include "ow/Base64.h"
#include "ow/detail/minjson.hpp"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <unordered_map>

namespace ow {

namespace {

const char* MimeFromExt(const std::string& ext) {
    static const std::unordered_map<std::string, const char*> k = {
        {".html", "text/html"},   {".htm", "text/html"},  {".js", "text/javascript"},
        {".mjs", "text/javascript"}, {".css", "text/css"},{".json", "application/json"},
        {".svg", "image/svg+xml"},{".png", "image/png"},  {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},  {".gif", "image/gif"},  {".webp", "image/webp"},
        {".ico", "image/x-icon"}, {".woff", "font/woff"}, {".woff2", "font/woff2"},
        {".ttf", "font/ttf"},     {".otf", "font/otf"},   {".map", "application/json"},
        {".txt", "text/plain"},   {".wasm", "application/wasm"},
    };
    auto it = k.find(ext);
    return it != k.end() ? it->second : "application/octet-stream";
}

/// Respuesta binaria sin copia: GBytes estático sobre memoria existente.
void FinishBytesStatic(WebKitURISchemeRequest* request,
                       const uint8_t* data, size_t len,
                       const char* mime,
                       const char* extraHeaderName = nullptr,
                       const char* extraHeaderValue = nullptr) {
#if WEBKIT_CHECK_VERSION(2, 40, 0)
    GBytes* bytes = g_bytes_new_static(data, len);
    WebKitURISchemeResponse* resp = webkit_uri_scheme_response_new(
        g_memory_input_stream_new_from_bytes(bytes),
        static_cast<gint64>(len));
    g_bytes_unref(bytes);
    webkit_uri_scheme_response_set_status(resp, 200, "OK");
    webkit_uri_scheme_response_set_content_type(resp, mime);
    if (extraHeaderName) {
        SoupMessageHeaders* headers = soup_message_headers_new(
            SOUP_MESSAGE_HEADERS_RESPONSE);
        soup_message_headers_append(headers, extraHeaderName,
                                    extraHeaderValue ? extraHeaderValue : "");
        webkit_uri_scheme_response_set_http_headers(resp, headers);
    }
    webkit_uri_scheme_request_finish_with_response(request, resp);
    g_object_unref(resp);
#else
    // fallback pre-2.40: copia única al GBytes (no ocurre en 4.1 moderno)
    auto* copy = new std::string(reinterpret_cast<const char*>(data), len);
    GBytes* bytes = g_bytes_new_with_free_func(
        copy->data(), copy->size(),
        [](gpointer p) { delete static_cast<std::string*>(p); }, copy);
    GInputStream* stream = g_memory_input_stream_new_from_bytes(bytes);
    g_bytes_unref(bytes);
    webkit_uri_scheme_request_finish(request, stream, -1, mime);
    g_object_unref(stream);
#endif
}

void FinishError(WebKitURISchemeRequest* request, int code, const char* msg) {
    GError* err = g_error_new(G_IO_ERROR, code, "%s", msg);
    webkit_uri_scheme_request_finish_error(request, err);
    g_error_free(err);
}

class WebKitGTKBackend final : public IWebviewBackend {
public:
    ~WebKitGTKBackend() override = default;

    bool Create(void* parentNativeWindow, const std::vector<std::string>& args) override {
        (void)args; // v1: sin flags extra
        GtkWindow* parent = GTK_WINDOW(parentNativeWindow);
        if (!parent) return false;

        manager_ = webkit_user_content_manager_new();
        context_ = webkit_web_context_new();

        // Construct properties: contexto propio + NUESTRO content manager
        // (si no, WebKit crea el suyo y los scripts nunca llegan).
        view_ = GTK_WIDGET(g_object_new(WEBKIT_TYPE_WEB_VIEW,
                                        "web-context", context_,
                                        "user-content-manager", manager_,
                                        nullptr));

        RegisterKernelSchemes();

        gtk_container_add(GTK_CONTAINER(parent), view_);
        gtk_widget_show(view_);
        return view_ != nullptr;
    }

    void InjectInitScript(const std::string& js) override {
        if (!manager_) {
            log::Error("webview", "InjectInitScript sin manager");
            return;
        }
        log::Debug("webview", "inyectando script de " + std::to_string(js.size()) + " bytes");
        WebKitUserScript* script = webkit_user_script_new(
            js.c_str(), WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
            WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, nullptr, nullptr);
        webkit_user_content_manager_add_script(manager_, script);
        webkit_user_script_unref(script);
    }

    void SetMessageHandler(WebMessageHandler handler) override {
        if (!manager_) return;
        handler_ = std::move(handler);
        webkit_user_content_manager_register_script_message_handler(manager_, "ow");
        g_signal_connect(manager_, "script-message-received::ow",
                         G_CALLBACK(+[](WebKitUserContentManager*,
                                        WebKitJavascriptResult* result,
                                        gpointer user_data) {
                             auto* self = static_cast<WebKitGTKBackend*>(user_data);
                             JSCValue* value =
                                 webkit_javascript_result_get_js_value(result);
                             if (jsc_value_is_string(value)) {
                                 char* str = jsc_value_to_string(value);
                                 if (self->handler_)
                                     self->handler_(std::string_view(str));
                                 g_free(str);
                             }
                         }),
                         this);
    }

    void LoadURL(const std::string& url) override {
        if (view_)
            webkit_web_view_load_uri(WEBKIT_WEB_VIEW(view_), url.c_str());
    }

    void EvalJS(const std::string& js, EvalCallback cb) override {
        if (!view_) return;
        auto* boxed = new EvalCallback(std::move(cb));
        webkit_web_view_evaluate_javascript(
            WEBKIT_WEB_VIEW(view_), js.c_str(), static_cast<gssize>(js.size()),
            nullptr, nullptr, nullptr,
            [](GObject* obj, GAsyncResult* res, gpointer user_data) {
                std::unique_ptr<EvalCallback> cb(static_cast<EvalCallback*>(user_data));
                GError* err = nullptr;
                JSCValue* value = webkit_web_view_evaluate_javascript_finish(
                    WEBKIT_WEB_VIEW(obj), res, &err);
                std::string result = "null";
                bool ok = true;
                if (err) {
                    ok = false;
                    result = std::string("{\"owError\":") +
                             json::Value(std::string(err->message)).Serialize() + "}";
                    g_error_free(err);
                } else if (value) {
                    char* str = jsc_value_to_string(value);
                    if (jsc_value_is_string(value)) {
                        result = json::Value(std::string(str)).Serialize();
                    } else {
                        result = str ? str : "null";
                    }
                    g_free(str);
                }
                if (*cb) (*cb)(result, ok);
            },
            boxed);
    }

    void RegisterAssetScheme(const std::string& scheme,
                             const std::filesystem::path& root) override {
        (void)scheme; // siempre "app" — registrado desde Create()
        assetRoot_ = root;
    }

    void Resize(int x, int y, int w, int h) override {
        (void)x; (void)y; (void)w; (void)h; // GTK gestiona layout
    }

    void* NativeWidget() const override { return view_; }

private:
    void RegisterKernelSchemes() {
        // ── app:// ─ assets del bundle ───────────────────────────────────
        webkit_web_context_register_uri_scheme(
            context_, "app",
            [](WebKitURISchemeRequest* request, gpointer user_data) {
                auto* self = static_cast<WebKitGTKBackend*>(user_data);
                const auto& root = self->assetRoot_;
                if (root.empty()) {
                    FinishError(request, G_IO_ERROR_NOT_FOUND, "assets no configurados");
                    return;
                }
                std::string uri = webkit_uri_scheme_request_get_uri(request);
                auto pos = uri.find("://");
                std::string rel =
                    pos == std::string::npos ? "" : uri.substr(pos + 3);

                std::error_code ec;
                std::filesystem::path full = (root / rel).lexically_normal();
                auto [rEnd, fEnd] =
                    std::mismatch(root.begin(), root.end(), full.begin());
                if (rEnd != root.end()) {
                    FinishError(request, G_IO_ERROR_PERMISSION_DENIED, "forbidden");
                    return;
                }
                if (!std::filesystem::is_regular_file(full, ec)) {
                    FinishError(request, G_IO_ERROR_NOT_FOUND, "not found");
                    return;
                }
                auto size = std::filesystem::file_size(full, ec);
                if (ec) {
                    FinishError(request, G_IO_ERROR_FAILED, "stat failed");
                    return;
                }
                FILE* f = std::fopen(full.c_str(), "rb");
                if (!f) {
                    FinishError(request, G_IO_ERROR_FAILED, "open failed");
                    return;
                }
                // assets pequeños: una copia aceptable; grandes → ow-shm://
                auto* payload = new std::string;
                payload->resize(size);
                size_t rd = std::fread(payload->data(), 1, size, f);
                std::fclose(f);
                payload->resize(rd);
                GBytes* bytes = g_bytes_new_with_free_func(
                    payload->data(), payload->size(),
                    [](gpointer p) { delete static_cast<std::string*>(p); },
                    payload);
                GInputStream* stream = g_memory_input_stream_new_from_bytes(bytes);
                g_bytes_unref(bytes);
                webkit_uri_scheme_request_finish(
                    request, stream, -1, MimeFromExt(full.extension().string()));
                g_object_unref(stream);
            },
            this, nullptr);

        // ── ow-shm://<id> ─ regiones compartidas SIN COPIA (F3) ─────────
        webkit_web_context_register_uri_scheme(
            context_, "ow-shm",
            [](WebKitURISchemeRequest* request, gpointer) {
                std::string uri = webkit_uri_scheme_request_get_uri(request);
                auto pos = uri.find("://");
                std::string id = pos == std::string::npos ? "" : uri.substr(pos + 3);
                auto amp = id.find('?');
                if (amp != std::string::npos) id.resize(amp);

                size_t len = 0;
                const uint8_t* data = shm::Data(id.c_str(), &len);
                if (!data) {
                    FinishError(request, G_IO_ERROR_NOT_FOUND, "región inexistente");
                    return;
                }
                // cero copias del lado kernel: GBytes estático sobre el mmap
                FinishBytesStatic(request, data, len, "application/octet-stream",
                                  "Access-Control-Allow-Origin", "*");
            },
            nullptr, nullptr);

        // ── ow-sync://i/<payload-urlenc> ─ invoke SÍNCRONO (F3) ─────────
        // El renderer se bloquea en XHR hasta que este handler responde.
        // ⚠️ REENTRANCIA: el handler corre en el main thread con JS bloqueado.
        //    Las funciones invocadas aquí NO deben llamar EvalJS.
        webkit_web_context_register_uri_scheme(
            context_, "ow-sync",
            [](WebKitURISchemeRequest* request, gpointer) {
                namespace br = ow::bridge;
                std::string uri = webkit_uri_scheme_request_get_uri(request);
                auto slash = uri.rfind('/');
                std::string enc =
                    slash == std::string::npos ? "" : uri.substr(slash + 1);

                // decodifica %XX (+ deja '+' como espacio no aplica aquí:
                // encodeURIComponent no genera '+')
                std::string text;
                text.reserve(enc.size());
                for (size_t i = 0; i < enc.size(); ++i) {
                    if (enc[i] == '%' && i + 2 < enc.size()) {
                        auto hexv = [](char c) -> int {
                            if (c >= '0' && c <= '9') return c - '0';
                            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                            return -1;
                        };
                        int hi = hexv(enc[i + 1]), lo = hexv(enc[i + 2]);
                        if (hi >= 0 && lo >= 0) {
                            text += static_cast<char>((hi << 4) | lo);
                            i += 2;
                            continue;
                        }
                    }
                    text += enc[i];
                }

                br::Message msg;
                std::string body;
                if (!br::DecodeMessage(text, msg)) {
                    body = "{\"ok\":false,\"error\":\"mensaje inválido\"}";
                } else {
                    ow_request_t req{};
                    req.json = msg.json.c_str();
                    req.json_len = static_cast<uint32_t>(msg.json.size());
                    req.bin = msg.bin.empty() ? nullptr : msg.bin.data();
                    req.bin_len = static_cast<uint32_t>(msg.bin.size());
                    ow_response_t res{};
                    Dispatcher::Get().Execute(msg.window, msg.module, msg.method,
                                              &req, &res);
                    if (res.status != 0) {
                        json::Object e;
                        e.emplace_back("message", json::Value(std::string(
                                                      res.error ? res.error : "")));
                        body = "{\"ok\":false,\"r\":" +
                               json::Value(std::move(e)).Serialize() + "}";
                    } else {
                        body = "{\"ok\":true,\"r\":" +
                               std::string(res.json, res.json_len) + "}";
                    }
                }
                FinishBytesStatic(request,
                                  reinterpret_cast<const uint8_t*>(body.data()),
                                  body.size(), "application/json",
                                  "Access-Control-Allow-Origin", "*");
            },
            nullptr, nullptr);
    }

    GtkWidget* view_ = nullptr;
    WebKitUserContentManager* manager_ = nullptr;
    WebKitWebContext* context_ = nullptr;
    WebMessageHandler handler_;
    std::filesystem::path assetRoot_;
};

} // namespace

std::unique_ptr<IWebviewBackend> CreateWebviewBackend() {
    return std::make_unique<WebKitGTKBackend>();
}

} // namespace ow
