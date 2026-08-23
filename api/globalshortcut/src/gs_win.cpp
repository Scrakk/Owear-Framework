// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/globalshortcut/src/gs_win.cpp — RegisterHotKey en hilo dedicado con
// ventana fantasma: WM_HOTKEY llega a la cola del hilo que registró, así que
// registrar y bombear mensajes ocurre en el MISMO hilo (el dueño del hwnd).
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace gs {

using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static const ow_module_host_t* g_host = nullptr;
static std::mutex g_mu;
static std::map<int, std::string> g_accels;
static int g_next = 1;

constexpr UINT kMsgRegister = WM_APP + 71;
constexpr UINT kMsgUnregister = WM_APP + 72;

static HWND s_hwnd = nullptr;
static std::mutex s_cmdMu;
static std::condition_variable s_cmdCv;
struct Cmd {
    std::string accel;
    int id = 0;
    bool done = false;
    bool ok = false;
    std::string err;
} *s_pending = nullptr;

// "Ctrl+Shift+P" → MOD_* + VK
static bool ParseAccel(const std::string& accel, UINT* mods, UINT* vk) {
    UINT m = MOD_NOREPEAT;
    std::string key;
    size_t start = 0;
    while (true) {
        size_t plus = accel.find('+', start);
        std::string part =
            accel.substr(start, plus == std::string::npos
                                     ? std::string::npos
                                     : plus - start);
        if (part == "Ctrl" || part == "Control") m |= MOD_CONTROL;
        else if (part == "Shift") m |= MOD_SHIFT;
        else if (part == "Alt") m |= MOD_ALT;
        else if (part == "Super" || part == "Meta" || part == "Win") m |= MOD_WIN;
        else if (!part.empty()) key = part;
        if (plus == std::string::npos) break;
        start = plus + 1;
    }
    if (key.size() == 1 && key[0] >= 'a' && key[0] <= 'z') key[0] -= 32;
    if (key.size() == 1 && key[0] >= 'A' && key[0] <= 'Z') { *vk = key[0]; }
    else if (key.size() == 1 && key[0] >= '0' && key[0] <= '9') { *vk = key[0]; }
    else if (key.rfind("F", 0) == 0 && key.size() >= 2 && key.size() <= 3) {
        int f = atoi(key.c_str() + 1);
        if (f < 1 || f > 24 || f == 12) return false; // F12 reservado debugger
        *vk = VK_F1 + (f - 1);
    }
    else if (key == "Space") *vk = VK_SPACE;
    else if (key == "Enter" || key == "Return") *vk = VK_RETURN;
    else if (key == "Tab") *vk = VK_TAB;
    else if (key == "Up") *vk = VK_UP;
    else if (key == "Down") *vk = VK_DOWN;
    else if (key == "Left") *vk = VK_LEFT;
    else if (key == "Right") *vk = VK_RIGHT;
    else if (key == "Home") *vk = VK_HOME;
    else if (key == "End") *vk = VK_END;
    else if (key == "PageUp") *vk = VK_PRIOR;
    else if (key == "PageDown") *vk = VK_NEXT;
    else if (key == "Insert") *vk = VK_INSERT;
    else if (key == "Delete") *vk = VK_DELETE;
    else return false;
    *mods = m;
    return true;
}

static LRESULT CALLBACK GsWndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == kMsgRegister || m == kMsgUnregister) {
        Cmd* c = reinterpret_cast<Cmd*>(lp);
        if (m == kMsgRegister) {
            UINT mods = 0, vk = 0;
            if (ParseAccel(c->accel, &mods, &vk))
                c->ok = RegisterHotKey(nullptr, c->id, mods, vk) != 0;
            if (!c->ok) c->err = "registro fallido (¿en uso?)";
        } else {
            c->ok = UnregisterHotKey(nullptr, c->id) != 0;
            if (!c->ok) c->err = "unregister fallido";
        }
        {
            std::lock_guard lk(s_cmdMu);
            c->done = true;
        }
        s_cmdCv.notify_all();
        return 0;
    }
    if (m == WM_HOTKEY) {
        std::string acc;
        {
            std::lock_guard lock(g_mu);
            auto it = g_accels.find(static_cast<int>(wp));
            if (it != g_accels.end()) acc = it->second;
        }
        if (!acc.empty() && g_host && g_host->emit_event) {
            std::string json = "{\"id\":" + std::to_string(wp) +
                               ",\"accelerator\":" +
                               ow::json::Value(acc).Serialize() + "}";
            g_host->emit_event(g_host->ctx, 0, "globalShortcut.press",
                               json.c_str());
        }
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

static void GsThread() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = &GsWndProc;
    wc.lpszClassName = L"owear-gs";
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

static bool CallGs(bool reg, const std::string& accel, int* id,
                   std::string& err) {
    static std::once_flag once;
    std::call_once(once, [] { std::thread(GsThread).detach(); });
    // espera a que el hilo cree la ventana fantasma
    for (int i = 0; i < 100 && !s_hwnd; ++i) Sleep(10);
    if (!s_hwnd) {
        err = "globalShortcut no disponible";
        return false;
    }

    Cmd c;
    c.accel = accel;
    c.id = *id;
    PostMessageW(s_hwnd, reg ? kMsgRegister : kMsgUnregister, 0,
                 reinterpret_cast<LPARAM>(&c));
    {
        std::unique_lock lk(s_cmdMu);
        s_cmdCv.wait(lk, [&] { return c.done; });
    }
    *id = c.id;
    err = c.err;
    return c.ok;
}

void registerFn(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        parsed.value->AsArray().empty() || !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "accelerator requerido");
    std::string acc = parsed.value->AsArray()[0].AsString();

    int id;
    {
        std::lock_guard lock(g_mu);
        id = g_next++;
    }
    std::string err;
    if (!CallGs(true, acc, &id, err))
        return RespondError(res, err.empty() ? "accelerator inválido" : err);

    {
        std::lock_guard lock(g_mu);
        g_accels[id] = acc;
    }
    RespondOk(res, Value(static_cast<int64_t>(id)).Serialize().c_str());
}

void unregister(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        parsed.value->AsArray().empty())
        return RespondError(res, "bindingId requerido");
    int id = static_cast<int>(parsed.value->AsArray()[0].AsInt());

    {
        std::lock_guard lock(g_mu);
        if (!g_accels.count(id)) return RespondOk(res, "null");
    }
    std::string err;
    CallGs(false, "", &id, err);
    {
        std::lock_guard lock(g_mu);
        g_accels.erase(id);
    }
    RespondOk(res, "null");
}

} // namespace gs

extern "C" OW_MODULE_EXPORT void ow_module_set_host(const ow_module_host_t* h) {
    gs::g_host = h;
}

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"register", &gs::registerFn},
        {"unregister", &gs::unregister},
    };
    static const ow_module_desc_t d{
        "globalshortcut", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}

