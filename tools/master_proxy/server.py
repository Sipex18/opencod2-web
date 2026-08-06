#!/usr/bin/env python3
"""Same-origin HTTP masterlist proxy for CoD2 web Join Game.

Browsers cannot UDP-query cod2master / getinfo, and api.cod.pm has no CORS
(and COEP require-corp blocks cross-origin). This process runs on the VPS and
is reverse-proxied at /play/api/ → http://127.0.0.1:20711/.

The upstream masterlist carries no usable latency: the web client used to
average the connected players' pings, which is their latency to the server, and
999 whenever a server was empty. So this proxy also measures the real
VPS→server round trip with connectionless UDP `getinfo` sweeps and injects it as
`ping_ms`. Because a browser has no UDP, a joining client's traffic has to pass
through this host anyway, which makes VPS→server the meaningful half of the
latency to display.

Usage:
  python3 tools/master_proxy/server.py
  # or: PORT=20711 python3 server.py
"""

from __future__ import annotations

import json
import os
import re
import select
import socket
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

UPSTREAM = os.environ.get("COD2_MASTER_UPSTREAM", "https://api.cod.pm").rstrip("/")
PORT = int(os.environ.get("PORT", "20711"))
CACHE_TTL = float(os.environ.get("COD2_MASTER_CACHE_TTL", "30"))

# One sweep = one getinfo to every known server, then collect for RESPONSE_WINDOW.
PING_INTERVAL = float(os.environ.get("COD2_PING_INTERVAL", "2.0"))
PING_RESPONSE_WINDOW = float(os.environ.get("COD2_PING_WINDOW", "1.2"))
# Servers that never answer keep this value rather than a stale measurement.
PING_UNREACHABLE = 999
# Drop a measurement that is older than this so the UI cannot show a dead value.
PING_MAX_AGE = float(os.environ.get("COD2_PING_MAX_AGE", "20"))

_PATH_RE = re.compile(r"^/(masterlist|pings)/(cod2|cod|coduo|cod4)(?:/(1\.\d))?/?$")

_cache_lock = threading.Lock()
_cache: dict[str, tuple[float, bytes, str]] = {}


