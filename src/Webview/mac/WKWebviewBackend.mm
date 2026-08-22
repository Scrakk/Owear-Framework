// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Webview/mac/WKWebviewBackend.mm — backend WKWebView.
//
// - Init scripts: WKUserContentController addUserScript (document start).
// - JS→nativo: scriptMessageHandler "ow" (body = NSString con el JSON).
// - nativo→JS: evaluateJavaScript.
// - Assets: WKURLSchemeHandler para app:// (anti-traversal incluido).
//
#include "../IWebviewBackend.hpp"
#include "../../Core/Log.hpp"

#include "ow/Json.h" // error JSON de EvalJS (json::Object)

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

#include <filesystem>
#include <string>
#include <vector>

namespace ow {
namespace {

NSString* StdToNs(const std::string& s) {
    return [NSString stringWithUTF8String:s.c_str()];
}

} // namespace
} // namespace ow

// ── Objetos ObjC (fuera del namespace C++) ──────────────────────────────────

@interface OwMessageProxy : NSObject
@property(nonatomic, copy) void (^block)(WKScriptMessage* msg);
- (instancetype)initWithBlock:(void (^)(WKScriptMessage*))block;
@end

@implementation OwMessageProxy
- (instancetype)initWithBlock:(void (^)(WKScriptMessage*))block {
    if ((self = [super init])) _block = block;
    return self;
}
- (void)userContentController:(WKUserContentController*)ucc
      didReceiveScriptMessage:(WKScriptMessage*)message {
    if (self.block) self.block(message);
}
@end

@interface OwSchemeHandler : NSObject <WKURLSchemeHandler>
@property(nonatomic) std::filesystem::path root;
@end

@implementation OwSchemeHandler

- (void)webView:(WKWebView*)webView
    startURLSchemeTask:(id<WKURLSchemeTask>)task {
    NSURL* url = task.request.URL;
    std::string rel(url.path.UTF8String ?: "/");
    if (!rel.empty() && rel.front() == '/') rel.erase(0, 1);
    if (rel.find("..") != std::string::npos) {
        [task didFailWithError:[NSError errorWithDomain:NSCocoaErrorDomain
                                                   code:NSFileReadUnknownError
                                               userInfo:nil]];
        return;
    }
    std::filesystem::path full = (_root / rel).lexically_normal();
    auto [rEnd, fEnd] =
        std::mismatch(_root.begin(), _root.end(), full.begin());
    if (rEnd != _root.end() || !std::filesystem::is_regular_file(full)) {
        [task didFailWithError:[NSError errorWithDomain:NSCocoaErrorDomain
                                                   code:NSFileNoSuchFileError
                                               userInfo:nil]];
        return;
    }

    NSData* data = [NSData dataWithContentsOfFile:
                             [NSString stringWithUTF8String:full.c_str()]];
    if (!data) {
        [task didFailWithError:[NSError errorWithDomain:NSCocoaErrorDomain
                                                   code:NSFileReadUnknownError
                                               userInfo:nil]];
        return;
    }

    NSDictionary<NSString*, NSString*>* mimeByExt = @{
        @"html": @"text/html",   @"htm": @"text/html",
        @"js": @"text/javascript", @"mjs": @"text/javascript",
        @"css": @"text/css",     @"json": @"application/json",
        @"svg": @"image/svg+xml", @"png": @"image/png",
        @"jpg": @"image/jpeg",   @"jpeg": @"image/jpeg",
        @"woff2": @"font/woff2", @"woff": @"font/woff",
        @"ttf": @"font/ttf",     @"ico": @"image/x-icon",
        @"wasm": @"application/wasm",
    };
    std::string ext = full.extension().string();
    if (!ext.empty() && ext.front() == '.') ext.erase(0, 1);
    NSString* mime = mimeByExt[[NSString stringWithUTF8String:ext.c_str()]]
                         ?: @"application/octet-stream";

    NSHTTPURLResponse* resp = [[NSHTTPURLResponse alloc]
        initWithURL:url
           statusCode:200
          HTTPVersion:@"HTTP/1.1"
         headerFields:@{@"Content-Type": mime}];
    [task didReceiveResponse:resp];
    [task didReceiveData:data];
    [task didFinish];
}

- (void)webView:(WKWebView*)webView
    stopURLSchemeTask:(id<WKURLSchemeTask>)task {
}

@end

// ── Backend C++ ──────────────────────────────────────────────────────────────

