// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
#include "ModuleLoader.hpp"

#include "Log.hpp"
#include "../Bridge/Dispatcher.hpp"

#include <dlfcn.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <mutex>

namespace ow {

namespace {
std::mutex g_mu;
std::vector<void*> g_handles;
std::vector<std::filesystem::path> SplitPathList(const char* raw) {
    std::vector<std::filesystem::path> out;
    if (!raw || !*raw) return out;
    std::string_view s(raw);
    size_t start = 0;
    while (start <= s.size()) {
        auto end = s.find(':', start);
        if (end == std::string_view::npos) end = s.size();
        if (end > start) out.emplace_back(s.substr(start, end - start));
        start = end + 1;
    }
    return out;
}
} // namespace

std::vector<std::filesystem::path> ModuleLoader::SearchPaths() {
    std::vector<std::filesystem::path> paths = SplitPathList(std::getenv("OW_MODULES_DIR"));
    char exeBuf[4096] = {};
    ssize_t n = readlink("/proc/self/exe", exeBuf, sizeof(exeBuf) - 1);
    if (n > 0) {
        exeBuf[n] = 0;
        paths.emplace_back(std::filesystem::path(exeBuf).parent_path() / "modules");
    }
    return paths;
}

size_t ModuleLoader::RegisterStatic(const ow_module_desc_t* desc) {
    return Dispatcher::Get().RegisterModule(desc, "builtin");
}

size_t ModuleLoader::LoadFile(const std::filesystem::path& file) {
    void* handle = dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        log::Error("loader", std::string("dlopen falló: ") + dlerror());
        return 0;
    }
    auto entry = reinterpret_cast<ow_module_entry_t>(dlsym(handle, "ow_module_descriptor"));
    if (!entry) {
        log::Error("loader", file.string() + ": símbolo ow_module_descriptor ausente");
        dlclose(handle);
        return 0;
    }
    const ow_module_desc_t* desc = entry();
    size_t count = Dispatcher::Get().RegisterModule(desc, file.filename().string());
    if (count > 0) {
        std::lock_guard lock(g_mu);
        g_handles.push_back(handle);
    } else {
        dlclose(handle);
    }
    return count;
}

size_t ModuleLoader::LoadAll() {
    size_t total = 0;
    for (const auto& dir : SearchPaths()) {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) continue;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
            if (!e.is_regular_file()) continue;
            auto ext = e.path().extension().string();
            if (ext != ".so" && ext != ".owm") continue;
            total += LoadFile(e.path());
        }
    }
    return total;
}

void ModuleLoader::Shutdown() {
    std::lock_guard lock(g_mu);
    for (void* h : g_handles) dlclose(h);
    g_handles.clear();
}

} // namespace ow
