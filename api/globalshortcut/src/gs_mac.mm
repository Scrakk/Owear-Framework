// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/globalshortcut/src/gs_mac.mm — RegisterEventHotKey (Carbon HIToolbox),
// mecanismo estándar de atajos globales en macOS.
//
#import <Carbon/Carbon.h>

#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#include <map>
#include <mutex>

namespace gs {

using ow::json::Value;
using ow::json::Parse;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static const ow_module_host_t* g_host = nullptr;
struct Binding {
    std::string accel;
    EventHotKeyRef ref = nullptr;
};
static std::map<int, Binding> g_bindings;
static std::mutex g_mu;
static int g_next = 1;

// "Ctrl+Shift+P" → modificadores Carbon + tecla virtual
static bool ParseAccel(const std::string& accel, UInt32* mods, UInt32* vk) {
    UInt32 m = 0;
    std::string key;
    size_t start = 0;
    while (true) {
        size_t plus = accel.find('+', start);
        std::string part =
            accel.substr(start, plus == std::string::npos ? std::string::npos
                                                          : plus - start);
        if (part == "Ctrl" || part == "Control") m |= controlKey;
        else if (part == "Shift") m |= shiftKey;
        else if (part == "Alt" || part == "Option") m |= optionKey;
        else if (part == "Super" || part == "Meta" || part == "Cmd") m |= cmdKey;
        else if (!part.empty()) key = part;
        if (plus == std::string::npos) break;
        start = plus + 1;
    }
    if (key.size() == 1 && key[0] >= 'a' && key[0] <= 'z')
        *vk = kVK_ANSI_A + (key[0] - 'a');
    else if (key.size() == 1 && key[0] >= 'A' && key[0] <= 'Z')
        *vk = kVK_ANSI_A + (key[0] - 'A');
    else if (key.size() == 1 && key[0] >= '0' && key[0] <= '9')
        *vk = kVK_ANSI_0 + (key[0] - '0');
    else if (key.rfind("F", 0) == 0 && atoi(key.c_str() + 1) >= 1)
        *vk = kVK_F1 + (atoi(key.c_str() + 1) - 1);
    else if (key == "Space") *vk = kVK_Space;
    else if (key == "Enter" || key == "Return") *vk = kVK_Return;
    else if (key == "Tab") *vk = kVK_Tab;
    else if (key == "Up") *vk = kVK_UpArrow;
    else if (key == "Down") *vk = kVK_DownArrow;
    else if (key == "Left") *vk = kVK_LeftArrow;
    else if (key == "Right") *vk = kVK_RightArrow;
    else if (key == "Home") *vk = kVK_Home;
    else if (key == "End") *vk = kVK_End;
    else if (key == "PageUp") *vk = kVK_PageUp;
    else if (key == "PageDown") *vk = kVK_PageDown;
    else if (key == "Delete") *vk = kVK_Delete;
    else return false;
    *mods = m;
    return true;
}

static void EmitPress(int id, const std::string& acc) {
    if (!g_host || !g_host->emit_event) return;
    std::string json = "{\"id\":" + std::to_string(id) +
                       ",\"accelerator\":" +
                       ow::json::Value(acc).Serialize() + "}";
    g_host->emit_event(g_host->ctx, 0, "globalShortcut.press", json.c_str());
}

} // namespace gs

static OSStatus GsHandler(EventHandlerCallRef, EventRef ev, void*) {
    EventHotKeyID hkid{};
    OSStatus st = ::GetEventParameter(ev, kEventParamDirectObject,
                                      typeEventHotKeyID, nullptr, sizeof(hkid),
                                      nullptr, &hkid);
    if (st != noErr) return st;
    std::lock_guard lock(gs::g_mu);
    auto it = gs::g_bindings.find(static_cast<int>(hkid.id));
    if (it != gs::g_bindings.end()) gs::EmitPress(it->first, it->second.accel);
    return noErr;
}

namespace gs {

void registerFn(const ow_request_t* req, ow_response_t* res) {
    auto parsed = Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        parsed.value->AsArray().empty() || !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "accelerator requerido");
    std::string acc = parsed.value->AsArray()[0].AsString();

    UInt32 mods = 0, vk = 0;
    if (!ParseAccel(acc, &mods, &vk))
        return RespondError(res, "accelerator inválido");

    static bool installed = false;
    if (!installed) {
        EventTypeSpec spec{kEventClassKeyboard, kEventHotKeyPressed};
        InstallEventHandler(GetApplicationEventTarget(), &GsHandler, 1, &spec,
                            nullptr, nullptr);
        installed = true;
    }

    int id;
    {
        std::lock_guard lock(g_mu);
        id = g_next++;
    }
    EventHotKeyRef ref = nullptr;
    EventHotKeyID hkid{'owea', static_cast<UInt32>(id)};
    OSStatus st = RegisterEventHotKey(vk, mods, hkid,
                                      GetApplicationEventTarget(), 0, &ref);
    if (st != noErr || !ref)
        return RespondError(res, "registro fallido (¿en uso por el sistema?)");

    {
        std::lock_guard lock(g_mu);
        g_bindings[id] = {acc, ref};
    }
    RespondOk(res, Value(static_cast<int64_t>(id)).Serialize().c_str());
}

void unregister(const ow_request_t* req, ow_response_t* res) {
    auto parsed = Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        parsed.value->AsArray().empty())
        return RespondError(res, "bindingId requerido");
    int id = static_cast<int>(parsed.value->AsArray()[0].AsInt());

    std::lock_guard lock(g_mu);
    auto it = g_bindings.find(id);
    if (it == g_bindings.end()) return RespondOk(res, "null");
    UnregisterEventHotKey(it->second.ref);
    g_bindings.erase(it);
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

