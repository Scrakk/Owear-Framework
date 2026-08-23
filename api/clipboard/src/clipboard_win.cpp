// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/clipboard/src/clipboard_win.cpp — Win32 clipboard (texto; imagen CF_DIB).
// VERIFICAR-EN-WINDOWS.
//
#include "ow/Base64.h"
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow/Shm.h"
#include "ow_api.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

using namespace Gdiplus;
using namespace Gdiplus::DllExports; // MinGW declara la flat API aquí


#include <mutex>
#include <vector>

namespace clip {

using ow::json::Value;
using ow::json::Object;
using ow::Module::RespondError;
using ow::Module::RespondOk;

// GDI+ una vez por proceso; COM por hilo (necesario para IStream).
static ULONG_PTR s_gdipToken = 0;
static void EnsureGdiplus() {
    static std::once_flag once;
    std::call_once(once, [] {
        GdiplusStartupInput si;
        GdiplusStartup(&s_gdipToken, &si, nullptr);
    });
    thread_local bool comOk = false;
    if (!comOk) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        comOk = true;
    }
}

static const CLSID kPngClsid = {
    0x557CF406, 0x1A04, 0x11D3, {0x9A, 0x73, 0x00, 0x00, 0xF8, 0x1E, 0xF3, 0x2E}};

static bool SavePngToStream(GpBitmap* bmp, IStream** out) {
    if (CreateStreamOnHGlobal(nullptr, FALSE, out) != S_OK || !*out)
        return false;
    if (GdipSaveImageToStream(bmp, *out, &kPngClsid, nullptr) != Ok)
        return false;
    LARGE_INTEGER zero{};
    (*out)->Seek(zero, STREAM_SEEK_SET, nullptr);
    return true;
}


void readText(const ow_request_t*, ow_response_t* res) {
    if (!OpenClipboard(nullptr)) return RespondError(res, "OpenClipboard falló");
    HANDLE h = GetClipboardData(CF_TEXT);
    std::string out = h ? std::string(static_cast<const char*>(GlobalLock(h))) : "";
    if (h) GlobalUnlock(h);
    CloseClipboard();
    RespondOk(res, Value(out).Serialize().c_str());
}

void writeText(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "text requerido");
    std::string text = parsed.value->AsArray()[0].AsString();
    if (!OpenClipboard(nullptr)) return RespondError(res, "OpenClipboard falló");
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    memcpy(GlobalLock(h), text.c_str(), text.size() + 1);
    GlobalUnlock(h);
    SetClipboardData(CF_TEXT, h);
    CloseClipboard();
    RespondOk(res, "null");
}

void readImage(const ow_request_t*, ow_response_t* res) {
    EnsureGdiplus();
    if (!OpenClipboard(nullptr)) return RespondError(res, "OpenClipboard falló");
    HBITMAP hbmp = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));
    if (!hbmp) {
        CloseClipboard();
        return RespondError(res, "portapapeles sin imagen");
    }

    GpBitmap* bmp = nullptr;
    if (GdipCreateBitmapFromHBITMAP(hbmp, nullptr, &bmp) != Ok || !bmp) {
        CloseClipboard();
        return RespondError(res, "GdipCreateBitmapFromHBITMAP falló");
    }

    BITMAP bm{};
    GetObjectW(hbmp, sizeof(bm), &bm);

    IStream* stream = nullptr;
    bool ok = SavePngToStream(bmp, &stream);
    GdipDisposeImage(bmp);
    CloseClipboard();
    if (!ok || !stream)
        return RespondError(res, "codificación PNG falló");

    STATSTG stg{};
    stream->Stat(&stg, STATFLAG_NONAME);
    size_t len = static_cast<size_t>(stg.cbSize.QuadPart);
    std::string png(len, '\0');
    ULONG read = 0;
    stream->Read(png.data(), static_cast<ULONG>(len), &read);
    png.resize(read);
    stream->Release();

    const char* id =
        ow_shm_put(reinterpret_cast<const uint8_t*>(png.data()), png.size());
    Object o;
    {
        Object shm;
        shm.emplace_back("id", Value(std::string(id ? id : "")));
        shm.emplace_back("size", Value(static_cast<int64_t>(png.size())));
        o.emplace_back("__ow_shm", Value(std::move(shm)));
    }
    o.emplace_back("width", Value(static_cast<int64_t>(bm.bmWidth)));
    o.emplace_back("height", Value(static_cast<int64_t>(bm.bmHeight)));
    o.emplace_back("format", Value("png"));
    RespondOk(res, Value(std::move(o)).Serialize().c_str());
}

void writeImage(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    if (!parsed.value || !parsed.value->IsArray() ||
        parsed.value->AsArray().empty() ||
        !parsed.value->AsArray()[0].IsString())
        return RespondError(res, "pngB64 requerido");
    std::vector<uint8_t> png;
    if (!ow::b64::Decode(parsed.value->AsArray()[0].AsString(), png))
        return RespondError(res, "b64 inválido");
    EnsureGdiplus();

    IStream* stream = nullptr;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, png.size());
    if (!h) return RespondError(res, "GlobalAlloc falló");
    void* p = GlobalLock(h);
    memcpy(p, png.data(), png.size());
    GlobalUnlock(h);
    if (CreateStreamOnHGlobal(h, TRUE, &stream) != S_OK || !stream) {
        GlobalFree(h);
        return RespondError(res, "CreateStreamOnHGlobal falló");
    }

    GpBitmap* bmp = nullptr;
    if (GdipCreateBitmapFromStream(stream, &bmp) != Ok || !bmp) {
        stream->Release();
        return RespondError(res, "PNG inválido");
    }
    HBITMAP hbmp = nullptr;
    if (GdipCreateHBITMAPFromBitmap(bmp, &hbmp, 0xFFFFFFFF) != Ok || !hbmp) {
        GdipDisposeImage(bmp);
        stream->Release();
        return RespondError(res, "GetHBITMAP falló");
    }
    GdipDisposeImage(bmp);
    stream->Release(); // el HGLOBAL vive hasta liberar el stream (fDelete=TRUE)

    if (!OpenClipboard(nullptr)) {
        DeleteObject(hbmp);
        return RespondError(res, "OpenClipboard falló");
    }
    EmptyClipboard();
    SetClipboardData(CF_BITMAP, hbmp); // la clipboard toma posesión
    CloseClipboard();
    RespondOk(res, "null");
}
void clear(const ow_request_t*, ow_response_t* res) {
    OpenClipboard(nullptr);
    EmptyClipboard();
    CloseClipboard();
    RespondOk(res, "null");
}

} // namespace clip

extern "C" OW_MODULE_EXPORT const ow_module_desc_t* ow_module_descriptor(void) {
    static const ow_fn_entry_t fns[] = {
        {"readText", &clip::readText},   {"writeText", &clip::writeText},
        {"readImage", &clip::readImage}, {"writeImage", &clip::writeImage},
        {"clear", &clip::clear},
    };
    static const ow_module_desc_t d{
        "clipboard", OW_VERSION_STRING, fns, sizeof(fns) / sizeof(fns[0])};
    return &d;
}
