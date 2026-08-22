// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/notification/src/notification_linux.cpp — org.freedesktop.Notifications
// (notificaciones DEL SISTEMA via D-Bus, estándar freedesktop).
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#include <gio/gio.h>

namespace notif {

using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

// args: [title, body?, appName?]
void show(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        parsed.value->AsArray().empty() || !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "title requerido");

    std::string title = parsed.value->AsArray()[0].AsString();
    std::string body = parsed.value->AsArray().size() > 1 &&
                               parsed.value->AsArray()[1].IsString()
                           ? parsed.value->AsArray()[1].AsString()
                           : "";
    std::string app = "Owear";
    if (parsed.value->AsArray().size() > 2 &&
        parsed.value->AsArray()[2].IsString())
        app = parsed.value->AsArray()[2].AsString();

    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if (!bus) return RespondError(res, "sin sesión D-Bus");

    GVariantBuilder actions;
    g_variant_builder_init(&actions, G_VARIANT_TYPE("as")); // sin acciones v1

    GError* e = nullptr;
    GVariant* result = g_dbus_connection_call_sync(
        bus, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
        "org.freedesktop.Notifications", "Notify",
        g_variant_new("(susssasa{sv}i)", app.c_str(),  // app_name
                      0u,                              // replaces_id
                      "",                              // icon (sin icono v1)
                      title.c_str(), body.c_str(), &actions,
                      nullptr,                          // hints
                      -1),                             // expire default
        G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &e);
    g_object_unref(bus);

    if (e) {
        std::string err = e->message;
        g_error_free(e);
        return RespondError(res, err);
    }
    guint nid = 0;
    g_variant_get(result, "(u)", &nid);
    g_variant_unref(result);
    RespondOk(res, Value(static_cast<int64_t>(nid)).Serialize().c_str());
}

} // namespace notif

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {{"show", &notif::show}};
    static const ow_module_desc_t d{
        "notification", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}
