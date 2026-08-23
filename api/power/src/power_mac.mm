// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/power/src/power_mac.mm — monitor de suspensión vía NSWorkspace y
// bloqueos de energía vía IOPMAssertion (IOKit).
//
#import <Cocoa/Cocoa.h>
#import <IOKit/pwr_mgt/IOPMLib.h>

#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#include <map>

namespace pw {

using ow::json::Value;
using ow::json::Parse;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static const ow_module_host_t* g_host = nullptr;
static id g_sleepToken = nil;
static id g_wakeToken = nil;
static std::map<int, IOPMAssertionID> g_inhibits;
static int s_nextInhibit = 1;

void monitorStart(const ow_request_t*, ow_response_t* res) {
    if (g_sleepToken) return RespondOk(res, "\"ya-activo\"");

    NSOperationQueue* q = [NSOperationQueue mainQueue];
    g_sleepToken = [[[NSWorkspace sharedWorkspace] notificationCenter]
        addObserverForName:NSWorkspaceWillSleepNotification
                    object:nil
                     queue:q
                usingBlock:^(NSNotification*) {
                  if (g_host && g_host->emit_event)
                      g_host->emit_event(g_host->ctx, 0, "power.suspend",
                                         "null");
                }];
    g_wakeToken = [[[NSWorkspace sharedWorkspace] notificationCenter]
        addObserverForName:NSWorkspaceDidWakeNotification
                    object:nil
                     queue:q
                usingBlock:^(NSNotification*) {
                  if (g_host && g_host->emit_event)
                      g_host->emit_event(g_host->ctx, 0, "power.resume",
                                         "null");
                }];
    RespondOk(res, "null");
}

// args: [what?("prevent-display-sleep"|"prevent-app-suspension")]
void inhibitStart(const ow_request_t* req, ow_response_t* res) {
    auto parsed = Parse(std::string_view(req->json, req->json_len));
    std::string what = "prevent-display-sleep";
    if (parsed.value && parsed.value->IsArray() &&
        !parsed.value->AsArray().empty() &&
        parsed.value->AsArray()[0].IsString())
        what = parsed.value->AsArray()[0].AsString();

    CFStringRef type =
        (what == "prevent-app-suspension")
            ? kIOPMAssertionTypePreventSystemSleep
            : kIOPMAssertionTypePreventUserIdleDisplaySleep;
    IOPMAssertionID aid = 0;
    IOReturn rc =
        IOPMAssertionCreateWithName(type, kIOPMAssertionLevelOn,
                                    CFSTR("owear"), &aid);
    if (rc != kIOReturnSuccess)
        return RespondError(res, "IOPMAssertionCreateWithName falló");

    int id = s_nextInhibit++;
    g_inhibits[id] = aid;
    RespondOk(res, Value(static_cast<int64_t>(id)).Serialize().c_str());
}

void inhibitStop(const ow_request_t* req, ow_response_t* res) {
    auto parsed = Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        parsed.value->AsArray().empty())
        return RespondError(res, "inhibitId requerido");
    int id = static_cast<int>(parsed.value->AsArray()[0].AsInt());

    auto it = g_inhibits.find(id);
    if (it == g_inhibits.end()) return RespondOk(res, "null");
    IOPMAssertionRelease(it->second);
    g_inhibits.erase(it);
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

