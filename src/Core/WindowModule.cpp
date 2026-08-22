// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Core/WindowModule.cpp — builtin "ow-window": acciones de ventana
// expuestas al renderer (titlebar custom). Mismo ABI que los .owm de usuario.
//
// beginMoveDrag/beginResizeDrag se resuelven antes del dispatcher porque
// necesitan la ventana invocante real (Window_common.cpp).
//
#include "App.hpp"
#include "../Control/ControlServer.hpp"
#include "../Window/Window_p.hpp"
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow/Window.h"

#include <map>

namespace ow {

std::map<WindowId, Window*>& LiveWindows(); // definido en ControlServer.cpp

namespace {

Window* WinFromArgs(const json::Value& args) {
    if (!args.IsArray() || args.AsArray().empty()) return nullptr;
    auto it = LiveWindows().find(static_cast<WindowId>(args.AsArray()[0].AsInt()));
    return it == LiveWindows().end() ? nullptr : it->second;
}

void minimize(const ow_request_t* req, ow_response_t* res) {
    auto parsed = json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return Module::RespondError(res, "args inválidos");
    if (Window* w = WinFromArgs(*parsed.value)) { w->Minimize(); Module::RespondOk(res, "null"); }
    else Module::RespondError(res, "ventana no encontrada");
}

void maximize(const ow_request_t* req, ow_response_t* res) {
    auto parsed = json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().size() < 2)
        return Module::RespondError(res, "se esperan [windowId, maximizar]");
    const auto& a = parsed.value->AsArray();
    auto it = LiveWindows().find(static_cast<WindowId>(a[0].AsInt()));
    if (it == LiveWindows().end()) return Module::RespondError(res, "ventana no encontrada");
    if (a[1].AsBool()) it->second->Maximize();
    else it->second->Unmaximize();
    Module::RespondOk(res, "null");
}

void close(const ow_request_t* req, ow_response_t* res) {
    auto parsed = json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return Module::RespondError(res, "args inválidos");
    if (Window* w = WinFromArgs(*parsed.value)) { w->Close(); Module::RespondOk(res, "null"); }
    else Module::RespondError(res, "ventana no encontrada");
}

void focus(const ow_request_t* req, ow_response_t* res) {
    auto parsed = json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return Module::RespondError(res, "args inválidos");
    if (Window* w = WinFromArgs(*parsed.value)) { w->Focus(); Module::RespondOk(res, "null"); }
    else Module::RespondError(res, "ventana no encontrada");
}

void setTitle(const ow_request_t* req, ow_response_t* res) {
    auto parsed = json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().size() < 2)
        return Module::RespondError(res, "se esperan [windowId, title]");
    const auto& a = parsed.value->AsArray();
    auto it = LiveWindows().find(static_cast<WindowId>(a[0].AsInt()));
    if (it == LiveWindows().end()) return Module::RespondError(res, "ventana no encontrada");
    it->second->SetTitle(a[1].AsString());
    Module::RespondOk(res, "null");
}

// F3.4 — el renderer veta/confirma el cierre pendiente
void respondCloseRequest(const ow_request_t* req, ow_response_t* res) {
    auto parsed = json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().size() < 2)
        return Module::RespondError(res, "se esperan [windowId, requestId, allow]");
    const auto& a = parsed.value->AsArray();
    if (a.size() < 3) return Module::RespondError(res, "falta allow");
    auto it = LiveWindows().find(static_cast<WindowId>(a[0].AsInt()));
    if (it == LiveWindows().end()) return Module::RespondError(res, "ventana no encontrada");
    it->second->impl()->RespondJsClose(
        static_cast<uint64_t>(a[1].AsInt()), a[2].AsBool());
    Module::RespondOk(res, "null");
}

void isMaximized(const ow_request_t* req, ow_response_t* res) {
    auto parsed = json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value) return Module::RespondError(res, "args inválidos");
    if (Window* w = WinFromArgs(*parsed.value))
        Module::RespondOk(res, w->IsMaximized() ? "true" : "false");
    else Module::RespondError(res, "ventana no encontrada");
}

const ow_fn_entry_t kFns[] = {
    {"minimize", &minimize},
    {"maximize", &maximize},
    {"close", &close},
    {"focus", &focus},
    {"setTitle", &setTitle},
    {"isMaximized", &isMaximized},
    {"respondCloseRequest", &respondCloseRequest},
};

const ow_module_desc_t kDesc{"ow-window", "0.1.0", kFns,
                             sizeof(kFns) / sizeof(kFns[0])};

} // namespace

namespace internal {
const ow_module_desc_t* WindowModuleDescriptorImpl() { return &kDesc; }
} // namespace internal

} // namespace ow
