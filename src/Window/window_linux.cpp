// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/window_linux.cpp — implementación GTK3 (WebKitGTK embebido).
//
// Titlebar en Linux:
//  - Default: decoraciones del gestor de ventanas.
//  - Hidden/Custom: gtk_window_set_decorated(false) + drag regions CSS
//    ([data-ow-drag]). Los botones overlay nativos son exclusivos de
//    Windows/macOS; en Linux la app dibuja los suyos.
//
#include "Window_p.hpp"
#include "../Core/Log.hpp"
#include "ow/detail/minjson.hpp"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <map>

namespace ow {

namespace {
std::atomic<uint64_t> g_nextWindowToken{1};
} // namespace

struct Window::Impl::PlatformData {
    GtkWidget* window = nullptr;
    bool fullscreen = false;
    uint64_t token = 0;
};

Window::~Window() = default;
Window::Impl::~Impl() {
    alive->store(false); // callbacks diferidos (outbox/timer) dejan de tocar this
    delete pdata;
}

// ── plataforma: creación ─────────────────────────────────────────────────────
bool Window::Impl::PCreate() {
    pdata = new PlatformData();
    pdata->token = g_nextWindowToken.fetch_add(1);

    GtkWidget* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    pdata->window = win;
    gtk_window_set_title(GTK_WINDOW(win), opts.title.c_str());
    gtk_window_set_default_size(GTK_WINDOW(win), opts.width, opts.height);

    if (!opts.resizable) gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
    if (opts.minWidth > 0 || opts.minHeight > 0)
        gtk_window_set_geometry_hints(GTK_WINDOW(win), nullptr, nullptr, GdkWindowHints(0));

    PApplyTitleBar();

    g_signal_connect(win, "delete-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer user_data) -> gboolean {
                         // F3.4: el flujo central decide (veto nativo → JS → timeout)
                         static_cast<Window::Impl*>(user_data)->BeginCloseFlow();
                         return TRUE; // siempre bloqueamos; Destroy() lo cierra
                     }),
                     this);

    g_signal_connect(win, "destroy",
                     G_CALLBACK(+[](GtkWidget*, gpointer user_data) {
                         auto* impl = static_cast<Window::Impl*>(user_data);
                         Window::Impl::EmitPlatformEvent(impl, "closed");
                     }),
                     this);

    g_signal_connect(win, "configure-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventConfigure* e, gpointer user_data) -> gboolean {
                         auto* impl = static_cast<Window::Impl*>(user_data);
                         json::Object o;
                         o.emplace_back("x", json::Value(e->x));
                         o.emplace_back("y", json::Value(e->y));
                         o.emplace_back("width", json::Value(e->width));
                         o.emplace_back("height", json::Value(e->height));
                         Window::Impl::EmitPlatformEvent(impl, "resize",
                                         json::Value(std::move(o)).Serialize());
                         return FALSE;
                     }),
                     this);

    g_signal_connect(win, "window-state-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventWindowState* e, gpointer user_data) -> gboolean {
                         auto* impl = static_cast<Window::Impl*>(user_data);
                         bool maximized = e->new_window_state & GDK_WINDOW_STATE_MAXIMIZED;
                         bool fullscreen = e->new_window_state & GDK_WINDOW_STATE_FULLSCREEN;
                         impl->pdata->fullscreen = fullscreen;
                         Window::Impl::EmitPlatformEvent(impl, maximized ? "maximize" : "unmaximize");
                         Window::Impl::EmitPlatformEvent(impl, fullscreen ? "enterFullScreen" : "leaveFullScreen");
                         return FALSE;
                     }),
                     this);

    g_signal_connect(win, "focus-in-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer user_data) -> gboolean {
                         Window::Impl::EmitPlatformEvent(static_cast<Window::Impl*>(user_data), "focus");
                         return FALSE;
                     }),
                     this);
    g_signal_connect(win, "focus-out-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer user_data) -> gboolean {
                         Window::Impl::EmitPlatformEvent(static_cast<Window::Impl*>(user_data), "blur");
                         return FALSE;
                     }),
                     this);

    gtk_widget_show_all(win);

    if (!webview->Create(win, opts.webviewArgs)) return false;

    // ── eventos de navegación (siempre activos) ────────────────────────
    GtkWidget* view = GTK_WIDGET(webview->NativeWidget());
    if (WEBKIT_IS_WEB_VIEW(view)) {
        g_signal_connect(view, "load-changed",
            G_CALLBACK(+[](WebKitWebView* v, WebKitLoadEvent ev, gpointer ud) {
                auto* impl = static_cast<Window::Impl*>(ud);
                const char* name = nullptr;
                switch (ev) {
                case WEBKIT_LOAD_STARTED: name = "navigationStarted"; break;
                case WEBKIT_LOAD_COMMITTED: name = "loadCommitted"; break;
                case WEBKIT_LOAD_FINISHED: name = "didFinishLoad"; break;
                default: return;
                }
                if (ev == WEBKIT_LOAD_STARTED) {
                    const gchar* u = webkit_web_view_get_uri(v);
                    log::Debug("nav", std::string("STARTED: ") + (u ? u : "?"));
                }
                Window::Impl::EmitPlatformEvent(impl, name);
            }), this);
        g_signal_connect(view, "load-failed",
            G_CALLBACK(+[](WebKitWebView* v, WebKitLoadEvent, gchar* failing_uri,
                           GError* err, gpointer ud) -> gboolean {
                auto* impl = static_cast<Window::Impl*>(ud);
                json::Object o;
                o.emplace_back("url", json::Value(std::string(
                                          failing_uri ? failing_uri : "")));
                o.emplace_back("code",
                               json::Value(static_cast<int64_t>(err ? err->code : 0)));
                o.emplace_back("description", json::Value(std::string(
                                                  err ? err->message : "")));
                Window::Impl::EmitPlatformEvent(impl, "didFailLoad",
                    json::Value(std::move(o)).Serialize());
                return FALSE; // deja que WebKit muestre su página de error
            }), this);
        g_object_bind_property(view, "title", win, "title", G_BINDING_DEFAULT);
        g_signal_connect(view, "notify::title",
            G_CALLBACK(+[](WebKitWebView* v, GParamSpec*, gpointer ud) {
                auto* impl = static_cast<Window::Impl*>(ud);
                const gchar* t = webkit_web_view_get_title(v);
                json::Object o;
                o.emplace_back("title", json::Value(std::string(t ? t : "")));
                Window::Impl::EmitPlatformEvent(impl, "pageTitleUpdated",
                    json::Value(std::move(o)).Serialize());
            }), this);
    }

    // scheme app:// → sirve archivos del directorio de assets si existe
    const char* assetsDir = std::getenv("OW_ASSETS_DIR");
    if (assetsDir && *assetsDir)
        webview->RegisterAssetScheme("app", std::filesystem::path(assetsDir));
    else
        webview->RegisterAssetScheme("app", std::filesystem::current_path() / "dist");

    return true;
}

