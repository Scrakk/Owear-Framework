#!/usr/bin/env python3
# Copyright 2026 Owear Contributors
# SPDX-License-Identifier: Apache-2.0
#
# tests/e2e/run_suites.py — corre las suites E2E (all/builtins) contra un
# kernel Owear corriendo bajo Xvfb. Usado por CI y localmente.
#
import argparse, http.server, json, os, socket, subprocess, sys, threading, time

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


def run_suite(sock_path, page, timeout_s=60):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(15)
    s.connect(sock_path)
    f = s.makefile("rw")
    ids = [1000]

    def send(cmd, params):
        ids[0] += 1
        f.write(json.dumps({"id": ids[0], "cmd": cmd, "params": params}) + "\n")
        f.flush()
        return ids[0]

    def wait_for(i):
        try:
            while True:
                line = f.readline()
                if not line:
                    return {"eof": True}
                m = json.loads(line)
                if m.get("id") == i:
                    return m
        except socket.timeout:
            return {"timeout": True}

    send("window.create", {"width": 760, "height": 420, "url": page})
    wid = wait_for(ids[0])["result"]["windowId"]
    time.sleep(2.5)

    result = None
    for _ in range(int(timeout_s / 0.5)):
        send("window.eval",
             {"windowId": wid,
              "js": "JSON.stringify({d:window.__done||0,R:window.__R||{}})"})
        m = wait_for(ids[0])
        try:
            data = json.loads(json.loads(m["result"]))
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
        # findInPage bajo Xvfb está degradado: se reporta como warning aparte
        is_find = k == "win.findInPage" or "findInPage" in k
        good = not bad and not ('matches":0' in str(v) and is_find)
        ok += good
        fail += not good
        mark = "✓" if good else ("⚠" if is_find else "✗")
        lines.append(f"{mark} {k:24s} → {str(v)[:70]}")
    lines.append(f"--- {ok} OK / {fail} FALLOS ({page})")
    return ok, fail, lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pages", nargs="+", default=["all.html", "builtins.html"])
    ap.add_argument("--port", type=int, default=8123)
    args = ap.parse_args()

    sock_dir = os.environ.get("XDG_RUNTIME_DIR", "/tmp")
    import glob
    socks = sorted(glob.glob(os.path.join(sock_dir, "owear-*.sock")),
                   key=os.path.getmtime)
    if not socks:
        print("no hay kernel owear corriendo (socket no encontrado)")
        sys.exit(2)
    sock_path = socks[-1]

    serve(args.port)
    total_ok = total_fail = 0
    for page in args.pages:
        ok, fail, lines = run_suite(sock_path, f"http://localhost:{args.port}/{page}")
        total_ok += ok
        total_fail += fail
        print("\n".join(lines))
    print(f"\nTOTAL: {total_ok} OK / {total_fail} FALLOS")
    sys.exit(1 if total_fail else 0)


if __name__ == "__main__":
    main()