class Pinger:
    """Measures VPS→server RTT for every address seen in a served masterlist."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._targets: dict[tuple[str, int], float] = {}
        self._results: dict[tuple[str, int], tuple[int, float]] = {}
        self._sweeps = 0

    def note_targets(self, addrs: list[tuple[str, int]]) -> None:
        now = time.time()
        with self._lock:
            for addr in addrs:
                self._targets[addr] = now

    def snapshot(self) -> dict[str, int]:
        cutoff = time.time() - PING_MAX_AGE
        with self._lock:
            return {
                "%s:%d" % addr: ms
                for addr, (ms, when) in self._results.items()
                if when >= cutoff
            }

    def stats(self) -> dict[str, int]:
        with self._lock:
            return {
                "targets": len(self._targets),
                "measured": len(self._results),
                "sweeps": self._sweeps,
            }

    def run_forever(self) -> None:
        while True:
            started = time.time()
            try:
                self._sweep()
            except Exception as exc:  # noqa: BLE001 — a sweep must never kill the thread
                print("ping sweep failed: %s" % exc, flush=True)
            sleep_for = PING_INTERVAL - (time.time() - started)
            if sleep_for > 0:
                time.sleep(sleep_for)

    def _sweep(self) -> None:
        with self._lock:
            targets = list(self._targets)
        if not targets:
            time.sleep(PING_INTERVAL)
            return

        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setblocking(False)
        try:
            # CoD2 connectionless packet: 0xFFFFFFFF + command. The challenge is
            # echoed back in infoResponse, but matching on the source address is
            # enough here and keeps one packet per server.
            payload = b"\xff\xff\xff\xffgetinfo cod2web"
            sent_at: dict[tuple[str, int], float] = {}
            for addr in targets:
                try:
                    sock.sendto(payload, addr)
                except OSError:
                    continue
                sent_at[addr] = time.perf_counter()

            answered: set[tuple[str, int]] = set()
            measured: dict[tuple[str, int], int] = {}
            deadline = time.perf_counter() + PING_RESPONSE_WINDOW
            while True:
                remaining = deadline - time.perf_counter()
                if remaining <= 0:
                    break
                if not select.select([sock], [], [], remaining)[0]:
                    break
                try:
                    _, src = sock.recvfrom(2048)
                except OSError:
                    break
                start = sent_at.get(src)
                if start is None or src in answered:
                    continue
                answered.add(src)
                rtt = int(round((time.perf_counter() - start) * 1000.0))
                measured[src] = min(max(rtt, 1), 998)
        finally:
            sock.close()

        now = time.time()
        with self._lock:
            for addr in sent_at:
                if addr in measured:
                    self._results[addr] = (measured[addr], now)
                else:
                    self._results[addr] = (PING_UNREACHABLE, now)
            # Forget servers that dropped out of the masterlist entirely.
            for addr in list(self._results):
                if addr not in self._targets:
                    del self._results[addr]
            self._sweeps += 1


_pinger = Pinger()


def _fetch_upstream(game: str, ver: str) -> tuple[bytes, str]:
    key = f"{game}/{ver}"
    now = time.time()
    with _cache_lock:
        hit = _cache.get(key)
        if hit and now - hit[0] < CACHE_TTL:
            return hit[1], hit[2]

    url = f"{UPSTREAM}/masterlist/{game}/{ver}"
    req = urllib.request.Request(
        url,
        headers={
            "User-Agent": "cod2-web-master-proxy/1.0",
            "Accept": "application/json",
        },
        method="GET",
    )
    with urllib.request.urlopen(req, timeout=25) as resp:
        body = resp.read()
        ctype = resp.headers.get("Content-Type", "application/json;charset=utf-8")

    try:
        data = json.loads(body.decode("utf-8", errors="replace"))
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"upstream non-JSON: {exc}") from exc
    if not isinstance(data, dict) or "servers" not in data:
        raise RuntimeError("upstream missing servers[]")

    body = _inject_pings(data)

    with _cache_lock:
        _cache[key] = (now, body, ctype)
    return body, ctype


def _inject_pings(data: dict) -> bytes:
    """Registers every address for probing and stamps known RTTs onto the list."""
    addrs: list[tuple[str, int]] = []
    servers = data.get("servers") or []
    for srv in servers:
        if not isinstance(srv, dict):
            continue
        ip = srv.get("ip")
        try:
            port = int(srv.get("port"))
        except (TypeError, ValueError):
            continue
        if not ip or not 0 < port < 65536:
            continue
        addrs.append((str(ip), port))

    _pinger.note_targets(addrs)
    known = _pinger.snapshot()

    for srv in servers:
        if not isinstance(srv, dict):
            continue
        ms = known.get("%s:%s" % (srv.get("ip"), srv.get("port")))
        if ms is not None:
            srv["ping_ms"] = ms

    return json.dumps(data, separators=(",", ":")).encode("utf-8")


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt: str, *args) -> None:
        sys_stderr = __import__("sys").stderr
        sys_stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    def _send(self, code: int, body: bytes, ctype: str) -> None:
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self) -> None:
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_GET(self) -> None:
        path = self.path.split("?", 1)[0]
        if path in ("/", "/health"):
            body = json.dumps({"ok": 1, "service": "cod2-master-proxy",
                               "ping": _pinger.stats()}).encode("utf-8")
            self._send(200, body, "application/json")
            return

        m = _PATH_RE.match(path)
        if not m:
            self._send(404, b'{"error":1,"description":"invalid route"}\n', "application/json")
            return

        kind = m.group(1)
        game = m.group(2)
        ver = m.group(3) or "1.3"

        if kind == "pings":
            # Cheap poll route so the client can refresh latency without
            # rebuilding its whole server list.
            body = json.dumps({"pings": _pinger.snapshot()},
                              separators=(",", ":")).encode("utf-8")
            self._send(200, body, "application/json")
            return

        try:
            body, ctype = _fetch_upstream(game, ver)
            self._send(200, body, ctype)
        except Exception as exc:  # noqa: BLE001 — surface to client JSON
            err = json.dumps({"error": 1, "description": str(exc)}).encode("utf-8")
            self._send(502, err, "application/json")


def main() -> None:
    threading.Thread(target=_pinger.run_forever, name="pinger", daemon=True).start()
    server = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    print(f"cod2 master proxy on http://127.0.0.1:{PORT}/  upstream={UPSTREAM}"
          f"  ping every {PING_INTERVAL}s", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
