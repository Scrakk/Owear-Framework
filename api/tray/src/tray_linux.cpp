// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/tray/src/tray_linux.cpp — StatusNotifierItem vía libappindicator
// (estándar de facto en GNOME/KDE con la extensión apropiada).
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#ifdef HAVE_AYATANA
#include <libayatana-appindicator/app-indicator.h>
#define INDICATOR_TYPE APP_INDICATOR_TYPE
#else
#include <libappindicator/app-indicator.h>
#endif

namespace tray {

using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static AppIndicator* g_indicator = nullptr;

void create(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    std::string id = "owear-tray";
    if (parsed.value && parsed.value->IsArray() && !parsed.value->AsArray().empty() &&
        parsed.value->AsArray()[0].IsString())
        id = parsed.value->AsArray()[0].AsString();

    if (!g_indicator) {
        g_indicator = app_indicator_new(id.c_str(), "application-default-icon",
                                        APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
        app_indicator_set_status(g_indicator, APP_INDICATOR_STATUS_ACTIVE);
    }
    RespondOk(res, "null");
}

void setIcon(const ow_request_t* req, ow_response_t* res) {
    if (!g_indicator) return RespondError(res, "tray.create primero");
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty() ||
        !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "iconPath requerido (nombre de tema o ruta)");
    // AppIndicator usa nombres de tema/rutas sin extensión; copiar a ~/.icons es
    // responsabilidad del app packager.
    app_indicator_set_icon_full(g_indicator,
                                parsed.value->AsArray()[0].AsString().c_str(), "");
    RespondOk(res, "null");
}

void setTitle(const ow_request_t* req, ow_response_t* res) {
    if (!g_indicator) return RespondError(res, "tray.create primero");
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray()) return RespondError(res, "title");
    app_indicator_set_title(g_indicator,
                            parsed.value->AsArray()[0].AsString().c_str());
    RespondOk(res, "null");
}

} // namespace tray

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"create", &tray::create},
        {"setIcon", &tray::setIcon},
        {"setTitle", &tray::setTitle},
    };
    static const ow_module_desc_t d{
        "tray", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}
