// Copyright 2026 Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/power/src/power_win.cpp — PowerRegisterSuspendResumeNotification
// (monitor suspend/resume) cargado DINÁMICAMENTE de powrprof.dll (mismo
// patrón que ConPTY en pty_win: compila con cualquier SDK) y
// SetThreadExecutionState para los inhibidores.
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <map>
#include <mutex>

#ifndef DEVICE_NOTIFY_CALLBACK
#define DEVICE_NOTIFY_CALLBACK 2u
#endif

namespace pw {

using ow::json::Value;
using ow::json::Parse;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static const ow_module_host_t* g_host = nullptr;

// ── monitor ──────────────────────────────────────────────────────────────────
using NotifyCallback = void (*)(void*, ULONG, void*);
struct NotifyUserCtx {
    NotifyCallback cb;
    void* ctx;
};
using RegisterFn = unsigned long (__stdcall*)(unsigned long, void*, void**);
using UnregisterFn = unsigned long (__stdcall*)(void*);

static void OnPowerEvent(void*, ULONG type, void*) {
    if (!g_host || !g_host->emit_event) return;
    // PBT_APMSUSPEND=4 · PBT_APMRESUMESUSPEND=7 · PBT_APMRESUMEAUTOMATIC=18
    if (type == 4)
        g_host->emit_event(g_host->ctx, 0, "power.suspend", "null");
    else if (type == 7 || type == 18)
        g_host->emit_event(g_host->ctx, 0, "power.resume", "null");
}

static NotifyUserCtx s_ncb{&OnPowerEvent, nullptr};
static void* s_regHandle = nullptr;

void monitorStart(const ow_request_t*, ow_response_t* res) {
    if (s_regHandle) return RespondOk(res, "\"ya-activo\"");

    HMODULE pp = LoadLibraryW(L"powrprof.dll");
    if (!pp) return RespondError(res, "powrprof no disponible");
    auto reg = reinterpret_cast<RegisterFn>(
        GetProcAddress(pp, "PowerRegisterSuspendResumeNotification"));
    if (!reg) {
        FreeLibrary(pp);
        return RespondError(res, "PowerRegisterSuspendResumeNotification no "
                                 "disponible (< Windows 8)");
    }
    unsigned long rc =
        reg(DEVICE_NOTIFY_CALLBACK, &s_ncb, &s_regHandle);
    if (rc != ERROR_SUCCESS) {
        s_regHandle = nullptr;
        return RespondError(res, "registro de notificaciones falló");
    }
    RespondOk(res, "null");
}

// ── inhibidores ──────────────────────────────────────────────────────────────
static std::mutex s_mu;
static std::map<int, bool> s_inhibits; // id → preventAppSuspension
static int s_nextInhibit = 1;

static void ApplyUnion() {
    bool display = false, system = false;
    for (auto& [id, app] : s_inhibits) {
        if (app) system = true;
        else display = true;
    }
    ULONG flags = ES_CONTINUOUS;
    if (display) flags |= ES_DISPLAY_REQUIRED;
    if (system) flags |= ES_SYSTEM_REQUIRED;
    SetThreadExecutionState(flags);
}

void inhibitStart(const ow_request_t* req, ow_response_t* res) {
    auto parsed = Parse(std::string_view(req->json, req->json_len));
    std::string what = "prevent-display-sleep";
    if (parsed.value && parsed.value->IsArray() &&
        !parsed.value->AsArray().empty() &&
        parsed.value->AsArray()[0].IsString())
        what = parsed.value->AsArray()[0].AsString();

    std::lock_guard lock(s_mu);
    int id = s_nextInhibit++;
    s_inhibits[id] = (what == "prevent-app-suspension");
    ApplyUnion();
    RespondOk(res, Value(static_cast<int64_t>(id)).Serialize().c_str());
}

void inhibitStop(const ow_request_t* req, ow_response_t* res) {
    auto parsed = Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        parsed.value->AsArray().empty())
        return RespondError(res, "inhibitId requerido");
    int id = static_cast<int>(parsed.value->AsArray()[0].AsInt());

    std::lock_guard lock(s_mu);
    if (s_inhibits.erase(id) == 0) return RespondOk(res, "null");
    ApplyUnion();
    RespondOk(res, "null");
}

} // namespace pw

extern "C" OW_MODULE_EXPORT void ow_module_set_host(const ow_module_host_t* h) {
    pw::g_host = h;
}

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"monitorStart", &pw::monitorStart},
        {"inhibitStart", &pw::inhibitStart},
        {"inhibitStop", &pw::inhibitStop},
    };
    static const ow_module_desc_t d{
        "power", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}

