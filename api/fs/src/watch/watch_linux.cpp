// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/fs/src/watch/watch_linux.cpp — backend inotify.
//
#include "watch.hpp"

#include <dirent.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <deque>
#include <set>
#include <unordered_map>

namespace fswatch {

namespace {

constexpr size_t kEventBufSize = 64 * 1024;

void WalkDirs(const std::string& root, bool recursive,
              std::set<std::string>& dirs, std::string& err) {
    DIR* d = opendir(root.c_str());
    if (!d) {
        err = "opendir falló: " + root;
        return;
    }
    dirs.insert(root);
    while (auto* e = readdir(d)) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        std::string full = root + "/" + e->d_name;
        if (e->d_type == DT_DIR && recursive) WalkDirs(full, recursive, dirs, err);
    }
    closedir(d);
}

} // namespace

struct Ctx {
    int ifd;
    std::unordered_map<int, std::string> wdPaths;
};

bool Start(int id, std::string& err) {
    Watcher* w = Get(id);
    if (!w) {
        err = "watcher inexistente";
        return false;
    }

    int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd < 0) {
        err = "inotify_init1 falló";
        return false;
    }

    // mapa wd → path (para eventos recursivos)
    auto* ctx = new Ctx();
    ctx->ifd = fd;

    std::set<std::string> dirs;
    WalkDirs(w->path, w->recursive, dirs, err);
    if (!err.empty()) {
        ::close(fd);
        delete ctx;
        return false;
    }
    for (const auto& dir : dirs) {
        int wd = inotify_add_watch(fd, dir.c_str(),
                                   IN_CREATE | IN_MODIFY | IN_DELETE |
                                       IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE);
        if (wd >= 0) ctx->wdPaths[wd] = dir;
    }

    w->stop = false;
    w->thread = std::thread([id, ctx]() {
        auto* w = Get(id);
        char buf[kEventBufSize];
        std::vector<Event> batch;
        int64_t lastFlush = 0;

        while (w && !w->stop) {
            ssize_t n = ::read(ctx->ifd, buf, sizeof(buf));
            if (n < 0) {
                usleep(30 * 1000); // 30 ms poll (IN_NONBLOCK)
                continue;
            }

            ssize_t off = 0;
            while (off + static_cast<ssize_t>(sizeof(struct inotify_event)) <= n) {
                auto* ev = reinterpret_cast<struct inotify_event*>(buf + off);
                off += sizeof(struct inotify_event) + ev->len;

                std::string dirPath;
                auto it = ctx->wdPaths.find(ev->wd);
                if (it != ctx->wdPaths.end()) dirPath = it->second;
                std::string name = ev->name ? ev->name : "";
                if (name.empty()) continue;

                const char* type =
                    (ev->mask & (IN_CREATE | IN_MOVED_TO)) ? "create"
                    : (ev->mask & (IN_DELETE | IN_MOVED_FROM)) ? "delete"
                                                               : "modify";
                std::string full = dirPath + "/" + name;

                // recursivo: nuevo directorio → vigilarlo también
                if ((ev->mask & IN_CREATE) && (ev->mask & IN_ISDIR)) {
                    Watcher* wl = Get(id);
                    if (wl && wl->recursive) {
                        int nwd = inotify_add_watch(
                            ctx->ifd, full.c_str(),
                            IN_CREATE | IN_MODIFY | IN_DELETE | IN_MOVED_FROM |
                                IN_MOVED_TO | IN_CLOSE_WRITE);
                        if (nwd >= 0) ctx->wdPaths[nwd] = full;
                    }
                }

                batch.push_back({type, full});
            }

            // batching ~50 ms (anti-tormenta de eventos)
            if (!batch.empty()) {
                auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch())
                                 .count();
                if (nowMs - lastFlush >= 50 || batch.size() > 512) {
                    FlushEvents(id, batch);
                    lastFlush = nowMs;
                }
            }
        }

        ::close(ctx->ifd);
        delete ctx;
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

// el kernel inyecta el host justo tras el dlopen
extern "C" OW_MODULE_EXPORT void ow_module_set_host(const ow_module_host_t* h) {
    fswatch::SetHost(h);
}
