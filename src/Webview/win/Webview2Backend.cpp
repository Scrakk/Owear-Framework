// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Webview/win/Webview2Backend.cpp — backend WebView2 (Edge Chromium).
//
// Requiere: Microsoft.Web.WebView2 SDK (headers + WebView2Loader).
//   - NuGet: Microsoft.Web.WebView2 → include/ + build/native/WebView2Loader
//   - CMake (F-windows): target_link_libraries ... WebView2Loader
//
// Assets locales: SetVirtualHostNameToFolderMapping("app.owear", root)
//   → la app carga https://app.owear/index.html (origen https real, sin CORS).
//
// VERIFICAR-EN-WINDOWS: primer build del SDK y rutas del loader.
//
#include "../IWebviewBackend.hpp"
#include "../../Core/Log.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#include <wrl/client.h>
#include <wrl/event.h>

#include <filesystem>
#include <string>
#include <vector>

namespace ow {

using namespace Microsoft::WRL;

namespace {

std::wstring Utf8ToWide(std::string_view s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string WideToUtf8(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// Perfil de usuario fuera del dir del exe (puede ser read-only en installs
// de sistema) — patrón GetUserDataDir de ole/browser_host.
std::wstring UserDataDir() {
    PWSTR local = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr,
                                       &local))) {
        dir = std::wstring(local) + L"\\owear\\WebView2";
        CoTaskMemFree(local);
    } else {
        dir = L".\\owear-webview2";
    }
    std::filesystem::create_directories(dir);
    return dir;
}

class Webview2Backend final : public IWebviewBackend {
public:
    bool Create(void* parentNativeWindow, const std::vector<std::string>& args) override {
        hwnd_ = static_cast<HWND>(parentNativeWindow);
        if (!hwnd_) return false;

        // COM apartment en el hilo de UI (requisito de WebView2)
        thread_local bool comInit = false;
        if (!comInit) {
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            comInit = true;
        }

        auto envOptions = Make<CoreWebView2EnvironmentOptions>();
        if (!args.empty()) {
            std::wstring joined;
            for (const auto& a : args) {
                joined += Utf8ToWide(a) + L" ";
            }
            envOptions->put_AdditionalBrowserArguments(joined.c_str());
        }

        HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, UserDataDir().c_str(), envOptions.Get(),
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                    if (FAILED(result)) {
                        log::Error("webview2", "environment falló");
                        return E_FAIL;
                    }
                    environment_ = env;
                    return env->CreateCoreWebView2Controller(
                        hwnd_,
                        Callback<
                            ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [this](HRESULT result, ICoreWebView2Controller* ctrl)
                                -> HRESULT {
                                if (FAILED(result) || !ctrl) {
                                    log::Error("webview2", "controller falló");
                                    return E_FAIL;
                                }
                                controller_ = ctrl;
                                controller_->get_CoreWebView2(&webview_);
                                OnControllerReady();
                                return S_OK;
                            })
                            .Get());
                })
                .Get());

        return SUCCEEDED(hr);
    }

    void OnControllerReady() {
        // scripts de init encolados antes de que existiera el webview
        for (const auto& js : pendingInitScripts_)
            webview_->AddScriptToExecuteOnDocumentCreated(Utf8ToWide(js).c_str(),
                                                          nullptr);
        pendingInitScripts_.clear();

        // mensajes JS→nativo (texto crudo)
        if (messageHandler_) AttachMessageHandler();

        // assets locales
        if (!assetRoot_.empty()) AttachAssetMapping();

        if (!pendingUrl_.empty()) {
            std::string u = pendingUrl_;
            pendingUrl_.clear();
            LoadURL(u);
        }

        RECT rc;
        GetClientRect(hwnd_, &rc);
        controller_->put_Bounds(rc);
    }

    void AttachMessageHandler() {
        webview_->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args)
                    -> HRESULT {
                    LPWSTR msg = nullptr;
                    if (SUCCEEDED(args->TryGetWebMessageAsString(&msg)) && msg) {
                        if (messageHandler_)
                            messageHandler_(WideToUtf8(msg));
                        CoTaskMemFree(msg);
                    }
                    return S_OK;
                })
                .Get(),
            nullptr);
    }

    void AttachAssetMapping() {
        ComPtr<ICoreWebView2_3> wv3;
        if (SUCCEEDED(webview_.As(&wv3))) {
            wv3->SetVirtualHostNameToFolderMapping(
                L"app.owear", assetRoot_.wstring().c_str(),
                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
        }
    }

    void InjectInitScript(const std::string& js) override {
        if (webview_)
            webview_->AddScriptToExecuteOnDocumentCreated(Utf8ToWide(js).c_str(),
                                                          nullptr);
        else
            pendingInitScripts_.push_back(js);
    }

    void SetMessageHandler(WebMessageHandler handler) override {
        messageHandler_ = std::move(handler);
        if (webview_) AttachMessageHandler();
    }

    void LoadURL(const std::string& url) override {
        if (!webview_) {
            pendingUrl_ = url;
            return;
        }
        std::string u = url;
        if (u.rfind("app://", 0) == 0) u = "https://app.owear/" + u.substr(6);
        webview_->Navigate(Utf8ToWide(u).c_str());
    }

    void EvalJS(const std::string& js, EvalCallback cb) override {
        if (!webview_) return;
        // ExecuteScript entrega JSON.stringify(resultado) — perfecto para el bridge
        auto* boxed = new EvalCallback(std::move(cb));
        webview_->ExecuteScript(
            Utf8ToWide(js).c_str(),
            Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                [boxed](HRESULT error, LPCWSTR resultJson) -> HRESULT {
                    std::unique_ptr<EvalCallback> cb(boxed);
                    std::string result = "null";
                    if (SUCCEEDED(error) && resultJson) result = WideToUtf8(resultJson);
                    if (*cb) (*cb)(result, SUCCEEDED(error));
                    return S_OK;
                })
                .Get());
    }

    void RegisterAssetScheme(const std::string& scheme,
                             const std::filesystem::path& root) override {
        (void)scheme; // Windows usa host virtual fijo app.owear
        assetRoot_ = root;
        if (webview_) AttachAssetMapping();
    }

    void Resize(int x, int y, int w, int h) override {
        (void)x;
        (void)y;
        RECT rc{0, 0, static_cast<LONG>(w), static_cast<LONG>(h)};
        if (controller_) controller_->put_Bounds(rc);
    }

    void* NativeWidget() const override { return hwnd_; }

private:
    HWND hwnd_ = nullptr;
    ComPtr<ICoreWebView2Environment> environment_;
    ComPtr<ICoreWebView2Controller> controller_;
    ComPtr<ICoreWebView2> webview_;
    WebMessageHandler messageHandler_;
    std::vector<std::string> pendingInitScripts_;
    std::string pendingUrl_;
    std::filesystem::path assetRoot_;
};

} // namespace

std::unique_ptr<IWebviewBackend> CreateWebviewBackend() {
    return std::make_unique<Webview2Backend>();
}

} // namespace ow
