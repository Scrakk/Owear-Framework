// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/process/src/module.cpp — API process: spawn · write · kill · list · PTY.
//
#include "registry.hpp"
#include "ow/Base64.h"
#include "ow/Json.h"
#include "ow/Module.h"

#include <csignal>
#include <vector>

// implementaciones por plataforma (namespace proc)
namespace proc {
long SpawnPipes(int* outId, const std::string& cmd,
                std::vector<std::string> args, const std::string& cwd,
                std::map<std::string, std::string> env, bool useShell,
                uint32_t windowId, std::string& err);
bool WriteStdin(int id, const char* data, size_t len, std::string& err);
bool Kill(int id, int sig, std::string& err);
std::vector<int> List();
long PtyOpen(int* outId, const std::string& cmd,
             std::vector<std::string> args, const std::string& cwd,
             std::map<std::string, std::string> env, int cols, int rows,
             uint32_t windowId, std::string& err);
bool PtyResize(int id, int cols, int rows, std::string& err);
} // namespace proc
using proc::SpawnPipes;
using proc::WriteStdin;
using proc::Kill;
using proc::List;
using proc::PtyOpen;
using proc::PtyResize;

#ifdef _WIN32
bool WritePtyWin(int id, const char* d, size_t l, std::string& e);
#endif

namespace procmod {

using ow::json::Array;
using ow::json::Object;
using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static std::vector<std::string> StrArray(const Value& args, size_t idx) {
    std::vector<std::string> out;
    if (!args.IsArray() || args.AsArray().size() <= idx) return out;
    const auto& v = args.AsArray()[idx];
    if (v.IsArray())
        for (const auto& s : v.AsArray())
            if (s.IsString()) out.push_back(s.AsString());
    return out;
}

static std::string OptStr(const Value& args, size_t idx) {
    if (!args.IsArray() || args.AsArray().size() <= idx) return {};
    const auto& v = args.AsArray()[idx];
    return v.IsString() ? v.AsString() : "";
}

static std::map<std::string, std::string> OptEnv(const Value& args, size_t idx) {
    std::map<std::string, std::string> out;
    if (!args.IsArray() || args.AsArray().size() <= idx) return out;
    const auto& v = args.AsArray()[idx];
    if (v.IsObject())
        for (const auto& [k, val] : v.AsObject())
            out[k] = val.IsString() ? val.AsString() : val.Serialize();
    return out;
}

void spawn(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty())
        return RespondError(res, "cmd requerido");
    const auto& a = parsed.value->AsArray();
    if (!a[0].IsString()) return RespondError(res, "cmd debe ser string");

    int id = -1;
    std::string err;
    long pid = SpawnPipes(&id, a[0].AsString(), StrArray(*parsed.value, 1),
                          OptStr(*parsed.value, 2), OptEnv(*parsed.value, 3),
                          a.size() > 4 && a[4].AsBool(), req->window_id, err);
    if (pid < 0) return RespondError(res, err.empty() ? "spawn falló" : err);

    Object o;
    o.emplace_back("procId", Value(static_cast<int64_t>(id)));
    o.emplace_back("pid", Value(static_cast<int64_t>(pid)));
    RespondOk(res, Value(std::move(o)).Serialize().c_str());
}

void writeFn(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().size() < 2)
        return RespondError(res, "se esperan [procId, data]");
    const auto& a = parsed.value->AsArray();
    int id = static_cast<int>(a[0].AsInt());

    std::string bytes;
    if (a[1].IsString()) {
        bytes = a[1].AsString();
    } else if (const Value* o = a[1].Find("b64"); o && o->IsString()) {
        if (!ow::b64::Decode(o->AsString(), bytes)) return RespondError(res, "b64 inválido");
    } else {
        return RespondError(res, "data inválida");
    }

    std::lock_guard lock(proc::g_mu);
    auto* p = proc::Get(id);
    if (!p) return RespondError(res, "proc inexistente");

    std::string err;
#ifdef _WIN32
    bool ok = p->kind == proc::Kind::Pty ? WritePtyWin(id, bytes.data(), bytes.size(), err)
                                         : WriteStdin(id, bytes.data(), bytes.size(), err);
#else
    bool ok = WriteStdin(id, bytes.data(), bytes.size(), err);
#endif
    if (!ok) return RespondError(res, err);
    RespondOk(res, "null");
}

void killFn(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty())
        return RespondError(res, "procId requerido");
    int id = static_cast<int>(parsed.value->AsArray()[0].AsInt());
    int sig = SIGTERM;
#ifndef _WIN32
    if (parsed.value->AsArray().size() > 1 && parsed.value->AsArray()[1].IsNumber())
        sig = static_cast<int>(parsed.value->AsArray()[1].AsInt());

    // PTY: cerrar el master termina la sesión
    std::lock_guard lock(proc::g_mu);
    if (auto* p = proc::Get(id); p && p->kind == proc::Kind::Pty && p->masterFd >= 0) {
        ::close(p->masterFd);
        p->masterFd = -1;
        return RespondOk(res, "null");
    }
#else
    (void)sig;
#endif

    std::string err;
    if (!Kill(id, sig, err)) return RespondError(res, err);
    RespondOk(res, "null");
}

void list(const ow_request_t*, ow_response_t* res) {
    Array arr;
    for (int id : List()) arr.emplace_back(Value(static_cast<int64_t>(id)));
    RespondOk(res, Value(std::move(arr)).Serialize().c_str());
}

void ptyOpen(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty() ||
        !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "cmd requerido (ej: /bin/bash)");
    const auto& a = parsed.value->AsArray();

    int cols = 80, rows = 24;
    if (a.size() > 3 && a[3].IsNumber()) cols = static_cast<int>(a[3].AsInt());
    if (a.size() > 4 && a[4].IsNumber()) rows = static_cast<int>(a[4].AsInt());

    int id = -1;
    std::string err;
    long pid = PtyOpen(&id, a[0].AsString(), StrArray(*parsed.value, 1),
                       OptStr(*parsed.value, 2), OptEnv(*parsed.value, 5),
                       cols, rows, req->window_id, err);
    if (pid < 0) return RespondError(res, err.empty() ? "pty.open falló" : err);

    Object o;
    o.emplace_back("procId", Value(static_cast<int64_t>(id)));
    o.emplace_back("pid", Value(static_cast<int64_t>(pid)));
    RespondOk(res, Value(std::move(o)).Serialize().c_str());
}

void ptyResize(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        parsed.value->AsArray().size() < 3)
        return RespondError(res, "se esperan [procId, cols, rows]");
    const auto& a = parsed.value->AsArray();
    std::string err;
    if (!PtyResize(static_cast<int>(a[0].AsInt()), static_cast<int>(a[1].AsInt()),
                   static_cast<int>(a[2].AsInt()), err))
        return RespondError(res, err);
    RespondOk(res, "null");
}

} // namespace procmod

extern "C" OW_MODULE_EXPORT void ow_module_set_host(const ow_module_host_t* h) {
    proc::SetHost(h);
}

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"spawn", &procmod::spawn},
        {"write", &procmod::writeFn},
        {"kill", &procmod::killFn},
        {"list", &procmod::list},
        {"ptyOpen", &procmod::ptyOpen},
        {"ptyResize", &procmod::ptyResize},
    };
    static const ow_module_desc_t d{
        "process", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}
