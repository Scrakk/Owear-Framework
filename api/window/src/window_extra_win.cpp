// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/window/src/window_extra_win.cpp — builtin "window-extras" (Windows).
// Funciones avanzadas de ventana acopladas a WebView2.
//
#include "../../../src/Core/BuiltinUtil.hpp"
#include "ow/Base64.h"
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow/Shm.h"
#include "ow_api.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>

#include <WebView2.h>
#include <wrl/client.h>
#include <wrl/event.h>

#include <chrono>
#include <string>
#include <vector>

namespace winx {

using namespace Microsoft::WRL;
using ow::json::Array;
using ow::json::Object;
using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static inline uint32_t WinId(const Value& args) {
    if (!args.IsArray() || args.AsArray().empty() || !args.AsArray()[0].IsNumber())
        return 0;
    return static_cast<uint32_t>(args.AsArray()[0].AsInt());
}

#define NEED_WIN(id)                                                          \
    HWND hwndWin = static_cast<HWND>(ow::builtin::WindowById(id));            \
    ICoreWebView2* wv = static_cast<ICoreWebView2*>(ow::builtin::WebviewById(id)); \
    if (!hwndWin || !wv) return RespondError(res, "ventana no encontrada");

// ── helpers de controller: el backend expone el HWND; el controller vive en
// el backend. Para zoom/topmost usamos APIs del HWND y del webview. ──────────

static bool GetBoolArg(const Value& args, size_t idx, bool dflt) {
    if (args.IsArray() && args.AsArray().size() > idx && args.AsArray()[idx].IsBool())
        return args.AsArray()[idx].AsBool();
    return dflt;
}

static double GetDoubleArg(const Value& args, size_t idx, double dflt) {
    if (args.IsArray() && args.AsArray().size() > idx && args.AsArray()[idx].IsNumber())
        return args.AsArray()[idx].AsDouble();
    return dflt;
}

void openDevTools(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    // OpenDevToolsWindow existe desde ICoreWebView2_14; si no está, error claro
    using OpenDevFn = HRESULT(__stdcall ICoreWebView2::*)(void);
    // QueryInterface a ICoreWebView2_14
    ICoreWebView2_14* wv14 = nullptr;
    if (SUCCEEDED(wv->QueryInterface(IID_PPV_ARGS(&wv14))) && wv14) {
        bool show = GetBoolArg(args, 1, true);
        if (show) wv14->OpenDevToolsWindow();
        wv14->Release();
        RespondOk(res, "null");
        return;
    }
    RespondError(res, "OpenDevTools no disponible en este runtime");
}

// ── capturePage → PNG → SHM ──────────────────────────────────────────────────

struct SnapCtx {
    volatile bool done = false;
    HRESULT hr = E_FAIL;
    std::vector<uint8_t> png;
};

void capturePage(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)

    // stream en memoria para recibir el PNG
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) || !stream)
        return RespondError(res, "CreateStreamOnHGlobal falló");

    SnapCtx ctx;
    auto cb = Microsoft::WRL::Callback<ICoreWebView2CapturePreviewCompletedHandler>(
        [&ctx](HRESULT error) -> HRESULT {
            ctx.hr = error;
            ctx.done = true;
            return S_OK;
        });
    HRESULT hr = wv->CapturePreview(
        COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG, stream, cb.Get());
    if (FAILED(hr)) {
        stream->Release();
        return RespondError(res, "CapturePreview devolvió 0x" +
                                     std::to_string(static_cast<unsigned long>(hr)));
    }

    // pump de mensajes del hilo UI hasta que el callback dispare (patrón
    // PumpUntil de la plataforma)
    auto start = std::chrono::steady_clock::now();
    MSG msg;
    while (!ctx.done) {
        BOOL r = PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE);
        if (r) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        } else {
            Sleep(10);
        }
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
            ctx.done = true;
            ctx.hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }
    }

    if (FAILED(ctx.hr)) {
        stream->Release();
        return RespondError(res, "capture falló: 0x" +
                                     std::to_string(static_cast<unsigned long>(ctx.hr)));
    }

    // lee el stream completo
    STATSTG st{};
    stream->Stat(&st, STATFLAG_NONAME);
    LARGE_INTEGER zero{};
    zero.QuadPart = 0;
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    ctx.png.resize(static_cast<size_t>(st.cbSize.QuadPart));
    ULONG read = 0;
    stream->Read(ctx.png.data(), static_cast<ULONG>(ctx.png.size()), &read);
    ctx.png.resize(read);
    stream->Release();

    const char* sid = ow_shm_put(ctx.png.data(), ctx.png.size());
    if (!sid || !*sid) return RespondError(res, "SHM llena");

    Object shm;
    shm.emplace_back("id", Value(std::string(sid)));
    shm.emplace_back("size", Value(static_cast<int64_t>(ctx.png.size())));
    Object o;
    o.emplace_back("__ow_shm", Value(std::move(shm)));
    o.emplace_back("format", Value("png"));
    RespondOk(res, Value(std::move(o)).Serialize().c_str());
}

// ── propiedades de ventana vía Win32 ────────────────────────────────────────

