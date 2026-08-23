// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/notification/src/notification_mac.mm — UNUserNotificationCenter cuando
// el proceso tiene bundle id (app empaquetada); si no, entrega REAL vía
// /usr/bin/osascript → Notification Center (funciona sin bundle).
// VERIFICAR-EN-MACOS.
//
#import <Cocoa/Cocoa.h>
#import <UserNotifications/UserNotifications.h>

#include "ow/Json.h"
#include "ow/Module.h"
#include "ow_api.h"

namespace notif {

using ow::json::Value;
using ow::json::Parse;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static int64_t s_nextId = 1;

static void ShowViaOsascript(const std::string& title, const std::string& body) {
    auto esc = [](std::string s) {
        std::string out;
        for (char c : s) {
            if (c == '"' || c == '\\') out += '\\';
            out += c;
        }
        return out;
    };
    NSString* script = [NSString
        stringWithFormat:@"display notification \"%s\" with title \"%s\"",
                         esc(body).c_str(), esc(title).c_str()];
    NSTask* task = [[NSTask alloc] init];
    task.launchPath = @"/usr/bin/osascript";
    task.arguments = @[ @"-e", script ];
    [task launch]; // entrega asíncrona real por Notification Center
}

// args: [title, body?, appName?]
void show(const ow_request_t* req, ow_response_t* res) {
    auto parsed = Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        parsed.value->AsArray().empty() || !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "title requerido");
    std::string title = parsed.value->AsArray()[0].AsString();
    std::string body = parsed.value->AsArray().size() > 1 &&
                               parsed.value->AsArray()[1].IsString()
                           ? parsed.value->AsArray()[1].AsString()
                           : "";

    bool delivered = false;
    if ([NSBundle mainBundle].bundleIdentifier != nil) {
        @try {
            UNUserNotificationCenter* c =
                [UNUserNotificationCenter currentNotificationCenter];
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            __block BOOL granted = NO;
            [c requestAuthorizationWithOptions:
                    (UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                             completionHandler:^(BOOL g, NSError*) {
                               granted = g;
                               dispatch_semaphore_signal(sem);
                             }];
            dispatch_semaphore_wait(
                sem, dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC));
            if (granted) {
                UNMutableNotificationContent* content =
                    [[UNMutableNotificationContent alloc] init];
                content.title = [NSString stringWithUTF8String:title.c_str()];
                content.body = [NSString stringWithUTF8String:body.c_str()];
                UNNotificationRequest* rq = [UNNotificationRequest
                    requestWithIdentifier:[NSString stringWithFormat:
                                                       @"owear-%lld", s_nextId]
                                   content:content
                                   trigger:nil];
                [c addNotificationRequest:rq withCompletionHandler:^{}];
                delivered = true;
            }
        } @catch (NSException*) {
            delivered = false;
        }
    }
    if (!delivered) ShowViaOsascript(title, body);

    RespondOk(res, Value(s_nextId++).Serialize().c_str());
}

} // namespace notif

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {{"show", &notif::show}};
    static const ow_module_desc_t d{
        "notification", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}