// ── plataforma: titlebar ─────────────────────────────────────────────────────
void Window::Impl::PApplyTitleBar() {
    if (!pdata || !pdata->window) return;
    switch (opts.titleBarStyle) {
    case TitleBarStyle::Default:
        gtk_window_set_decorated(GTK_WINDOW(pdata->window), TRUE);
        break;
    case TitleBarStyle::Hidden:
    case TitleBarStyle::Custom:
        // Custom == Hidden + drag regions del lado web. Overlay nativo es
        // Win/Mac only (documentado).
        gtk_window_set_decorated(GTK_WINDOW(pdata->window), FALSE);
        break;
    }
}

// ── plataforma: ciclo de vida ────────────────────────────────────────────────
void Window::Impl::PShow() { if (pdata) gtk_widget_show(pdata->window); }
void Window::Impl::PHide() { if (pdata) gtk_widget_hide(pdata->window); }
void Window::Impl::PFocus() {
    if (pdata) {
        gtk_window_present(GTK_WINDOW(pdata->window));
    }
}
void Window::Impl::PClose() {
    if (pdata)
        g_signal_emit_by_name(pdata->window, "delete-event", nullptr, nullptr);
}
void Window::Impl::PDestroy() {
    if (pdata) gtk_widget_destroy(pdata->window);
}
void Window::Impl::PMinimize() {
    if (pdata) gtk_window_iconify(GTK_WINDOW(pdata->window));
}
void Window::Impl::PMaximize() {
    if (pdata) gtk_window_maximize(GTK_WINDOW(pdata->window));
}
void Window::Impl::PUnmaximize() {
    if (pdata) gtk_window_unmaximize(GTK_WINDOW(pdata->window));
}
void Window::Impl::PRestore() {
    if (pdata) gtk_window_deiconify(GTK_WINDOW(pdata->window));
}
void Window::Impl::PSetFullScreen(bool enabled) {
    if (!pdata) return;
    if (enabled) gtk_window_fullscreen(GTK_WINDOW(pdata->window));
    else gtk_window_unfullscreen(GTK_WINDOW(pdata->window));
}
bool Window::Impl::PIsMaximized() const {
    if (!pdata || !pdata->window) return false;
    GdkWindow* gdk = gtk_widget_get_window(pdata->window);
    return gdk && (gdk_window_get_state(gdk) & GDK_WINDOW_STATE_MAXIMIZED);
}
bool Window::Impl::PIsMinimized() const {
    if (!pdata || !pdata->window) return false;
    GdkWindow* gdk = gtk_widget_get_window(pdata->window);
    return gdk && (gdk_window_get_state(gdk) & GDK_WINDOW_STATE_ICONIFIED);
}
bool Window::Impl::PIsFullScreen() const { return pdata && pdata->fullscreen; }

