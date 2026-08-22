// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
// src/Window/Window_p.hpp — estado común de Window + API de plataforma.
//
// Las plataformas definen TODOS los métodos P* y ~Impl en window_<plat>.cpp.
// ~Impl es la key function: el vtable vive solo en el TU de plataforma,
// así Window_common.cpp no necesita conocer PlatformData.
//
#pragma once

#include "ow/Window.h"
#include "ow/Bridge/Codec.h"
#include "../Webview/IWebviewBackend.hpp"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace ow {

class Window::Impl {
public:
    /// Estado opaco de plataforma, definido en window_<plat>.cpp.
    struct PlatformData;

    Impl(Window* self, const WindowOptions& opts);

    // ── estado común ───────────────────────────────────────────────
    Window* self = nullptr;
    WindowOptions opts;
    std::unique_ptr<IWebviewBackend> webview;
    WindowId id = 0;
    bool closeRequestVeto = false;

    /// Flag de vida compartido: los callbacks diferidos lo comprueban
    /// antes de tocar this (evita dangling tras Destroy).
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);

    struct Listener {
        ListenerId id;
        std::function<void(EventPayload)> fn;
    };
    std::map<std::string, std::vector<Listener>> listeners;
    ListenerId nextListenerId = 1;

    // ── lógica común (Window_common.cpp) ───────────────────────────
    void InitCommon();
    void HandleWebViewMessage(std::string_view text);
    void HandleInternalInvoke(const bridge::Message& msg);
    void FireEvent(const std::string& name, std::string_view payloadJson);

    /// Emite evento a listeners nativos + JS vía OUTBOX (batched, F3.2).
    static void EmitPlatformEvent(Impl* impl, const std::string& name,
                                  std::string_view payloadJson = "null");

    // Outbox F3.2: coalesce múltiples _apply/_event en UN eval por tick.
    enum class OpKind { Apply, Event };
    struct Op {
        OpKind kind;
        std::string a, b, c; // apply: id,ok,json | event: window,name,payloadJson
    };
    std::mutex outboxMu;
    std::vector<Op> outbox;
    bool outboxScheduled = false;
    void EnqueueOp(Op op);
    static void ScheduleFlush(Impl* impl);
    static void FlushOutbox(Impl* impl);

    // F3.4: cierre con veto desde JS
    uint64_t jsCloseSeq = 0;            // secuencia de peticiones
    uint64_t jsCloseRequestId = 0;      // 0 = sin petición pendiente
    bool jsCloseResponded = false;
    /// Flujo central de cierre: veto nativo → aviso JS/SDK → timeout → destroy.
    /// Devuelve true si el cierre continúa (destruye), false si fue vetado.
    bool BeginCloseFlow();
    void RespondJsClose(uint64_t requestId, bool allow);
    static void CloseTimerFired(Window::Impl* impl); // llamado por PlatformDelay

    // ── API de plataforma (window_<plat>.cpp) ──────────────────────
    struct PlatformData* pdata = nullptr;

    virtual bool PCreate();
    virtual void PShow();
    virtual void PHide();
    virtual void PFocus();
    virtual void PClose();
    virtual void PDestroy();
    virtual void PMinimize();
    virtual void PMaximize();
    virtual void PUnmaximize();
    virtual void PRestore();
    virtual void PSetFullScreen(bool enabled);
    virtual bool PIsMaximized() const;
    virtual bool PIsMinimized() const;
    virtual bool PIsFullScreen() const;
    virtual Bounds PGetBounds() const;
    virtual void PSetBounds(const Bounds& bounds);
    virtual void PCenter();
    virtual void PSetTitle(const std::string& title);
    virtual std::string PGetTitle() const;
    virtual void PApplyTitleBar();
    virtual void PBeginMoveDrag();
    virtual void PBeginResizeDrag(const std::string& edge);

    virtual ~Impl();  // key function — definida por plataforma
};

/// Genera el script de bridge inyectado en cada documento (todas las plataformas).
std::string BuildBridgeScript();

} // namespace ow
