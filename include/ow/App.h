// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
//
// ow/App.h — ciclo de vida de la aplicación.
// La implementación vive en src/Core/App_<platform>.cpp (convención de sufijos).
//

#include "ow/Common.h"

namespace ow {

struct AppOptions {
    std::string id    = "dev.owear.app";   // identificador único de la app
    std::string name  = "Owear App";
    std::string version = "0.1.0";
};

/// Punto de entrada del kernel. Bloquea hasta Quit().
/// En modo JS-driven (Electron-like), el kernel arranca antes y expone el
/// Control Socket; ver src/Control/.
class App {
public:
    /// Inicializa plataforma, registra módulos builtin y corre el main loop.
    /// Devuelve el código de salida cuando el loop termina.
    static int Main(int argc, char** argv, const AppOptions& options);

    /// Callback tras Bootstrap (plataforma lista, módulos cargados) y antes
    /// de arrancar el loop. Lugar correcto para crear ventanas.
    static void OnReady(std::function<void()> fn);

    /// Encola una devolución en el main loop (thread-safe).
    static void Post(std::function<void()> fn);

    static void Quit(int exitCode = 0);

    static const AppOptions& Options();
};

} // namespace ow
