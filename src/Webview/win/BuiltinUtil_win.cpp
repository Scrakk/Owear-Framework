// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Webview/win/BuiltinUtil_win.cpp — host + resolución windowId→HWND
// (el widget nativo del backend Webview2 es el HWND huésped).
//
#include "../../Core/BuiltinUtil.hpp"
#include "../../Control/ControlServer.hpp"
#include "../../Window/Window_p.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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
    void* w = it->second->NativeHandle();
    if (!w || !IsWindow(static_cast<HWND>(w))) return nullptr;
    return static_cast<HWND>(w);
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
