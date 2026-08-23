// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/menu/src/menu_win.cpp — menú contextual nativo (TrackPopupMenu) en un
// hilo de UI dedicado: los menús Win32 exigen ventana y bomba de mensajes
// propias. Superficie idéntica a Linux: popup(items) + evento menu.click.
// setApplicationMenu es noop POR DISEÑO (igual que Linux: no imponemos
// menubar; el IDE dibuja el suyo en web si prefiere).
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace menu {

using ow::json::Array;
using ow::json::Value;
using ow::json::Parse;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static const ow_module_host_t* g_host = nullptr;

constexpr UINT kMsgPopup = WM_APP + 85;

static HWND s_hwnd = nullptr;
static std::mutex s_mu;
static std::condition_variable s_cv;

struct PopupCmd {
    std::string json;
    uint32_t win = 0;
    bool done = false;
    std::string chosen;
};

static std::vector<std::string> s_labels; // cmdId-1000 → label

static void AppendItems(HMENU m, const Array& items) {
    for (const auto& it : items) {
        if (!it.IsObject()) continue;
        std::string type = it.Find("type") && (*it.Find("type")).IsString()
                               ? (*it.Find("type")).AsString()
                               : "normal";
        if (type == "separator") {
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            continue;
        }
        std::string label =
            it.Find("label") && (*it.Find("label")).IsString()
                ? (*it.Find("label")).AsString()
                : "";
        int wl = MultiByteToWideChar(CP_UTF8, 0, label.c_str(),
                                     static_cast<int>(label.size()), nullptr, 0);
        std::wstring w(wl, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, label.c_str(),
                            static_cast<int>(label.size()), w.data(), wl);

        const Value* sub = it.Find("submenu");
        if (sub && sub->IsArray()) {
            HMENU sm = CreatePopupMenu();
            AppendItems(sm, sub->AsArray());
            AppendMenuW(m, MF_POPUP, reinterpret_cast<UINT_PTR>(sm), w.c_str());
        } else {
            UINT_PTR cmd = s_labels.size() + 1000;
            s_labels.push_back(label);
            AppendMenuW(m, MF_STRING, cmd, w.c_str());
        }
    }
}

static LRESULT CALLBACK MenuWndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == kMsgPopup) {
        auto* c = reinterpret_cast<PopupCmd*>(lp);
        HMENU hm = CreatePopupMenu();
        s_labels.clear();
        auto parsed = Parse(c->json);
        if (parsed.value && parsed.value->IsArray() &&
            !parsed.value->AsArray().empty() &&
            (*parsed.value).AsArray()[0].IsArray())
            AppendItems(hm, (*parsed.value).AsArray()[0].AsArray());

        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(h); // requisito para que el menú descarte bien
        int cmd = TrackPopupMenuEx(hm,
                                   TPM_RIGHTBUTTON | TPM_RETURNCMD |
                                       TPM_NONOTIFY,
                                   pt.x, pt.y, h, nullptr);
        PostMessageW(h, WM_NULL, 0, 0); // disipa el estado de foreground
        DestroyMenu(hm);

        if (cmd >= 1000 && static_cast<size_t>(cmd - 1000) < s_labels.size())
            c->chosen = s_labels[static_cast<size_t>(cmd - 1000)];
        {
            std::lock_guard lk(s_mu);
            c->done = true;
        }
        s_cv.notify_all();
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static void MenuThread() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = &MenuWndProc;
    wc.lpszClassName = L"owear-menu";
    wc.hInstance = GetModuleHandleW(nullptr);
    RegisterClassW(&wc);
    s_hwnd = CreateWindowExW(0, wc.lpszClassName, nullptr, 0, 0, 0, 0, 0,
                             HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    MSG msg;
    while (GetMessage(&msg, s_hwnd, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

// args: [items]
void popup(const ow_request_t* req, ow_response_t* res) {
    auto parsed = Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        parsed.value->AsArray().empty())
        return RespondError(res, "items requeridos");

    static std::once_flag once;
    std::call_once(once, [] { std::thread(MenuThread).detach(); });
    for (int i = 0; i < 100 && !s_hwnd; ++i) Sleep(10);
    if (!s_hwnd) return RespondError(res, "hilo de menú no disponible");

    PopupCmd c;
    c.json = std::string(req->json, req->json_len);
    c.win = req->window_id;
    PostMessageW(s_hwnd, kMsgPopup, 0, reinterpret_cast<LPARAM>(&c));
    {
        std::unique_lock lk(s_mu);
        s_cv.wait(lk, [&] { return c.done; }); // bloquea hasta selección
    }

    if (!c.chosen.empty() && g_host && g_host->emit_event) {
        std::string json = "{\"label\":" + Value(c.chosen).Serialize() +
                           ",\"windowId\":" + std::to_string(c.win) + "}";
        g_host->emit_event(g_host->ctx, c.win, "menu.click", json.c_str());
    }
    RespondOk(res, "null");
}

} // namespace menu

extern "C" OW_MODULE_EXPORT void ow_module_set_host(const ow_module_host_t* h) {
    menu::g_host = h;
}

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"popup", &menu::popup},
        {"setApplicationMenu",
         [](const ow_request_t*, ow_response_t* res) {
             // noop por diseño — idéntico a Linux (no imponemos menubar)
             ow::Module::RespondOk(res, "\"noop\"");
         }},
    };
    static const ow_module_desc_t d{
        "menu", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}

