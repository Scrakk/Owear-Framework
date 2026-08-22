// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/fs/src/watch/watch.hpp — watcher de filesystem (interfaz común).
// Backends: watch_linux.cpp (inotify) · watch_win.cpp (ReadDirectoryChangesW)
//           watch_mac.mm (kqueue EVFILT_VNODE)
//
#pragma once

#include "ow_api.h"

#include <atomic>
#include <set>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace fswatch {

struct Event {
    std::string type; // create | modify | delete
    std::string path;
};

struct Watcher {
    int id = 0;
    std::string path;
    bool recursive = false;
    uint32_t window_id = 0;
    std::thread thread;
    std::atomic<bool> stop{false};
};

/// Registra el host del kernel (llamado por ow_module_set_host).
void SetHost(const ow_module_host_t* host);  // definido en watch.cpp

int Create(std::string path, bool recursive, uint32_t windowId);
Watcher* Get(int id);
void Destroy(int id);
std::set<int> AllIds();
void FlushEvents(int watcherId, std::vector<Event>& events);

/// Arranca el hilo lector del backend. false + err en fallo.
bool Start(int id, std::string& err);
void Stop(int id);

} // namespace fswatch
