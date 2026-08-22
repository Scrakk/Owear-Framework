// VERIFICAR-EN-MACOS: NSPasteboard.
// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/clipboard/src/clipboard_mac.mm — NSPasteboard (texto + imagen PNG).
//
#include "ow/Base64.h"
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow/Shm.h"
#include "ow_api.h"

#import <Cocoa/Cocoa.h>
#include <vector>

namespace clip {

using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

static NSPasteboard* Sys() { return [NSPasteboard generalPasteboard]; }

void readText(const ow_request_t*, ow_response_t* res) {
    NSString* t = [Sys() stringForType:NSPasteboardTypeString];
    RespondOk(res, Value(t ? std::string(t.UTF8String) : std::string()).Serialize().c_str());
}

void writeText(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty() ||
        !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "text requerido");
    [Sys() clearContents];
    [Sys() setString:[NSString stringWithUTF8String:parsed.value->AsArray()[0].AsString().c_str()]
             forType:NSPasteboardTypeString];
    RespondOk(res, "null");
}

void readImage(const ow_request_t*, ow_response_t* res) {
    NSData* png = [Sys() dataForType:NSPasteboardTypePNG];
    NSImage* nsImg = nil;
    if (png) {
        nsImg = [[NSImage alloc] initWithData:png];
    } else {
        // Algunas apps sólo publican TIFF; lo convertimos a PNG.
        NSData* tiff = [Sys() dataForType:NSPasteboardTypeTIFF];
        if (!tiff) return RespondOk(res, "null");
        nsImg = [[NSImage alloc] initWithData:tiff];
        NSBitmapImageRep* rep =
            [NSBitmapImageRep imageRepWithData:tiff];
        if (!rep) return RespondOk(res, "null");
        png = [rep representationUsingType:NSBitmapImageFileTypePNG
                                 properties:@{}];
    }
    if (!png || !nsImg) return RespondOk(res, "null");

    NSSize size = nsImg.size;
    const uint8_t* bytes = static_cast<const uint8_t*>(png.bytes);
    size_t len = png.length;
    const char* id = ow_shm_put(bytes, len);
    std::string json = "{\"__ow_shm\":{\"id\":\"" + std::string(id ?: "") +
                       "\",\"size\":" + std::to_string(len) +
                       ",\"width\":" + std::to_string((int)size.width) +
                       ",\"height\":" + std::to_string((int)size.height) +
                       ",\"format\":\"png\"}}";
    RespondOk(res, json.c_str());
}

void writeImage(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() || parsed.value->AsArray().empty() ||
        !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "pngB64 requerido");
    std::vector<uint8_t> png;
    if (!ow::b64::Decode(parsed.value->AsArray()[0].AsString(), png))
        return RespondError(res, "b64 inválido");

    NSData* data = [NSData dataWithBytes:png.data() length:png.size()];
    NSImage* img = [[NSImage alloc] initWithData:data];
    if (!img) return RespondError(res, "PNG inválido");

    [Sys() clearContents];
    [Sys() setData:data forType:NSPasteboardTypePNG];
    RespondOk(res, "null");
}

void clear(const ow_request_t*, ow_response_t* res) {
    [Sys() clearContents];
    RespondOk(res, "null");
}

} // namespace clip

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"readText", &clip::readText},
        {"writeText", &clip::writeText},
        {"readImage", &clip::readImage},
        {"writeImage", &clip::writeImage},
        {"clear", &clip::clear},
    };
    static const ow_module_desc_t d{
        "clipboard", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}