// ── plataforma: geometría ────────────────────────────────────────────────────
Window::Bounds Window::Impl::PGetBounds() const {
    Bounds b;
    if (!pdata || !pdata->window) return b;
    gint w = 0, h = 0;
    gtk_window_get_size(GTK_WINDOW(pdata->window), &w, &h);
    gint x = 0, y = 0;
    gtk_window_get_position(GTK_WINDOW(pdata->window), &x, &y);
    b.x = x; b.y = y; b.w = w; b.h = h;
    return b;
}
void Window::Impl::PSetBounds(const Bounds& bounds) {
    if (!pdata || !pdata->window) return;
    gtk_window_move(GTK_WINDOW(pdata->window), bounds.x, bounds.y);
    gtk_window_resize(GTK_WINDOW(pdata->window), bounds.w, bounds.h);
}
void Window::Impl::PCenter() {
    if (pdata) gtk_window_set_position(GTK_WINDOW(pdata->window), GTK_WIN_POS_CENTER);
}

// ── plataforma: título ───────────────────────────────────────────────────────
void Window::Impl::PSetTitle(const std::string& t) {
    if (pdata) gtk_window_set_title(GTK_WINDOW(pdata->window), t.c_str());
}
std::string Window::Impl::PGetTitle() const {
    if (!pdata || !pdata->window) return {};
    const gchar* t = gtk_window_get_title(GTK_WINDOW(pdata->window));
    return t ? t : "";
}

// ── plataforma: drags de titlebar custom ─────────────────────────────────────
void Window::Impl::PBeginMoveDrag() {
    if (!pdata || !pdata->window) return;
    GdkWindow* gdk = gtk_widget_get_window(pdata->window);
    if (!gdk) return;
    gint rx = 0, ry = 0;
    GdkSeat* seat = nullptr;
    GdkDisplay* display = gtk_widget_get_display(pdata->window);
    if (display) {
        GdkSeat* s = gdk_display_get_default_seat(display);
        if (s) seat = s;
    }
    guint32 timestamp = GDK_CURRENT_TIME;
    if (seat) {
        GdkDevice* dev = gdk_seat_get_pointer(seat);
        if (dev) gdk_device_get_position(dev, nullptr, &rx, &ry);
    }
    gtk_window_begin_move_drag(GTK_WINDOW(pdata->window), 1, rx, ry, timestamp);
}

void Window::Impl::PBeginResizeDrag(const std::string& edge) {
    if (!pdata || !pdata->window) return;
    GdkWindow* gdk = gtk_widget_get_window(pdata->window);
    if (!gdk) return;
    GdkWindowEdge e = GDK_WINDOW_EDGE_SOUTH_EAST;
    if (edge == "left") e = GDK_WINDOW_EDGE_WEST;
    else if (edge == "right") e = GDK_WINDOW_EDGE_EAST;
    else if (edge == "top") e = GDK_WINDOW_EDGE_NORTH;
    else if (edge == "bottom") e = GDK_WINDOW_EDGE_SOUTH;
    else if (edge == "top-left") e = GDK_WINDOW_EDGE_NORTH_WEST;
    else if (edge == "top-right") e = GDK_WINDOW_EDGE_NORTH_EAST;
    else if (edge == "bottom-left") e = GDK_WINDOW_EDGE_SOUTH_WEST;
    else if (edge == "bottom-right") e = GDK_WINDOW_EDGE_SOUTH_EAST;

    gint rx = 0, ry = 0;
    GdkDisplay* display = gtk_widget_get_display(pdata->window);
    if (display) {
        GdkSeat* seat = gdk_display_get_default_seat(display);
        if (seat) {
            GdkDevice* dev = gdk_seat_get_pointer(seat);
            if (dev) gdk_device_get_position(dev, nullptr, &rx, &ry);
        }
    }
    gtk_window_begin_resize_drag(GTK_WINDOW(pdata->window), e, 1, rx, ry,
                                 GDK_CURRENT_TIME);
}

} // namespace ow
