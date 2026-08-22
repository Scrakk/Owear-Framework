// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Runtime/NodeManager.cpp — descarga/verificación/extracción (común).
// Spawn por plataforma en NodeManager_<platform>.cpp.
//
#include "NodeManager.hpp"

#include "../Core/Log.hpp"
#include "Http.hpp"
#include "Sha256.hpp"
#include "TarGz.hpp"
#include "ow/detail/minjson.hpp"

#include <chrono>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ow {

namespace fs = std::filesystem;

std::filesystem::path NodeManager::CacheRoot() {
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    if (xdg && *xdg) return fs::path(xdg) / "owear" / "node";
#ifdef _WIN32
    const char* la = std::getenv("LOCALAPPDATA");
    if (la && *la) return fs::path(la) / "owear" / "cache" / "node";
#endif
    const char* home = std::getenv("HOME");
    return fs::path(home && *home ? home : "/tmp") / ".cache" / "owear" / "node";
}

std::string NodeManager::PlatformTag() {
#ifdef _WIN32
    return "win";
#elif defined(__APPLE__)
    return "darwin";
#else
    return "linux";
#endif
}

std::string NodeManager::ArchTag() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__arm__)
    return "armv7l";
#elif defined(__riscv) && __riscv_xlen == 64
    return "riscv64";
#else
    return "x64";
#endif
}

bool NodeManager::FetchIndex(std::vector<Release>& out, std::string& error) {
    std::string body;
    if (!http::DownloadToString("https://nodejs.org/dist/index.json", body, error))
        return false;
    auto parsed = json::Parse(body);
    if (!parsed.value || !parsed.value->IsArray()) {
        error = "index.json inválido";
        return false;
    }
    for (const auto& e : parsed.value->AsArray()) {
        Release r;
        if (const json::Value* v = e.Find("version"); v && v->IsString())
            r.version = v->AsString();
        if (const json::Value* v = e.Find("lts"); v)
            r.lts = !v->IsNull(); // "lts" es codename string cuando aplica, null si no
        if (!r.version.empty()) out.push_back(std::move(r));
    }
    return !out.empty();
}

static int SemverCompare(const std::string& a, const std::string& b) {
    // a/b: "v22.4.0"
    auto parse = [](const std::string& s, int out[3]) {
        int idx = 0, num = 0;
        for (char c : s) {
            if (c == 'v') continue;
            if (c == '.') {
                if (idx < 3) out[idx++] = num;
                num = 0;
            } else if (c >= '0' && c <= '9') {
                num = num * 10 + (c - '0');
            }
        }
        if (idx < 3) out[idx++] = num;
        while (idx < 3) out[idx++] = 0;
    };
    int pa[3] = {}, pb[3] = {};
    parse(a, pa);
    parse(b, pb);
    for (int i = 0; i < 3; ++i)
        if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
    return 0;
}

const NodeManager::Release* NodeManager::Pick(const std::vector<Release>& list,
                                              const std::string& range) {
    const Release* best = nullptr;
    if (range.empty() || range == "latest") {
        for (const auto& r : list)
            if (!best || SemverCompare(r.version, best->version) > 0) best = &r;
        return best;
    }
    if (range == "lts") {
        for (const auto& r : list)
            if (r.lts && (!best || SemverCompare(r.version, best->version) > 0)) best = &r;
        return best;
    }
    // exacto o prefijo de major: "v22" | "22" | "v22.4.0"
    std::string want = range;
    if (!want.empty() && want[0] != 'v') want = "v" + want;
    for (const auto& r : list) {
        bool match = r.version == want;
        if (!match) {
            // prefijo: v22.x.x
            auto dot = want.find('.', 1);
            std::string prefix = dot == std::string::npos ? want : want.substr(0, dot);
            match = r.version.compare(0, prefix.size(), prefix) == 0 &&
                    (r.version.size() == prefix.size() || r.version[prefix.size()] == '.');
        }
        if (match && (!best || SemverCompare(r.version, best->version) > 0)) best = &r;
    }
    return best;
}

