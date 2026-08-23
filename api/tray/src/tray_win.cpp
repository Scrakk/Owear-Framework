// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/tray/src/tray_win.cpp — Shell_NotifyIconW (icono real en el área de
// notificación). Ventana fantasma propia recibe los callbacks del icono.
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

namespace tray {

using ow::json::Value;
using ow::json::Array;
using ow::json::Object;
using ow::json::Parse;
using ow::Module::RespondError;
using ow::Module::RespondOk;

constexpr UINT kCallbackMsg = WM_APP + 80;

static HWND s_hwnd = nullptr;
static NOTIFYICONDATAW s_nid{};
static bool s_added = false;

static LRESULT CALLBACK TrayWndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == kCallbackMsg && s_added) {
        // v1: superficie idéntica a Linux (sin eventos de click aún)
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static bool EnsureWindow() {
    if (s_hwnd) return true;
    WNDCLASSW wc{};
    wc.lpfnWndProc = &TrayWndProc;
    wc.lpszClassName = L"owear-tray";
    wc.hInstance = GetModuleHandleW(nullptr);
    RegisterClassW(&wc);
    s_hwnd = CreateWindowExW(0, wc.lpszClassName, nullptr, 0, 0, 0, 0, 0,
                             HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    return s_hwnd != nullptr;
}

void create(const ow_request_t* req, ow_response_t* res) {
    auto parsed = Parse(std::string_view(req->json, req->json_len));
    std::wstring tip = L"Owear";
    if (parsed.value && parsed.value->IsArray() &&
        !parsed.value->AsArray().empty() &&
        parsed.value->AsArray()[0].IsString()) {
        const std::string& id8 = parsed.value->AsArray()[0].AsString();
        int wl = MultiByteToWideChar(CP_UTF8, 0, id8.c_str(),
                                     static_cast<int>(id8.size()), nullptr, 0);
        std::wstring w(wl, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, id8.c_str(), static_cast<int>(id8.size()),
                            w.data(), wl);
        tip = w;
    }

    if (!EnsureWindow())
        return RespondError(res, "no se pudo crear la ventana del tray");
    if (s_added)
        return RespondOk(res, "null"); // ya existe: actualiza tooltip

    ZeroMemory(&s_nid, sizeof(s_nid));
    s_nid.cbSize = sizeof(s_nid);
    s_nid.hWnd = s_hwnd;
    s_nid.uID = 1;
    s_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    s_nid.uCallbackMessage = kCallbackMsg;
    s_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy(s_nid.szTip, tip.c_str(), 127);

    s_added = Shell_NotifyIconW(NIM_ADD, &s_nid) != FALSE;
    if (!s_added) return RespondError(res, "Shell_NotifyIcon falló");
    RespondOk(res, "null");
}

void setIcon(const ow_request_t* req, ow_response_t* res) {
    if (!s_added) return RespondError(res, "tray.create primero");
    auto parsed = Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        parsed.value->AsArray().empty() || !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "iconPath requerido (.ico)");
    const std::string& p8 = parsed.value->AsArray()[0].AsString();
    int wl = MultiByteToWideChar(CP_UTF8, 0, p8.c_str(),
                                 static_cast<int>(p8.size()), nullptr, 0);
    std::wstring w(wl, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, p8.c_str(), static_cast<int>(p8.size()),
                        w.data(), wl);

    HICON icon = static_cast<HICON>(LoadImageW(nullptr, w.c_str(), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON),
                                               GetSystemMetrics(SM_CYSMICON),
                                               LR_LOADFROMFILE));
    if (!icon) return RespondError(res, "no se pudo cargar el .ico");

    s_nid.uFlags = NIF_ICON;
    s_nid.hIcon = icon;
    if (!Shell_NotifyIconW(NIM_MODIFY, &s_nid))
        return RespondError(res, "Shell_NotifyIcon falló");
    RespondOk(res, "null");
}

void setTitle(const ow_request_t* req, ow_response_t* res) {
    if (!s_added) return RespondError(res, "tray.create primero");
    auto parsed = Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        parsed.value->AsArray().empty() || !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "title requerido");
    const std::string& t8 = parsed.value->AsArray()[0].AsString();
    int wl = MultiByteToWideChar(CP_UTF8, 0, t8.c_str(),
                                 static_cast<int>(t8.size()), nullptr, 0);
    std::wstring w(wl, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, t8.c_str(), static_cast<int>(t8.size()),
                        w.data(), wl);

    s_nid.uFlags = NIF_TIP;
    wcsncpy(s_nid.szTip, w.c_str(), 127);
    s_nid.szTip[127] = L'\0';
    if (!Shell_NotifyIconW(NIM_MODIFY, &s_nid))
        return RespondError(res, "Shell_NotifyIcon falló");
    RespondOk(res, "null");
}

} // namespace tray

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"create", &tray::create},
        {"setIcon", &tray::setIcon},
        {"setTitle", &tray::setTitle},
    };
    static const ow_module_desc_t d{
        "tray", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}

