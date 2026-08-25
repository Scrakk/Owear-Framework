#!/usr/bin/env python3
# Copyright 2026 Owear Contributors
# SPDX-License-Identifier: Apache-2.0
#
# tests/e2e/owconn.py — cliente NDJSON del control socket de Owear,
# multiplataforma: UDS (Linux/macOS) o named pipe (Windows) detrás de la
# misma interfaz line-reader. Usado por run_suites.py.
#
import os
import sys
import time


class LineConn:
    """Conexión NDJSON: write_line(bytes) + read_line(timeout) -> bytes."""

    def write_line(self, data: bytes) -> None:
        raise NotImplementedError

    def read_line(self, timeout_s: float = 15.0) -> bytes:
        raise NotImplementedError

    def close(self) -> None:
        pass


def find_kernel_conn(wait_s: float = 30.0) -> "LineConn":
    """Descubre el control socket del kernel y conecta."""
    if os.name == "nt":
        return _find_win(wait_s)
    return _find_unix(wait_s)


# ── POSIX: UDS en $XDG_RUNTIME_DIR/owear-<pid>.sock ──────────────────────────
def _find_unix(wait_s: float) -> "LineConn":
    import glob
    import socket

    sock_dirs = [os.environ.get("XDG_RUNTIME_DIR", ""),
                 os.environ.get("TMPDIR", ""), "/tmp"]
    deadline = time.time() + wait_s
    while time.time() < deadline:
        socks = []
        for d in sock_dirs:
            if d and os.path.isdir(d):
                socks += glob.glob(os.path.join(d, "owear-*.sock"))
        socks = sorted(socks, key=os.path.getmtime)
        if socks:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(socks[-1])
            s.setblocking(True)
            acc = bytearray()

            class Uds(LineConn):
                def write_line(self, data):
                    s.sendall(data + b"\n")

                def read_line(self, timeout_s=15.0):
                    # recv con timeout por operación: sin makefile (un timeout
                    # envenena el buffer del file object y el siguiente
                    # readline lanza OSError — mordido en CI).
                    nonlocal acc
                    deadline = time.time() + timeout_s
                    while b"\n" not in acc:
                        if time.time() > deadline:
                            return b""
                        s.settimeout(max(0.1, deadline - time.time()))
                        try:
                            chunk = s.recv(8192)
                        except socket.timeout:
                            return b""
                        except OSError:
                            return b""
                        if not chunk:
                            return b""
                        acc += chunk
                    i = acc.index(b"\n")
                    line = bytes(acc[:i])
                    del acc[:i + 1]
                    return line

                def close(self):
                    try:
                        s.close()
                    except OSError:
                        pass

            return Uds()
        time.sleep(0.3)
    print("no hay kernel owear corriendo (socket no encontrado)")
    sys.exit(2)


