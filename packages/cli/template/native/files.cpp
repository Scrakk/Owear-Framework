// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// native/files.cpp — módulo nativo PROPIO de la app.
// Se compila a .owm y se invoca desde el renderer con:
//   import { files } from '@owear/native'
//   await files.readText('/etc/hostname')
//
#include <ow/Json.h>
#include <ow/Module.h>
#include "ow_api.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

static void readText(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || !parsed.value->AsArray()[0].IsString())
        return ow::Module::RespondError(res, "readText(path): path requerido");
    std::ifstream f(parsed.value->AsArray()[0].AsString(), std::ios::binary);
    if (!f) return ow::Module::RespondError(res, "no se pudo abrir");
    std::ostringstream ss;
    ss << f.rdbuf();
    ow::Module::RespondOk(res, ow::json::Value(ss.str()).Serialize().c_str());
}

static void exists(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || !parsed.value->AsArray()[0].IsString())
        return ow::Module::RespondError(res, "exists(path): path requerido");
    std::error_code ec;
    bool ok = fs::exists(parsed.value->AsArray()[0].AsString(), ec);
    ow::Module::RespondOk(res, ok ? "true" : "false");
}

OW_MODULE_BEGIN(files, "1.0.0")
OW_FN(readText)
OW_FN(exists)
OW_MODULE_END()
