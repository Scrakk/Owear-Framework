// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Core/App_linux.cpp — main loop GTK.
//
#include "App.hpp"
#include "Log.hpp"

#include <gtk/gtk.h>

#include <functional>
#include <memory>
#include <mutex>
#include <queue>

namespace ow::internal {

bool PlatformInit(int argc, char** argv) {
    gtk_init(&argc, &argv);
    return true;
}

namespace {
std::mutex g_pendingMu;
std::queue<std::function<void()>> g_pending;

gboolean OnIdle(gpointer) {
    std::queue<std::function<void()>> batch;
    {
        std::lock_guard lock(g_pendingMu);
        batch.swap(g_pending);
    }
    while (!batch.empty()) {
        auto fn = std::move(batch.front());
        batch.pop();
        try {
            fn();
        } catch (const std::exception& e) {
            log::Error("app", std::string("excepción en Post(): ") + e.what());
        }
    }
    return G_SOURCE_CONTINUE; // fuente permanente; GTK la mata con gtk_main_quit
}
} // namespace

void PlatformPost(std::function<void()> fn) {
    {
        std::lock_guard lock(g_pendingMu);
        g_pending.push(std::move(fn));
    }
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, OnIdle, nullptr, nullptr);
}

int RunMainLoop() {
    gtk_main();
    return 0;
}

void PlatformQuit() {
    // gtk_main_quit es seguro desde cualquier hilo vía idle del loop GTK.
    g_idle_add_full(G_PRIORITY_HIGH_IDLE, [](gpointer) -> gboolean {
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }, nullptr, nullptr);
}

void PlatformDelay(int ms, std::function<void()> fn) {
    auto* boxed = new std::function<void()>(std::move(fn));
    g_timeout_add(static_cast<guint>(ms), [](gpointer data) -> gboolean {
        std::unique_ptr<std::function<void()>> fn(
            static_cast<std::function<void()>*>(data));
        try {
            (*fn)();
        } catch (...) {
        }
        return G_SOURCE_REMOVE;
    }, boxed);
}

} // namespace ow::internal
