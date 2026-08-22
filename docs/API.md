# API Reference completa — Owear 0.1.0

> Generada contra el código real (`d9ded9b`). Toda llamada del renderer pasa
> por `window.ow`; toda API Node habla con el kernel por el Control Socket.

---

## 1 · Renderer — `window.ow` (inyectado por el kernel en cada documento)

```ts
ow.invoke(module: string, fn: string, ...args): Promise<unknown>
ow.invokeSync(module: string, fn: string, ...args): unknown        // ⚠️ bloquea el renderer
ow.readShared(handle: { id: string; size: number }): Promise<ArrayBuffer>
ow.on(name: string, cb: (payload: unknown) => void): () => void    // devuelve unsubscribe
ow.emit(name: string, payload?: unknown): void                     // JS → nativo + listeners JS
window.__owWindowId: number                                        // id de la ventana actual
```

Schemes disponibles desde el WebView:

| Scheme | Uso |
|---|---|
| `app://<ruta>` | assets del bundle (dist/) |
| `ow-shm://<id>` | región de memoria compartida sin copia |
| `ow-sync://i/<payload>` | invoke síncrono (interno de `invokeSync`) |

Atributos DOM reconocidos por el kernel:

```html
<div data-ow-drag>…</div>                          <!-- zona de arrastre de titlebar -->
<button data-ow-no-drag>…</button>                 <!-- excluye del drag -->
<div data-ow-resize="bottom-right">…</div>         <!-- resize manual frameless -->
<!-- edges: left|right|top|bottom|top-left|top-right|bottom-left|bottom-right -->
```

---

## 2 · Módulos nativos — invocables con `ow.invoke('<módulo>', '<fn>', …args)`

### 2.1 `fs` (stock, builtin + .owm)

```ts
fs.readText(path): Promise<string>
fs.readFile(path): Promise<{ b64: string } | { __ow_shm: { id: string; size: number } }>
//                                    ^ <256 KB      ^ ≥256 KB → leer con ow.readShared()
fs.writeFile(path, data: string, encoding?: 'utf8' | 'base64'): Promise<null>
fs.readDir(path): Promise<{ name: string; type: 'file' | 'dir' | 'other' }[]>
fs.stat(path): Promise<{ size: number; isFile: boolean; isDir: boolean; mtimeMs: number } | null>
fs.mkdir(path, recursive?: boolean): Promise<null>
fs.remove(path, recursive?: boolean): Promise<null>
fs.exists(path): Promise<boolean>
```

### 2.2 `ow-window` (builtin interno — titlebar y ciclo de vida)

```ts
owWindow.minimize(windowId): Promise<null>
owWindow.maximize(windowId, enabled: boolean): Promise<null>
owWindow.close(windowId): Promise<null>            // dispara closeRequested (vetable)
owWindow.focus(windowId): Promise<null>
owWindow.setTitle(windowId, title: string): Promise<null>
owWindow.isMaximized(windowId): Promise<boolean>
owWindow.respondCloseRequest(windowId, requestId: number, allow: boolean): Promise<null>

// interceptados antes del dispatcher (necesitan la ventana invocante):
owWindow.beginMoveDrag()                           // arrastrar la ventana
owWindow.beginResizeDrag(edge: string)
```

### 2.3 Módulos propios (.owm) — ABI-C

```cpp
#include <ow/Json.h>
#include <ow/Module.h>

static void miFuncion(const ow_request_t* req, ow_response_t* res) {
    // req:  json (array de args), bin/bin_len, window_id, host
    // res:  status(0=ok), error, json, bin/bin_len
}

OW_MODULE_BEGIN(miModulo, "1.0.0")
OW_FN(miFuncion)
OW_MODULE_END()
```

API disponible dentro de un módulo:

