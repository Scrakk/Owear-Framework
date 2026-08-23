// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
#include "ModuleLoader.hpp"

#include "Log.hpp"
#include "../Bridge/Dispatcher.hpp"
#include "ow_api.h"
#include "../Bridge/Shm.hpp"
#include "../Control/ControlServer.hpp"
#include "ow/App.h"
#include "ow/Bridge/Codec.h"
#include "ow/Window.h"
#include "ow/detail/minjson.hpp"

#include <map>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <dlfcn.h>
  #include <unistd.h>
#endif
#if defined(__APPLE__)
  #include <mach-o/dyld.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <mutex>

// receptor del host para builtins (definido en BuiltinUtil_<plat>)
#if defined(OW_BUILTINS_GTK)
extern "C" void ow_builtin_receive_host(const ::ow_module_host_t* h);
#endif

namespace ow {

namespace {
std::mutex g_mu;
std::vector<void*> g_handles;
std::vector<std::filesystem::path> SplitPathList(const char* raw) {
    std::vector<std::filesystem::path> out;
    if (!raw || !*raw) return out;
    std::string_view s(raw);
#if defined(_WIN32)
    const char sep = ';'; // ':' colisiona con las unidades (C:\…)
#else
    const char sep = ':';
#endif
    size_t start = 0;
    while (start <= s.size()) {
        auto end = s.find(sep, start);
        if (end == std::string_view::npos) end = s.size();
        if (end > start) out.emplace_back(s.substr(start, end - start));
        start = end + 1;
    }
    return out;
}
} // namespace

namespace {

/// Emite un evento de módulo: window_id != 0 → a esa ventana;
/// 0 → broadcast al SDK (control socket) y a todas las ventanas vivas.
void HostEmitEvent(void*, uint32_t window_id, const char* name, const char* json) {
    // Los módulos emiten desde hilos de fondo (watchers, PTY, pipes):
    // TODO el trabajo con GTK/sockets se marshaling al main thread.
    std::string payload = json && json[0] ? json : "null";
    std::string evtName = name ? name : "";
    App::Post([window_id, evtName = std::move(evtName), payload = std::move(payload)] {
        using json::Value;

        if (window_id != 0) {
            auto it = LiveWindows().find(window_id);
            if (it != LiveWindows().end()) it->second->EmitToJS(evtName, payload);
            return;
        }

        json::Object params;
        params.emplace_back("name", Value(evtName));
        {
            auto parsed = json::Parse(payload);
            params.emplace_back("payload",
                                parsed.value ? std::move(*parsed.value) : Value(nullptr));
        }
        ControlServer::Get().BroadcastEvent("module.event",
                                            Value(std::move(params)).Serialize());
        for (auto& [id, w] : LiveWindows()) w->EmitToJS(evtName, payload);
    });
}

void HostLog(void*, int level, const char* msg) {
    using L = log::Level;
    L l = level <= 0 ? L::Debug : level == 1 ? L::Info : level == 2 ? L::Warn : L::Error;
    log::Write(l, "module", msg ? msg : "");
}

ow_module_host_t MakeHost() {
    ow_module_host_t h{};
    h.version = OW_HOST_ABI_VERSION;
    h.ctx = nullptr;
    h.emit_event = &HostEmitEvent;
    h.log = &HostLog;
    return h;
}
const ow_module_host_t g_host = MakeHost();
} // namespace

namespace {
/// Ruta absoluta del ejecutable actual. Vacía si no se pudo resolver.
std::filesystem::path CurrentExePath() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return {};
    return std::filesystem::path(buf, buf + n);
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) return {};
    std::error_code ec;
    auto p = std::filesystem::canonical(buf, ec);
    return ec ? std::filesystem::path(buf) : p;
#else
    char exeBuf[4096] = {};
    ssize_t n = readlink("/proc/self/exe", exeBuf, sizeof(exeBuf) - 1);
    if (n <= 0) return {};
    exeBuf[n] = 0;
    return std::filesystem::path(exeBuf);
#endif
}
} // namespace

std::vector<std::filesystem::path> ModuleLoader::SearchPaths() {
    std::vector<std::filesystem::path> paths = SplitPathList(std::getenv("OW_MODULES_DIR"));
    auto exe = CurrentExePath();
    if (!exe.empty()) paths.emplace_back(exe.parent_path() / "modules");
    return paths;
}

size_t ModuleLoader::RegisterStatic(const ow_module_desc_t* desc) {
    return Dispatcher::Get().RegisterModule(desc, "builtin");
}

size_t ModuleLoader::LoadFile(const std::filesystem::path& file) {
#if defined(_WIN32)
    HMODULE handle = ::LoadLibraryW(file.c_str());
    if (!handle) {
        log::Error("loader", "LoadLibrary falló para " + file.string() +
                                  " (err " + std::to_string(::GetLastError()) + ")");
        return 0;
    }
    auto entry = reinterpret_cast<ow_module_entry_t>(
        reinterpret_cast<void*>(::GetProcAddress(handle, "ow_module_descriptor")));
    if (!entry) {
        log::Error("loader", file.string() + ": símbolo ow_module_descriptor ausente");
        ::FreeLibrary(handle);
        return 0;
    }
    if (auto setHost = reinterpret_cast<ow_module_set_host_t>(reinterpret_cast<void*>(
            ::GetProcAddress(handle, "ow_module_set_host"))); setHost) {
        setHost(&g_host);
    }
    const ow_module_desc_t* desc = entry();
    size_t count = Dispatcher::Get().RegisterModule(desc, file.filename().string());
    if (count > 0) {
        std::lock_guard lock(g_mu);
        g_handles.push_back(reinterpret_cast<void*>(handle));
    } else {
        ::FreeLibrary(handle);
    }
    return count;
#else
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
    // host callbacks (emit_event/log) — opcional para el módulo
    if (auto setHost = reinterpret_cast<ow_module_set_host_t>(
            dlsym(handle, "ow_module_set_host")); setHost) {
        setHost(&g_host);
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
#endif
}

size_t ModuleLoader::LoadAll() {
    size_t total = 0;
    for (const auto& dir : SearchPaths()) {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) continue;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
            if (!e.is_regular_file()) continue;
            auto ext = e.path().extension().string();
#if defined(_WIN32)
            if (ext != ".dll" && ext != ".owm") continue;
#elif defined(__APPLE__)
            if (ext != ".dylib" && ext != ".so" && ext != ".owm") continue;
#else
            if (ext != ".so" && ext != ".owm") continue;
#endif
            total += LoadFile(e.path());
        }
    }
    return total;
}

void ModuleLoader::ProvideHostToBuiltins() {
#if defined(OW_BUILTINS_GTK)
    ::ow_builtin_receive_host(static_cast<const ::ow_module_host_t*>(&g_host));
#endif
}

void ModuleLoader::Shutdown() {
    std::lock_guard lock(g_mu);
#if defined(_WIN32)
    for (void* h : g_handles) ::FreeLibrary(reinterpret_cast<HMODULE>(h));
#else
    for (void* h : g_handles) dlclose(h);
#endif
    g_handles.clear();
}

} // namespace ow
