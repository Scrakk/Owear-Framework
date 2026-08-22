// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/clipboard/src/clipboard_win.cpp — Win32 clipboard (texto; imagen CF_DIB).
// VERIFICAR-EN-WINDOWS.
//
#include "ow/Base64.h"
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow/Shm.h"
#include "ow_api.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <vector>

namespace clip {

using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

void readText(const ow_request_t*, ow_response_t* res) {
    if (!OpenClipboard(nullptr)) return RespondError(res, "OpenClipboard falló");
    HANDLE h = GetClipboardData(CF_TEXT);
    std::string out = h ? std::string(static_cast<const char*>(GlobalLock(h))) : "";
    if (h) GlobalUnlock(h);
    CloseClipboard();
    RespondOk(res, Value(out).Serialize().c_str());
}

void writeText(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "text requerido");
    std::string text = parsed.value->AsArray()[0].AsString();
    if (!OpenClipboard(nullptr)) return RespondError(res, "OpenClipboard falló");
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    memcpy(GlobalLock(h), text.c_str(), text.size() + 1);
    GlobalUnlock(h);
    SetClipboardData(CF_TEXT, h);
    CloseClipboard();
    RespondOk(res, "null");
}

void readImage(const ow_request_t*, ow_response_t* res) {
    // v1: sin encoder BMP→PNG nativo; requiere WIC. TODO-verify F-next.
    RespondError(res, "readImage pendiente en Windows (WIC)");
}
void writeImage(const ow_request_t*, ow_response_t* res) {
    RespondError(res, "writeImage pendiente en Windows (WIC)");
}
void clear(const ow_request_t*, ow_response_t* res) {
    OpenClipboard(nullptr);
    EmptyClipboard();
    CloseClipboard();
    RespondOk(res, "null");
}

} // namespace clip

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"readText", &clip::readText},   {"writeText", &clip::writeText},
        {"readImage", &clip::readImage}, {"writeImage", &clip::writeImage},
        {"clear", &clip::clear},
    };
    static const ow_module_desc_t d{
        "clipboard", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}
