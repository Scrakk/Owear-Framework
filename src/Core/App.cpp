// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Core/App.cpp — estado común de la aplicación.
// El main loop vive en App_<platform>.cpp.
//
#include "App.hpp"
#include "ModuleLoader.hpp"

#include "../Bridge/Dispatcher.hpp"
#include "../Control/ControlServer.hpp"
#include "../Runtime/NodeManager.hpp"
#include "Log.hpp"
#include "ow/App.h"

#include <cstdlib>

namespace ow {

namespace {
AppOptions g_options;
bool g_initialized = false;
int g_exitCode = 0;
std::function<void()> g_onReady;
} // namespace

const AppOptions& App::Options() { return g_options; }

void App::OnReady(std::function<void()> fn) { g_onReady = std::move(fn); }
void App::Post(std::function<void()> fn) { internal::PlatformPost(std::move(fn)); }
void App::Quit(int exitCode) {
    g_exitCode = exitCode;
    internal::RequestQuit(exitCode);
}

namespace internal {

// WindowModule.cpp + builtins acoplados al WebView (api/window, api/session,
// api/crashreporter).
const ow_module_desc_t* WindowModuleDescriptorImpl();
const ow_module_desc_t* WindowExtrasDescriptor();
const ow_module_desc_t* SessionDescriptor();
const ow_module_desc_t* CrashReporterDescriptor();

void RegisterBuiltinModules() {
    Dispatcher::Get().RegisterModule(WindowModuleDescriptorImpl(), "builtin:ow-window");
    Dispatcher::Get().RegisterModule(WindowExtrasDescriptor(), "builtin:window");
    Dispatcher::Get().RegisterModule(SessionDescriptor(), "builtin:session");
    Dispatcher::Get().RegisterModule(CrashReporterDescriptor(), "builtin:crashreporter");
}

bool Bootstrap(int argc, char** argv, const AppOptions& options) {
    if (g_initialized) return true;
    g_options = options;

    if (!PlatformInit(argc, argv)) return false;

    RegisterBuiltinModules();
    ModuleLoader::ProvideHostToBuiltins();

    size_t loaded = ModuleLoader::LoadAll();
    log::Info("app", "funciones de módulos dinámicos cargadas: " + std::to_string(loaded));

    // Servidor de control: siempre activo (el SDK JS conecta por aquí).
    if (!ControlServer::Get().Start()) {
        log::Warn("app", "control server no disponible; modo nativo-only");
    } else {
        log::Info("app", "control socket: " + ControlServer::Get().SocketPath());
    }

    // Modo JS-driven: spawn del sidecar con el entry de la app.
    const char* appMain = std::getenv("OW_APP_MAIN");
    if (appMain && *appMain) {
        auto node = NodeManager::Ensure("latest");
        if (node.IsOk()) {
            long pid = NodeManager::Spawn(node.Value(), appMain);
            if (pid > 0)
                log::Info("app", "sidecar node pid=" + std::to_string(pid) + " → " + appMain);
            else
                log::Error("app", "no se pudo lanzar el sidecar node");
        } else {
            log::Error("app", "node runtime no disponible: " + node.Error());
        }
    }

    g_initialized = true;
    return true;
}

void RequestQuit(int exitCode) {
    g_exitCode = exitCode;
    PlatformQuit();
}

int ExitCode() { return g_exitCode; }

} // namespace internal

int App::Main(int argc, char** argv, const AppOptions& options) {
    if (!internal::Bootstrap(argc, argv, options)) return 1;
    if (g_onReady) g_onReady();
    int code = internal::RunMainLoop();
    ModuleLoader::Shutdown();
    ControlServer::Get().Stop();
    return code;
}

} // namespace ow
