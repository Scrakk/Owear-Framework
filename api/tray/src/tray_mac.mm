// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/tray/src/tray_mac.mm — NSStatusItem real en la barra de menús.
// VERIFICAR-EN-MACOS (compila; requiere sesión gráfica para verse).
//
#import <Cocoa/Cocoa.h>

#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

namespace tray {

using ow::json::Value;
using ow::json::Parse;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static NSStatusItem* s_item = nullptr;

void create(const ow_request_t* req, ow_response_t* res) {
    auto parsed = Parse(std::string_view(req->json, req->json_len));
    NSString* label = @"Owear";
    if (parsed.value && parsed.value->IsArray() &&
        !parsed.value->AsArray().empty() &&
        parsed.value->AsArray()[0].IsString())
        label = [NSString stringWithUTF8String:
                     parsed.value->AsArray()[0].AsString().c_str()];

    if (!s_item) {
        s_item = [[NSStatusBar systemStatusBar]
            statusItemWithLength:NSVariableStatusItemLength];
        s_item.button.title = label;
    }
    RespondOk(res, "null");
}

void setIcon(const ow_request_t* req, ow_response_t* res) {
    if (!s_item) return RespondError(res, "tray.create primero");
    auto parsed = Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        parsed.value->AsArray().empty() || !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "iconPath requerido");
    NSImage* img = [[NSImage alloc]
        initWithContentsOfFile:[NSString stringWithUTF8String:
                                    parsed.value->AsArray()[0].AsString()
                                        .c_str()]];
    if (!img) return RespondError(res, "no se pudo cargar la imagen");
    img.size = NSMakeSize(18, 18); // tamaño estándar de la barra
    s_item.button.image = img;
    RespondOk(res, "null");
}

void setTitle(const ow_request_t* req, ow_response_t* res) {
    if (!s_item) return RespondError(res, "tray.create primero");
    auto parsed = Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        parsed.value->AsArray().empty() || !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "title requerido");
    s_item.button.title = [NSString stringWithUTF8String:
                               parsed.value->AsArray()[0].AsString().c_str()];
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

