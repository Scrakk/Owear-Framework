// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
// src/Webview/IWebviewBackend.hpp — contrato interno entre Window y backends.
// Cada plataforma implementa esto en Webview/<plat>/. Cambiarlo rompe las 3.
#pragma once

#include "ow/Common.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace ow {

/// Handler de mensajes entrantes desde JS (texto JSON del bridge).
using WebMessageHandler = std::function<void(std::string_view text)>;
/// Callback de evaluación JS: resultado como JSON ("null" si no hay).
using EvalCallback = std::function<void(std::string_view resultJson, bool ok)>;

class IWebviewBackend {
public:
    virtual ~IWebviewBackend() = default;

    /// Crea el WebView nativo embebido en `parentNativeWindow` (opaco:
    /// GtkWindow* en Linux, HWND en Windows, NSView* en macOS).
    virtual bool Create(void* parentNativeWindow, const std::vector<std::string>& args) = 0;

    /// Script de inicialización del bridge (inyectado en cada documento).
    virtual void InjectInitScript(const std::string& js) = 0;

    /// Registra el receptor de mensajes JS→nativo.
    virtual void SetMessageHandler(WebMessageHandler handler) = 0;

    virtual void LoadURL(const std::string& url) = 0;

    virtual void EvalJS(const std::string& js, EvalCallback cb = nullptr) = 0;

    /// Registra un scheme personalizado que sirve archivos locales.
    /// `root` es el directorio base; requests son scheme://<ruta-relativa>.
    virtual void RegisterAssetScheme(const std::string& scheme,
                                     const std::filesystem::path& root) = 0;

    virtual void Resize(int x, int y, int w, int h) = 0;

    /// Referencia nativa cruda (GtkWidget*, etc) — para tests y debug.
    virtual void* NativeWidget() const = 0;
};

/// Factory por plataforma (definida en Webview/<plat>/).
std::unique_ptr<IWebviewBackend> CreateWebviewBackend();

} // namespace ow
