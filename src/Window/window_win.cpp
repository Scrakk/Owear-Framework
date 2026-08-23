// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/window_win.cpp — ventana Win32 + WebView2 + titlebar.
//
// Titlebar:
//  - Default: WS_OVERLAPPEDWINDOW estándar.
//  - Hidden/Custom: WS_POPUP (frameless) + drag via WM_NCLBUTTONDOWN/HTCAPTION.
//    Overlay nativo (botones min/max/close dibujados por DWM sobre la titlebar
//    custom) usa la técnica Chromium: WM_NCCALCSIZE con frame extendido.
//    VERIFICAR-EN-WINDOWS: hit-testing de botones y snap layouts (F3).
//
#include "Window_p.hpp"
#include "../Core/Log.hpp"
#include "ow/detail/minjson.hpp"

#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE // IDC_ARROW y macros de recurso expanden a la variante W
#endif
#include <windows.h>
#include <windowsx.h>

#include <atomic>
#include <map>
#include <cstring>

namespace ow {

struct Window::Impl::PlatformData {
    HWND hwnd = nullptr;
    WNDPROC origProc = nullptr;
    bool fullscreen = false;
    bool customTitlebar = false; // F3.5: overlay DWM sobre contenido full-size
    WINDOWPLACEMENT preFullscreen{};
};

namespace {
std::atomic<uint32_t> g_nextWinId{1};
constexpr wchar_t kOwWindowClass[] = L"OwearWindow";

// Conversiones UTF-8 <-> UTF-16 correctas (mismo enfoque que
// src/Webview/win/Webview2Backend.cpp). Las conversiones ingenuas
// byte-a-byte (std::wstring(s.begin(), s.end())) truncan/corrompen
// cualquier carácter no-ASCII.
std::wstring Utf8ToWide(std::string_view s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string WideToUtf8(const wchar_t* w) {
    if (!w) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// mapa hwnd → Impl (los callbacks C no pueden capturar)
std::map<HWND, Window::Impl*>& HwndMap() {
    static std::map<HWND, Window::Impl*> m;
    return m;
}
Window::Impl* ImplFromHwnd(HWND hwnd) {
    auto& m = HwndMap();
    auto it = m.find(hwnd);
    return it == m.end() ? nullptr : it->second;
}

LRESULT CALLBACK OwWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* pdata = reinterpret_cast<Window::Impl::PlatformData*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CLOSE: {
        // F3.4: usa el flujo central (veto nativo -> aviso JS/SDK con
        // requestId -> timeout de OW_CLOSE_TIMEOUT_MS -> destroy), igual que
        // Linux (delete-event) y macOS (NSWindowWillCloseNotification). El
        // cierre real de la ventana lo decide BeginCloseFlow (via
        // RespondJsClose o el timer), así que aquí siempre bloqueamos el
        // WM_CLOSE por defecto devolviendo 0.
        if (auto* impl = ImplFromHwnd(hwnd)) {
            impl->BeginCloseFlow();
        }
        return 0;
    }
    case WM_DESTROY: {
        if (auto* impl = ImplFromHwnd(hwnd))
            Window::Impl::EmitPlatformEvent(impl, "closed");
        return 0;
    }
    case WM_SIZE: {
        if (auto* impl = ImplFromHwnd(hwnd)) {
            json::Object o;
            o.emplace_back("width", json::Value(static_cast<int64_t>(LOWORD(lp))));
            o.emplace_back("height", json::Value(static_cast<int64_t>(HIWORD(lp))));
            Window::Impl::EmitPlatformEvent(impl, "resize",
                                    json::Value(std::move(o)).Serialize());
        }
        break;
    }
    case WM_MOVE: {
        if (auto* impl = ImplFromHwnd(hwnd)) {
            json::Object o;
            o.emplace_back("x", json::Value(static_cast<int64_t>(GET_X_LPARAM(lp))));
            o.emplace_back("y", json::Value(static_cast<int64_t>(GET_Y_LPARAM(lp))));
            Window::Impl::EmitPlatformEvent(impl, "move",
                                    json::Value(std::move(o)).Serialize());
        }
        break;
    }
    case WM_ACTIVATE: {
        if (auto* impl = ImplFromHwnd(hwnd))
            Window::Impl::EmitPlatformEvent(impl, wp != WA_INACTIVE ? "focus" : "blur");
        break;
    }
    case WM_NCCALCSIZE: {
        // F3.5 — overlay real: si es ventana con titlebar CUSTOM, el área
        // cliente cubre TODA la ventana; DWM sigue dibujando los botones
        // min/max/close encima del contenido y los clicks llegan como
        // mensajes no-cliente (WM_NCHITTEST → DefWindowProc los resuelve).
        if (wp && pdata && pdata->customTitlebar) {
            return 0;
        }
        break;
    }
    case WM_NCHITTEST: {
        // deja que DWM resuelva botones/bordes; el drag de la titlebar custom
        // llega por beginMoveDrag() (HTCAPTION sintético).
        break;
    }
    default:
        break;
    }

    if (pdata && pdata->origProc) return CallWindowProcW(pdata->origProc, hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void RegisterClassOnce() {
    static bool done = false;
    if (done) return;
    WNDCLASSW wc{};
    wc.lpfnWndProc = OwWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kOwWindowClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    done = true;
}
} // namespace

Window::~Window() = default;
Window::Impl::~Impl() {
    alive->store(false); // callbacks diferidos dejan de tocar this
    delete pdata;
}

bool Window::Impl::PCreate() {
    log::Info("window", "PCreate: inicio");
    pdata = new PlatformData();
    RegisterClassOnce();

    DWORD style = WS_OVERLAPPEDWINDOW;
    pdata->customTitlebar = opts.titleBarStyle == TitleBarStyle::Custom &&
                            !opts.frameless;
    if (opts.frameless || opts.titleBarStyle == TitleBarStyle::Hidden)
        style = WS_POPUP | WS_THICKFRAME | WS_SYSMENU |
                (opts.resizable ? WS_MAXIMIZEBOX | WS_MINIMIZEBOX : 0);
    // Custom conserva el estilo completo: WM_NCCALCSIZE extiende el cliente.

    std::wstring title = Utf8ToWide(opts.title);
    HWND hwnd = CreateWindowExW(0, kOwWindowClass, title.c_str(), style,
                                CW_USEDEFAULT, CW_USEDEFAULT, opts.width, opts.height,
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) {
        log::Error("window", "CreateWindowExW falló: " +
                                 std::to_string(GetLastError()));
        return false;
    }
    log::Info("window", "PCreate: hwnd creado");

    pdata->hwnd = hwnd;
    HwndMap()[hwnd] = this;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pdata));
    pdata->origProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(OwWndProc)));

    if (!opts.resizable) {
        DWORD s = GetWindowLongW(hwnd, GWL_STYLE);
        SetWindowLongW(hwnd, GWL_STYLE, s & ~(WS_THICKFRAME | WS_MAXIMIZEBOX));
    }

    log::Info("window", "PCreate: webview->Create");
    if (!webview->Create(hwnd, opts.webviewArgs)) {
        log::Error("window", "backend webview rechazó la creación");
        return false;
    }

    const char* assetsDir = std::getenv("OW_ASSETS_DIR");
    if (assetsDir && *assetsDir)
        webview->RegisterAssetScheme("app", std::filesystem::path(assetsDir));
    else
        webview->RegisterAssetScheme("app", std::filesystem::current_path() / "dist");

    if (opts.show) ShowWindow(hwnd, SW_SHOW);
    log::Info("window", "PCreate: ok");
    return true;
}

