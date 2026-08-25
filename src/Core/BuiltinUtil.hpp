// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Core/BuiltinUtil.hpp — helpers para módulos builtin acoplados al kernel.
// Neutro de plataforma: los widgets nativos viajan como void*; cada
// BuiltinUtil_<plat> define su resolución concreta.
//
#pragma once

#include "Log.hpp"
#include "ow_api.h"

#include <map>
#include <string>

namespace ow::builtin {

/// Host del kernel (emit_event/log) — inyectado por ModuleLoader.
const ow_module_host_t* Host();
void SetHost(const ow_module_host_t* h);

/// Convierte windowId → handle de la ventana top-level (HWND en Windows,
/// GtkWindow* en Linux, NSView/NSWindow en macOS). nullptr si inválida.
/// En Linux devuelve GtkWindow*; el tipo fuerte vive en BuiltinUtil_linux.
void* WindowById(uint32_t id);

/// Convierte windowId → widget del webview (WebKitWebView*/ICoreWebView2
/// host HWND/WKWebView*). nullptr si inválida.
void* WebviewById(uint32_t id);

/// Emite evento (con marshaling ya hecho por el host).
inline void Emit(uint32_t windowId, const char* name, const std::string& json) {
    auto* h = Host();
    if (h && h->emit_event) h->emit_event(h->ctx, windowId, name, json.c_str());
}

} // namespace ow::builtin
