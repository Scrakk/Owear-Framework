// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Core/BuiltinUtil.hpp — helpers para módulos builtin acoplados al kernel.
//
#pragma once

#include "Log.hpp"
#include "ow_api.h"

#include <gtk/gtk.h>
#include <map>
#include <string>

namespace ow::builtin {

/// Host del kernel (emit_event/log) — inyectado por ModuleLoader.
const ow_module_host_t* Host();
void SetHost(const ow_module_host_t* h);

/// Convierte windowId → GtkWindow* (toplevel del webview). nullptr si inválida.
GtkWindow* WindowById(uint32_t id);

/// Convierte windowId → WebKitWebView*. nullptr si inválida.
void* WebviewById(uint32_t id);

/// Nested loop: bloquea en gtk_main_iteration hasta que `done` sea true.
/// Para puentes síncronos sobre APIs async de WebKit (mismo patrón que los
/// diálogos modales). NO llamar desde otro hilo.
inline void PumpUntil(const volatile bool& done) {
    while (!done) {
        gtk_main_iteration();
    }
}

/// Emite evento (con marshaling ya hecho por el host).
inline void Emit(uint32_t windowId, const char* name, const std::string& json) {
    auto* h = Host();
    if (h && h->emit_event) h->emit_event(h->ctx, windowId, name, json.c_str());
}

} // namespace ow::builtin
