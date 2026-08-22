// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
//
// ow/Window.h — ventanas multiplataforma con titlebar custom/overlay.
// Contrato compartido: window_win.cpp / window_mac.mm / window_linux.cpp
//

#include "ow/Common.h"

namespace ow {

enum class TitleBarStyle {
    Default,   // decoraciones del sistema tal cual
    Hidden,    // sin titlebar; la ventana completa es contenido (frameless)
    Custom     // titlebar custom + botones nativos overlay (donde el SO lo permite)
};

struct TitleBarOverlay {
    bool enabled = false;
    std::string color = "#00000000";       // fondo bajo los botones nativos
    std::string symbolColor = "#ffffff";   // color de glifos (min/max/close)
    int height = 36;                       // alto en px lógicos
};

struct WindowOptions {
    std::string title = "Owear";
    int width = 1024;
    int height = 768;
    int minWidth = 0, minHeight = 0;
    int maxWidth = 0, maxHeight = 0;
    bool resizable = true;
    bool center = true;
    bool show = true;
    bool frameless = false;

    TitleBarStyle titleBarStyle = TitleBarStyle::Default;
    TitleBarOverlay titleBarOverlay;

    /// URL inicial: http(s):// (dev server), app://<bundle>/... o file://
    std::string url = "app://index.html";

    /// Argumentos extra para el proceso del WebView (debug flags, etc).
    std::vector<std::string> webviewArgs;
};

/// Payload de evento como JSON serializado ("null" si el evento no lleva datos).
using EventPayload = std::string_view;
using ListenerId = uint64_t;

class Window : NonCopyable {
public:
    explicit Window(const WindowOptions& options);
    ~Window();

    WindowId Id() const;

    // ── ciclo de vida ──────────────────────────────────────────────
    void Show();
    void Hide();
    void Close();          // dispara closeRequested; puede ser vetado por listeners
    void Destroy();        // cierre inmediato sin veto
    void Focus();

    // ── estado ─────────────────────────────────────────────────────
    void Minimize();
    void Maximize();
    void Unmaximize();
    void Restore();
    void SetFullScreen(bool enabled);
    bool IsMaximized() const;
    bool IsMinimized() const;
    bool IsFullScreen() const;

    // ── geometría ──────────────────────────────────────────────────
    struct Bounds { int x = 0, y = 0, w = 0, h = 0; };
    Bounds GetBounds() const;
    void SetBounds(const Bounds&);
    void Center();

    // ── apariencia / titlebar ───────────────────────────────────────
    void SetTitle(const std::string& title);
    std::string Title() const;
    void SetTitleBarStyle(TitleBarStyle style);
    void SetTitleBarOverlay(const TitleBarOverlay& overlay);

    // ── webview ────────────────────────────────────────────────────
    void LoadURL(const std::string& url);
    /// Evalúa JS en la página. callback recibe el resultado JSON o null.
    void EvalJS(const std::string& js,
                std::function<void(std::string_view resultJson)> callback = nullptr);

    // ── eventos hacia JS (bridge) y C++ ─────────────────────────────
    /// Emite un evento a todos los suscriptores JS de esta ventana.
    void EmitToJS(const std::string& name, std::string_view jsonPayload);
    /// Suscriptor nativo. Los eventos nativos se reenvían a JS automáticamente.
    ListenerId On(const std::string& name, std::function<void(EventPayload)> cb);
    void Off(ListenerId id);

    /// Referencia cruda al backend de plataforma (solo para src/ interno).
    void* NativeHandle() const;

    /// Detrás de este puntero vive el estado de plataforma.
    class Impl;  // definido en src/Window/Window_p.hpp
    std::unique_ptr<Impl> impl_;

  public:
    /// Interno (ControlServer): respuesta del SDK al closeRequested.
    class Impl* impl() { return impl_.get(); }
};

} // namespace ow
