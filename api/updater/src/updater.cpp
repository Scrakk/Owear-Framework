// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/updater/src/updater.cpp — autoUpdater genérico.
// Manifest JSON: {"version":"2.0.0","url":"https://…/app.bin","sha256":"…","notes":"…"}
// checkForUpdates(feed) · downloadUpdate() · installAndRelaunch()
// (reemplazo atómico del binario + execv). El IDE puede traer su propio
// updater si prefiere — esto es el backend estándar del framework.
//
#include "../../../src/Runtime/Http.hpp"
#include "../../../src/Runtime/Sha256.hpp"
#include "ow/Base64.h"
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow/Shm.h"
#include "ow_api.h"
#include "updater_platform.hpp"

#include <filesystem>
#include <fstream>

namespace upd {

namespace fs = std::filesystem;
using ow::json::Array;
using ow::json::Object;
using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static Value g_manifest = Value(nullptr);
static std::string g_downloaded;

static int SemverCompare(const std::string& a, const std::string& b) {
    auto parse = [](const std::string& s, int out[3]) {
        int idx = 0, num = 0;
        for (char c : s) {
            if (c == 'v') continue;
            if (c == '.') { if (idx < 3) out[idx++] = num; num = 0; }
            else if (c >= '0' && c <= '9') num = num * 10 + (c - '0');
        }
        if (idx < 3) out[idx++] = num;
        while (idx < 3) out[idx++] = 0;
    };
    int pa[3] = {}, pb[3] = {};
    parse(a, pa); parse(b, pb);
    for (int i = 0; i < 3; ++i)
        if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
    return 0;
}

// args: [feedUrl, currentVersion]
void checkForUpdates(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().size() < 2)
        return RespondError(res, "se esperan [feedUrl, currentVersion]");
    const auto& a = parsed.value->AsArray();
    std::string current = a[1].AsString();

    std::string body, err;
    if (!ow::http::DownloadToString(a[0].AsString(), body, err))
        return RespondError(res, err);
    auto manifest = ow::json::Parse(body);
    if (!manifest.value || !manifest.value->IsObject())
        return RespondError(res, "manifest inválido");
    const Value* ver = manifest.value->Find("version");
    if (!ver || !ver->IsString()) return RespondError(res, "manifest sin version");

    g_manifest = std::move(*manifest.value);
    int cmp = SemverCompare((*g_manifest.Find("version")).AsString(), current);
    Object o;
    o.emplace_back("updateAvailable", Value(cmp > 0));
    o.emplace_back("version", *g_manifest.Find("version"));
    if (const Value* n = g_manifest.Find("notes"); n)
        o.emplace_back("notes", *n);
    RespondOk(res, Value(std::move(o)).Serialize().c_str());
}

void downloadUpdate(const ow_request_t*, ow_response_t* res) {
    const Value* url = g_manifest.Find("url");
    const Value* sha = g_manifest.Find("sha256");
    if (!url || !url->IsString())
        return RespondError(res, "checkForUpdates primero");
    auto dest = fs::temp_directory_path() / "owear-update.bin";
    std::string err;
    if (!ow::http::DownloadToFile(url->AsString(), dest, err))
        return RespondError(res, err);

    if (sha && sha->IsString()) {
        std::ifstream f(dest, std::ios::binary);
        ow::crypto::Sha256 h;
        char buf[65536];
        while (f.read(buf, sizeof(buf)) || f.gcount() > 0)
            h.Update(reinterpret_cast<uint8_t*>(buf), static_cast<size_t>(f.gcount()));
        if (h.Hex() != sha->AsString()) {
            fs::remove(dest);
            return RespondError(res, "SHA256 del update no coincide");
        }
    }
    g_downloaded = dest.string();
    RespondOk(res, Value(g_downloaded).Serialize().c_str());
}

void installAndRelaunch(const ow_request_t*, ow_response_t* res) {
    if (g_downloaded.empty()) return RespondError(res, "downloadUpdate primero");

    std::string exe = CurrentExePath();
    if (exe.empty()) return RespondError(res, "no se resolvió el exe actual");

    RespondOk(res, "null");
    std::string err;
    if (!ReplaceAndRelaunch(g_downloaded, exe, err)) {
        // ya respondimos "ok" (el reemplazo pudo iniciar); registrar el fallo
        // real de relanzamiento no tiene canal de vuelta aquí, pero al menos
        // no dejamos un execv/relanzamiento silenciosamente indefinido.
        RespondError(res, err);
    }
}

} // namespace upd

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"checkForUpdates", &upd::checkForUpdates},
        {"downloadUpdate", &upd::downloadUpdate},
        {"installAndRelaunch", &upd::installAndRelaunch},
    };
    static const ow_module_desc_t d{
        "updater", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}
