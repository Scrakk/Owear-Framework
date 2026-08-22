// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/dialog/src/dialog_mac.mm — NSOpenPanel / NSSavePanel / NSAlert.
// VERIFICAR-EN-MACOS.
//
#import <Cocoa/Cocoa.h>

#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

namespace dlg {

using ow::json::Array;
using ow::json::Object;
using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static NSString* StdToNs(const std::string& s) {
    return [NSString stringWithUTF8String:s.c_str()];
}

// args: [mode("open"|"multi"|"save"|"dir"), title?, defaultPath?, filters?]
void open(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray())
        return RespondError(res, "args inválidos");
    const auto& a = parsed.value->AsArray();
    std::string mode = a.size() > 0 && a[0].IsString() ? a[0].AsString() : "open";
    std::string title = a.size() > 1 && a[1].IsString() ? a[1].AsString() : "";
    std::string defPath = a.size() > 2 && a[2].IsString() ? a[2].AsString() : "";
    bool save = mode == "save";
    bool dir = mode == "dir";
    bool multi = mode == "multi";

    if (save) {
        NSSavePanel* panel = [NSSavePanel savePanel];
        if (!title.empty()) panel.title = StdToNs(title);
        if (!defPath.empty()) panel.nameFieldStringValue = [StdToNs(defPath) lastPathComponent];
        if ([panel runModal] == NSModalResponseOK) {
            RespondOk(res, Value(std::string(panel.URL.path.UTF8String))
                               .Serialize().c_str());
        } else {
            RespondOk(res, "null");
        }
        return;
    }

    NSOpenPanel* panel = [NSOpenPanel openPanel];
    if (!title.empty()) panel.title = StdToNs(title);
    panel.canChooseDirectories = dir;
    panel.canChooseFiles = !dir;
    panel.allowsMultipleSelection = multi;
    if (!defPath.empty()) panel.directoryURL = [NSURL fileURLWithPath:StdToNs(defPath)];
    if (a.size() > 3 && a[3].IsArray()) {
        NSMutableArray<NSString*>* types = [NSMutableArray array];
        for (const auto& f : a[3].AsArray()) {
            const Value* exts = f.Find("extensions");
            if (!exts || !exts->IsArray()) continue;
            for (const auto& e : exts->AsArray())
                if (e.IsString()) [types addObject:StdToNs(e.AsString())];
        }
        panel.allowedFileTypes = types;
    }

    if ([panel runModal] == NSModalResponseOK) {
        Array arr;
        for (NSURL* u in panel.URLs)
            arr.emplace_back(Value(std::string(u.path.UTF8String ?: "")));
        if (multi)
            RespondOk(res, Value(std::move(arr)).Serialize().c_str());
        else if (!arr.empty())
            RespondOk(res, arr[0].Serialize().c_str());
        else
            RespondOk(res, "null");
    } else {
        RespondOk(res, "null");
    }
}

// args: [type, title, message, buttons?]
void messageBox(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().size() < 3)
        return RespondError(res, "se esperan [type, title, message]");
    const auto& a = parsed.value->AsArray();

    NSAlert* alert = [[NSAlert alloc] init];
    std::string type = a[0].IsString() ? a[0].AsString() : "info";
    if (type == "warning") alert.alertStyle = NSAlertStyleWarning;
    else if (type == "error") alert.alertStyle = NSAlertStyleCritical;
    else alert.alertStyle = NSAlertStyleInformational;

    if (a.size() > 1 && a[1].IsString()) alert.messageText = StdToNs(a[1].AsString());
    if (a.size() > 2 && a[2].IsString()) alert.informativeText = StdToNs(a[2].AsString());

    if (a.size() > 3 && a[3].IsArray()) {
        for (const auto& b : a[3].AsArray())
            if (b.IsString())
                [alert addButtonWithTitle:StdToNs(b.AsString())];
    } else {
        [alert addButtonWithTitle:@"OK"];
    }

    NSModalResponse r = [alert runModal];
    // primer botón = 1000
    RespondOk(res, Value(static_cast<int64_t>(r - 1000)).Serialize().c_str());
}

} // namespace dlg

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"open", &dlg::open},
        {"messageBox", &dlg::messageBox},
    };
    static const ow_module_desc_t d{
        "dialog", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}
