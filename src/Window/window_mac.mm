// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Window/window_mac.mm — NSWindow + WKWebView + traffic lights overlay.
//
// Titlebar:
//  - Default: decoraciones estándar.
//  - Hidden: styleMask sin .titled (frameless puro).
//  - Custom: titlebarAppearsTransparent + fullSizeContentView → el contenido
//    cubre la titlebar y los botones semáforo quedan flotando encima
//    (equivalente a titleBarOverlay de Electron).
//
#include "Window_p.hpp"
#include "../Core/Log.hpp"
#include "ow/detail/minjson.hpp"

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

namespace ow {

struct Window::Impl::PlatformData {
    NSWindow* window = nil;
    WKWebView* webview = nil; // creado por el backend, referenciado aquí
    bool fullscreen = false;
    NSRect preFullscreen{};
};

Window::~Window() = default;
Window::Impl::~Impl() {
    alive->store(false);
    delete pdata;
}

namespace {

std::map<NSWindow*, Window::Impl*>& WindowMap() {
    static std::map<NSWindow*, Window::Impl*> m;
    return m;
}

std::string NsToStd(NSString* s) {
    return s ? std::string(s.UTF8String) : std::string();
}

NSString* StdToNs(const std::string& s) {
    return [NSString stringWithUTF8String:s.c_str()];
}

void Emit(Window::Impl* impl, const std::string& name, std::string_view payload = "null") {
    Window::Impl::EmitPlatformEvent(impl, name, payload);
}

} // namespace

bool Window::Impl::PCreate() {
    pdata = new PlatformData();

    NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                       NSWindowStyleMaskMiniaturizable |
                       (opts.resizable ? NSWindowStyleMaskResizable : 0);
    if (opts.frameless || opts.titleBarStyle == TitleBarStyle::Hidden)
        style = opts.resizable ? NSWindowStyleMaskResizable : 0;

    NSRect frame = NSMakeRect(0, 0, opts.width, opts.height);
    NSWindow* win = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:style
                    backing:NSBackingStoreBuffered
                      defer:NO];
    win.title = StdToNs(opts.title);
    win.releasedWhenClosed = NO;

    if (opts.titleBarStyle == TitleBarStyle::Custom && !(opts.frameless ||
                                                         opts.titleBarStyle == TitleBarStyle::Hidden)) {
        win.titlebarAppearsTransparent = YES;
        win.styleMask |= NSWindowStyleMaskFullSizeContentView;
        // los semáforos quedan visibles sobre el contenido
    }

    pdata->window = win;
    WindowMap()[win] = this;

    // eventos del ciclo de vida
    NSNotificationCenter* nc = [NSNotificationCenter defaultCenter];
    void (^resizeBlock)(NSNotification*) = ^(NSNotification* n) {
        if (auto* impl = WindowMap()[n.object]) {
            NSRect f = impl->pdata->window.frame;
            json::Object o;
            o.emplace_back("width", json::Value(static_cast<int64_t>(f.size.width)));
            o.emplace_back("height", json::Value(static_cast<int64_t>(f.size.height)));
            Emit(impl, "resize", json::Value(std::move(o)).Serialize());
        }
    };
    [nc addObserverForName:NSWindowDidResizeNotification object:win queue:nil
                  usingBlock:resizeBlock];
    [nc addObserverForName:NSWindowWillCloseNotification object:win queue:nil
                  usingBlock:^(NSNotification* n) {
        if (auto* impl = WindowMap()[n.object]) {
            impl->BeginCloseFlow(); // F3.4: veto JS + timeout central
        }
    }];
    [nc addObserverForName:NSWindowDidEnterFullScreenNotification object:win queue:nil
                  usingBlock:^(NSNotification* n) {
        if (auto* impl = WindowMap()[n.object]) {
            impl->pdata->fullscreen = true;
            Emit(impl, "enterFullScreen");
        }
    }];
    [nc addObserverForName:NSWindowDidExitFullScreenNotification object:win queue:nil
                  usingBlock:^(NSNotification* n) {
        if (auto* impl = WindowMap()[n.object]) {
            impl->pdata->fullscreen = false;
            Emit(impl, "leaveFullScreen");
        }
    }];

    if (!webview->Create(win, opts.webviewArgs)) return false;

    const char* assetsDir = std::getenv("OW_ASSETS_DIR");
    if (assetsDir && *assetsDir)
        webview->RegisterAssetScheme("app", std::filesystem::path(assetsDir));
    else
        webview->RegisterAssetScheme("app", std::filesystem::current_path() / "dist");

    if (opts.show) PShow();
    return true;
}

void Window::Impl::PShow() { if (pdata) [pdata->window makeKeyAndOrderFront:nil]; }
void Window::Impl::PHide() { if (pdata) [pdata->window orderOut:nil]; }
void Window::Impl::PFocus() {
    if (pdata) [pdata->window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}
void Window::Impl::PClose() { if (pdata) [pdata->window performClose:nil]; }
void Window::Impl::PDestroy() {
    if (!pdata) return;
    WindowMap().erase(pdata->window);
    [pdata->window close];
}
void Window::Impl::PMinimize() { if (pdata) [pdata->window miniaturize:nil]; }
void Window::Impl::PMaximize() {
    if (pdata) [pdata->window zoom:nil];
}
void Window::Impl::PUnmaximize() {
    if (pdata) [pdata->window zoom:nil];
}
void Window::Impl::PRestore() { if (pdata) [pdata->window deminiaturize:nil]; }
void Window::Impl::PSetFullScreen(bool enabled) {
    if (!pdata) return;
    if (enabled != pdata->fullscreen)
        [pdata->window toggleFullScreen:nil];
}
bool Window::Impl::PIsMaximized() const { return pdata && pdata->window.isZoomed; }
bool Window::Impl::PIsMinimized() const { return pdata && pdata->window.isMiniaturized; }
bool Window::Impl::PIsFullScreen() const { return pdata && pdata->fullscreen; }

Window::Bounds Window::Impl::PGetBounds() const {
    Bounds b;
    if (!pdata) return b;
    NSScreen* screen = pdata->window.screen ?: [NSScreen mainScreen];
    CGFloat flipH = screen.frame.size.height;
    NSRect f = pdata->window.frame;
    b.x = static_cast<int>(f.origin.x);
    b.y = static_cast<int>(flipH - f.origin.y - f.size.height); // top-left como Win/Linux
    b.w = static_cast<int>(f.size.width);
    b.h = static_cast<int>(f.size.height);
    return b;
}
void Window::Impl::PSetBounds(const Bounds& bounds) {
    if (!pdata) return;
    NSScreen* screen = pdata->window.screen ?: [NSScreen mainScreen];
    CGFloat flipH = screen.frame.size.height;
    NSRect f = NSMakeRect(bounds.x, flipH - bounds.y - bounds.h, bounds.w, bounds.h);
    [pdata->window setFrame:f display:YES];
}
void Window::Impl::PCenter() { if (pdata) [pdata->window center]; }

void Window::Impl::PSetTitle(const std::string& t) {
    if (pdata) pdata->window.title = StdToNs(t);
}
std::string Window::Impl::PGetTitle() const {
    return pdata ? NsToStd(pdata->window.title) : std::string();
}

void Window::Impl::PApplyTitleBar() {}

void Window::Impl::PBeginMoveDrag() {
    if (!pdata) return;
    [pdata->window performDragWithName:@"ow-drag"];
}

void Window::Impl::PBeginResizeDrag(const std::string&) {
    // macOS gestiona resize por bordes nativos; con frameless se usan
    // overlays custom (F3). v1: no-op.
}

} // namespace ow
