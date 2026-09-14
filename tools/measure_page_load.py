#!/usr/bin/env python3
"""Measure how long the web client takes to come up, without a human watching.

Drives a headless Chromium over the DevTools protocol: navigates to the page,
waits for the shell to report the runtime as ready, then prints the navigation
timing and the shell's own boot log (every line carries an ISO timestamp).

Usage:
  python tools/measure_page_load.py [url] [settle-seconds]
  COD2_BROWSER="C:/path/to/msedge.exe" python tools/measure_page_load.py

Requires the page to be served already, e.g. tools/serve_web_local.py.
"""
from __future__ import annotations

import asyncio
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

try:
    import websockets
except ImportError:
    print("pip install websockets", file=sys.stderr)
    raise SystemExit(1)

DEFAULT_URL = "http://127.0.0.1:8080/cod2.html"

CANDIDATES = (
    os.environ.get("COD2_BROWSER", ""),
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
    "/usr/bin/chromium",
    "/usr/bin/google-chrome",
)


def find_browser() -> str:
    for path in CANDIDATES:
        if path and os.path.isfile(path):
            return path
    raise SystemExit("no Chromium found; set COD2_BROWSER")


def free_port() -> int:
    sock = socket.socket()
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return port


async def drive(port: int, url: str, settle: float):
    targets = json.load(urllib.request.urlopen("http://127.0.0.1:%d/json/list" % port))
    pages = [t for t in targets if t.get("type") == "page"]
    if not pages:
        return [], {}

    console: list[str] = []
    result: dict = {}
    async with websockets.connect(pages[0]["webSocketDebuggerUrl"],
                                  max_size=None, open_timeout=20) as ws:
        msg_id = 0

        async def send(method: str, params: dict | None = None) -> int:
            nonlocal msg_id
            msg_id += 1
            await ws.send(json.dumps({"id": msg_id, "method": method,
                                      "params": params or {}}))
            return msg_id

        async def evaluate(expr: str, timeout: float = 5.0):
            rid = await send("Runtime.evaluate", {"expression": expr,
                                                  "returnByValue": True})
            deadline = time.time() + timeout
            while time.time() < deadline:
                try:
                    raw = await asyncio.wait_for(ws.recv(), timeout=2.0)
                except asyncio.TimeoutError:
                    break
                msg = json.loads(raw)
                if msg.get("id") == rid:
                    return (msg.get("result") or {}).get("result", {}).get("value")
            return None

        await send("Runtime.enable")
        await send("Log.enable")
        await send("Page.enable")
        await send("Page.navigate", {"url": url})

        deadline = time.time() + settle
        while time.time() < deadline:
            try:
                raw = await asyncio.wait_for(ws.recv(), timeout=2.0)
            except asyncio.TimeoutError:
                continue
            msg = json.loads(raw)
            method = msg.get("method")
            params = msg.get("params") or {}
            if method == "Runtime.consoleAPICalled":
                text = " ".join(str(a.get("value", a.get("description", "")))
                                for a in params.get("args", []))
                console.append("%-6s %s" % (params.get("type", "?"), text[:220]))
            elif method == "Log.entryAdded":
                entry = params.get("entry") or {}
                console.append("%-6s %s" % (entry.get("level", "?"),
                                            str(entry.get("text"))[:220]))

        result["timing"] = await evaluate("JSON.stringify(performance.timing.toJSON())")
        result["bootlog"] = await evaluate(
            "(typeof __cod2GetLog === 'function') ? __cod2GetLog() : ''")
    return console, result


def main() -> int:
    url = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_URL
    settle = float(sys.argv[2]) if len(sys.argv) > 2 else 25.0

    port = free_port()
    profile = tempfile.mkdtemp(prefix="cod2measure")
    proc = subprocess.Popen([
        find_browser(), "--headless=new", "--disable-gpu", "--no-first-run",
        "--no-default-browser-check", "--autoplay-policy=no-user-gesture-required",
        "--remote-debugging-port=%d" % port,
        "--user-data-dir=%s" % profile, "about:blank",
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(60):
            try:
                urllib.request.urlopen("http://127.0.0.1:%d/json/version" % port, timeout=1)
                break
            except OSError:
                time.sleep(0.25)
        else:
            print("browser did not expose the debug port", file=sys.stderr)
            return 1
        console, result = asyncio.run(drive(port, url, settle))
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
        shutil.rmtree(profile, ignore_errors=True)

    print("=== console (%d) ===" % len(console))
    for line in console[:60]:
        print("  " + line)

    if result.get("bootlog"):
        print("=== boot log ===")
        for line in str(result["bootlog"]).splitlines()[:80]:
            print("  " + line)

    if result.get("timing"):
        timing = json.loads(result["timing"])
        base = timing.get("navigationStart") or 0
        print("=== navigation ===")
        for key in ("responseStart", "domContentLoadedEventStart",
                    "domContentLoadedEventEnd", "loadEventStart", "loadEventEnd"):
            value = timing.get(key)
            if value:
                print("  %-26s %7.0f ms" % (key, value - base))
    return 0


if __name__ == "__main__":
    sys.exit(main())
