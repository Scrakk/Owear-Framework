// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/net/src/net.cpp — HTTP(S) nativo SIN CORS.
// Backend: cliente TLS propio del kernel (src/Runtime/Http.cpp).
//
#include "Runtime/Http.hpp"
#include "Runtime/Sha256.hpp"
#include "ow/Base64.h"
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow/Shm.h"
#include "ow_api.h"

#include <filesystem>
#include <fstream>
#include <map>

namespace netmod {

namespace fs = std::filesystem;
using ow::json::Array;
using ow::json::Object;
using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

// args: [ {method, url, headers{}, body?, bodyB64?, timeoutMs?} ]
void request(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || !parsed.value->AsArray()[0].IsObject())
        return RespondError(res, "objeto {method,url,...} requerido");
    const auto& o = parsed.value->AsArray()[0];

    std::string method = "GET", url;
    if (const Value* v = o.Find("method"); v && v->IsString()) method = v->AsString();
    if (const Value* v = o.Find("url"); v && v->IsString()) url = v->AsString();
    if (url.empty()) return RespondError(res, "url requerida");
    if (method != "GET") return RespondError(res, "v1 solo GET (POST con streams en F-next)");

    std::string body, err;
    if (!ow::http::DownloadToString(url, body, err)) return RespondError(res, err);

    Object out;
    out.emplace_back("status", Value(static_cast<int64_t>(200)));

    // cuerpo: SHM si grande, texto si no
    if (body.size() >= 256 * 1024) {
        const char* id = ow_shm_put(reinterpret_cast<const uint8_t*>(body.data()),
                                    body.size());
        Object shm;
        shm.emplace_back("id", Value(std::string(id ?: "")));
        shm.emplace_back("size", Value(static_cast<int64_t>(body.size())));
        out.emplace_back("body", Value(std::move(shm)));
    } else {
        out.emplace_back("body", Value(std::move(body)));
    }
    RespondOk(res, Value(std::move(out)).Serialize().c_str());
}

// args: [url, destPath]
void download(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().size() < 2)
        return RespondError(res, "se esperan [url, destPath]");
    const auto& a = parsed.value->AsArray();
    if (!a[0].IsString() || !a[1].IsString())
        return RespondError(res, "url y destPath string");

    std::string err;
    if (!ow::http::DownloadToFile(a[0].AsString(), a[1].AsString(), err))
        return RespondError(res, err);

    // sha256 del resultado (integridad para updaters/descargas)
    std::ifstream f(a[1].AsString(), std::ios::binary);
    ow::crypto::Sha256 h;
    char buf[65536];
    while (f.read(buf, sizeof(buf)) || f.gcount() > 0)
        h.Update(reinterpret_cast<uint8_t*>(buf), static_cast<size_t>(f.gcount()));

    Object o;
    o.emplace_back("sha256", Value(h.Hex()));
    RespondOk(res, Value(std::move(o)).Serialize().c_str());
}

} // namespace netmod

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"request", &netmod::request},
        {"download", &netmod::download},
    };
    static const ow_module_desc_t d{
        "net", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}
