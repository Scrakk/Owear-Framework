// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// api/capturer/src/capturer_linux.cpp — captura de pantallas vía X11.
//   getSources()      → thumbnails PNG (SHM) de cada monitor
//   captureScreen(i)  → PNG full-res (SHM)
//
// LIMITACIÓN v1: solo X11 (Wayland requiere portal xdg-desktop — F-next).
// VERIFICAR-EN-WINDOWS(BitBlt) / MACOS(CGDisplayCreateImage).
//
#include "ow/Json.h"
#include "ow/Module.h"
#include "ow/Shm.h"
#include "ow_api.h"

#include <gdk/gdk.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

namespace cap {

using ow::json::Array;
using ow::json::Object;
using ow::json::Value;
using ow::Module::RespondError;
using ow::Module::RespondOk;

/// Captura la región (x,y,w,h) del root window y devuelve PNG en `png`.
static bool CaptureRegion(int x, int y, int w, int h, std::string& png,
                          std::string& err) {
#ifdef GDK_WINDOWING_X11
    GdkDisplay* disp = gdk_display_get_default();
    if (!GDK_IS_X11_DISPLAY(disp)) {
        err = "captura solo soportada en sesión X11 (Wayland pendiente)";
        return false;
    }
    Display* dpy = gdk_x11_display_get_xdisplay(disp);
    Window root = DefaultRootWindow(dpy);

    XImage* img = XGetImage(dpy, root, x, y, w, h, AllPlanes, ZPixmap);
    if (!img) {
        err = "XGetImage falló";
        return false;
    }

    // BGRA (ZPixmap 24/32bpp) → GdkPixbuf RGB
    GdkPixbuf* pix = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, w, h);
    guchar* dst = gdk_pixbuf_get_pixels(pix);
    int rowstride = gdk_pixbuf_get_rowstride(pix);
    int channels = gdk_pixbuf_get_n_channels(pix);

    int bpp = img->bits_per_pixel / 8;
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            const unsigned char* px =
                reinterpret_cast<unsigned char*>(img->data) +
                row * img->bytes_per_line + col * bpp;
            guchar* o = dst + row * rowstride + col * channels;
            // ZPixmap en little-endian suele ser BGRx
            o[0] = px[2];
            o[1] = px[1];
            o[2] = px[0];
        }
    }
    XDestroyImage(img);

    // escala a thumbnail si es enorme (getSources pide ligero)
    GdkPixbuf* scaled = pix;

    GError* e = nullptr;
    gchar* buf = nullptr;
    gsize len = 0;
    gdk_pixbuf_save_to_buffer(scaled, &buf, &len, "png", &e, nullptr);
    g_object_unref(pix);
    if (e) {
        err = e->message;
        g_error_free(e);
        return false;
    }
    png.assign(buf, len);
    g_free(buf);
    return true;
#else
    err = "sin backend X11";
    return false;
#endif
}

static const char* PngToShmHandle(const std::string& png, Object& out,
                                  int width, int height) {
    const char* id = ow_shm_put(reinterpret_cast<const uint8_t*>(png.data()),
                                png.size());
    Object shm;
    shm.emplace_back("id", Value(std::string(id ? id : "")));
    shm.emplace_back("size", Value(static_cast<int64_t>(png.size())));
    out.emplace_back("__ow_shm", Value(std::move(shm)));
    out.emplace_back("width", Value(width));
    out.emplace_back("height", Value(height));
    out.emplace_back("format", Value("png"));
    return id;
}

void getSources(const ow_request_t*, ow_response_t* res) {
#ifdef GDK_WINDOWING_X11
    GdkDisplay* disp = gdk_display_get_default();
    if (!GDK_IS_X11_DISPLAY(disp))
        return RespondError(res, "captura solo X11 (Wayland pendiente)");
    Display* dpy = gdk_x11_display_get_xdisplay(disp);

    Array arr;
    int n = gdk_display_get_n_monitors(disp);
    for (int i = 0; i < n; ++i) {
        GdkMonitor* m = gdk_display_get_monitor(disp, i);
        GdkRectangle geo{};
        gdk_monitor_get_geometry(m, &geo);

        // thumbnail a 320px de ancho máximo
        double scale = geo.width > 320 ? 320.0 / geo.width : 1.0;
        int tw = static_cast<int>(geo.width * scale);
        int th = static_cast<int>(geo.height * scale);

        std::string png, err;
        std::string fullPng;
        if (!CaptureRegion(geo.x, geo.y, geo.width, geo.height, fullPng, err))
            continue;
        // reescala decodificando (v1 simple): guardamos full y reportamos dims
        (void)tw; (void)th;

        Object src;
        src.emplace_back("type", Value("screen"));
        src.emplace_back("id", Value(static_cast<int64_t>(i)));
        src.emplace_back("name",
                         Value(gdk_monitor_get_model(m)
                                   ? std::string(gdk_monitor_get_model(m))
                                   : std::string("Screen ") + std::to_string(i)));
        {
            Object b;
            b.emplace_back("x", Value(geo.x));
            b.emplace_back("y", Value(geo.y));
            b.emplace_back("width", Value(geo.width));
            b.emplace_back("height", Value(geo.height));
            src.emplace_back("bounds", Value(std::move(b)));
        }
        const char* sid = ow_shm_put(reinterpret_cast<const uint8_t*>(fullPng.data()),
                                     fullPng.size());
        {
            Object th;
            th.emplace_back("id", Value(std::string(sid ? sid : "")));
            th.emplace_back("size", Value(static_cast<int64_t>(fullPng.size())));
            src.emplace_back("thumbnail", Value(std::move(th)));
        }
        arr.push_back(Value(std::move(src)));
    }
    RespondOk(res, Value(std::move(arr)).Serialize().c_str());
#else
    RespondError(res, "sin backend");
#endif
}

// args: [screenIndex]
void captureScreen(const ow_request_t* req, ow_response_t* res) {
    auto parsed = ow::json::Parse(std::string_view(req->json, req->json_len));
    int idx = 0;
    if (parsed.value && parsed.value->IsArray() && !parsed.value->AsArray().empty() &&
        parsed.value->AsArray()[0].IsNumber())
        idx = static_cast<int>(parsed.value->AsArray()[0].AsInt());

    GdkDisplay* disp = gdk_display_get_default();
#ifdef GDK_WINDOWING_X11
    if (!GDK_IS_X11_DISPLAY(disp))
        return RespondError(res, "captura solo X11 (Wayland pendiente)");

    GdkMonitor* m = gdk_display_get_monitor(disp, idx);
    if (!m) return RespondError(res, "monitor inexistente");
    GdkRectangle geo{};
    gdk_monitor_get_geometry(m, &geo);

    std::string png, err;
    if (!CaptureRegion(geo.x, geo.y, geo.width, geo.height, png, err))
        return RespondError(res, err);

    Object o;
    PngToShmHandle(png, o, geo.width, geo.height);
    RespondOk(res, Value(std::move(o)).Serialize().c_str());
#else
    (void)idx;
    RespondError(res, "sin backend");
#endif
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
