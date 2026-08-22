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

---

# APIs modulares (api/<nombre>) — F6/F7/F8/F9 ✅ Linux

Estructura: cada API vive en `api/<nombre>/` y compila a `.owm` independiente
(actualización modular). 13 módulos + builtin ow-window cargando y verificados
E2E en Linux (19/19 pruebas en verde).

| API | Estado Linux | Win/Mac |
|---|---|---|
| fs (v2: handles fd-style, copy/rename/chmod/symlink/lstat/realpath/mkdtemp/access/truncate, **watch** inotify) | ✅ E2E | watch: ReadDirectoryChangesW/kqueue — VERIFICAR |
| process (**PTY Manager real** forkpty + spawn pipes + exit/stdout eventos) | ✅ E2E (bash interactivo) | ConPTY — VERIFICAR |
| path (join/resolve/dirname/basename/extname/normalize + dirs XDG/Known Folders) | ✅ E2E | Known Folders — VERIFICAR |
| dialog (GtkFileChooserNative / IFileDialog / NSPanel) | ✅ registrado (modal no automatizable) | VERIFICAR |
| clipboard (texto + imagen PNG→SHM) | ✅ E2E texto | WIC pendiente |
| screen (monitores/workarea/scale/cursor) | ✅ E2E | VERIFICAR |
| net (HTTP(S) nativo sin CORS + download SHA256; respuestas ≥256KB → SHM) | ✅ E2E | mismo código |
| notification (sistema: org.freedesktop.Notifications) | ✅ E2E (id real) | toast/UNUser — pendiente |
| power (logind sleep/resume + ScreenSaver inhibit) | ✅ E2E | PowerBroadcast — pendiente |
| shell (openExternal/openPath/showItemInFolder via FileManager1 D-Bus) | ✅ validación+launch | IShellLink — VERIFICAR |
| updater (manifest+semver+sha256+replace atómico+execv relaunch) | ✅ E2E check/download | igual |
| menu (popup JSON declarativo opcional; setApplicationMenu noop en Linux por diseño) | ✅ | NSMenu/HMENU — pendiente |
| globalshortcut (X11 XGrabKey; **opcional**, Wayland no soportado) | ✅ registrado | RegisterHotKey — pendiente |
| tray (libayatana-appindicator — requiere `libayatana-appindicator3-dev`) | ⏸ omitida sin deps | Shell_NotifyIcon/NSStatusItem — pendiente |

## Builtins kernel-coupled ✅ Linux

| API | Estado |
|---|---|
| window-extras (devtools ✓ capturePage PNG→SHM ✓ alwaysOnTop ✓ opacity ✓ flashFrame ✓ setIcon ✓ userAgent ✓ zoom ✓; printToPDF/progressBar → error claro v1) | ✅ E2E 13/13 |
| session (cookies get/set/delete ✓ clearStorage ✓ proxy ✓ downloads con eventos ✓) | ✅ E2E cookies |
| capturer (getSources thumbnails + captureScreen full → SHM PNG; **X11-only**, Wayland = error documentado) | ✅ E2E bajo Xvfb |
| crashreporter (señales + backtrace a cache/crashes/) | ✅ registrado |

Total: **18 módulos** (14 .owm dinámicos + 4 builtins kernel), 32 pruebas E2E
en verde sobre Linux (19 all.html + 13 builtins.html bajo Xvfb).
