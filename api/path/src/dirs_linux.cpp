// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/path/src/dirs_linux.cpp — directorios estándar XDG.
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#include <cstdlib>
#include <unistd.h>
#include <limits.h>

namespace {

std::string EnvOr(const char* env, std::string def) {
    const char* v = std::getenv(env);
    return (v && *v) ? v : def;
}
std::string Home() { return EnvOr("HOME", "/tmp"); }

} // namespace

namespace pathdirs {
using ow::json::Value;
using ow::Module::RespondOk;

void homeDir(const ow_request_t*, ow_response_t* res) {
    RespondOk(res, Value(Home()).Serialize().c_str());
}
void appDataDir(const ow_request_t* req, ow_response_t* res) {
    // XDG_DATA_HOME
    RespondOk(res, Value(EnvOr("XDG_DATA_HOME", Home() + "/.local/share"))
                       .Serialize().c_str());
}
void userDataDir(const ow_request_t* req, ow_response_t* res) {
    // userData/<appId> — usa OW_APP_ID si existe
    std::string id = EnvOr("OW_APP_ID", "app");
    RespondOk(res, Value(EnvOr("XDG_CONFIG_HOME", Home() + "/.config") + "/" + id)
                       .Serialize().c_str());
}
void cacheDir(const ow_request_t* req, ow_response_t* res) {
    RespondOk(res, Value(EnvOr("XDG_CACHE_HOME", Home() + "/.cache"))
                       .Serialize().c_str());
}
void tempDir(const ow_request_t* req, ow_response_t* res) {
    RespondOk(res, Value(EnvOr("TMPDIR", "/tmp")).Serialize().c_str());
}
void configDir(const ow_request_t* req, ow_response_t* res) {
    RespondOk(res, Value(EnvOr("XDG_CONFIG_HOME", Home() + "/.config"))
                       .Serialize().c_str());
}
void exeDir(const ow_request_t* req, ow_response_t* res) {
    char buf[PATH_MAX] = {};
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    std::string exe = n > 0 ? std::string(buf, static_cast<size_t>(n)) : "";
    auto slash = exe.rfind('/');
    RespondOk(res, Value(slash == std::string::npos ? "" : exe.substr(0, slash))
                       .Serialize().c_str());
}
void cwd(const ow_request_t*, ow_response_t* res) {
    char buf[PATH_MAX] = {};
    RespondOk(res,
              Value(std::string(getcwd(buf, sizeof(buf)) ?: "")).Serialize().c_str());
}

const ow_fn_entry_t kDirsFns[] = {
    {"homeDir", &homeDir},     {"appDataDir", &appDataDir},
    {"userDataDir", &userDataDir}, {"cacheDir", &cacheDir},
    {"tempDir", &tempDir},     {"configDir", &configDir},
    {"exeDir", &exeDir},       {"cwd", &cwd},
};
} // namespace pathdirs

const ow_fn_entry_t kDirsFns[] = {
    {"homeDir", &pathdirs::homeDir},         {"appDataDir", &pathdirs::appDataDir},
    {"userDataDir", &pathdirs::userDataDir}, {"cacheDir", &pathdirs::cacheDir},
    {"tempDir", &pathdirs::tempDir},         {"configDir", &pathdirs::configDir},
    {"exeDir", &pathdirs::exeDir},           {"cwd", &pathdirs::cwd},
};
