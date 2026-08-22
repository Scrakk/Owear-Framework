// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
// src/Core/App.hpp — interfaz interna entre App común y App_<platform>.cpp
#pragma once

#include "ow/App.h"
#include "ow_api.h"

#include <functional>
#include <string>

namespace ow::internal {

/// Inicialización de plataforma (gtk_init / CoInitialize / NSApplication).
bool PlatformInit(int argc, char** argv);

/// Corre el main loop hasta PlatformQuit. Devuelve código de salida.
int RunMainLoop();

/// Solicita salida del main loop (thread-safe).
void PlatformQuit();

/// Encola fn en el main loop (thread-safe).
void PlatformPost(std::function<void()> fn);

/// Ejecuta fn en el main loop tras `ms` (thread-safe).
void PlatformDelay(int ms, std::function<void()> fn);

/// Bootstrap común: módulos builtin, loader dinámico, control server, sidecar.
bool Bootstrap(int argc, char** argv, const AppOptions& options);

void RequestQuit(int exitCode);

/// Descriptor del módulo interno ow-window (titlebar custom, drags, etc).
const ow_module_desc_t* WindowModuleDescriptor();

} // namespace ow::internal