namespace ow {

namespace {

class WKWebviewBackend final : public IWebviewBackend {
public:
    bool Create(void* parentNativeWindow,
                const std::vector<std::string>& args) override {
        NSWindow* parent = static_cast<NSWindow*>(parentNativeWindow);
        if (!parent) return false;

        config_ = [WKWebViewConfiguration new];

        // El scheme "app://" debe registrarse en la configuración ANTES de
        // instanciar el WKWebView: WKWebViewConfiguration se COPIA al crear
        // la vista, así que cambios posteriores (incluido setURLSchemeHandler)
        // no tienen efecto sobre la instancia ya creada. El handler lee
        // `root` de forma perezosa en cada request (igual que el patrón de
        // WebKitGTKBackend::Create en Linux), así que RegisterAssetScheme()
        // puede seguir llamándose después de Create(), como hacen todas las
        // plataformas hoy.
        assetHandler_ = [[OwSchemeHandler alloc] init];
        assetHandler_.root = pendingAssetRoot_;
        [config_ setURLSchemeHandler:assetHandler_ forURLScheme:@"app"];

        // scripts pendientes (inyectados antes de existir el webview)
        for (const auto& js : pendingScripts_) AddUserScript(js);
        pendingScripts_.clear();

        webview_ = [[WKWebView alloc] initWithFrame:parent.contentView.bounds
                                      configuration:config_];
        webview_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [parent.contentView addSubview:webview_];
        ucc_ = config_.userContentController;

        if (handler_) AttachMessageHandler();
        if (!pendingUrl_.empty()) {
            std::string u;
            u.swap(pendingUrl_);
            LoadURL(u);
        }
        return true;
    }

    void AddUserScript(const std::string& js) {
        WKUserScript* s = [[WKUserScript alloc]
            initWithSource:StdToNs(js)
             injectionTime:WKUserScriptInjectionTimeAtDocumentStart
          forMainFrameOnly:NO];
        [config_.userContentController addUserScript:s];
    }

    void AttachMessageHandler() {
        OwMessageProxy* proxy =
            [[OwMessageProxy alloc] initWithBlock:^(WKScriptMessage* msg) {
                if ([msg.body isKindOfClass:[NSString class]] && handler_) {
                    handler_(std::string_view(
                        ((NSString*)msg.body).UTF8String)); // cast: body es id
                }
            }];
        proxy_ = proxy;
        [config_.userContentController addScriptMessageHandler:proxy name:@"ow"];
    }

    void InjectInitScript(const std::string& js) override {
        if (webview_)
            AddUserScript(js); // ya existe → añade al controlador vivo
        else
            pendingScripts_.push_back(js);
    }

    void SetMessageHandler(WebMessageHandler handler) override {
        handler_ = std::move(handler);
        if (webview_) AttachMessageHandler();
    }

    void LoadURL(const std::string& url) override {
        if (!webview_) {
            pendingUrl_ = url;
            return;
        }
        NSURL* ns = [NSURL URLWithString:StdToNs(url)];
        if (ns) [webview_ loadRequest:[NSURLRequest requestWithURL:ns]];
    }

    void EvalJS(const std::string& js, EvalCallback cb) override {
        if (!webview_) return;
        auto* boxed = new EvalCallback(std::move(cb));
        [webview_ evaluateJavaScript:StdToNs(js)
                   completionHandler:^(id result, NSError* err) {
                     std::unique_ptr<EvalCallback> local(boxed);
                     bool ok = err == nil;
                     std::string out = "null";
                     if (ok) {
                         if ([result isKindOfClass:[NSString class]])
                             out = ((NSString*)result).UTF8String;
                         else if (result != nil) {
                             NSData* d = [NSJSONSerialization
                                 dataWithJSONObject:result
                                            options:0
                                              error:nil];
                             if (d)
                                 out = std::string(
                                     static_cast<const char*>(d.bytes),
                                     d.length);
                         } else {
                             out = "null";
                         }
                     } else {
                         json::Object o;
                         o.emplace_back("message", json::Value(std::string(
                                                       err.localizedDescription
                                                           .UTF8String)));
                         out = json::Value(std::move(o)).Serialize();
                     }
                     if (*local) (*local)(out, ok);
                   }];
    }

    void RegisterAssetScheme(const std::string& scheme,
                             const std::filesystem::path& root) override {
        if (scheme != "app") {
            // v1: solo "app" se registra (en Create(), antes de instanciar
            // el WKWebView). Ver comentario en Create().
            log::Warn("webview", "RegisterAssetScheme: esquema no soportado en macOS: " + scheme);
            return;
        }
        if (assetHandler_) {
            // El WKWebView ya existe (o está a punto de crearse en Create());
            // el handler ya quedó registrado en la config. Solo actualizamos
            // la raíz, que se lee al vuelo en cada request.
            assetHandler_.root = root;
        } else {
            // Create() aún no se ha llamado: guarda la raíz para aplicarla
            // cuando se cree el handler.
            pendingAssetRoot_ = root;
        }
    }

    void Resize(int x, int y, int w, int h) override {
        (void)x; (void)y; (void)w; (void)h; // autolayout
    }

    void* NativeWidget() const override { return webview_; }

private:
    WKWebViewConfiguration* config_ = nil;
    WKUserContentController* ucc_ = nil;
    WKWebView* webview_ = nil;
    OwMessageProxy* proxy_ = nil;
    OwSchemeHandler* assetHandler_ = nil;
    WebMessageHandler handler_;
    std::vector<std::string> pendingScripts_;
    std::string pendingUrl_;
    std::filesystem::path pendingAssetRoot_;
};

} // namespace

std::unique_ptr<IWebviewBackend> CreateWebviewBackend() {
    return std::make_unique<WKWebviewBackend>();
}

} // namespace ow
