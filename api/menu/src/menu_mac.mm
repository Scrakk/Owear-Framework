// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/menu/src/menu_mac.mm — menú contextual nativo (NSMenu popUp).
// Superficie idéntica a Linux: popup(items) + evento menu.click.
// setApplicationMenu es noop POR DISEÑO (igual que Linux en v1: no imponemos
// menubar; el IDE dibuja el suyo en web si prefiere).
//
#import <Cocoa/Cocoa.h>

#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

namespace menu {

using ow::json::Array;
using ow::json::Value;
using ow::json::Parse;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static const ow_module_host_t* g_host = nullptr;

@interface OwMenuTarget : NSObject
@property(nonatomic, copy) void (^block)(void);
- (void)fire:(id)sender;
@end
@implementation OwMenuTarget
- (void)fire:(id)sender {
    if (self.block) self.block();
}
@end

static void AppendItems(NSMenu* m, const Array& items, uint32_t win) {
    for (const auto& it : items) {
        if (!it.IsObject()) continue;
        std::string type =
            it.Find("type") && (*it.Find("type")).IsString()
                ? (*it.Find("type")).AsString()
                : "normal";
        if (type == "separator") {
            [m addItem:[NSMenuItem separatorItem]];
            continue;
        }
        std::string label =
            it.Find("label") && (*it.Find("label")).IsString()
                ? (*it.Find("label")).AsString()
                : "";

        const Value* sub = it.Find("submenu");
        if (sub && sub->IsArray()) {
            NSString* title = [NSString stringWithUTF8String:label.c_str()];
            NSMenuItem* holder =
                [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
            NSMenu* sm = [[NSMenu alloc] initWithTitle:title];
            AppendItems(sm, sub->AsArray(), win);
            [holder setSubmenu:sm];
            [m addItem:holder];
            continue;
        }

        NSMenuItem* item = [[NSMenuItem alloc]
            initWithTitle:[NSString stringWithUTF8String:label.c_str()]
                   action:@selector(fire:)
            keyEquivalent:@""];
        OwMenuTarget* target = [[OwMenuTarget alloc] init];
        target.block = ^{
          if (!g_host || !g_host->emit_event) return;
          std::string json =
              "{\"label\":" + ow::json::Value(label).Serialize() +
              ",\"windowId\":" + std::to_string(win) + "}";
          g_host->emit_event(g_host->ctx, win, "menu.click", json.c_str());
        };
        item.target = target;
        [m addItem:item];
    }
}

// args: [items]
void popup(const ow_request_t* req, ow_response_t* res) {
    auto parsed = Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        parsed.value->AsArray().empty())
        return RespondError(res, "items requeridos");

    NSMenu* m = [[NSMenu alloc] initWithTitle:@"owear"];
    AppendItems(m, (*parsed.value).AsArray()[0].AsArray(), req->window_id);

    // bloquea hasta que el usuario cierre/seleccione (paridad con Win/Linux)
    [m popUpMenuPositioningItem:nil
                     atLocation:[NSEvent mouseLocation]
                         inView:nil];
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

