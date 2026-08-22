// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
// api/screen/src/screen_win.cpp — EnumDisplayMonitors. VERIFICAR-EN-WINDOWS.
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace scr {
using ow::json::Array;
using ow::json::Object;
using ow::json::Value;
using ow::Module::RespondOk;

static BOOL CALLBACK MonProc(HMONITOR h, HDC, LPRECT, LPARAM data) {
    auto* arr = reinterpret_cast<Array*>(data);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoA(h, &mi);
    Object o;
    static int idx = 0;
    o.emplace_back("id", Value(static_cast<int64_t>(idx++)));
    o.emplace_back("primary", Value((mi.dwFlags & MONITORINFOF_PRIMARY) != 0));
    {
        Object b;
        b.emplace_back("x", Value((int)mi.rcMonitor.left));
        b.emplace_back("y", Value((int)mi.rcMonitor.top));
        b.emplace_back("width", Value((int)(mi.rcMonitor.right - mi.rcMonitor.left)));
        b.emplace_back("height", Value((int)(mi.rcMonitor.bottom - mi.rcMonitor.top)));
        o.emplace_back("bounds", Value(std::move(b)));
    }
    arr->emplace_back(Value(std::move(o)));
    return TRUE;
}

void getAllDisplays(const ow_request_t*, ow_response_t* res) {
    Array arr;
    EnumDisplayMonitors(nullptr, nullptr, MonProc, (LPARAM)&arr);
    RespondOk(res, Value(std::move(arr)).Serialize().c_str());
}
void getPrimaryDisplay(const ow_request_t*, ow_response_t* res) {
    POINT p{0, 0};
    HMONITOR h = MonitorFromPoint(p, MONITOR_DEFAULTTOPRIMARY);
    (void)h;
    Array arr;
    EnumDisplayMonitors(nullptr, nullptr, MonProc, (LPARAM)&arr);
    for (auto& v : arr)
        if (v.Find("primary") && v.Find("primary")->AsBool())
            return RespondOk(res, v.Serialize().c_str());
    RespondOk(res, "null");
}
void getCursorScreenPoint(const ow_request_t*, ow_response_t* res) {
    POINT p;
    GetCursorPos(&p);
    Object o;
    o.emplace_back("x", Value(p.x));
    o.emplace_back("y", Value(p.y));
    RespondOk(res, Value(std::move(o)).Serialize().c_str());
}
} // namespace scr

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"getAllDisplays", &scr::getAllDisplays},
        {"getPrimaryDisplay", &scr::getPrimaryDisplay},
        {"getCursorScreenPoint", &scr::getCursorScreenPoint},
    };
    static const ow_module_desc_t d{"screen", OW_VERSION_STRING, fns,
                                    sizeof(fns) / sizeof(fns[0])};
    return &d;
}
