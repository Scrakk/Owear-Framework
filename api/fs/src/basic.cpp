// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/fs/src/basic.cpp — operaciones básicas de la API fs.
//   readText readFile writeFile readDir stat mkdir remove exists
//
#include "ow/Base64.h"
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow/Shm.h"
#include "ow_api.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace fsimpl {

namespace fs = std::filesystem;

using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

bool ArgPath(const Value& args, std::string& out) {
    if (!args.IsArray() || args.AsArray().empty() || !args.AsArray()[0].IsString())
        return false;
    out = args.AsArray()[0].AsString();
    return !out.empty();
}

std::string ReadAll(const fs::path& p, std::string& err) {
    std::ifstream f(p, std::ios::binary);
    if (!f) { err = "no se pudo abrir " + p.string(); return {}; }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void readText(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return RespondError(res, "args inválidos");
    std::string path;
    if (!ArgPath(*parsed.value, path)) return RespondError(res, "path requerido");
    std::string err;
    std::string data = ReadAll(path, err);
    if (!err.empty()) return RespondError(res, err);
    RespondOk(res, Value(std::move(data)).Serialize().c_str());
}

void readFile(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return RespondError(res, "args inválidos");
    std::string path;
    if (!ArgPath(*parsed.value, path)) return RespondError(res, "path requerido");
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return RespondError(res, "no es archivo: " + path);
    uintmax_t size = fs::file_size(path, ec);
    if (ec) return RespondError(res, "stat falló");

    static constexpr uintmax_t kShmThreshold = 256 * 1024; // 256 KB
    if (size >= kShmThreshold) {
        // F3: región compartida — el renderer la lee con ow.readShared()
        // sin pasar por JSON/base64 (scheme ow-shm:// sirve el mmap directo).
        std::string err;
        std::string data = ReadAll(path, err);
        if (!err.empty()) return RespondError(res, err);
        const char* id = ow_shm_put(reinterpret_cast<const uint8_t*>(data.data()),
                                    data.size());
        if (id && *id) {
            std::string json = "{\"__ow_shm\":{\"id\":\"" + std::string(id) +
                               "\",\"size\":" + std::to_string(size) + "}}";
            RespondOk(res, json.c_str());
            return;
        }
        // fallo SHM → cae a base64
    }

    std::string err;
    std::string data = ReadAll(path, err);
    if (!err.empty()) return RespondError(res, err);
    // fallback v1: base64 embebido en JSON
    static thread_local std::string b64;
    b64 = ow::b64::Encode(data);
    std::string json = std::string("{\"b64\":\"") + b64 + "\"}";
    RespondOk(res, json.c_str());
}

void writeFile(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().size() < 2)
        return RespondError(res, "se esperan [path, data, encoding?]");
    const auto& a = parsed.value->AsArray();
    if (!a[0].IsString()) return RespondError(res, "path requerido");
    std::string encoding = "utf8";
    if (a.size() > 2 && a[2].IsString()) encoding = a[2].AsString();

    std::string bytes;
    if (encoding == "base64") {
        if (!a[1].IsString() || !ow::b64::Decode(a[1].AsString(), bytes))
            return RespondError(res, "data base64 inválida");
    } else {
        bytes = a[1].AsString();
    }

    std::error_code ec;
    fs::path p(a[0].AsString());
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return RespondError(res, "no se pudo escribir " + p.string());
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!f) return RespondError(res, "escritura fallida");
    RespondOk(res, "null");
}

void readDir(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return RespondError(res, "args inválidos");
    std::string path;
    if (!ArgPath(*parsed.value, path)) return RespondError(res, "path requerido");
    std::error_code ec;
    if (!fs::is_directory(path, ec)) return RespondError(res, "no es directorio: " + path);
    ow::json::Array arr;
    for (const auto& e : fs::directory_iterator(path, ec)) {
        std::string type = e.is_directory(ec) ? "dir"
                           : e.is_regular_file(ec) ? "file" : "other";
        ow::json::Object obj;
        obj.emplace_back("name", ow::json::Value(e.path().filename().string()));
        obj.emplace_back("type", ow::json::Value(std::move(type)));
        arr.emplace_back(ow::json::Value(std::move(obj)));
    }
    RespondOk(res, ow::json::Value(std::move(arr)).Serialize().c_str());
}