std::optional<fs::path> NodeManager::FindCached() {
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(CacheRoot(), ec)) {
        if (!e.is_directory()) continue;
#ifdef _WIN32
        auto bin = e.path() / "node.exe";
#else
        auto bin = e.path() / "bin" / "node";
#endif
        if (fs::exists(bin, ec)) return bin;
    }
    return std::nullopt;
}

Result<fs::path> NodeManager::Ensure(const std::string& range) {
    // 1. cache directa si el range es exacto y ya existe
    std::vector<Release> list;
    std::string error;
    if (!FetchIndex(list, error)) {
        // sin red → usa cache existente aunque no coincida
        if (auto cached = FindCached()) {
            log::Warn("node", "sin red; usando runtime cacheado");
            return Result<fs::path>::Ok(*cached);
        }
        return Result<fs::path>::Err(error);
    }

    const Release* rel = Pick(list, range);
    if (!rel) return Result<fs::path>::Err("ninguna release satisface '" + range + "'");

    std::string plat = PlatformTag();
    std::string arch = ArchTag();
    std::string dirName = "node-" + rel->version + "-" + plat + "-" + arch;
    std::string fileName = dirName + (plat == "win" ? ".zip" : ".tar.gz");
    fs::path target = CacheRoot() / dirName;

#ifndef _WIN32
    fs::path nodeBin = target / "bin" / "node";
#else
    fs::path nodeBin = target / "node.exe";
#endif

    std::error_code ec;
    if (fs::exists(nodeBin, ec)) return Result<fs::path>::Ok(nodeBin);

    // 2. descarga tarball + SHASUMS256.txt
    std::string base = "https://nodejs.org/dist/" + rel->version + "/";
    fs::path tmpDir = CacheRoot() / "tmp";
    fs::create_directories(tmpDir, ec);
    fs::path tgzPath = tmpDir / fileName;
    fs::path shaPath = tmpDir / "SHASUMS256.txt";

    log::Info("node", "descargando " + fileName);
    if (!http::DownloadToFile(base + fileName, tgzPath, error))
        return Result<fs::path>::Err(error);

    // 3. verificación SHA256
    std::string shasums;
    if (!http::DownloadToString(base + "SHASUMS256.txt", shasums, error))
        return Result<fs::path>::Err(error);

    crypto::Sha256 h;
    {
        std::ifstream f(tgzPath, std::ios::binary);
        char buf[65536];
        while (f.read(buf, sizeof(buf)) || f.gcount() > 0)
            h.Update(reinterpret_cast<uint8_t*>(buf), static_cast<size_t>(f.gcount()));
    }
    std::string actual = h.Hex();

    std::istringstream ss(shasums);
    std::string line;
    bool verified = false;
    while (std::getline(ss, line)) {
        auto pos = line.find(fileName);
        if (pos != std::string::npos && line.size() >= 64) {
            std::string expected = line.substr(0, 64);
            verified = (expected == actual);
            break;
        }
    }
    if (!verified) {
        fs::remove(tgzPath, ec);
        return Result<fs::path>::Err("SHA256 no coincide para " + fileName +
                                     " (esperaba suma registrada)");
    }
    log::Info("node", "sha256 verificado ✓");

    // 4. extracción
    if (plat == "win") {
        return Result<fs::path>::Err(
            "extracción .zip pendiente en Windows (F3); usa Node del sistema");
    }
    if (!archive::ExtractTarGz(tgzPath, CacheRoot(), error) || error.empty() == false) {
        fs::remove_all(target, ec);
        return Result<fs::path>::Err(error.empty() ? "extracción fallida" : error);
    }
    if (!fs::exists(nodeBin, ec)) {
        fs::remove_all(target, ec);
        return Result<fs::path>::Err("extracción incompleta: falta " + nodeBin.string());
    }
    fs::permissions(nodeBin,
                    fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                        fs::perms::others_read | fs::perms::others_exec,
                    ec);
    fs::remove(tgzPath, ec);
    log::Info("node", "runtime listo: " + nodeBin.string());
    return Result<fs::path>::Ok(nodeBin);
}

} // namespace ow
