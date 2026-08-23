// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Core/App_win.cpp — main loop Win32.
//
#include "App.hpp"
#include "Log.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h> // CoInitializeEx (LEAN_AND_MEAN lo excluye)

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

namespace ow::internal {

namespace {
constexpr UINT kWmOwPump = WM_APP + 0x4F51; // mensaje interno de drenaje
DWORD g_mainThreadId = 0;
HWND g_pumpHwnd = nullptr;

std::mutex g_pendingMu;
std::queue<std::function<void()>> g_pending;

static void DrainPending() {
    std::queue<std::function<void()>> batch;
    {
        std::lock_guard lock(g_pendingMu);
        batch.swap(g_pending);
    }
    while (!batch.empty()) {
        auto fn = std::move(batch.front());
        batch.pop();
        try {
            fn();
        } catch (...) {
            // nunca matar el loop por un callback
        }
    }
}

LRESULT CALLBACK PumpWndProc(HWND h, UINT m, WPARAM, LPARAM) {
    if (m == kWmOwPump) {
        DrainPending();
        return 0;
    }
    return DefWindowProcW(h, m, 0, 0);
}
} // namespace

bool PlatformInit(int argc, char** argv) {
    (void)argc;
    (void)argv;
    g_mainThreadId = GetCurrentThreadId();

    // COM apartment single-threaded para WebView2
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool comOk = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;

    // ventana fantasma en el hilo principal: canal de despacho confiable
    // (PostThreadMessage es más frágil: fallos silenciosos y colas tempranas)
    WNDCLASSW wc{};
    wc.lpfnWndProc = &PumpWndProc;
    wc.lpszClassName = L"owear-pump";
    wc.hInstance = GetModuleHandleW(nullptr);
    RegisterClassW(&wc);
    g_pumpHwnd = CreateWindowExW(0, wc.lpszClassName, nullptr, 0, 0, 0, 0, 0,
                                 HWND_MESSAGE, nullptr, wc.hInstance, nullptr);

    return comOk && g_pumpHwnd != nullptr;
}

void PlatformPost(std::function<void()> fn) {
    bool wake = false;
    {
        std::lock_guard lock(g_pendingMu);
        wake = g_pending.empty(); // solo despierta si la cola estaba vacía
        g_pending.push(std::move(fn));
    }
    if (!wake) return;
    if (g_pumpHwnd &&
        !PostMessageW(g_pumpHwnd, kWmOwPump, 0, 0)) {
        log::Error("app", "PostMessage de despacho falló: " +
                              std::to_string(GetLastError()));
        // reintento por el canal viejo antes de rendirse
        PostThreadMessageW(g_mainThreadId, kWmOwPump, 0, 0);
    }
}

int RunMainLoop() {
    MSG msg;
    BOOL r;
    while ((r = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        DrainPending();
        if (msg.message == WM_QUIT) break;
    }
    return 0;
}

void PlatformQuit() {
    // WM_QUIT vía mensaje de hilo: seguro desde cualquier hilo.
    PostThreadMessageW(g_mainThreadId, WM_QUIT, 0, 0);
}

void PlatformDelay(int ms, std::function<void()> fn) {
    auto alive = std::make_shared<std::atomic<bool>>(true);
    auto* boxed = new std::function<void()>(std::move(fn));
    std::thread([ms, boxed, alive] {
        Sleep(static_cast<DWORD>(ms));
        if (!alive->load()) {
            delete boxed;
            return;
        }
        PlatformPost([boxed] {
            std::unique_ptr<std::function<void()>> f(boxed);
            try {
                (*f)();
            } catch (...) {
            }
        });
    }).detach();
}

} // namespace ow::internal