| Header | Contenido |
|---|---|
| `ow_api.h` | `ow_module_desc_t`, `ow_fn_entry_t`, `ow_request_t`, `ow_response_t`, `OW_MODULE_EXPORT` |
| `ow/Module.h` | `OW_MODULE_BEGIN` / `OW_FN` / `OW_MODULE_END`, `Module::RespondOk(res, json, bin?)`, `Module::RespondError(res, msg)` |
| `ow/Shm.h` | `ow_shm_put(data,len)→id`, `ow_shm_data(id,&len)→ptr`, `ow_shm_shutdown()` (símbolos del host, `-rdynamic`) |
| `ow/Base64.h` | `b64::Encode(string_view)`, `b64::Decode(sv, out)` |
| `ow/Json.h` | `json::Parse(sv)→ParseResult`, `Value` (Null/Bool/Int/Double/String/Array/Object), `.Find()/.As*/.Serialize()`, `json::JsLiteral()` |

Contrato: los buffers de `res` viven solo durante la llamada (el host copia al
instante); prohibido lanzar excepciones hacia el host.

---

## 3 · SDK Node — `@owear/core` (main process estilo Electron)

```ts
import { app, BrowserWindow, platform } from '@owear/core'
```

### app

```ts
app.whenReady(): Promise<void>                       // conecta con el kernel
app.quit(exitCode?): Promise<void>
app.info(): Promise<{ pid; version; socket }>
app.ensureNodeRuntime(range?: string): Promise<{ path }>   // 'latest'|'lts'|'v22'|…
app.__channel                                        // acceso crudo al canal
```

### BrowserWindow

```ts
new BrowserWindow(options?: WindowOptions)

// opciones
interface WindowOptions {
  title?: string
  width?: number; height?: number
  x?: number; y?: number
  resizable?: boolean
  frameless?: boolean
  titleBarStyle?: 'default' | 'hidden' | 'custom'
  url?: string
}

// métodos
win.id: number | null
win.loadURL(url): Promise<void>
win.eval<T>(js): Promise<T>                          // resultado JSON-serializado
win.show(); win.hide(); win.focus()
win.close()                                          // vetable
win.destroy()                                        // inmediato
win.minimize(); win.maximize(); win.unmaximize()
win.setFullScreen(enabled)
win.isMaximized(): Promise<boolean>; win.isMinimized(): Promise<boolean>
win.getBounds(): Promise<Bounds>; win.setBounds(partial): Promise<void>
win.setTitle(title): Promise<void>
win.closeRespond(requestId: number, allow: boolean): Promise<void>   // F3.4

// eventos (win.on / win.once)
'ready-to-show' | 'closed' | 'resize'(Bounds) | 'move'(Bounds)
'focus' | 'blur' | 'maximize' | 'unmaximize'
'enterFullScreen' | 'leaveFullScreen'
'closeRequested'({ requestId }) | 'disconnected' | 'error'
```

---

## 4 · Protocolo de Control (kernel ↔ clientes NDJSON)

UDS `$XDG_RUNTIME_DIR/owear-<pid>.sock` (Linux/macOS) · named pipe
`\\.\pipe\owear-<pid>` (Windows). Formato:
`{"id":N,"cmd":"…","params":{…}}` → `{"id":N,"ok":bool,"result":…|"error":…}`

| Comando | Params |
|---|---|
| `app.info` | — |
| `app.quit` | `{exitCode?}` |
| `node.ensure` | `{range}` |
| `event.emit` | `{name, payload?, windowId?}` → reemite como `sdk.event` |
| `window.create` | `WindowOptions` completo |
| `window.close / destroy / show / hide / focus / minimize / maximize{enabled} / unmaximize / setFullScreen{enabled}` | `{windowId}` |
| `window.isMaximized / isMinimized` | `{windowId}` → bool |
| `window.getBounds` | `{windowId}` → `{x,y,width,height}` |
| `window.setBounds` | `{windowId, x?, y?, width?, height?}` |
| `window.setTitle` | `{windowId, title}` |
| `window.loadURL` | `{windowId, url}` |
| `window.respondCloseRequest` | `{windowId, requestId, allow}` |
| `window.eval` | `{windowId, js}` → JSON (respuesta async) |

Eventos kernel→cliente:

