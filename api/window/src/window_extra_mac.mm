// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/window/src/window_extra_mac.mm — builtin "window-extras" (macOS).
// Funciones avanzadas de ventana acopladas a WKWebView.
//
#include "../../../src/Core/BuiltinUtil.hpp"
#include "ow/Base64.h"
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow/Shm.h"
#include "ow_api.h"

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

#include <string>

namespace winxmac {

using ow::json::Object;
using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static inline uint32_t WinId(const Value& args) {
    if (!args.IsArray() || args.AsArray().empty() || !args.AsArray()[0].IsNumber())
        return 0;
    return static_cast<uint32_t>(args.AsArray()[0].AsInt());
}

#define NEED_WIN(id)                                                          \
    WKWebView* view = static_cast<WKWebView*>(ow::builtin::WebviewById(id));  \
    if (!view || ![view isKindOfClass:[WKWebView class]])                     \
        return RespondError(res, "ventana no encontrada");

static bool GetBoolArg(const Value& args, size_t idx, bool dflt) {
    if (args.IsArray() && args.AsArray().size() > idx && args.AsArray()[idx].IsBool())
        return args.AsArray()[idx].AsBool();
    return dflt;
}

static double GetDoubleArg(const Value& args, size_t idx, double dflt) {
    if (args.IsArray() && args.AsArray().size() > idx && args.AsArray()[idx].IsNumber())
        return args.AsArray()[idx].AsDouble();
    return dflt;
}

void openDevTools(const ow_request_t* req, ow_response_t* res) {
    // WKWebView no expone inspector programáticamente público (solo vía menú
    // contextual con developer extras activado). Error claro, no silencio.
    RespondError(res, "openDevTools no soportado en WKWebView v1");
}

void capturePage(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)

    __block bool done = false;
    __block NSData* png = nil;
    __block NSString* errText = nil;

    WKSnapshotConfiguration* cfg = [WKSnapshotConfiguration new];
    [view takeSnapshotWithConfiguration:cfg
                      completionHandler:^(NSImage* img, NSError* err) {
        if (err) { errText = err.localizedDescription; }
        else if (img) {
            CGImageRef cg = img.CGImage;
            NSBitmapImageRep* rep =
                [[NSBitmapImageRep alloc] initWithCGImage:cg];
            png = [rep representationUsingType:NSBitmapImageFileTypePNG
                                    properties:@{}];
        }
        done = true;
    }];

    // pump del runloop principal hasta que el callback dispare
    NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:5.0];
    while (!done && [deadline timeIntervalSinceNow] > 0) {
        [[NSRunLoop mainRunLoop] runMode:NSDefaultRunLoopMode
                              beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    }
    if (!done || !png)
        return RespondError(res,
            std::string("snapshot falló: ") +
            (errText ? errText.UTF8String : "timeout"));

    const char* sid = ow_shm_put(
        reinterpret_cast<const uint8_t*>(png.bytes), png.length);
    if (!sid || !*sid) return RespondError(res, "SHM llena");

    Object shm;
    shm.emplace_back("id", Value(std::string(sid)));
    shm.emplace_back("size", Value(static_cast<int64_t>(png.length)));
    Object o;
    o.emplace_back("__ow_shm", Value(std::move(shm)));
    o.emplace_back("format", Value("png"));
    RespondOk(res, Value(std::move(o)).Serialize().c_str());
}

void setAlwaysOnTop(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    NSWindow* win = view.window;
    if (!win) return RespondError(res, "ventana no encontrada");
    [win setLevel:GetBoolArg(args, 1, false) ? NSFloatingWindowLevel
                                             : NSNormalWindowLevel];
    RespondOk(res, "null");
}
void isAlwaysOnTop(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    NSWindow* win = view.window;
    RespondOk(res, (win && win.level > NSNormalWindowLevel) ? "true" : "false");
}

void setOpacity(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    NSWindow* win = view.window;
    if (!win) return RespondError(res, "ventana no encontrada");
    double op = GetDoubleArg(args, 1, 1.0);
    win.alphaValue = op < 0 ? 0 : op > 1 ? 1 : op;
    RespondOk(res, "null");
}

void flashFrame(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    NSWindow* win = view.window;
    if (!win) return RespondError(res, "ventana no encontrada");
    if (GetBoolArg(args, 1, false)) {
        [win orderFrontRegardless]; // atención sin robar focus agresivo
    }
    RespondOk(res, "null");
}

void setUserAgent(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    if (!args.IsArray() || args.AsArray().size() < 2 || !args.AsArray()[1].IsString())
        return RespondError(res, "userAgent requerido");
    view.customUserAgent =
        [NSString stringWithUTF8String:args.AsArray()[1].AsString().c_str()];
    RespondOk(res, "null");
}

void zoom(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    double factor = GetDoubleArg(args, 1, 1.0);
    // WKWebView pagina en pasos; para factor arbitrario usamos magnification
    view.pageZoom = factor > 0 ? factor : 1.0;
    RespondOk(res, "null");
}

void reload(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    [view reload];
    RespondOk(res, "null");
}
void stop(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    [view stopLoading];
    RespondOk(res, "null");
}
void goBack(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    [view goBack];
    RespondOk(res, "null");
}
void goForward(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    [view goForward];
    RespondOk(res, "null");
}
void canGoBack(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    RespondOk(res, view.canGoBack ? "true" : "false");
}
void canGoForward(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    RespondOk(res, view.canGoForward ? "true" : "false");
}
void getURL(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    NSURL* u = view.URL;
    RespondOk(res, Value(u ? std::string(u.absoluteString.UTF8String) : "")
                       .Serialize()
                       .c_str());
}
void getTitle(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    Value args = parsed.value ? std::move(*parsed.value) : Value(nullptr);
    uint32_t id = WinId(args);
    NEED_WIN(id)
    RespondOk(res, Value(view.title ? std::string(view.title.UTF8String) : "")
                       .Serialize()
                       .c_str());
}

} // namespace winxmac

namespace ow::internal {
const ow_module_desc_t* WindowExtrasDescriptorMac(void) {
    static const ow_fn_entry_t fns[] = {
        {"openDevTools", &winxmac::openDevTools},
        {"capturePage", &winxmac::capturePage},
        {"setAlwaysOnTop", &winxmac::setAlwaysOnTop},
        {"isAlwaysOnTop", &winxmac::isAlwaysOnTop},
        {"setOpacity", &winxmac::setOpacity},
        {"flashFrame", &winxmac::flashFrame},
        {"setUserAgent", &winxmac::setUserAgent},
        {"zoom", &winxmac::zoom},
        {"reload", &winxmac::reload},
        {"stop", &winxmac::stop},
        {"goBack", &winxmac::goBack},
        {"goForward", &winxmac::goForward},
        {"canGoBack", &winxmac::canGoBack},
        {"canGoForward", &winxmac::canGoForward},
        {"getURL", &winxmac::getURL},
        {"getTitle", &winxmac::getTitle},
    };
    static const ow_module_desc_t d{
        "window", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}
} // namespace ow::internal
