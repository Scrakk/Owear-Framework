// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/path/src/path.cpp — utilidades puras de rutas (multiplataforma).
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#include <filesystem>

namespace pathimpl {

namespace fs = std::filesystem;
using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static bool Arg(const Value& args, size_t idx, std::string& out) {
    if (!args.IsArray() || args.AsArray().size() <= idx ||
        !args.AsArray()[idx].IsString())
        return false;
    out = args.AsArray()[idx].AsString();
    return true;
}

void join(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray())
        return RespondError(res, "args inválidos");
    fs::path p;
    for (const auto& seg : parsed.value->AsArray()) {
        if (!seg.IsString()) return RespondError(res, "segmentos string requeridos");
        p /= seg.AsString();
    }
    RespondOk(res, Value(p.string()).Serialize().c_str());
}

void resolve(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty())
        return RespondError(res, "args inválidos");
    fs::path p = fs::current_path();
    for (const auto& seg : parsed.value->AsArray()) {
        if (!seg.IsString()) return RespondError(res, "segmentos string requeridos");
        std::string s = seg.AsString();
        if (!s.empty() && (s[0] == '/' || s[0] == '\\' ||
                           (s.size() > 1 && s[1] == ':')))
            p = s; // absoluta reinicia
        else
            p /= s;
    }
    RespondOk(res, Value(fs::absolute(p).lexically_normal().string())
                       .Serialize().c_str());
}

void dirname(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return RespondError(res, "args inválidos");
    std::string p;
    if (!Arg(*parsed.value, 0, p)) return RespondError(res, "path requerido");
    RespondOk(res, Value(fs::path(p).parent_path().string())
                       .Serialize().c_str());
}

void basenameFn(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return RespondError(res, "args inválidos");
    std::string p;
    if (!Arg(*parsed.value, 0, p)) return RespondError(res, "path requerido");
    fs::path f = fs::path(p).filename();
    // ext opcional para quitar
    if (parsed.value && parsed.value->AsArray().size() > 1 &&
        parsed.value->AsArray()[1].IsString()) {
        std::string ext = parsed.value->AsArray()[1].AsString();
        std::string fs_ = f.extension().string();
        if (fs_ == ext || "." + fs_ == ext) f = f.stem();
    }
    RespondOk(res, Value(f.string()).Serialize().c_str());
}

void extname(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return RespondError(res, "args inválidos");
    std::string p;
    if (!Arg(*parsed.value, 0, p)) return RespondError(res, "path requerido");
    RespondOk(res, Value(fs::path(p).extension().string()).Serialize().c_str());
}

void normalize(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return RespondError(res, "args inválidos");
    std::string p;
    if (!Arg(*parsed.value, 0, p)) return RespondError(res, "path requerido");
    RespondOk(res,
              Value(fs::path(p).lexically_normal().string()).Serialize().c_str());
}

} // namespace pathimpl

// dirs por plataforma
namespace pathdirs {
extern void homeDir(const ow_request_t*, ow_response_t*);
extern void appDataDir(const ow_request_t*, ow_response_t*);
extern void userDataDir(const ow_request_t*, ow_response_t*);
extern void cacheDir(const ow_request_t*, ow_response_t*);
extern void tempDir(const ow_request_t*, ow_response_t*);
extern void configDir(const ow_request_t*, ow_response_t*);
extern void exeDir(const ow_request_t*, ow_response_t*);
extern void cwd(const ow_request_t*, ow_response_t*);
}

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    using namespace pathimpl;
    static const ow_fn_entry_t all[] = {
        {"join", &join},           {"resolve", &resolve},
        {"dirname", &dirname},     {"basename", &basenameFn},
        {"extname", &extname},     {"normalize", &normalize},
        {"homeDir", &pathdirs::homeDir},
        {"appDataDir", &pathdirs::appDataDir},
        {"userDataDir", &pathdirs::userDataDir},
        {"cacheDir", &pathdirs::cacheDir},
        {"tempDir", &pathdirs::tempDir},
        {"configDir", &pathdirs::configDir},
        {"exeDir", &pathdirs::exeDir},
        {"cwd", &pathdirs::cwd},
    };
    static const ow_module_desc_t d{
        "path", OW_VERSION_STRING, all, sizeof(all) / sizeof(all[0])};
    return &d;
}