void setAlwaysOnTop(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    SetWindowPos(hwndWin, GetBoolArg(args, 1, false) ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    RespondOk(res, "null");
}
void isAlwaysOnTop(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    DWORD ex = GetWindowLongW(hwndWin, GWL_EXSTYLE);
    RespondOk(res, (ex & WS_EX_TOPMOST) ? "true" : "false");
}

void setOpacity(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    double opacity = GetDoubleArg(args, 1, 1.0);
    if (opacity < 0) opacity = 0;
    if (opacity > 1) opacity = 1;
    LONG ex = GetWindowLongW(hwndWin, GWL_EXSTYLE);
    BYTE alpha = static_cast<BYTE>(opacity * 255.0);
    if (opacity >= 1.0) {
        SetWindowLongW(hwndWin, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
        SetLayeredWindowAttributes(hwndWin, 0, 255, LWA_ALPHA);
    } else {
        SetWindowLongW(hwndWin, GWL_EXSTYLE, ex | WS_EX_LAYERED);
        SetLayeredWindowAttributes(hwndWin, 0, alpha, LWA_ALPHA);
    }
    RespondOk(res, "null");
}

void flashFrame(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    FLASHWINFO fi{sizeof(fi)};
    fi.hwnd = hwndWin;
    bool on = GetBoolArg(args, 1, false);
    fi.dwFlags = on ? FLASHW_ALL : FLASHW_STOP;
    fi.uCount = on ? 4 : 0;
    FlashWindowEx(&fi);
    RespondOk(res, "null");
}

void setUserAgent(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    if (!args.IsArray() || args.AsArray().size() < 2 || !args.AsArray()[1].IsString())
        return RespondError(res, "userAgent requerido");
    ICoreWebView2Settings* st = nullptr;
    if (FAILED(wv->get_Settings(&st)) || !st)
        return RespondError(res, "settings no disponibles");
    ICoreWebView2Settings2* st2 = nullptr;
    if (SUCCEEDED(st->QueryInterface(IID_PPV_ARGS(&st2))) && st2) {
        // UTF-8 → UTF-16
        const std::string& ua = args.AsArray()[1].AsString();
        int n = MultiByteToWideChar(CP_UTF8, 0, ua.c_str(),
                                    static_cast<int>(ua.size()), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, ua.c_str(), static_cast<int>(ua.size()),
                            w.data(), n);
        st2->put_UserAgent(w.c_str());
        st2->Release();
    }
    st->Release();
    RespondOk(res, "null");
}

void zoom(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    double factor = GetDoubleArg(args, 1, 1.0);
    // El zoom factor vive en el controller (no accesible por el puntero del
    // webview); se aplica con Ctrl+rueda nativamente. Registramos el valor
    // vía ExecuteScript sobre CSS zoom como fallback determinista.
    std::wstring js = L"document.body.style.zoom=" + std::to_wstring(factor) + L";";
    wv->ExecuteScript(js.c_str(), nullptr);
    RespondOk(res, "null");
}

// ── navegación + estado ─────────────────────────────────────────────────────

void reload(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    wv->Reload();
    RespondOk(res, "null");
}
void stop(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    wv->Stop();
    RespondOk(res, "null");
}
void goBack(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    wv->GoBack();
    RespondOk(res, "null");
}
void goForward(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    wv->GoForward();
    RespondOk(res, "null");
}
void canGoBack(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    BOOL b = FALSE;
    wv->get_CanGoBack(&b);
    RespondOk(res, b ? "true" : "false");
}
void canGoForward(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    BOOL b = FALSE;
    wv->get_CanGoForward(&b);
    RespondOk(res, b ? "true" : "false");
}
void getURL(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    LPWSTR s = nullptr;
    wv->get_Source(&s);
    std::string out = s ? "ok" : "";
    std::string url = s ? [](LPWSTR w) {
        int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        std::string r(n > 0 ? n - 1 : 0, '\0');
        if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, r.data(), n,
                                       nullptr, nullptr);
        return r;
    }(s) : "";
    if (s) CoTaskMemFree(s);
    RespondOk(res, Value(url).Serialize().c_str());
}
void getTitle(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    LPWSTR s = nullptr;
    wv->get_DocumentTitle(&s);
    std::string t = s ? [](LPWSTR w) {
        int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        std::string r(n > 0 ? n - 1 : 0, '\0');
        if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, r.data(), n,
                                       nullptr, nullptr);
        return r;
    }(s) : "";
    if (s) CoTaskMemFree(s);
    RespondOk(res, Value(t).Serialize().c_str());
}

} // namespace winx

namespace ow::internal {
const ow_module_desc_t* WindowExtrasDescriptorWin(void) {
    static const ow_fn_entry_t fns[] = {
        {"openDevTools", &winx::openDevTools},
        {"capturePage", &winx::capturePage},
        {"setAlwaysOnTop", &winx::setAlwaysOnTop},
        {"isAlwaysOnTop", &winx::isAlwaysOnTop},
        {"setOpacity", &winx::setOpacity},
        {"flashFrame", &winx::flashFrame},
        {"setUserAgent", &winx::setUserAgent},
        {"zoom", &winx::zoom},
        {"reload", &winx::reload},
        {"stop", &winx::stop},
        {"goBack", &winx::goBack},
        {"goForward", &winx::goForward},
        {"canGoBack", &winx::canGoBack},
        {"canGoForward", &winx::canGoForward},
        {"getURL", &winx::getURL},
        {"getTitle", &winx::getTitle},
    };
    static const ow_module_desc_t d{
        "window", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}
} // namespace ow::internal
