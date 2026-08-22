// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/capturer/src/capturer_win.cpp — captura via BitBlt/GDI.
// VERIFICAR-EN-WINDOWS: implementación v1 pendiente — expone la API con
// error claro hasta portar el pipeline X11→GDI.
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

namespace cap {

using ow::json::Value;
using ow::Module::RespondError;

void getSources(const ow_request_t*, ow_response_t* res) {
    RespondError(res, "captura en macOS pendiente de implementación (CGDisplayCreateImage)");
}
void captureScreen(const ow_request_t*, ow_response_t* res) {
    RespondError(res, "captura en macOS pendiente de implementación (CGDisplayCreateImage)");
}

} // namespace cap

OW_MODULE_EXPORT extern "C" const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"getSources", &cap::getSources},
        {"captureScreen", &cap::captureScreen},
    };
    static const ow_module_desc_t d{
        "capturer", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}
