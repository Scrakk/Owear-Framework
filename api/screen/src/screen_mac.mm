// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
// api/screen/src/screen_mac.mm — NSScreen. VERIFICAR-EN-MACOS.
#import <Cocoa/Cocoa.h>

#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

namespace scr {

using ow::json::Array;
using ow::json::Object;
using ow::json::Value;
using ow::Module::RespondOk;

static NSString* Ns(const std::string& s) {
    return [NSString stringWithUTF8String:s.c_str()];
}

void getAllDisplays(const ow_request_t*, ow_response_t* res) {
    Array arr;
    int i = 0;
    for (NSScreen* s in [NSScreen screens]) {
        NSRect f = s.frame;
        Object o;
        o.emplace_back("id", Value(static_cast<int64_t>(i)));
        o.emplace_back("primary", Value(i == 0));
        Object b;
        b.emplace_back("x", Value((int)f.origin.x));
        b.emplace_back("y", Value((int)f.origin.y));
        b.emplace_back("width", Value((int)f.size.width));
        b.emplace_back("height", Value((int)f.size.height));
        o.emplace_back("bounds", Value(std::move(b)));
        o.emplace_back(
            "scaleFactor",
            Value(static_cast<int64_t>((int)s.backingScaleFactor)));
        arr.push_back(Value(std::move(o)));
        ++i;
    }
    RespondOk(res, Value(std::move(arr)).Serialize().c_str());
}
void getPrimaryDisplay(const ow_request_t*, ow_response_t* res) {
    // índice 0 = pantalla con la barra de menú en macOS
    getAllDisplays(nullptr, res);
}
void getCursorScreenPoint(const ow_request_t*, ow_response_t* res) {
    NSPoint loc = [NSEvent mouseLocation];
    Object o;
    o.emplace_back("x", Value((int)loc.x));
    o.emplace_back("y", Value((int)loc.y));
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
