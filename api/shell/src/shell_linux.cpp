// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/shell/src/shell_linux.cpp — openExternal/openPath/showItemInFolder.
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#include <gio/gio.h>

namespace sh {

using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static bool LaunchUri(const std::string& uri, std::string& err) {
    GAppLaunchContext* ctx = g_app_launch_context_new();
    GError* e = nullptr;
    gboolean ok = g_app_info_launch_default_for_uri(uri.c_str(), ctx, &e);
    g_object_unref(ctx);
    if (!ok) {
        if (e) {
            err = e->message;
            g_error_free(e);
        } else
            err = "launch falló";
        return false;
    }
    return true;
}

void openExternal(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "url requerida");
    std::string url = parsed.value->AsArray()[0].AsString();
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0 &&
        url.rfind("mailto:", 0) != 0)
        return RespondError(res, "solo http(s)/mailto en openExternal");
    std::string err;
    if (!LaunchUri(url, err)) return RespondError(res, err);
    RespondOk(res, "null");
}

void openPath(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->AsArray().empty() == false ||
        parsed.value->AsArray().empty())
        return RespondError(res, "path requerido");
    std::string path = parsed.value->AsArray()[0].AsString();
    // file:// URI con escape mínimo de espacios
    std::string uri = "file://";
    for (char c : path) uri += (c == ' ') ? "%20" : std::string(1, c);
    std::string err;
    if (!LaunchUri(uri, err)) return RespondError(res, err);
    RespondOk(res, "\"\"");
}

void showItemInFolder(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || parsed.value->AsArray().empty())
        return RespondError(res, "path requerido");
    std::string path = parsed.value->AsArray()[0].AsString();
    // org.freedesktop.FileManager1.ShowItems (Nautilus/Dolphin/etc)
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if (bus) {
        GVariantBuilder b;
        g_variant_builder_init(&b, G_VARIANT_TYPE("(ass)"));
        GVariantBuilder arr;
        g_variant_builder_init(&arr, G_VARIANT_TYPE("as"));
        std::string uri = "file://" + path;
        g_variant_builder_add(&arr, "s", uri.c_str());
        g_variant_builder_add(&b, "as", &arr);
        g_variant_builder_add(&b, "s", "");
        GError* e = nullptr;
        g_dbus_connection_call_sync(bus, "org.freedesktop.FileManager1",
                                    "/org/freedesktop/FileManager1",
                                    "org.freedesktop.FileManager1", "ShowItems",
                                    g_variant_builder_end(&b), nullptr,
                                    G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &e);
        g_object_unref(bus);
        if (!e) return RespondOk(res, "null");
        g_error_free(e); // fallback abajo
    }
    auto slash = path.rfind('/');
    std::string dir = slash == std::string::npos ? "." : path.substr(0, slash);
    std::string err;
    LaunchUri("file://" + dir, err);
    RespondOk(res, "null");
}

} // namespace sh

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"openExternal", &sh::openExternal},
        {"openPath", &sh::openPath},
        {"showItemInFolder", &sh::showItemInFolder},
    };
    static const ow_module_desc_t d{
        "shell", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}
