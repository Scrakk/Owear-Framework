// Copyright 2026 Owear Contributors
// SPDX-License-Identifier: Apache-2.0
//
// src/Webview/win/Webview2Stub_win.cpp — stub sin WebView2 SDK.
// Permite compilar y enlazar el kernel en runners sin el SDK. Activar
// OW_WITH_WEBVIEW2 para el backend real (NuGet Microsoft.Web.WebView2).
//
#include "../IWebviewBackend.hpp"

namespace ow {

std::unique_ptr<IWebviewBackend> CreateWebviewBackend() {
    return nullptr; // InitCommon() lo reporta con log::Error claro
}

} // namespace ow
