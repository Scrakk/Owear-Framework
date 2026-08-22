// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/process/src/registry.hpp — registro de procesos/PTYs vivos + emisión
// de eventos (stdout/stderr/exit) vía host del kernel.
//
#pragma once

#include "ow/Base64.h"
#include "ow/Shm.h"
#include "ow_api.h"

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <sys/types.h>  // pid_t (mingw) / MSVC no lo define: ver typedef abajo
#if defined(_MSC_VER)
typedef int pid_t;
#endif
#else
#include <sys/types.h>
#endif

namespace proc {

enum class Kind { Pipe, Pty };

struct Proc {
    int id = 0;
    Kind kind = Kind::Pipe;
    pid_t pid = -1;                // -1 en Windows (usamos handle)
    void* handle = nullptr;        // HANDLE del proceso en Windows
    int stdinFd = -1;              // fd/pipe de stdin (-1 si no aplica)
    void* stdinHandle = nullptr;   // HANDLE stdin en Windows
    int masterFd = -1;             // fd master del PTY
    void* ptyHandle = nullptr;     // HPCON en Windows
    uint32_t window_id = 0;
    std::thread reader;
    std::thread waiter;
    std::atomic<bool> stop{false};
};

inline std::mutex g_mu;
inline std::map<int, Proc*> g_procs;
inline int g_nextId = 1;
inline const ow_module_host_t* g_host = nullptr;

inline void SetHost(const ow_module_host_t* h) { g_host = h; }

inline Proc* Get(int id) {
    auto it = g_procs.find(id);
    return it == g_procs.end() ? nullptr : it->second;
}

inline void Remove(int id) {
    std::lock_guard lock(g_mu);
    auto it = g_procs.find(id);
    if (it == g_procs.end()) return;
    Proc* p = it->second;
    // Los hilos reader/waiter pueden estar ejecutando este mismo código (p.ej.
    // el propio hilo `reader` llama a Remove() justo antes de terminar), así
    // que no podemos hacer join() aquí. Se hace detach() para que el destructor
    // de std::thread no llame a std::terminate() al borrar `p`.
    if (p->reader.joinable()) p->reader.detach();
    if (p->waiter.joinable()) p->waiter.detach();
    delete p;
    g_procs.erase(it);
}

/// Emite evento del proceso (el host hace marshaling al main thread).
inline void Emit(uint32_t windowId, const char* name, const std::string& json) {
    if (g_host && g_host->emit_event)
        g_host->emit_event(g_host->ctx, windowId, name, json.c_str());
}

/// Chunk de datos → evento con b64 (≤192KB) o handle SHM (mayor).
inline void EmitData(uint32_t windowId, int procId, const char* which,
                     const char* data, size_t len) {
    std::string json;
    if (len >= 256 * 1024) {
        const char* sid = ow_shm_put(reinterpret_cast<const uint8_t*>(data), len);
        if (sid && *sid) {
            json = "{\"procId\":" + std::to_string(procId) +
                   ",\"__ow_shm\":{\"id\":\"" + sid +
                   "\",\"size\":" + std::to_string(len) + "}}";
            Emit(windowId, (std::string("process.") + which).c_str(), json);
            return;
        }
    }
    std::string b64 = ow::b64::Encode(reinterpret_cast<const uint8_t*>(data), len);
    json = "{\"procId\":" + std::to_string(procId) + ",\"b64\":\"" + b64 + "\"}";
    Emit(windowId, (std::string("process.") + which).c_str(), json);
}

} // namespace proc
