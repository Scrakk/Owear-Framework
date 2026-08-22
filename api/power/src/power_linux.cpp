// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/power/src/power_linux.cpp — logind PrepareForSleep (monitor) +
// org.freedesktop.ScreenSaver Inhibit (blocker). D-Bus estándar freedesktop.
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#include <gio/gio.h>

#include <map>

namespace pw {

using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static const ow_module_host_t* g_host = nullptr;
static GDBusConnection* g_bus = nullptr;
static guint g_signalId = 0;
static int g_nextInhibit = 1;
static std::map<int, guint32> g_inhibits;

static void OnPrepareForSleep(GDBusConnection*, const gchar*, const gchar*,
                              const gchar*, const gchar*, GVariant* params,
                              gpointer) {
    gboolean sleeping = FALSE;
    g_variant_get(params, "(b)", &sleeping);
    if (g_host && g_host->emit_event)
        g_host->emit_event(g_host->ctx, 0,
                           sleeping ? "power.suspend" : "power.resume",
                           "null");
}

void monitorStart(const ow_request_t*, ow_response_t* res) {
    if (g_bus) return RespondOk(res, "\"ya-activo\"");
    g_bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
    if (!g_bus) return RespondError(res, "sin bus de sistema");
    g_signalId = g_dbus_connection_signal_subscribe(
        g_bus, "org.freedesktop.login1", "org.freedesktop.login1.Manager",
        "PrepareForSleep", "/org/freedesktop/login1", nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE, OnPrepareForSleep, nullptr, nullptr);
    RespondOk(res, "null");
}

void inhibitStart(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    std::string what = "prevent-display-sleep";
    if (parsed.value && parsed.value->IsArray() && !parsed.value->AsArray().empty() &&
        parsed.value->AsArray()[0].IsString())
        what = parsed.value->AsArray()[0].AsString();
    (void)what; // v1: siempre display-sleep via ScreenSaver

    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if (!bus) return RespondError(res, "sin sesión D-Bus");
    GError* e = nullptr;
    GVariant* r = g_dbus_connection_call_sync(
        bus, "org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver",
        "org.freedesktop.ScreenSaver", "Inhibit",
        g_variant_new("(ss)", "owear-app", "owear blocker"),
        G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &e);
    g_object_unref(bus);
    if (e) {
        std::string err = e->message;
        g_error_free(e);
        return RespondError(res, err);
    }
    guint32 cookie = 0;
    g_variant_get(r, "(u)", &cookie);
    g_variant_unref(r);
    int id = g_nextInhibit++;
    g_inhibits[id] = cookie;
    RespondOk(res, Value(static_cast<int64_t>(id)).Serialize().c_str());
}

void inhibitStop(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty())
        return RespondError(res, "inhibitId requerido");
    int id = static_cast<int>(parsed.value->AsArray()[0].AsInt());
    auto it = g_inhibits.find(id);
    if (it == g_inhibits.end()) return RespondOk(res, "null");
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if (bus) {
        GError* e = nullptr;
        g_dbus_connection_call_sync(bus, "org.freedesktop.ScreenSaver",
                                    "/org/freedesktop/ScreenSaver",
                                    "org.freedesktop.ScreenSaver", "UnInhibit",
                                    g_variant_new("(u)", it->second), nullptr,
                                    G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &e);
        if (e) g_error_free(e);
        g_object_unref(bus);
    }
    g_inhibits.erase(it);
    RespondOk(res, "null");
}

} // namespace pw

extern "C" OW_MODULE_EXPORT void ow_module_set_host(const ow_module_host_t* h) {
    pw::g_host = h;
}

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"monitorStart", &pw::monitorStart},
        {"inhibitStart", &pw::inhibitStart},
        {"inhibitStop", &pw::inhibitStop},
    };
    static const ow_module_desc_t d{
        "power", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}
