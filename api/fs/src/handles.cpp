// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/fs/src/handles.cpp — handles fd-style: open/read/write/close/size.
// Lecturas posicionales grandes devuelven handle SHM (sin base64).
//
#include "ow/Base64.h"
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow/Shm.h"
#include "ow_api.h"

#include <fstream>
#include <map>
#include <mutex>

namespace fshandle {

namespace fs = std::filesystem;

using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

struct Handle {
    std::fstream file;
    std::string path;
};

std::mutex g_mu;
std::map<int, Handle> g_handles;
int g_nextId = 1;

bool GetArg(const Value& args, size_t idx, std::string& out) {
    if (!args.IsArray() || args.AsArray().size() <= idx ||
        !args.AsArray()[idx].IsString())
        return false;
    out = args.AsArray()[idx].AsString();
    return true;
}

void openFn(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return RespondError(res, "args inválidos");
    std::string path, flags = "r";
    if (!GetArg(*parsed.value, 0, path)) return RespondError(res, "path requerido");
    if (parsed.value->AsArray().size() > 1 && parsed.value->AsArray()[1].IsString())
        flags = parsed.value->AsArray()[1].AsString();

    std::ios::openmode mode = std::ios::binary;
    if (flags == "r") mode |= std::ios::in;
    else if (flags == "w") mode |= std::ios::out | std::ios::trunc;
    else if (flags == "a") mode |= std::ios::out | std::ios::app;
    else if (flags == "r+") mode |= std::ios::in | std::ios::out;
    else if (flags == "w+") mode |= std::ios::in | std::ios::out | std::ios::trunc;
    else return RespondError(res, "flags inválidos (r|w|a|r+|w+)");

    Handle h;
    h.path = path;
    h.file.open(path, mode);
    if (!h.file.is_open()) return RespondError(res, "open falló: " + path);

    std::lock_guard lock(g_mu);
    int id = g_nextId++;
    g_handles[id] = std::move(h);
    RespondOk(res, Value(static_cast<int64_t>(id)).Serialize().c_str());
}

Handle* GetHandle(const Value& args, size_t idx, int& outId) {
    if (!args.IsArray() || args.AsArray().size() <= idx || !args.AsArray()[idx].IsNumber())
        return nullptr;
    outId = static_cast<int>(args.AsArray()[idx].AsInt());
    auto it = g_handles.find(outId);
    return it == g_handles.end() ? nullptr : &it->second;
}

void readFn(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return RespondError(res, "args inválidos");
    int id = -1;
    auto* h = GetHandle(*parsed.value, 0, id);
    if (!h) return RespondError(res, "fd inválido");

    uint64_t length = 4 * 1024 * 1024; // default 4 MB
    if (parsed.value->AsArray().size() > 2 && parsed.value->AsArray()[2].IsNumber())
        length = static_cast<uint64_t>(parsed.value->AsArray()[2].AsInt());

    if (parsed.value->AsArray().size() > 1 && parsed.value->AsArray()[1].IsNumber())
        h->file.seekg(static_cast<std::streamoff>(parsed.value->AsArray()[1].AsInt()));

    std::string buf;
    buf.resize(length);
    h->file.read(buf.data(), static_cast<std::streamsize>(length));
    buf.resize(h->file.gcount() < 0 ? 0 : static_cast<size_t>(h->file.gcount()));
    bool eof = h->file.eof() || buf.empty();

    // ≥256 KB → SHM sin copia por JSON
    if (buf.size() >= 256 * 1024) {
        const char* sid = ow_shm_put(reinterpret_cast<const uint8_t*>(buf.data()),
                                     buf.size());
        if (sid && *sid) {
            std::string json = "{\"__ow_shm\":{\"id\":\"" + std::string(sid) +
                               "\",\"size\":" + std::to_string(buf.size()) +
                               "},\"eof\":" + (eof ? "true" : "false") + "}";
            return RespondOk(res, json.c_str());
        }
    }

    std::string b64 = ow::b64::Encode(buf);
    std::string json = "{\"b64\":\"" + b64 + "\",\"eof\":" +
                       (eof ? "true" : "false") + "}";
    RespondOk(res, json.c_str());
}

void writeFn(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        parsed.value->AsArray().size() < 2)
        return RespondError(res, "se esperan [fd, data, offset?]");
    const auto& a = parsed.value->AsArray();

    int id = -1;
    auto* h = GetHandle(*parsed.value, 0, id);
    if (!h) return RespondError(res, "fd inválido");

    std::string bytes;
    if (a[1].IsString()) {
        bytes = a[1].AsString();
    } else if (const Value* o = a[1].Find("b64"); o && o->IsString()) {
        if (!ow::b64::Decode(o->AsString(), bytes))
            return RespondError(res, "b64 inválido");
    } else {
        return RespondError(res, "data debe ser string o {b64}");
    }

    if (a.size() > 2 && a[2].IsNumber())
        h->file.seekp(static_cast<std::streamoff>(a[2].AsInt()));

    h->file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!h->file) return RespondError(res, "write falló");
    RespondOk(res, Value(static_cast<int64_t>(bytes.size())).Serialize().c_str());
}

void sizeFn(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return RespondError(res, "args inválidos");
    int id = -1;
    auto* h = GetHandle(*parsed.value, 0, id);
    if (!h) return RespondError(res, "fd inválido");
    auto pos = h->file.tellp();
    h->file.seekp(0, std::ios::end);
    auto end = h->file.tellp();
    h->file.seekp(pos);
    RespondOk(res, Value(static_cast<int64_t>(end)).Serialize().c_str());
}

void closeFn(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return RespondError(res, "args inválidos");
    int id = -1;
    if (!GetHandle(*parsed.value, 0, id)) return RespondError(res, "fd inválido");
    std::lock_guard lock(g_mu);
    g_handles[id].file.close();
    g_handles.erase(id);
    RespondOk(res, "null");
}

} // namespace fshandle
