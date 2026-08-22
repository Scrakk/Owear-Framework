// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/fs/src/meta.cpp — metadatos y operaciones de estructura: copy, rename,
// chmod, symlink, lstat, realpath, mkdtemp, access, truncate, utimes.
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#include <chrono>
#include <filesystem>
#include <fstream>

#ifndef _WIN32
#include <sys/time.h>
#include <unistd.h>
#include <cstdio>
#endif

namespace fsimpl {

namespace fs = std::filesystem;

using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static bool Arg(const Value& args, size_t idx, std::string& out) {
    if (!args.IsArray() || args.AsArray().size() <= idx ||
        !args.AsArray()[idx].IsString())
        return false;
    out = args.AsArray()[idx].AsString();
    return !out.empty();
}

void copy(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return RespondError(res, "args inválidos");
    std::string src, dst;
    if (!Arg(*parsed.value, 0, src)) return RespondError(res, "src requerido");
    if (!Arg(*parsed.value, 1, dst)) return RespondError(res, "dest requerido");
    bool overwrite = parsed.value->AsArray().size() > 2 &&
                     parsed.value->AsArray()[2].AsBool();
    std::error_code ec;
    auto opts = overwrite ? fs::copy_options::overwrite_existing
                          : fs::copy_options::none;
    fs::copy(src, dst, opts | fs::copy_options::recursive, ec);
    if (ec) return RespondError(res, "copy falló: " + ec.message());
    RespondOk(res, "null");
}

void renameFn(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return RespondError(res, "args inválidos");
    std::string from, to;
    if (!Arg(*parsed.value, 0, from)) return RespondError(res, "oldPath requerido");
    if (!Arg(*parsed.value, 1, to)) return RespondError(res, "newPath requerido");
    std::error_code ec;
    fs::rename(from, to, ec);
    if (ec) return RespondError(res, "rename falló: " + ec.message());
    RespondOk(res, "null");
}

void chmodFn(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        parsed.value->AsArray().size() < 2)
        return RespondError(res, "se esperan [path, mode]");
    const auto& a = parsed.value->AsArray();
    if (!a[0].IsString() || !a[1].IsNumber())
        return RespondError(res, "path (str) y mode (octal numérico) requeridos");
    std::error_code ec;
    fs::permissions(a[0].AsString(),
                    static_cast<fs::perms>(a[1].AsInt()), fs::perm_options::replace, ec);
    if (ec) return RespondError(res, "chmod falló: " + ec.message());
    RespondOk(res, "null");
}

void symlinkFn(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return RespondError(res, "args inválidos");
    std::string target, link;
    if (!Arg(*parsed.value, 0, target)) return RespondError(res, "target requerido");
    if (!Arg(*parsed.value, 1, link)) return RespondError(res, "linkPath requerido");
    std::error_code ec;
    fs::create_symlink(target, link, ec);
    if (ec) return RespondError(res, "symlink falló: " + ec.message());
    RespondOk(res, "null");
}

void readlinkFn(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return RespondError(res, "args inválidos");
    std::string path;
    if (!Arg(*parsed.value, 0, path)) return RespondError(res, "path requerido");
    std::error_code ec;
    auto target = fs::read_symlink(path, ec);
    if (ec) return RespondError(res, "readlink falló: " + ec.message());
    RespondOk(res, Value(target.string()).Serialize().c_str());
}

void lstat(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return RespondError(res, "args inválidos");
    std::string path;
    if (!Arg(*parsed.value, 0, path)) return RespondError(res, "path requerido");
    std::error_code ec;
    fs::file_status st = fs::symlink_status(path, ec); // NO sigue symlinks
    if (ec || st.type() == fs::file_type::not_found)
        return RespondOk(res, "null");
    ow::json::Object o;
    o.emplace_back("isSymlink", Value(st.type() == fs::file_type::symlink));
    o.emplace_back("isFile", Value(fs::is_regular_file(st)));
    o.emplace_back("isDir", Value(fs::is_directory(st)));
    o.emplace_back("isCharDevice", Value(st.type() == fs::file_type::character));
    o.emplace_back("isFifo", Value(st.type() == fs::file_type::fifo));
    o.emplace_back("isSocket", Value(st.type() == fs::file_type::socket));
    RespondOk(res, ow::json::Value(std::move(o)).Serialize().c_str());
}

void realpath(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return RespondError(res, "args inválidos");
    std::string path;
    if (!Arg(*parsed.value, 0, path)) return RespondError(res, "path requerido");
    std::error_code ec;
    auto rp = fs::canonical(path, ec);
    if (ec) return RespondError(res, "realpath falló: " + ec.message());
    RespondOk(res, Value(rp.string()).Serialize().c_str());
}

void mkdtemp(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    std::string prefix = "owear-";
    if (parsed.value && parsed.value->IsArray() && !parsed.value->AsArray().empty() &&
        parsed.value->AsArray()[0].IsString())
        prefix = parsed.value->AsArray()[0].AsString();

    // plantilla estilo mkdtemp(3)
    auto tmpl = fs::temp_directory_path() / (prefix + "XXXXXX");
    std::string t = tmpl.string();
#ifdef _WIN32
    return RespondError(res, "mkdtemp no soportado en Windows v1");
#else
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", t.c_str());
    if (!::mkdtemp(buf))
        return RespondError(res, "mkdtemp falló");
    RespondOk(res, ow::json::Value(std::string(buf)).Serialize().c_str());
#endif
}

void accessFn(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return RespondError(res, "args inválidos");
    std::string path;
    if (!Arg(*parsed.value, 0, path)) return RespondError(res, "path requerido");
    int mode = 4; // lectura por defecto
    if (parsed.value->AsArray().size() > 1 &&
        parsed.value->AsArray()[1].IsNumber())
        mode = static_cast<int>(parsed.value->AsArray()[1].AsInt());
#ifdef _WIN32
    std::error_code ec;
    bool ok = fs::exists(path, ec);
#else
    bool ok = ::access(path.c_str(), mode) == 0;
#endif
    RespondOk(res, ok ? "true" : "false");
}

void truncateFn(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        parsed.value->AsArray().size() < 2)
        return RespondError(res, "se esperan [path, size]");
    const auto& a = parsed.value->AsArray();
    if (!a[0].IsString() || !a[1].IsNumber())
        return RespondError(res, "path (str) y size (num) requeridos");
    std::error_code ec;
    fs::resize_file(a[0].AsString(), static_cast<uintmax_t>(a[1].AsInt()), ec);
    if (ec) return RespondError(res, "truncate falló: " + ec.message());
    RespondOk(res, "null");
}

void utimes(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
#ifndef _WIN32
    if (!parsed.value || !parsed.value->IsArray() ||
        parsed.value->AsArray().size() < 3)
        return RespondError(res, "se esperan [path, atimeMs, mtimeMs]");
    const auto& a = parsed.value->AsArray();
    if (!a[0].IsString()) return RespondError(res, "path requerido");

    struct timeval tv[2];
    tv[0].tv_sec = a[1].AsInt() / 1000;
    tv[0].tv_usec = (a[1].AsInt() % 1000) * 1000;
    tv[1].tv_sec = a[2].AsInt() / 1000;
    tv[1].tv_usec = (a[2].AsInt() % 1000) * 1000;
    if (::utimes(a[0].AsString().c_str(), tv) != 0)
        return RespondError(res, "utimes falló");
    RespondOk(res, "null");
#else
    (void)req; (void)res;
    RespondError(res, "utimes no soportado en Windows v1");
#endif
}

} // namespace fsimpl

const ow_fn_entry_t kMetaFns[] = {
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
};
