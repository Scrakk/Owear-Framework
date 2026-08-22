// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/globalshortcut/src/gs_linux.cpp — XGrabKey en root window (X11).
// En Wayland no funciona (compositor aísla los atajos globales) — es
// OPCIONAL y las apps pueden usar su propio shortcut manager.
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>

#include <map>
#include <mutex>
#include <vector>

namespace gs {

using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static const ow_module_host_t* g_host = nullptr;
struct Binding {
    std::string accelerator;
    KeyCode keycode;
    unsigned mods;
};
static std::map<int, Binding> g_bindings;   // bindingId → binding
static std::mutex g_mu;
static int g_next = 1;

// parse mínimo "Ctrl+Shift+P" → modifiers + keysym
static bool ParseAccel(const std::string& accel, unsigned* mods, KeySym* sym) {
    unsigned m = 0;
    std::string key;
    size_t start = 0;
    while (true) {
        size_t plus = accel.find('+', start);
        std::string part = accel.substr(
            start, plus == std::string::npos ? std::string::npos : plus - start);
        if (part == "Ctrl" || part == "Control") m |= ControlMask;
        else if (part == "Shift") m |= ShiftMask;
        else if (part == "Alt") m |= Mod1Mask;
        else if (part == "Super" || part == "Meta") m |= Mod4Mask;
        else key = part;
        if (plus == std::string::npos) break;
        start = plus + 1;
    }
    *sym = XStringToKeysym(key.c_str());
    return *sym != NoSymbol && !key.empty();
}

void InstallFilter(); // filtro GDK para capturar keypress del root

void registerFn(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        parsed.value->AsArray().empty() || !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "accelerator requerido");
    std::string acc = parsed.value->AsArray()[0].AsString();

    GdkDisplay* gd = gdk_display_get_default();
    if (!GDK_IS_X11_DISPLAY(gd))
        return RespondError(res, "globalShortcut requiere sesión X11");

    Display* dpy = gdk_x11_display_get_xdisplay(gd);
    Window root = DefaultRootWindow(dpy);

    unsigned mods = 0;
    KeySym sym = 0;
    if (!ParseAccel(acc, &mods, &sym)) return RespondError(res, "accelerator inválido");
    KeyCode kc = XKeysymToKeycode(dpy, sym);

    // probar variantes NumLock/CapsLock
    unsigned combos[] = {0, Mod2Mask, LockMask, Mod2Mask | LockMask};
    for (unsigned extra : combos)
        XGrabKey(dpy, kc, mods | extra, root, True, GrabModeAsync, GrabModeAsync);

    XSync(dpy, False);

    std::lock_guard lock(g_mu);
    int id = g_next++;
    g_bindings[id] = {acc, kc, mods};

    InstallFilter();
    RespondOk(res, Value(static_cast<int64_t>(id)).Serialize().c_str());
}

void unregister(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        parsed.value->AsArray().empty())
        return RespondError(res, "bindingId requerido");
    int id = static_cast<int>(parsed.value->AsArray()[0].AsInt());

    std::lock_guard lock(g_mu);
    auto it = g_bindings.find(id);
    if (it == g_bindings.end()) return RespondOk(res, "null");
    GdkDisplay* gd = gdk_display_get_default();
    if (GDK_IS_X11_DISPLAY(gd)) {
        Display* dpy = gdk_x11_display_get_xdisplay(gd);
        Window root = DefaultRootWindow(dpy);
        unsigned combos[] = {0, Mod2Mask, LockMask, Mod2Mask | LockMask};
        for (unsigned extra : combos)
            XUngrabKey(dpy, it->second.keycode,
                       it->second.mods | extra, root);
        XSync(dpy, False);
    }
    g_bindings.erase(it);
    RespondOk(res, "null");
}

} // namespace gs

namespace {
GdkFilterReturn KeyFilter(GdkXEvent* gx, GdkEvent*, gpointer) {
    auto* ev = static_cast<XEvent*>(gx);
    if (ev->type != KeyPress) return GDK_FILTER_CONTINUE;

    std::lock_guard lock(gs::g_mu);
    for (auto& [id, b] : gs::g_bindings) {
        if (ev->xkey.keycode == b.keycode &&
            (ev->xkey.state & (ControlMask | ShiftMask | Mod1Mask | Mod4Mask)) ==
                b.mods) {
            if (gs::g_host && gs::g_host->emit_event) {
                std::string json =
                    "{\"id\":" + std::to_string(id) +
                    ",\"accelerator\":" +
                    ow::json::Value(b.accelerator).Serialize() + "}";
                gs::g_host->emit_event(gs::g_host->ctx, 0,
                                       "globalShortcut.press", json.c_str());
            }
            return GDK_FILTER_REMOVE;
        }
    }
    return GDK_FILTER_CONTINUE;
}
} // namespace

namespace gs {
void InstallFilter() {
    static bool installed = false;
    if (!installed) {
        gdk_window_add_filter(nullptr, &KeyFilter, nullptr);
        installed = true;
    }
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
