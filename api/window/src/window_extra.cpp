// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/window/src/window_extra.cpp — builtin "window-extras".
// Funciones avanzadas de ventana acopladas al WebView del SO.
//
// Linux: devtools · capturePage(PNG→SHM) · alwaysOnTop · opacity ·
//        flashFrame · setIcon · setUserAgent · zoomLevel.
// printToPDF/progressBar/ignoreMouseEvents/contentProtection → no soportado
// en WebKitGTK v1 (error claro, no silencio).
//
#include "../../../src/Core/BuiltinUtil.hpp"
#include "ow/Base64.h"
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow/Shm.h"
#include "ow_api.h"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

namespace winx {

using ow::json::Array;
using ow::json::Object;
using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static inline uint32_t WinId(const Value& args) {
    if (!args.IsArray() || args.AsArray().empty() || !args.AsArray()[0].IsNumber())
        return 0;
    return static_cast<uint32_t>(args.AsArray()[0].AsInt());
}

#define NEED_WIN(id)                        \
    GtkWindow* gtkWin = ow::builtin::WindowById(id); \
    WebKitWebView* view = static_cast<WebKitWebView*>(ow::builtin::WebviewById(id)); \
    if (!gtkWin || !view) return RespondError(res, "ventana no encontrada");

void openDevTools(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    WebKitWebInspector* insp = webkit_web_view_get_inspector(view);
    gboolean show = true;
    if (args.IsArray() && args.AsArray().size() > 1 && args.AsArray()[1].IsBool())
        show = args.AsArray()[1].AsBool();
    show ? webkit_web_inspector_show(insp) : webkit_web_inspector_close(insp);
    RespondOk(res, "null");
}

// ── capturePage → PNG → SHM ─────────────────────────────────────────────────

struct SnapCtx {
    volatile bool done = false;
    cairo_surface_t* surface = nullptr;
    std::string error;
};

static void OnSnapshot(GObject*, GAsyncResult* asyncRes, gpointer ud) {
    auto* ctx = static_cast<SnapCtx*>(ud);
    GError* err = nullptr;
    ctx->surface = webkit_web_view_get_snapshot_finish(
        WEBKIT_WEB_VIEW(g_async_result_get_source_object(asyncRes)), asyncRes, &err);
    if (err) {
        ctx->error = err->message;
        g_error_free(err);
    }
    ctx->done = true;
}

static cairo_status_t PngWrite(void* closure, const unsigned char* data,
                               unsigned int length) {
    auto* out = static_cast<std::string*>(closure);
    out->append(reinterpret_cast<const char*>(data), length);
    return CAIRO_STATUS_SUCCESS;
}

void capturePage(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)

    SnapCtx ctx;
    webkit_web_view_get_snapshot(view, WEBKIT_SNAPSHOT_REGION_VISIBLE,
                                 WEBKIT_SNAPSHOT_OPTIONS_NONE, nullptr,
                                 OnSnapshot, &ctx);
    ow::builtin::PumpUntil(ctx.done);

    if (!ctx.surface)
        return RespondError(res, "snapshot falló: " + ctx.error);

    std::string png;
    cairo_surface_write_to_png_stream(ctx.surface, &PngWrite, &png);
    cairo_surface_destroy(ctx.surface);

    const char* sid = ow_shm_put(reinterpret_cast<const uint8_t*>(png.data()),
                                 png.size());
    if (!sid || !*sid) return RespondError(res, "SHM llena");

    Object shm;
    shm.emplace_back("id", Value(std::string(sid)));
    shm.emplace_back("size", Value(static_cast<int64_t>(png.size())));
    Object o;
    o.emplace_back("__ow_shm", Value(std::move(shm)));
    o.emplace_back("format", Value("png"));
    RespondOk(res, Value(std::move(o)).Serialize().c_str());
}

// ── propiedades GTK simples ──────────────────────────────────────────────────

void setAlwaysOnTop(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    gtk_window_set_keep_above(gtkWin, args.AsArray().size() > 1 &&
                                          args.AsArray()[1].AsBool());
    RespondOk(res, "null");
}
void isAlwaysOnTop(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    uint32_t id = WinId(parsed.value ? *parsed.value : Value(nullptr));
    NEED_WIN(id)
    gboolean above = false;
    g_object_get(G_OBJECT(gtkWin), "keep-above", &above, nullptr);
    RespondOk(res, above ? "true" : "false");
}

void setOpacity(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    double opacity = 1.0;
    if (args.AsArray().size() > 1 && args.AsArray()[1].IsNumber())
        opacity = args.AsArray()[1].AsDouble();
    opacity = opacity < 0 ? 0 : opacity > 1 ? 1 : opacity;
    gtk_widget_set_opacity(GTK_WIDGET(gtkWin), opacity);
    RespondOk(res, "null");
}

void flashFrame(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    gtk_window_set_urgency_hint(gtkWin, args.AsArray().size() > 1 &&
                                            args.AsArray()[1].AsBool());
    RespondOk(res, "null");
}

void setIcon(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    std::string b64s;
    if (!args.IsArray() || args.AsArray().size() < 2 || !args.AsArray()[1].IsString())
        return RespondError(res, "pngB64 requerido");
    b64s = args.AsArray()[1].AsString();
    std::vector<uint8_t> png;
    if (!ow::b64::Decode(b64s, png))
        return RespondError(res, "b64 inválido");

    GError* err = nullptr;
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    gdk_pixbuf_loader_write(loader, png.data(), png.size(), &err);
    gdk_pixbuf_loader_close(loader, &err);
    GdkPixbuf* pix = err ? nullptr : gdk_pixbuf_loader_get_pixbuf(loader);
    if (!pix) {
        if (err) g_error_free(err);
        g_object_unref(loader);
        return RespondError(res, "PNG inválido");
    }
    gtk_window_set_icon(gtkWin, pix);
    g_object_unref(loader);
    RespondOk(res, "null");
}

void setUserAgent(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    if (!args.IsArray() || args.AsArray().size() < 2 || !args.AsArray()[1].IsString())
        return RespondError(res, "userAgent requerido");
    WebKitSettings* settings = webkit_web_view_get_settings(view);
    webkit_settings_set_user_agent(settings,
                                   args.AsArray()[1].AsString().c_str());
    RespondOk(res, "null");
}

void zoom(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    double factor = 1.0;
    if (args.AsArray().size() > 1 && args.AsArray()[1].IsNumber())
        factor = args.AsArray()[1].AsDouble();
    webkit_web_view_set_zoom_level(view, factor);
    RespondOk(res, "null");
}

} // namespace winx

namespace ow::internal {
const ow_module_desc_t* WindowExtrasDescriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"openDevTools", &winx::openDevTools},
        {"capturePage", &winx::capturePage},
        {"setAlwaysOnTop", &winx::setAlwaysOnTop},
        {"isAlwaysOnTop", &winx::isAlwaysOnTop},
        {"setOpacity", &winx::setOpacity},
        {"flashFrame", &winx::flashFrame},
        {"setIcon", &winx::setIcon},
        {"setUserAgent", &winx::setUserAgent},
        {"zoom", &winx::zoom},
    };
    static const ow_module_desc_t d{
        "window", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}
} // namespace ow::internal
