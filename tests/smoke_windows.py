#!/usr/bin/env python3
# Smoke E2E de Windows (runner CI): habla NDJSON con el control socket del
# kernel por el named pipe \\.\pipe\owear-<pid>:
#   1) espera la pipe · 2) window.create con URL data: (WebView2 REAL)
#   3) deja renderizar · 4) app.quit y exige salida limpia (código 0).
# Uso: python smoke_windows.py <pid_del_kernel>
import json
import sys
import time

pid = int(sys.argv[1])
pipe_path = rf"\\.\pipe\owear-{pid}"


def connect(timeout=20):
    deadline = time.time() + timeout
    last_err = None
    while time.time() < deadline:
        try:
            # CON buffer: readline sobre pipe crudo da EINVAL en Windows
            return open(pipe_path, "r+b")
        except OSError as e:  # la pipe aún no existe
            last_err = e
            time.sleep(0.2)
    print(f"[smoke] pipe no apareció: {last_err}", file=sys.stderr)
    sys.exit(2)


def rpc(f, req_id, cmd, params=None):
    line = json.dumps({"id": req_id, "cmd": cmd,
                       "params": params or {}}).encode() + b"\n"
    f.write(line)
    f.flush()
    raw = f.readline()
    if not raw:
        print("[smoke] pipe cerrada sin respuesta", file=sys.stderr)
        sys.exit(3)
    return json.loads(raw.decode())


f = connect()

r = rpc(f, 1, "window.create",
        {"title": "owear-smoke",
         "url": "data:text/html,<h1>owear smoke</h1>",
         "width": 800, "height": 600})
if not r.get("ok"):
    print(f"[smoke] window.create falló: {r}", file=sys.stderr)
    sys.exit(4)
wid = r.get("result", {}).get("windowId")
print(f"[smoke] ventana creada id={wid}")

time.sleep(4)  # environment+controller+navigation reales de WebView2

r = rpc(f, 2, "app.quit")
print(f"[smoke] app.quit → {r}")
sys.exit(0)
