// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/fs/src/watch/watch_win.cpp — ReadDirectoryChangesW.
// VERIFICAR-EN-WINDOWS: overlapped IO y rutas wide.
//
#include "watch.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>

namespace fswatch {

namespace {
struct Ctx {
    HANDLE dir = nullptr;
    OVERLAPPED ov{};
    std::vector<uint8_t> buffer = std::vector<uint8_t>(64 * 1024);
};
struct OverlappedCtx {
    OVERLAPPED ov{};
    std::vector<uint8_t> buffer = std::vector<uint8_t>(64 * 1024);
};
} // namespace

bool Start(int id, std::string& err) {
    Watcher* w = Get(id);
    if (!w) { err = "watcher inexistente"; return false; }

    auto* ctx = new Ctx{CreateFileA(w->path.c_str(), FILE_LIST_DIRECTORY,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE |
                                        FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS |
                                        FILE_FLAG_OVERLAPPED,
                                    nullptr),
                        {}, std::vector<uint8_t>(64 * 1024)};

    if (ctx->dir == INVALID_HANDLE_VALUE) {
        delete ctx;
        err = "CreateFile falló";
        return false;
    }

    w->stop = false;
    w->thread = std::thread([id, ctx]() {
        DWORD bytes = 0;
        while (true) {
            Watcher* w = Get(id);
            if (!w || w->stop) break;
            ResetEvent(ctx->ov.hEvent);
            BOOL ok = ReadDirectoryChangesW(
                ctx->dir, ctx->buffer.data(),
                static_cast<DWORD>(ctx->buffer.size()), w->recursive /*subárboles*/,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                    FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE,
                &bytes, &ctx->ov, nullptr);
            if (!ok && GetLastError() != ERROR_IO_PENDING) break;

            if (!GetOverlappedResult(ctx->dir, &ctx->ov, &bytes, TRUE)) break;
            if (bytes == 0) continue;

            std::vector<Event> batch;
            auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(ctx->buffer.data());
            for (;;) {
                std::string name(info->FileNameLength / sizeof(WCHAR), '\0');
                WideCharToMultiByte(CP_UTF8, 0, info->FileName,
                                    static_cast<int>(info->FileNameLength / sizeof(WCHAR)),
                                    name.data(), static_cast<int>(name.size()),
                                    nullptr, nullptr);
                const char* type =
                    info->Action == FILE_ACTION_ADDED ? "create"
                    : info->Action == FILE_ACTION_REMOVED ? "delete"
                                                          : "modify";
                batch.push_back({type, w->path + "\\" + name});
                if (info->NextEntryOffset == 0) break;
                info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                    reinterpret_cast<uint8_t*>(info) + info->NextEntryOffset);
            }
            FlushEvents(id, batch);
        }
        CancelIoEx(ctx->dir, nullptr);
        CloseHandle(ctx->dir);
        delete ctx;
    });
    return true;
}

void Stop(int id) {
    Watcher* w = Get(id);
    if (!w) return;
    w->stop = true;
    // el thread sale al completar el IO pendiente
    if (w->thread.joinable()) w->thread.detach();
}

} // namespace fswatch
