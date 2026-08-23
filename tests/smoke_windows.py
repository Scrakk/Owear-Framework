#!/usr/bin/env python3
# Smoke E2E de Windows (runner CI): habla NDJSON con el control socket del
# kernel por el named pipe \\.\pipe\owear-<pid>, vía WinAPI directa con
# ctypes (la capa CRT/open() da EINVAL leyendo pipes aquí).
#   1) espera la pipe · 2) window.create con URL data: (WebView2 REAL)
#   3) deja renderizar · 4) app.quit y exige salida limpia (código 0).
# Uso: python smoke_windows.py <pid_del_kernel>
import ctypes
import json
import sys
import time

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.CreateFileW.restype = ctypes.c_void_p
k32.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                            ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32,
                            ctypes.c_void_p]
k32.ReadFile.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32,
                         ctypes.POINTER(ctypes.c_uint32), ctypes.c_void_p]
k32.WriteFile.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint32,
                          ctypes.POINTER(ctypes.c_uint32), ctypes.c_void_p]

GENERIC_RW = 0x80000000 | 0x40000000
OPEN_EXISTING = 3
INVALID_HANDLE = (1 << (8 * ctypes.sizeof(ctypes.c_void_p))) - 1

pid = int(sys.argv[1])
pipe_path = rf"\\.\pipe\owear-{pid}"


def connect(timeout=20):
    deadline = time.time() + timeout
    last_err = 0
    while time.time() < deadline:
        h = k32.CreateFileW(pipe_path, GENERIC_RW, 0, None,
                            OPEN_EXISTING, 0, None)
        if h not in (0, INVALID_HANDLE):
            return h
        last_err = ctypes.get_last_error()
        time.sleep(0.2)
    print(f"[smoke] pipe no apareció (GetLastError={last_err})", file=sys.stderr)
    sys.exit(2)


def write_all(h, data: bytes):
    off = 0
    while off < len(data):
        n = ctypes.c_uint32(0)
        if not k32.WriteFile(h, data[off:], len(data) - off,
                             ctypes.byref(n), None) or n.value == 0:
            print("[smoke] WriteFile falló", file=sys.stderr)
            sys.exit(3)
        off += n.value


def read_line(h) -> bytes:
    buf = ctypes.create_string_buffer(4096)
    acc = b""
    while True:
        n = ctypes.c_uint32(0)
        if not k32.ReadFile(h, buf, 4096, ctypes.byref(n), None) or n.value == 0:
            print("[smoke] ReadFile falló o conexión cerrada",
                  file=sys.stderr)
            sys.exit(4)
        acc += buf.raw[:n.value]
        i = acc.find(b"\n")
        if i >= 0:
            return acc[:i]


def rpc(f, req_id, cmd, params=None):
    write_all(f, json.dumps({"id": req_id, "cmd": cmd,
                             "params": params or {}}).encode() + b"\n")
    return json.loads(read_line(f).decode())


h = connect()

r = rpc(h, 1, "window.create",
        {"title": "owear-smoke",
         "url": "data:text/html,<h1>owear smoke</h1>",
         "width": 800, "height": 600})
if not r.get("ok"):
    print(f"[smoke] window.create falló: {r}", file=sys.stderr)
    sys.exit(5)
wid = r.get("result", {}).get("windowId")
print(f"[smoke] ventana creada id={wid}")

time.sleep(4)  # environment+controller+navigation reales de WebView2

r = rpc(h, 2, "app.quit")
print(f"[smoke] app.quit → {r}")
sys.exit(0)

