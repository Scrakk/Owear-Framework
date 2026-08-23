// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/capturer/src/capturer_mac.mm — captura real vía CGDisplayCreateImage
// y codificación PNG con ImageIO en memoria (NSMutableData, sin archivos).
// Superficie idéntica a Linux: getSources() + captureScreen(idx) con SHM.
// VERIFICAR-EN-MACOS.
//
#import <Cocoa/Cocoa.h>
#import <ImageIO/ImageIO.h>

#include "ow/Json.h"
#include "ow/Module.h"
#include "ow/Shm.h"
#include "ow_api.h"

namespace cap {

using ow::json::Array;
using ow::json::Object;
using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static bool CaptureDisplay(CGDirectDisplayID disp, std::string& png,
                           int& w, int& h, std::string& err) {
    CGImageRef img = CGDisplayCreateImage(disp);
    if (!img) {
        err = "CGDisplayCreateImage falló";
        return false;
    }

    NSMutableData* md = [NSMutableData data];
    CGImageDestinationRef dst = CGImageDestinationCreateWithData(
        static_cast<CFMutableDataRef>(md), CFSTR("public.png"), 1, nullptr);
    if (!dst) {
        CGImageRelease(img);
        err = "CGImageDestinationCreateWithData falló";
        return false;
    }
    CGImageDestinationAddImage(dst, img, nullptr);
    bool ok = CGImageDestinationFinalize(dst) != false;
    CFRelease(dst);
    w = static_cast<int>(CGImageGetWidth(img));
    h = static_cast<int>(CGImageGetHeight(img));
    CGImageRelease(img);
    if (!ok) {
        err = "codificación PNG falló";
        return false;
    }
    png.assign(static_cast<const char*>(md.bytes), md.length);
    return true;
}

static void PngToShm(Object& out, const std::string& png, int w, int h) {
    const char* id =
        ow_shm_put(reinterpret_cast<const uint8_t*>(png.data()), png.size());
    Object shm;
    shm.emplace_back("id", Value(std::string(id ? id : "")));
    shm.emplace_back("size", Value(static_cast<int64_t>(png.size())));
    out.emplace_back("__ow_shm", Value(std::move(shm)));
    out.emplace_back("width", Value(w));
    out.emplace_back("height", Value(h));
    out.emplace_back("format", Value("png"));
}

void getSources(const ow_request_t*, ow_response_t* res) {
    NSArray<NSScreen*>* screens = [NSScreen screens];
    if (screens.count == 0) return RespondError(res, "sin pantallas");

    Array arr;
    for (NSUInteger i = 0; i < screens.count; ++i) {
        NSScreen* s = screens[i];
        auto* dispNum = static_cast<NSNumber*>(
            s.deviceDescription[@"NSScreenNumber"]);
        auto disp = static_cast<CGDirectDisplayID>(dispNum.unsignedIntValue);
        CGRect f = s.frame;

        Object src;
        src.emplace_back("type", Value("screen"));
        src.emplace_back("id", Value(static_cast<int64_t>(i)));
        src.emplace_back(
            "name",
            Value(std::string("Screen ") + std::to_string(i)));
        {
            Object b;
            b.emplace_back("x", Value(static_cast<int>(f.origin.x)));
            b.emplace_back("y", Value(static_cast<int>(f.origin.y)));
            b.emplace_back("width", Value(static_cast<int>(f.size.width)));
            b.emplace_back("height", Value(static_cast<int>(f.size.height)));
            src.emplace_back("bounds", Value(std::move(b)));
        }

        std::string png, err;
        int w = 0, h = 0;
        if (CaptureDisplay(disp, png, w, h, err)) {
            const char* sid = ow_shm_put(
                reinterpret_cast<const uint8_t*>(png.data()), png.size());
            Object th;
            th.emplace_back("id", Value(std::string(sid ? sid : "")));
            th.emplace_back("size", Value(static_cast<int64_t>(png.size())));
            src.emplace_back("thumbnail", Value(std::move(th)));
        }
        arr.push_back(Value(std::move(src)));
    }
    RespondOk(res, Value(std::move(arr)).Serialize().c_str());
}

// args: [screenIndex]
void captureScreen(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    int idx = 0;
    if (parsed.value && parsed.value->IsArray() &&
        !parsed.value->AsArray().empty() &&
        parsed.value->AsArray()[0].IsNumber())
        idx = static_cast<int>(parsed.value->AsArray()[0].AsInt());

    NSArray<NSScreen*>* screens = [NSScreen screens];
    if (idx < 0 || static_cast<NSUInteger>(idx) >= screens.count)
        return RespondError(res, "monitor inexistente");

    auto* dispNum = static_cast<NSNumber*>(
        screens[idx].deviceDescription[@"NSScreenNumber"]);
    auto disp = static_cast<CGDirectDisplayID>(dispNum.unsignedIntValue);

    std::string png, err;
    int w = 0, h = 0;
    if (!CaptureDisplay(disp, png, w, h, err))
        return RespondError(res, err);

    Object o;
    PngToShm(o, png, w, h);
    RespondOk(res, Value(std::move(o)).Serialize().c_str());
}

} // namespace cap

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"getSources", &cap::getSources},
        {"captureScreen", &cap::captureScreen},
    };
    static const ow_module_desc_t d{
        "capturer", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}

