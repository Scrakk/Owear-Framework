#!/usr/bin/env python3
# Copyright 2026 Owear Contributors
# SPDX-License-Identifier: Apache-2.0
#
# tests/e2e/run_suites.py — corre las suites E2E (all/builtins) contra un
# kernel Owear corriendo localmente. Multiplataforma: UDS (Linux/macOS) o
# named pipe (Windows) vía tests/e2e/owconn.py.
#
import argparse, http.server, json, os, sys, threading, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import owconn

HERE = os.path.dirname(os.path.abspath(__file__))
WWW = os.path.join(HERE, "www")


def serve(port):
    os.chdir(WWW)
    handler = lambda *a: http.server.SimpleHTTPRequestHandler(*a)
    try:
        srv = http.server.ThreadingHTTPServer(("127.0.0.1", port), handler)
        threading.Thread(target=srv.serve_forever, daemon=True).start()
    except OSError:
        # ya hay un servidor en ese puerto: sirve nuestras páginas, lo reutilizamos
        import urllib.request
        with urllib.request.urlopen(
                f"http://127.0.0.1:{port}/all.html", timeout=3) as r:
            assert r.status == 200


def run_suite(conn, page, timeout_s=60):
    ids = [1000]

    def send(cmd, params):
        ids[0] += 1
        conn.write_line(json.dumps({"id": ids[0], "cmd": cmd,
                                    "params": params}).encode())

    def wait_for(i):
        deadline = time.time() + 15
        while time.time() < deadline:
            line = conn.read_line(timeout_s=15)
            if not line:
                return {"eof": True}
            try:
                m = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(m, dict) and m.get("id") == i:
                return m
        return {"timeout": True}

    send("window.create", {"width": 760, "height": 420, "url": page})
    m = wait_for(ids[0])
    wid = (m.get("result") or {}).get("windowId")
    if not wid:
        print(f"FALLO creando ventana para {page}: {m}")
        return 0, 1, [f"FAIL window.create {page}"]
    time.sleep(2.5)

    result = None
    for _ in range(int(timeout_s / 0.5)):
        send("window.eval",
             {"windowId": wid,
              "js": "JSON.stringify({d:window.__done||0,R:window.__R||{}})"})
        m = wait_for(ids[0])
        try:
            raw = m.get("result") if isinstance(m, dict) else None
            data = json.loads(json.loads(raw)) if isinstance(raw, str) else {}
            if data.get("d") == 1:
                result = data["R"]
                break
        except Exception:
            pass
        time.sleep(0.5)

    ok = fail = 0
    lines = []
    for k, v in sorted((result or {}).items()):
        bad = str(v).startswith(("ERR", "timeout", "FAIL"))
        # findInPage en entornos sin aceleración está degradado: warning aparte
        is_find = k == "win.findInPage" or "findInPage" in k
        good = not bad and not ('matches":0' in str(v) and is_find)
        ok += good
        fail += not good
        mark = "OK" if good else ("WARN" if is_find else "FAIL")
        lines.append(f"{mark:4s} {k:24s} -> {str(v)[:70]}")
    lines.append(f"--- {ok} OK / {fail} FALLOS ({page})")
    return ok, fail, lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pages", nargs="+",
                    default=["all.html", "builtins.html"])
    ap.add_argument("--port", type=int, default=8123)
    ap.add_argument("--pid", type=int, default=0,
                    help="PID del kernel (Windows: pipe directa)")
    args = ap.parse_args()

    conn = owconn.find_kernel_conn(pid=args.pid)

    serve(args.port)
    total_ok = total_fail = 0
    for page in args.pages:
        ok, fail, lines = run_suite(conn, f"http://localhost:{args.port}/{page}")
        total_ok += ok
        total_fail += fail
        print("\n".join(lines))
    print(f"\nTOTAL: {total_ok} OK / {total_fail} FALLOS")
    sys.exit(1 if total_fail else 0)


if __name__ == "__main__":
    main()