void statFn(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return RespondError(res, "args inválidos");
    std::string path;
    if (!ArgPath(*parsed.value, path)) return RespondError(res, "path requerido");
    std::error_code ec;
    fs::file_status st = fs::status(path, ec);
    if (ec || st.type() == fs::file_type::not_found) return RespondOk(res, "null");
    ow::json::Object obj;
    obj.emplace_back("size", ow::json::Value(
        static_cast<int64_t>(fs::is_regular_file(st) ? fs::file_size(path, ec) : 0)));
    obj.emplace_back("isFile", ow::json::Value(fs::is_regular_file(st)));
    obj.emplace_back("isDir", ow::json::Value(fs::is_directory(st)));
    auto ftime = fs::last_write_time(path, ec);
    int64_t mtimeMs = 0;
    if (!ec) {
        auto sys = std::chrono::time_point_cast<std::chrono::milliseconds>(
            ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        mtimeMs = sys.time_since_epoch().count();
    }
    obj.emplace_back("mtimeMs", ow::json::Value(mtimeMs));
    RespondOk(res, ow::json::Value(std::move(obj)).Serialize().c_str());
}

void mkdirFn(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return RespondError(res, "args inválidos");
    std::string path;
    if (!ArgPath(*parsed.value, path)) return RespondError(res, "path requerido");
    bool recursive = parsed.value->AsArray().size() > 1 && parsed.value->AsArray()[1].AsBool();
    std::error_code ec;
    if (recursive) fs::create_directories(path, ec);
    else fs::create_directory(path, ec);
    if (ec) return RespondError(res, "mkdir falló: " + ec.message());
    RespondOk(res, "null");
}

void removeFn(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return RespondError(res, "args inválidos");
    std::string path;
    if (!ArgPath(*parsed.value, path)) return RespondError(res, "path requerido");
    bool recursive = parsed.value->AsArray().size() > 1 && parsed.value->AsArray()[1].AsBool();
    std::error_code ec;
    if (recursive) fs::remove_all(path, ec);
    else fs::remove(path, ec);
    if (ec) return RespondError(res, "remove falló: " + ec.message());
    RespondOk(res, "null");
}

void exists(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return RespondError(res, "args inválidos");
    std::string path;
    if (!ArgPath(*parsed.value, path)) return RespondError(res, "path requerido");
    std::error_code ec;
    bool ok = fs::exists(path, ec);
    RespondOk(res, ok ? "true" : "false");
}
} // namespace fsimpl

// ── tabla exportada ──
namespace fsimpl {  // definidos en meta.cpp / watch.cpp
extern void copy(const ow_request_t*, ow_response_t*);
extern void renameFn(const ow_request_t*, ow_response_t*);
extern void chmodFn(const ow_request_t*, ow_response_t*);
extern void symlinkFn(const ow_request_t*, ow_response_t*);
extern void readlinkFn(const ow_request_t*, ow_response_t*);
extern void lstat(const ow_request_t*, ow_response_t*);
extern void realpath(const ow_request_t*, ow_response_t*);
extern void mkdtemp(const ow_request_t*, ow_response_t*);
extern void accessFn(const ow_request_t*, ow_response_t*);
extern void truncateFn(const ow_request_t*, ow_response_t*);
extern void utimes(const ow_request_t*, ow_response_t*);
extern void watch(const ow_request_t*, ow_response_t*);
extern void unwatch(const ow_request_t*, ow_response_t*);
} // namespace fsimpl

namespace fshandle { // handles.cpp
extern void openFn(const ow_request_t*, ow_response_t*);
extern void readFn(const ow_request_t*, ow_response_t*);
extern void writeFn(const ow_request_t*, ow_response_t*);
extern void sizeFn(const ow_request_t*, ow_response_t*);
extern void closeFn(const ow_request_t*, ow_response_t*);
} // namespace fshandle

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"readText", &fsimpl::readText},
        {"readFile", &fsimpl::readFile},
        {"writeFile", &fsimpl::writeFile},
        {"readDir", &fsimpl::readDir},
        {"stat", &fsimpl::statFn},
        {"mkdir", &fsimpl::mkdirFn},
        {"remove", &fsimpl::removeFn},
        {"exists", &fsimpl::exists},
        // meta
        {"copy", &fsimpl::copy},
        {"rename", &fsimpl::renameFn},
        {"chmod", &fsimpl::chmodFn},
        {"symlink", &fsimpl::symlinkFn},
        {"readlink", &fsimpl::readlinkFn},
        {"lstat", &fsimpl::lstat},
        {"realpath", &fsimpl::realpath},
        {"mkdtemp", &fsimpl::mkdtemp},
        {"access", &fsimpl::accessFn},
        {"truncate", &fsimpl::truncateFn},
        {"utimes", &fsimpl::utimes},
        // handles
        {"open", &fshandle::openFn},
        {"read", &fshandle::readFn},
        {"write", &fshandle::writeFn},
        {"size", &fshandle::sizeFn},
        {"close", &fshandle::closeFn},
        // watch
        {"watch", &fsimpl::watch},
        {"unwatch", &fsimpl::unwatch},
    };
    static const ow_module_desc_t d{
        "fs", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}
