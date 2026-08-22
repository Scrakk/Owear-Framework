// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/clipboard/src/clipboard_linux.cpp — GtkClipboard (texto + imagen PNG).
//
#include "ow/Base64.h"
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow/Shm.h"
#include "ow_api.h"

#include <gtk/gtk.h>
#include <vector>

namespace clip {

using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static GtkClipboard* Sys() { return gtk_clipboard_get(GDK_SELECTION_CLIPBOARD); }

void readText(const ow_request_t*, ow_response_t* res) {
    gchar* t = gtk_clipboard_wait_for_text(Sys());
    RespondOk(res, Value(t ? std::string(t) : std::string()).Serialize().c_str());
    if (t) g_free(t);
}

void writeText(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty() ||
        !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "text requerido");
    gtk_clipboard_set_text(Sys(), parsed.value->AsArray()[0].AsString().c_str(), -1);
    gtk_clipboard_store(Sys());
    RespondOk(res, "null");
}

void readImage(const ow_request_t*, ow_response_t* res) {
    GdkPixbuf* img = gtk_clipboard_wait_for_image(Sys());
    if (!img) return RespondOk(res, "null");
    gchar* buf = nullptr;
    gsize len = 0;
    GError* err = nullptr;
    gdk_pixbuf_save_to_buffer(img, &buf, &len, "png", &err, nullptr);
    if (err || !buf) {
        if (err) g_error_free(err);
        g_object_unref(img);
        return RespondError(res, "PNG encode falló");
    }
    const char* id = ow_shm_put(reinterpret_cast<const uint8_t*>(buf), len);
    std::string json = "{\"__ow_shm\":{\"id\":\"" + std::string(id ?: "") +
                       "\",\"size\":" + std::to_string(len) +
                       ",\"width\":" + std::to_string(gdk_pixbuf_get_width(img)) +
                       ",\"height\":" + std::to_string(gdk_pixbuf_get_height(img)) +
                       ",\"format\":\"png\"}}";
    g_free(buf);
    g_object_unref(img);
    RespondOk(res, json.c_str());
}

void writeImage(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty() ||
        !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "pngB64 requerido");
    std::vector<uint8_t> png;
    if (!ow::b64::Decode(parsed.value->AsArray()[0].AsString(), png))
        return RespondError(res, "b64 inválido");

    GError* err = nullptr;
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    gdk_pixbuf_loader_write(loader, png.data(), png.size(), &err);
    gdk_pixbuf_loader_close(loader, &err);
    GdkPixbuf* img = err ? nullptr : gdk_pixbuf_loader_get_pixbuf(loader);
    if (!img) {
        if (err) g_error_free(err);
        g_object_unref(loader);
        return RespondError(res, "PNG inválido");
    }
    gtk_clipboard_set_image(Sys(), img);
    gtk_clipboard_store(Sys());
    g_object_unref(loader);
    RespondOk(res, "null");
}

void clear(const ow_request_t*, ow_response_t* res) {
    gtk_clipboard_clear(Sys());
    RespondOk(res, "null");
}

} // namespace clip

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"readText", &clip::readText},
        {"writeText", &clip::writeText},
        {"readImage", &clip::readImage},
        {"writeImage", &clip::writeImage},
        {"clear", &clip::clear},
    };
    static const ow_module_desc_t d{
        "clipboard", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}
