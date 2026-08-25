#!/usr/bin/env python3
# Copyright 2026 Owear Contributors
# SPDX-License-Identifier: Apache-2.0
#
# tests/quit_windows.py — pide app.quit por la pipe de control y verifica
# que el kernel sale limpio. Usado por CI al final del smoke Windows.
#
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
k32.PeekNamedPipe.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32,
                              ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32),
                              ctypes.c_void_p]

GENERIC_RW = 0x80000000 | 0x40000000
OPEN_EXISTING = 3
INVALID_HANDLE = (1 << (8 * ctypes.sizeof(ctypes.c_void_p))) - 1

pid = int(sys.argv[1])
pipe_path = rf"\\.\pipe\owear-{pid}"

deadline = time.time() + 10
h = None
while time.time() < deadline and h is None:
    h = k32.CreateFileW(pipe_path, GENERIC_RW, 0, None, OPEN_EXISTING, 0, None)
    if h in (0, INVALID_HANDLE):
        h = None
        time.sleep(0.2)
if h is None:
    print("[quit] pipe no encontrada", file=sys.stderr)
    sys.exit(2)


def write_all(data: bytes):
    off = 0
    while off < len(data):
        n = ctypes.c_uint32(0)
        if not k32.WriteFile(h, data[off:], len(data) - off,
                             ctypes.byref(n), None) or n.value == 0:
            print("[quit] WriteFile falló", file=sys.stderr)
            sys.exit(3)
        off += n.value


def read_line(timeout=10) -> bytes:
    buf = ctypes.create_string_buffer(4096)
    acc = b""
    deadline = time.time() + timeout
    while time.time() < deadline:
        avail = ctypes.c_uint32(0)
        if not k32.PeekNamedPipe(h, None, 0, None, ctypes.byref(avail), None):
            return b""  # cerrada: tras app.quit es lo esperado
        if avail.value == 0:
            time.sleep(0.05)
            continue
        n = ctypes.c_uint32(0)
        if not k32.ReadFile(h, buf, 4096, ctypes.byref(n), None) or n.value == 0:
            return b""
        acc += buf.raw[:n.value]
        i = acc.find(b"\n")
        if i >= 0:
            return acc[:i]
    return b""


write_all(json.dumps({"id": 900, "cmd": "app.quit"}).encode() + b"\n")
r = read_line()
try:
    ok = json.loads(r.decode()).get("ok")
except Exception:
    ok = False
print(f"[quit] respuesta ok={ok}")
sys.exit(0 if ok else 5)
