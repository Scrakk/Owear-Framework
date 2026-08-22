// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/path/src/dirs_win.cpp — directorios estándar de Windows.
// VERIFICAR-EN-WINDOWS (SHGetKnownFolderPath GUIDs).
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#include <string>

namespace {

std::string Known(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::string out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) {
        int n = WideCharToMultiByte(CP_UTF8, 0, p, -1, nullptr, 0, nullptr, nullptr);
        if (n > 0) {
            out.resize(n - 1);
            WideCharToMultiByte(CP_UTF8, 0, p, -1, out.data(), n, nullptr, nullptr);
        }
        CoTaskMemFree(p);
    }
    return out;
}

} // namespace

namespace pathdirs {
using ow::json::Value;
using ow::Module::RespondOk;

void homeDir(const ow_request_t*, ow_response_t* res) {
    RespondOk(res, Value(Known(FOLDERID_Profile)).Serialize().c_str());
}
void appDataDir(const ow_request_t* req, ow_response_t* res) {
    RespondOk(res, Value(Known(FOLDERID_RoamingAppData)).Serialize().c_str());
}
void userDataDir(const ow_request_t* req, ow_response_t* res) {
    char id[256] = "app";
    GetEnvironmentVariableA("OW_APP_ID", id, sizeof(id));
    RespondOk(res,
              Value(Known(FOLDERID_LocalAppData) + "\\" + id).Serialize().c_str());
}
void cacheDir(const ow_request_t* req, ow_response_t* res) {
    RespondOk(res, Value(Known(FOLDERID_LocalAppData) + "\\owear\\cache")
                       .Serialize().c_str());
}
void tempDir(const ow_request_t* req, ow_response_t* res) {
    char buf[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, buf);
    std::string t(buf);
    if (!t.empty() && t.back() == '\\') t.pop_back();
    RespondOk(res, Value(t).Serialize().c_str());
}
void configDir(const ow_request_t* req, ow_response_t* res) {
    RespondOk(res, Value(Known(FOLDERID_LocalAppData)).Serialize().c_str());
}
void exeDir(const ow_request_t* req, ow_response_t* res) {
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string exe(buf);
    auto slash = exe.rfind('\\');
    RespondOk(res, Value(slash == std::string::npos ? "" : exe.substr(0, slash))
                       .Serialize().c_str());
}
void cwd(const ow_request_t*, ow_response_t* res) {
    char buf[MAX_PATH] = {};
    GetCurrentDirectoryA(MAX_PATH, buf);
    RespondOk(res, Value(std::string(buf)).Serialize().c_str());
}
} // namespace pathdirs

const ow_fn_entry_t kDirsFns[] = {
    {"homeDir", &pathdirs::homeDir},         {"appDataDir", &pathdirs::appDataDir},
    {"userDataDir", &pathdirs::userDataDir}, {"cacheDir", &pathdirs::cacheDir},
    {"tempDir", &pathdirs::tempDir},         {"configDir", &pathdirs::configDir},
    {"exeDir", &pathdirs::exeDir},           {"cwd", &pathdirs::cwd},
};
