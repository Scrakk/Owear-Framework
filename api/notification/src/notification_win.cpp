// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/notification/src/notification_win.cpp — Shell_NotifyIconW con NIF_INFO:
// en Windows 10+ el sistema convierte los balloon tips en toasts reales del
// Action Center. Icono temporal: se añade al mostrar y se retira al ocultarse.
//
#ifndef UNICODE
#define UNICODE // macros de recurso en variante W
#endif
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <mutex>

namespace notif {

using ow::json::Value;
using ow::json::Parse;
using ow::Module::RespondError;
using ow::Module::RespondOk;

constexpr UINT kCallbackMsg = WM_APP + 90;
constexpr UINT kRemoveTimer = 1;

static HWND s_hwnd = nullptr;
static NOTIFYICONDATAW s_nid{};
static bool s_added = false;
static int64_t s_nextId = 1;
static std::mutex s_mu; // serializa show (un solo slot de icono)

static std::wstring ToWide(const std::string& s) {
    int wl = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                 static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(wl, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        w.data(), wl);
    return w;
}

static void RemoveIcon() {
    if (!s_added) return;
    Shell_NotifyIconW(NIM_DELETE, &s_nid);
    s_added = false;
    KillTimer(s_hwnd, kRemoveTimer);
}

static LRESULT CALLBACK NotifWndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == kCallbackMsg && s_added) {
        // NIN_BALLOONTIMEOUT / NIN_BALLOONHIDE → retira el icono temporal
        if (lp == NIN_BALLOONTIMEOUT || lp == NIN_BALLOONHIDE) RemoveIcon();
        return 0;
    }
    if (m == WM_TIMER && wp == kRemoveTimer) RemoveIcon();
    return DefWindowProcW(h, m, wp, lp);
}

static bool EnsureWindow() {
    if (s_hwnd) return true;
    WNDCLASSW wc{};
    wc.lpfnWndProc = &NotifWndProc;
    wc.lpszClassName = L"owear-notif";
    wc.hInstance = GetModuleHandleW(nullptr);
    RegisterClassW(&wc);
    s_hwnd = CreateWindowExW(0, wc.lpszClassName, nullptr, 0, 0, 0, 0, 0,
                             HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    return s_hwnd != nullptr;
}

// args: [title, body?, appName?]
void show(const ow_request_t* req, ow_response_t* res) {
    auto parsed = Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        parsed.value->AsArray().empty() || !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "title requerido");
    std::wstring title =
        ToWide(parsed.value->AsArray()[0].AsString());
    std::wstring body = parsed.value->AsArray().size() > 1 &&
                                parsed.value->AsArray()[1].IsString()
                            ? ToWide(parsed.value->AsArray()[1].AsString())
                            : L"";
    // appName (arg 2) es metadato del emisor en Win; el balloon ya muestra la
    // identidad del proceso — se acepta y se ignora.

    std::lock_guard lock(s_mu); // un solo icono: serializa shows concurrentes
    if (!EnsureWindow())
        return RespondError(res, "no se pudo crear la ventana de notificación");

    if (!s_added) {
        ZeroMemory(&s_nid, sizeof(s_nid));
        s_nid.cbSize = sizeof(s_nid);
        s_nid.hWnd = s_hwnd;
        s_nid.uID = 1;
        s_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        s_nid.uCallbackMessage = kCallbackMsg;
        s_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wcscpy(s_nid.szTip, L"Owear");
        s_added = Shell_NotifyIconW(NIM_ADD, &s_nid) != FALSE;
        if (!s_added) return RespondError(res, "Shell_NotifyIcon falló");
    }

    s_nid.uFlags = NIF_INFO;
    wcsncpy(s_nid.szInfoTitle, title.c_str(), 63);
    s_nid.szInfoTitle[63] = L'\0';
    wcsncpy(s_nid.szInfo, body.c_str(), 255);
    s_nid.szInfo[255] = L'\0';
    s_nid.dwInfoFlags = NIIF_INFO;
    if (!Shell_NotifyIconW(NIM_MODIFY, &s_nid))
        return RespondError(res, "Shell_NotifyIcon falló");

    // red de seguridad: si el sistema no emite NIN_BALLOON*, retira igual
    SetTimer(s_hwnd, kRemoveTimer, 12000, nullptr);

    RespondOk(res, Value(s_nextId++).Serialize().c_str());
}

} // namespace notif

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {{"show", &notif::show}};
    static const ow_module_desc_t d{
        "notification", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}

