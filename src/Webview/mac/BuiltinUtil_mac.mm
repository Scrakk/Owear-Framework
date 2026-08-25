// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Webview/mac/BuiltinUtil_mac.mm — host + resolución windowId→WKWebView
// (el widget nativo del backend WKWebView es la vista misma).
//
#include "../../Core/BuiltinUtil.hpp"
#include "../../Control/ControlServer.hpp"
#include "../../Window/Window_p.hpp"

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
    // En macOS el "toplevel" útil para los builtins es el propio webview
    // (WKWebView expone window a través de su windowWindow en Cocoa).
    auto it = LiveWindows().find(id);
    if (it == LiveWindows().end()) return nullptr;
    return it->second->NativeHandle();
}

void* WebviewById(uint32_t id) {
    auto it = LiveWindows().find(id);
    if (it == LiveWindows().end()) return nullptr;
    return it->second->NativeHandle();
}

} // namespace ow::builtin

extern "C" void ow_builtin_receive_host(const ow_module_host_t* h) {
    ow::builtin::SetHost(h);
}
