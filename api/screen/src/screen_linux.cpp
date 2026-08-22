// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/screen/src/screen_linux.cpp — monitores via GDK.
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

#include <gdk/gdk.h>

namespace scr {

using ow::json::Array;
using ow::json::Object;
using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static Value DisplayToJson(GdkDisplay* disp, int idx) {
    GdkMonitor* m = gdk_display_get_monitor(disp, idx);
    GdkRectangle geo{}, work{};
    gdk_monitor_get_geometry(m, &geo);
    gdk_monitor_get_workarea(m, &work);
    Object o;
    o.emplace_back("id", Value(static_cast<int64_t>(idx)));
    o.emplace_back("primary", Value(gdk_display_get_primary_monitor(disp) == m));
    {
        Object b;
        b.emplace_back("x", Value(geo.x));   b.emplace_back("y", Value(geo.y));
        b.emplace_back("width", Value(geo.width));
        b.emplace_back("height", Value(geo.height));
        o.emplace_back("bounds", Value(std::move(b)));
    }
    {
        Object w;
        w.emplace_back("x", Value(work.x));  w.emplace_back("y", Value(work.y));
        w.emplace_back("width", Value(work.width));
        w.emplace_back("height", Value(work.height));
        o.emplace_back("workArea", Value(std::move(w)));
    }
    o.emplace_back("scaleFactor", Value(static_cast<int64_t>(
        (int)gdk_monitor_get_scale_factor(m))));
    return Value(std::move(o));
}

void getAllDisplays(const ow_request_t*, ow_response_t* res) {
    GdkDisplay* d = gdk_display_get_default();
    Array arr;
    int n = gdk_display_get_n_monitors(d);
    for (int i = 0; i < n; ++i) arr.push_back(DisplayToJson(d, i));
    RespondOk(res, Value(std::move(arr)).Serialize().c_str());
}

void getPrimaryDisplay(const ow_request_t*, ow_response_t* res) {
    GdkDisplay* d = gdk_display_get_default();
    int primary = 0;
    for (int i = 0; i < gdk_display_get_n_monitors(d); ++i)
        if (gdk_display_get_monitor(d, i) == gdk_display_get_primary_monitor(d))
            primary = i;
    RespondOk(res, DisplayToJson(d, primary).Serialize().c_str());
}

void getCursorScreenPoint(const ow_request_t*, ow_response_t* res) {
    GdkSeat* seat = gdk_display_get_default_seat(gdk_display_get_default());
    GdkDevice* dev = gdk_seat_get_pointer(seat);
    int x = 0, y = 0;
    gdk_device_get_position(dev, nullptr, &x, &y);
    Object o;
    o.emplace_back("x", Value(x));
    o.emplace_back("y", Value(y));
    RespondOk(res, Value(std::move(o)).Serialize().c_str());
}

} // namespace scr

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"getAllDisplays", &scr::getAllDisplays},
        {"getPrimaryDisplay", &scr::getPrimaryDisplay},
        {"getCursorScreenPoint", &scr::getCursorScreenPoint},
    };
    static const ow_module_desc_t d{
        "screen", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}
