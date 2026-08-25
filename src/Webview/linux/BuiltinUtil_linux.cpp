// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Webview/linux/BuiltinUtil_linux.cpp — host + resolución windowId→GTK.
//
#include "../../Core/BuiltinUtil.hpp"
#include "../../Control/ControlServer.hpp"
#include "../../Window/Window_p.hpp"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

namespace ow::builtin {

namespace {
ow_module_host_t g_host{};
}

const ow_module_host_t* Host() { return g_host.ctx ? &g_host : nullptr; }

void SetHost(const ow_module_host_t* h) {
    if (!h) return;
    g_host = *h;
}

void* WindowById(uint32_t id) {
    auto it = LiveWindows().find(id);
    if (it == LiveWindows().end()) return nullptr;
    void* view = it->second->NativeHandle();
    if (!view || !GTK_IS_WIDGET(static_cast<GtkWidget*>(view))) return nullptr;
    GtkWidget* top = gtk_widget_get_toplevel(GTK_WIDGET(view));
    if (!gtk_widget_is_toplevel(top) || !GTK_IS_WINDOW(top)) return nullptr;
    return GTK_WINDOW(top);
}

void* WebviewById(uint32_t id) {
    auto it = LiveWindows().find(id);
    if (it == LiveWindows().end()) return nullptr;
    void* view = it->second->NativeHandle();
    if (!view || !WEBKIT_IS_WEB_VIEW(static_cast<GtkWidget*>(view))) return nullptr;
    return view;
}

} // namespace ow::builtin

extern "C" void ow_builtin_receive_host(const ow_module_host_t* h) {
    ow::builtin::SetHost(h);
}

// el loader inyecta el host a los builtins por aquí