```
{"event":"window.event","params":{"windowId":N,"name":"…","payload":…}}
{"event":"sdk.event","params":{…}}
```

Nombres de evento de ventana: `resize move focus blur maximize unmaximize
enterFullScreen leaveFullScreen closeRequested{requestId} closed`.

---

## 5 · API C++ nativa — apps nativas-first (`include/ow/`)

```cpp
#include <ow/App.h>
ow::App::Main(argc, argv, AppOptions{ .id, .name, .version })
ow::App::OnReady(fn)          // crea ventanas aquí
ow::App::Post(fn)             // callback al main loop (thread-safe)
ow::App::Quit(exitCode)

#include <ow/Window.h>
ow::Window w(WindowOptions{
  .title, .width, .height, .minWidth/Height, .maxWidth/Height,
  .resizable, .center, .show, .frameless,
  .titleBarStyle = TitleBarStyle::Default|Hidden|Custom,
  .titleBarOverlay = { color, symbolColor, height },
  .url,                     // http(s):// · app:// · file://
  .webviewArgs              // flags extra del navegador
});
w.Id(); w.Show(); w.Hide(); w.Close(); w.Destroy(); w.Focus();
w.Minimize(); w.Maximize(); w.Unmaximize(); w.Restore();
w.SetFullScreen(bool); w.IsMaximized/Minimized/FullScreen();
w.GetBounds() / w.SetBounds({x,y,w,h}); w.Center();
w.SetTitle(s) / w.Title(); w.SetTitleBarStyle(); w.SetTitleBarOverlay();
w.LoadURL(url);
w.EvalJS(js, cb(resultJson));       // resultado como JSON
w.EmitToJS(name, jsonPayload);      // evento a suscriptores JS
w.On(name, fn(payload)) -> ListenerId;  w.Off(id);
w.NativeHandle();                   // GtkWidget*/HWND/NSView*

#include <ow/Shm.h>                 // memoria compartida (C puro)
const char* ow_shm_put(const uint8_t*, size_t);
const uint8_t* ow_shm_data(const char* id, size_t* len);
void ow_shm_shutdown(void);

#include <ow/Common.h>              // Result<T>, OwBytes, WindowId, NonCopyable
```

Internos del kernel (para contribuidores, en `src/`): `bridge::Codec`,
`Dispatcher`, `ModuleLoader`, `ControlServer`, `NodeManager`, `http`,
`archive::ExtractTarGz`, `crypto::Sha256`, `shm::Put/Data`.

---

## 6 · CLI

```bash
ow create <dir>     # scaffoldea una app desde el template
ow dev              # kernel + vite dev server + sidecar node (hot reload)
ow build            # vite build + native/*.cpp → dist/modules/*.owm
owear-build-native  # compila native/*.cpp → .owm (usado por dev/build/plugin)
```

---

## 7 · Variables de entorno

| Variable | Quién la usa | Qué hace |
|---|---|---|
| `OW_APP_MAIN` | kernel | entry JS del sidecar (modo Electron-like) |
| `OW_DEV_SERVER_URL` | kernel/template | URL inicial de las ventanas en dev |
| `OW_CONTROL_SOCKET` | SDK/sidecar | ruta del socket de control |
| `OW_MODULES_DIR` | kernel | rutas `:` separadas con .owm extra |
| `OW_ASSETS_DIR` | kernel | raíz del scheme `app://` (default `./dist`) |
| `OW_DEMO` | kernel | ventana demo nativa |
| `OW_APP_NAME` / `OW_APP_ID` | kernel | identidad de la app |
| `OW_CLOSE_TIMEOUT_MS` | kernel | timeout del veto de cierre (default 1000) |
| `OW_KERNEL_BIN` | CLI | ruta al binario `owear` |
| `OW_MODULES_OUT` | CLI/plugin | destino de los .owm compilados |
| `OW_INCLUDE_DIR` | CLI | headers del framework para native/*.cpp |
| `XDG_RUNTIME_DIR` / `XDG_CACHE_HOME` | Linux | sockets y cache del runtime node |
