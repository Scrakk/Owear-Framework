// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Bridge/Shm.cpp — registro de regiones compartidas.
//
// Implementación v1 (Linux/macOS/Windows): archivo respaldado en
// $XDG_RUNTIME_DIR/owear-shm/<id> + mmap MAP_SHARED del proceso kernel.
// El WebView lee vía scheme `ow-shm://<id>` que sirve el puntero mapeado
// SIN copiar (g_bytes_new_static / IStream sobre memoria / NSData bytesNoCopy).
//
#include "Shm.hpp"

#include "../Core/Log.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

#include <cstdio>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>

namespace ow::shm {

namespace {

struct Region {
    std::string id;
    uint8_t* data = nullptr;
    size_t size = 0;
#ifdef _WIN32
    HANDLE map = nullptr;
#else
    int fd = -1;
#endif
    std::string path;
};

std::mutex g_mu;
std::unordered_map<std::string, Region> g_regions;

std::string RuntimeDir() {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg && *xdg) return xdg;
#ifdef _WIN32
    const char* la = std::getenv("LOCALAPPDATA");
    if (la && *la) return std::string(la) + "\\Temp";
#endif
    const char* tmp = std::getenv("TMPDIR");
    return (tmp && *tmp) ? tmp : "/tmp";
}

std::string GenId() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(rng()));
    return buf;
}

} // namespace

const char* Put(const uint8_t* data, size_t len) {
    if (!data || len == 0) return "";

    Region r;
    r.id = GenId();
    r.size = len;
    r.path = RuntimeDir() + "/owear-shm-" + r.id;

    // crea y llena el backing file
#ifdef _WIN32
    HANDLE file = CreateFileA(r.path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) return "";
    DWORD written = 0;
    WriteFile(file, data, static_cast<DWORD>(len), &written, nullptr);
    HANDLE map = CreateFileMappingA(file, nullptr, PAGE_READWRITE, 0,
                                    static_cast<DWORD>(len), nullptr);
    CloseHandle(file);
    if (!map) { DeleteFileA(r.path.c_str()); return ""; }
    r.map = map;
    void* view = MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, len);
    if (!view) { CloseHandle(map); DeleteFileA(r.path.c_str()); return ""; }
    memcpy(view, data, len);
    r.data = static_cast<uint8_t*>(view);
#else
    int fd = ::open(r.path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return "";
    if (::ftruncate(fd, static_cast<off_t>(len)) != 0) {
        ::close(fd);
        ::unlink(r.path.c_str());
        return "";
    }
    void* m = ::mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) {
        ::close(fd);
        ::unlink(r.path.c_str());
        return "";
    }
    memcpy(m, data, len);
    r.fd = fd;
    r.data = static_cast<uint8_t*>(m);
#endif

    std::lock_guard lock(g_mu);
    auto [it, ok] = g_regions.emplace(r.id, std::move(r));
    if (!ok) return "";
    log::Debug("shm", "región " + it->second.id + " (" +
                          std::to_string(len) + " bytes)");
    return it->second.id.c_str(); // válido hasta shutdown (estático en mapa)
}

const uint8_t* Data(const char* id, size_t* out_len) {
    std::lock_guard lock(g_mu);
    auto it = g_regions.find(id ? id : "");
    if (it == g_regions.end()) return nullptr;
    if (out_len) *out_len = it->second.size;
    return it->second.data;
}

void Shutdown() {
    std::lock_guard lock(g_mu);
    for (auto& [id, r] : g_regions) {
#ifdef _WIN32
        if (r.data) UnmapViewOfFile(r.data);
        if (r.map) CloseHandle(r.map);
        DeleteFileA(r.path.c_str());
#else
        if (r.data) ::munmap(r.data, r.size);
        if (r.fd >= 0) ::close(r.fd);
        ::unlink(r.path.c_str());
#endif
    }
    g_regions.clear();
}

size_t Count() {
    std::lock_guard lock(g_mu);
    return g_regions.size();
}

} // namespace ow::shm

// ── ABI-C pública para módulos ──────────────────────────────────────────────

extern "C" {

const char* ow_shm_put(const uint8_t* data, size_t len) {
    return ow::shm::Put(data, len);
}
const uint8_t* ow_shm_data(const char* id, size_t* out_len) {
    return ow::shm::Data(id, out_len);
}
void ow_shm_shutdown(void) { ow::shm::Shutdown(); }

} // extern "C"
