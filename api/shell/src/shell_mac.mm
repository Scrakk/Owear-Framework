// VERIFICAR-EN-MACOS: NSWorkspace.
// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/shell/src/shell_linux.cpp — openExternal/openPath/showItemInFolder.
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#import <Cocoa/Cocoa.h>

namespace sh {

using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

void openExternal(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || parsed.value->AsArray().empty())
        return RespondError(res, "url requerida");
    NSURL* url = [NSURL URLWithString:
        [NSString stringWithUTF8String:parsed.value->AsArray()[0].AsString().c_str()]];
    if (![[NSWorkspace sharedWorkspace] openURL:url])
        return RespondError(res, "openURL falló");
    RespondOk(res, "null");
}
void openPath(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || parsed.value->AsArray().empty())
        return RespondError(res, "path requerido");
    if (![[NSWorkspace sharedWorkspace] openFile:
        [NSString stringWithUTF8String:parsed.value->AsArray()[0].AsString().c_str()]])
        return RespondError(res, "openFile falló");
    RespondOk(res, "\"\"");
}
void showItemInFolder(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || parsed.value->AsArray().empty())
        return RespondError(res, "path requerido");
    [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[
        [NSURL fileURLWithPath:[NSString stringWithUTF8String:
            parsed.value->AsArray()[0].AsString().c_str()]]]];
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
