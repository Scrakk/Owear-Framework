# Roadmap Owear

Estado: F0–F4 construidos (Linux verificado end-to-end; Windows/macOS fuentes
completas pendientes de verificación en su SO destino).

## F0 — Esqueleto ✅
- [x] Monorepo pnpm + CMake presets 3 plataformas
- [x] Contratos públicos `include/ow/` (anti-drift por compile-error en CI)
- [x] Selección de fuentes por plataforma vía CMake (sufijos `_win/_mac/_linux`)
- [x] Tests unitarios (minjson, codec, sha256) — sin gtest, cero deps

## F1 — Ventana + Webview + Bridge ✅ (Linux) · 🔧 (win/mac)
- [x] Kernel: App loop GTK/Win32/NSApplication
- [x] Multi-ventana con WindowManager (`LiveWindows`)
- [x] WebKitGTK backend completo: init scripts, message handler, scheme app://
      con anti-path-traversal
- [x] Bridge invoke/event funcionando end-to-end (promesas JS ↔ nativo)
- [x] WebView2 backend (host virtual `app.owear`, WebMessageReceived)
- [x] WKWebView backend (URLSchemeHandler, scriptMessageHandler)
- [ ] Verificación física en Windows/macOS (CI matrix configurada)

## F2 — Módulos nativos (.owm) ✅
- [x] ABI-C estable (`ow_module_descriptor`), macros OW_MODULE_BEGIN/FN/END
- [x] Loader dlopen/LoadLibrary con búsqueda en OW_MODULES_DIR
- [x] Módulo stock `fs` (builtin + .owm independiente)
- [x] `owear-build-native`: compila native/*.cpp de la app → .owm
- [x] Codegen de bindings vía `@owear/vite-plugin` (`@owear/native` virtual)

## F3 — Rendimiento del puente ✅
- [x] Outbox batching: todos los _apply/_event salen en UN eval por tick
- [x] SHM sin copia (`ow-shm://`): regiones mmap servidas estáticas,
      `ow.readShared()` → ArrayBuffer. fs.readFile usa handle ≥ 256 KB.
      Verificado: 5 MB con SHA-256 idéntico host↔renderer.
- [x] Canal síncrono opcional (`ow.invokeSync` vía `ow-sync://` + XHR;
      advertencia de reentrancia documentada)
- [x] Veto de `closeRequested` desde JS/SDK (`requestId` +
      `ow-window.respondCloseRequest` + timeout configurable
      OW_CLOSE_TIMEOUT_MS, default 1 s). Verificado: veto ✓ allow ✓ timeout ✓
- [x] Overlay nativo Windows (WM_NCCALCSIZE técnica Chromium; VERIFICAR-EN-CI)
- [x] Base64 fallback para binarios pequeños

## F4 — Distribución ✅
- [x] Runtime Manager Node: descarga oficial nodejs.org + SHA256 + cache XDG
- [x] Sidecar Node (fork/exec / CreateProcess) con OW_CONTROL_SOCKET
- [x] ControlServer UDS/named-pipe (~20 comandos)
- [x] `@owear/core` SDK (app, BrowserWindow tipado)
- [x] `@owear/cli` (create/dev/build)
- [x] Template de app + flujo `pnpm dev`

## F5 — Port de Scrakk Studio
Reservado al autor.
