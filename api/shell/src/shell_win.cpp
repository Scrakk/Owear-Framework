// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
// api/shell/src/shell_win.cpp — ShellExecute. VERIFICAR-EN-WINDOWS.
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

namespace sh {
using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

void openExternal(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || parsed.value->AsArray().empty())
        return RespondError(res, "url requerida");
    HINSTANCE r = ShellExecuteA(nullptr, "open",
                                parsed.value->AsArray()[0].AsString().c_str(),
                                nullptr, nullptr, SW_SHOWNORMAL);
    if ((intptr_t)r <= 32) return RespondError(res, "ShellExecute falló");
    RespondOk(res, "null");
}
void openPath(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || parsed.value->AsArray().empty())
        return RespondError(res, "path requerido");
    HINSTANCE r = ShellExecuteA(nullptr, "open",
                                parsed.value->AsArray()[0].AsString().c_str(),
                                nullptr, nullptr, SW_SHOWNORMAL);
    if ((intptr_t)r <= 32) return RespondError(res, "ShellExecute falló");
    RespondOk(res, "\"\"");
}
void showItemInFolder(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || parsed.value->AsArray().empty())
        return RespondError(res, "path requerido");
    std::string args = "/select,\"" + parsed.value->AsArray()[0].AsString() + "\"";
    ShellExecuteA(nullptr, nullptr, "explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    RespondOk(res, "null");
}
} // namespace sh

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"openExternal", &sh::openExternal},
        {"openPath", &sh::openPath},
        {"showItemInFolder", &sh::showItemInFolder},
    };
    static const ow_module_desc_t d{"shell", OW_VERSION_STRING, fns,
                                    sizeof(fns) / sizeof(fns[0])};
    return &d;
}
