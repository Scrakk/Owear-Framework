# Protocolo de Bridge y Control

## 1. Renderer ↔ Kernel (bridge inyectado)

Script inyectado en cada documento (`src/Bridge/BridgeScript.cpp`) que expone
`window.ow`. Transporte autodetectado:

| Plataforma | JS → nativo | nativo → JS |
|---|---|---|
| Linux (WebKitGTK) | `window.webkit.messageHandlers.ow.postMessage` | `evaluate_javascript` |
| Windows (WebView2) | `window.chrome.webview.postMessage` | `ExecuteScript` |
| macOS (WKWebView) | `window.webkit.messageHandlers.ow` | `evaluateJavaScript` |

### API expuesta

```js
ow.invoke(module, fn, ...args)  // Promise; rechaza con Error(message)
ow.invokeSync(module, fn, ...args) // F3: síncrono vía ow-sync:// (bootstrap only)
ow.readShared({id, size})       // F3: ArrayBuffer de región ow-shm:// sin copia
ow.on(name, cb)                 // eventos del kernel/ventana
ow.emit(name, payload)          // evento JS → nativo + otros listeners JS
```

### Binarios grandes (F3)

`fs.readFile` devuelve `{__ow_shm:{id,size}}` para archivos ≥ 256 KB.
El renderer los lee con `ow.readShared(handle)` → ArrayBuffer servido
directamente desde el mmap (cero base64, cero copia del lado kernel).

### Cierre con veto (F3)

`closeRequested` llega con `{requestId}`. El renderer responde:

```js
ow.on('closeRequested', p => {
  ow.invoke('ow-window', 'respondCloseRequest', window.__owWindowId, p.requestId, false)
})
```

Sin respuesta en `OW_CLOSE_TIMEOUT_MS` (default 1000) el kernel cierra.
Desde el SDK Node: `win.closeRespond(requestId, allow)`.

### Wire format (frames de texto)

```jsonc
// invoke (JS→nativo)
{ "t": "invoke", "id": N, "m": "fs", "f": "readText", "a": ["/etc/x"], "w": windowId }

// resultado (nativo→JS, vía __ow._apply(id, ok, '<json>'))
{ "t": "result", "id": N, "ok": true, "r": <json>, "b": "<base64>" }

// evento (nativo→JS, vía __ow._event(w, '<name>', '<json>'))
{ "t": "event", "w": windowId, "n": "resize", "p": <json> }
```

Los payloads se inyectan como literales JS escapados (`json::JsLiteral`)
resueltos con `JSON.parse` dentro del documento — sin dobles serializaciones.

## 2. Sidecar Node ↔ Kernel (Control Socket)

UDS (`$XDG_RUNTIME_DIR/owear-<pid>.sock`) o named pipe
(`\\.\pipe\owear-<pid>`). NDJSON:

```jsonc
{"id":1,"cmd":"window.create","params":{"title":"Hi","url":"http://localhost:5173"}}
{"id":1,"ok":true,"result":{"windowId":1}}
{"id":2,"ok":false,"error":"ventana no encontrada"}
{"event":"window.event","params":{"windowId":1,"name":"resize","payload":{"width":600}}}
```

### Comandos v1

`app.info`, `app.quit`, `node.ensure {range}` ·
`window.create/close/destroy/show/hide/focus/minimize/maximize/unmaximize/
setFullScreen/isMaximized/isMinimized/getBounds/setBounds/setTitle/loadURL/
eval`

### Eventos reenviados

`resize move focus blur maximize unmaximize enterFullScreen leaveFullScreen
closeRequested closed`

## 3. Módulos nativos (.owm)

ABI-C único: el módulo exporta `ow_module_descriptor()` (ver `api/../include/ow_api.h`
y `include/ow/Module.h`). Contrato de memoria: los buffers de respuesta viven
solo durante la llamada — el host copia inmediatamente. Sin excepciones hacia
el host.
