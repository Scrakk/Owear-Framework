// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/capturer/src/capturer_win.cpp — captura real vía GDI (BitBlt sobre
// cada monitor) y codificación PNG con GDI+ flat API a IStream en memoria
// (sin archivos temporales). Superficie idéntica a Linux: getSources() y
// captureScreen(idx) con handles SHM.
//
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


#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace cap {

using ow::json::Array;
using ow::json::Object;
using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

struct Monitor {
    RECT rc{};
    std::wstring name;
};

static BOOL CALLBACK MonEnumProc(HMONITOR hMon, HDC, LPRECT, LPARAM ud) {
    auto* v = static_cast<std::vector<Monitor>*>(reinterpret_cast<void*>(ud));
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hMon, &mi))
        v->push_back({mi.rcMonitor, mi.szDevice});
    return TRUE;
}

static std::vector<Monitor> Monitors() {
    std::vector<Monitor> v;
    EnumDisplayMonitors(nullptr, nullptr, &MonEnumProc,
                        reinterpret_cast<LPARAM>(&v));
    return v;
}

// GDI+ init una vez por proceso; COM por hilo (IStream lo exige).
static ULONG_PTR s_gdipToken = 0;
static void EnsureGdiplus() {
    static std::once_flag once;
    std::call_once(once, [] {
        GdiplusStartupInput si;
        GdiplusStartup(&s_gdipToken, &si, nullptr);
    });
    struct TlsInit {
        bool done = false;
        ~TlsInit() { /* CoUninitialize al morir el hilo */ }
    };
    thread_local bool comOk = false;
    if (!comOk) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        comOk = true;
    }
}

static const CLSID kPngClsid = {
    0x557CF406, 0x1A04, 0x11D3, {0x9A, 0x73, 0x00, 0x00, 0xF8, 0x1E, 0xF3, 0x2E}};

// Captura la región y devuelve PNG en `png` (GDI BitBlt + GDI+ encode).
static bool CaptureRegion(int x, int y, int w, int h, std::string& png,
                          std::string& err) {
    EnsureGdiplus();

    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);
    HBITMAP hbmp = CreateCompatibleBitmap(screenDc, w, h);
    HGDIOBJ old = SelectObject(memDc, hbmp);
    BitBlt(memDc, 0, 0, w, h, screenDc, x, y, SRCCOPY | CAPTUREBLT);
    SelectObject(memDc, old);

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    std::vector<uint8_t> bits(static_cast<size_t>(w) * h * 4);
    int lines = GetDIBits(memDc, hbmp, 0, static_cast<UINT>(h), bits.data(),
                          &bi, DIB_RGB_COLORS);
    DeleteObject(hbmp);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
    if (lines != h) {
        err = "GetDIBits falló";
        return false;
    }

    GpBitmap* bmp = nullptr;
    if (GdipCreateBitmapFromScan0(w, h, w * 4, PixelFormat32bppRGB,
                                  bits.data(), &bmp) != Ok || !bmp) {
        err = "GdipCreateBitmapFromScan0 falló";
        return false;
    }

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(nullptr, FALSE, &stream) != S_OK || !stream) {
        GdipDisposeImage(bmp);
        err = "CreateStreamOnHGlobal falló";
        return false;
    }

    bool ok =
        GdipSaveImageToStream(bmp, stream, &kPngClsid, nullptr) == Ok;
    GdipDisposeImage(bmp);
    if (!ok) {
        stream->Release();
        err = "codificación PNG falló";
        return false;
    }

    STATSTG stg{};
    stream->Stat(&stg, STATFLAG_NONAME);
    size_t len = static_cast<size_t>(stg.cbSize.QuadPart);
    png.resize(len);
    LARGE_INTEGER zero{};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    ULONG read = 0;
    stream->Read(png.data(), static_cast<ULONG>(len), &read);
    png.resize(read);
    stream->Release();
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
    auto mons = Monitors();
    if (mons.empty()) return RespondError(res, "sin monitores");

    Array arr;
    int i = 0;
    for (auto& m : mons) {
        int w = m.rc.right - m.rc.left;
        int h = m.rc.bottom - m.rc.top;

        Object src;
        src.emplace_back("type", Value("screen"));
        src.emplace_back("id", Value(static_cast<int64_t>(i)));
        src.emplace_back(
            "name",
            Value(std::string(m.name.begin(), m.name.end())));

        {
            Object b;
            b.emplace_back("x", Value(static_cast<int64_t>(m.rc.left)));
            b.emplace_back("y", Value(static_cast<int64_t>(m.rc.top)));
            b.emplace_back("width", Value(static_cast<int64_t>(w)));
            b.emplace_back("height", Value(static_cast<int64_t>(h)));
            src.emplace_back("bounds", Value(std::move(b)));
        }

        std::string png, err;
        if (CaptureRegion(m.rc.left, m.rc.top, w, h, png, err)) {
            const char* sid = ow_shm_put(
                reinterpret_cast<const uint8_t*>(png.data()), png.size());
            Object th;
            th.emplace_back("id", Value(std::string(sid ? sid : "")));
            th.emplace_back("size", Value(static_cast<int64_t>(png.size())));
            src.emplace_back("thumbnail", Value(std::move(th)));
        }
        arr.push_back(Value(std::move(src)));
        ++i;
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

    auto mons = Monitors();
    if (idx < 0 || static_cast<size_t>(idx) >= mons.size())
        return RespondError(res, "monitor inexistente");

    RECT rc = mons[static_cast<size_t>(idx)].rc;
    int w = rc.right - rc.left, h = rc.bottom - rc.top;

    std::string png, err;
    if (!CaptureRegion(rc.left, rc.top, w, h, png, err))
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