void Window::Impl::PShow() { if (pdata) ShowWindow(pdata->hwnd, SW_SHOW); }
void Window::Impl::PHide() { if (pdata) ShowWindow(pdata->hwnd, SW_HIDE); }
void Window::Impl::PFocus() { if (pdata) SetForegroundWindow(pdata->hwnd); }
void Window::Impl::PClose() { if (pdata) PostMessageW(pdata->hwnd, WM_CLOSE, 0, 0); }
void Window::Impl::PDestroy() {
    if (pdata) {
        HwndMap().erase(pdata->hwnd);
        DestroyWindow(pdata->hwnd);
    }
}
void Window::Impl::PMinimize() {
    if (pdata) ShowWindow(pdata->hwnd, SW_MINIMIZE);
}
void Window::Impl::PMaximize() {
    if (pdata) ShowWindow(pdata->hwnd, SW_MAXIMIZE);
}
void Window::Impl::PUnmaximize() {
    if (pdata) ShowWindow(pdata->hwnd, SW_RESTORE);
}
void Window::Impl::PRestore() {
    if (pdata) ShowWindow(pdata->hwnd, SW_RESTORE);
}
void Window::Impl::PSetFullScreen(bool enabled) {
    if (!pdata) return;
    if (enabled) {
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(pdata->hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        pdata->preFullscreen = {sizeof(WINDOWPLACEMENT)};
        GetWindowPlacement(pdata->hwnd, &pdata->preFullscreen);
        SetWindowLongW(pdata->hwnd, GWL_STYLE,
                       GetWindowLongW(pdata->hwnd, GWL_STYLE) & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(pdata->hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        pdata->fullscreen = true;
    } else {
        SetWindowLongW(pdata->hwnd, GWL_STYLE,
                       GetWindowLongW(pdata->hwnd, GWL_STYLE) | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(pdata->hwnd, &pdata->preFullscreen);
        SetWindowPos(pdata->hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        pdata->fullscreen = false;
    }
}
bool Window::Impl::PIsMaximized() const {
    return pdata && IsZoomed(pdata->hwnd);
}
bool Window::Impl::PIsMinimized() const {
    return pdata && IsIconic(pdata->hwnd);
}
bool Window::Impl::PIsFullScreen() const { return pdata && pdata->fullscreen; }

Window::Bounds Window::Impl::PGetBounds() const {
    Bounds b;
    if (!pdata) return b;
    RECT r;
    if (GetWindowRect(pdata->hwnd, &r)) {
        b.x = r.left; b.y = r.top;
        b.w = r.right - r.left; b.h = r.bottom - r.top;
    }
    return b;
}
void Window::Impl::PSetBounds(const Bounds& bounds) {
    if (!pdata) return;
    MoveWindow(pdata->hwnd, bounds.x, bounds.y, bounds.w, bounds.h, TRUE);
}
void Window::Impl::PCenter() {
    if (!pdata) return;
    RECT rc;
    GetWindowRect(pdata->hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(pdata->hwnd, nullptr, (sw - w) / 2, (sh - h) / 2, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER);
}

void Window::Impl::PSetTitle(const std::string& t) {
    if (!pdata) return;
    std::wstring w = Utf8ToWide(t);
    SetWindowTextW(pdata->hwnd, w.c_str());
}
std::string Window::Impl::PGetTitle() const {
    if (!pdata) return {};
    wchar_t buf[512]{};
    GetWindowTextW(pdata->hwnd, buf, 512);
    return WideToUtf8(buf);
}

void Window::Impl::PApplyTitleBar() {
    // Hidden/Custom ya son frameless desde PCreate; cambios en caliente (F3)
}

void Window::Impl::PBeginMoveDrag() {
    if (!pdata) return;
    ReleaseCapture();
    PostMessageW(pdata->hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
}

void Window::Impl::PBeginResizeDrag(const std::string& edge) {
    if (!pdata) return;
    static const std::map<std::string, UINT> kEdges = {
        {"left", HTLEFT}, {"right", HTRIGHT},
        {"top", HTTOP}, {"bottom", HTBOTTOM},
        {"top-left", HTTOPLEFT}, {"top-right", HTTOPRIGHT},
        {"bottom-left", HTBOTTOMLEFT}, {"bottom-right", HTBOTTOMRIGHT},
    };
    auto it = kEdges.find(edge);
    if (it == kEdges.end()) return;
    ReleaseCapture();
    PostMessageW(pdata->hwnd, WM_NCLBUTTONDOWN, it->second, 0);
}

} // namespace ow
