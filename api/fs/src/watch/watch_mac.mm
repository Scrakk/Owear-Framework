// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/fs/src/watch/watch_mac.mm — kqueue EVFILT_VNODE (por directorio).
// FSEvents daría recursividad nativa; kqueue v1 cubre el caso base.
// VERIFICAR-EN-MACOS.
//
#include "watch.hpp"

#include <sys/event.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstring>
#include <set>
#include <chrono>

namespace fswatch {

bool Start(int id, std::string& err) {
    Watcher* w = Get(id);
    if (!w) { err = "watcher inexistente"; return false; }

    int kq = kqueue();
    if (kq < 0) { err = "kqueue() falló"; return false; }

    // abre todos los dirs (recursivos) y registra vnode watches
    std::set<std::string> dirs;
    if (w->recursive) {
        DIR* d = opendir(w->path.c_str());
        if (d) {
            dirs.insert(w->path);
            // walk superficial de 1 nivel por v1 (FSEvents en F-next)
            while (auto* e = readdir(d)) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
                std::string full = w->path + "/" + e->d_name;
                struct stat st{};
                if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
                    dirs.insert(full);
            }
            closedir(d);
        }
    } else {
        dirs.insert(w->path);
    }

    std::vector<int> fds;
    for (const auto& d : dirs) {
        int fd = open(d.c_str(), O_RDONLY | O_EVTONLY);
        if (fd >= 0) {
            struct kevent ev{};
            EV_SET(&ev, fd, EVFILT_VNODE,
                   EV_ADD | EV_CLEAR,
                   NOTE_WRITE | NOTE_DELETE | NOTE_RENAME, 0, nullptr);
            kevent(kq, &ev, 1, nullptr, 0, nullptr);
            fds.push_back(fd);
        }
    }

    w->stop = false;
    w->thread = std::thread([id, kq, fds]() mutable {
        while (auto* wl = Get(id); wl && !wl->stop) {
            struct kevent ev{};
            struct timespec ts{0, 200 * 1000 * 1000}; // 200 ms poll
            int n = kevent(kq, nullptr, 0, &ev, 1, &ts);
            if (n <= 0) continue;

            std::vector<Event> batch;
            for (auto& [_, path] : std::map<int, std::string>{}) {} // noop
            // sin nombre de archivo con EVFILT_VNODE: reportamos el dir tocado
            for (int fd : fds) {
                if (static_cast<int>(ev.ident) == fd) {
                    batch.push_back({"modify", Get(id) ? Get(id)->path : ""});
                    FlushEvents(id, batch);
                    break;
                }
            }
        }
        for (int fd : fds) ::close(fd);
        ::close(kq);
    });
    return true;
}

void Stop(int id) {
    Watcher* w = Get(id);
    if (!w) return;
    w->stop = true;
    if (w->thread.joinable()) w->thread.join();
}

} // namespace fswatch