# ── Windows: named pipe \\.\pipe\owear-<pid> vía WinAPI directa ─────────────
# La capa CRT open()/readline da EINVAL sobre pipes aquí (mordido): todo va
# por CreateFileW/PeekNamedPipe/ReadFile/WriteFile con ctypes.
def _find_win(wait_s: float) -> "LineConn":
    import ctypes

    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.CreateFileW.restype = ctypes.c_void_p
    k32.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32,
                                ctypes.c_uint32, ctypes.c_void_p,
                                ctypes.c_uint32, ctypes.c_uint32,
                                ctypes.c_void_p]
    for fn in ("ReadFile", "WriteFile"):
        setattr(k32, fn + ".argtypes", None)
    k32.ReadFile.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32,
                             ctypes.POINTER(ctypes.c_uint32), ctypes.c_void_p]
    k32.WriteFile.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint32,
                              ctypes.POINTER(ctypes.c_uint32), ctypes.c_void_p]
    k32.PeekNamedPipe.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                  ctypes.c_uint32, ctypes.c_void_p,
                                  ctypes.POINTER(ctypes.c_uint32),
                                  ctypes.c_void_p]

    GENERIC_RW = 0x80000000 | 0x40000000
    OPEN_EXISTING = 3
    INVALID = (1 << (8 * ctypes.sizeof(ctypes.c_void_p))) - 1

    # descubre el pid del kernel entre los pipes owear-* existentes probando
    pids = _win_kernel_pids()
    deadline = time.time() + wait_s
    h = None
    while time.time() < deadline and h is None:
        candidates = pids() if callable(pids) else pids
        for pid in candidates:
            path = rf"\\.\pipe\owear-{pid}"
            hh = k32.CreateFileW(path, GENERIC_RW, 0, None, OPEN_EXISTING,
                                 0, None)
            if hh not in (0, INVALID):
                h = hh
                break
        if h is None:
            time.sleep(0.3)
    if h is None:
        print("no hay kernel owear corriendo (pipe no encontrada)")
        sys.exit(2)

    class Pipe(LineConn):
        def __init__(self, handle):
            self.h = handle

        def write_line(self, data):
            off = 0
            buf = data + b"\n"
            while off < len(buf):
                n = ctypes.c_uint32(0)
                if not k32.WriteFile(self.h, buf[off:], len(buf) - off,
                                     ctypes.byref(n), None) or n.value == 0:
                    raise IOError("WriteFile falló en la pipe de control")
                off += n.value

        def read_line(self, timeout_s=15.0):
            deadline = time.time() + timeout_s
            acc = b""
            buf = ctypes.create_string_buffer(8192)
            while time.time() < deadline:
                avail = ctypes.c_uint32(0)
                if not k32.PeekNamedPipe(self.h, None, 0, None,
                                         ctypes.byref(avail), None):
                    return b""  # pipe cerrada por el kernel
                if avail.value == 0:
                    time.sleep(0.05)
                    continue
                n = ctypes.c_uint32(0)
                if not k32.ReadFile(self.h, buf, 8192, ctypes.byref(n), None) \
                        or n.value == 0:
                    return b""
                acc += buf.raw[:n.value]
                i = acc.find(b"\n")
                if i >= 0:
                    return acc[:i]
            return b""

        def close(self):
            k32.CloseHandle(self.h)

    return Pipe(h)


def _win_kernel_pids():
    """PIDs candidatos: procesos owear.exe vivos (vía toolhelp snapshot)."""
    import ctypes

    k32 = ctypes.WinDLL("kernel32", use_last_error=True)

    class PROCESSENTRY32W(ctypes.Structure):
        _fields_ = [("dwSize", ctypes.c_uint32),
                    ("cntUsage", ctypes.c_uint32),
                    ("th32ProcessID", ctypes.c_uint32),
                    ("th32DefaultHeapID", ctypes.POINTER(ctypes.c_uint)),
                    ("th32ModuleID", ctypes.c_uint32),
                    ("cntThreads", ctypes.c_uint32),
                    ("th32ParentProcessID", ctypes.c_uint32),
                    ("pcPriClassBase", ctypes.c_long),
                    ("dwFlags", ctypes.c_uint32),
                    ("szExeFile", ctypes.c_wchar * 260)]

    TH32CS_SNAPPROCESS = 0x2

    def snap():
        k32.CreateToolhelp32Snapshot.restype = ctypes.c_void_p
        k32.CreateToolhelp32Snapshot.argtypes = [ctypes.c_uint32, ctypes.c_uint32]
        k32.Process32FirstW.argtypes = [ctypes.c_void_p, ctypes.POINTER(PROCESSENTRY32W)]
        k32.Process32NextW.argtypes = [ctypes.c_void_p, ctypes.POINTER(PROCESSENTRY32W)]
        h = k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
        if not h or h == (1 << (8 * ctypes.sizeof(ctypes.c_void_p))) - 1:
            return []
        out = []
        e = PROCESSENTRY32W()
        e.dwSize = ctypes.sizeof(PROCESSENTRY32W)
        ok = k32.Process32FirstW(h, ctypes.byref(e))
        while ok:
            if e.szExeFile.lower().startswith("owear"):
                out.append(e.th32ProcessID)
            ok = k32.Process32NextW(h, ctypes.byref(e))
        k32.CloseHandle(h)
        return out

    return snap
