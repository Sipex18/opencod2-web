#!/usr/bin/env python3
"""WebSocket ↔ UDP relay for CoD2 browser clients.

Each WebSocket connection is bound to one remote CoD2 server endpoint,
specified as query parameters ?host=<ip>&port=<port>.  Binary WS frames
are forwarded as UDP datagrams; UDP replies are forwarded back as WS
binary frames.

The relay runs as a standalone asyncio server (default :20712) behind
nginx, which upgrades /play/api/gamews to this backend.

Security:
  - Only CoD2 server ports 28960-28970 (configurable) are allowed.
  - Each WS session is pinned to exactly one (host, port) pair.
  - Maximum datagram size matches CoD2 MAX_MSGLEN (16384).

Usage:
  python3 tools/game_proxy/relay.py
  # or: RELAY_PORT=20712 python3 relay.py
"""

from __future__ import annotations

import asyncio
import logging
import os
import signal
import struct
import sys
import urllib.parse

try:
    import websockets
    import websockets.server
except ImportError:
    print("ERROR: pip install websockets  (>= 12.0)", file=sys.stderr)
    sys.exit(1)

RELAY_PORT = int(os.environ.get("RELAY_PORT", "20712"))
RELAY_HOST = os.environ.get("RELAY_HOST", "127.0.0.1")

PORT_MIN = int(os.environ.get("COD2_PORT_MIN", "28960"))
PORT_MAX = int(os.environ.get("COD2_PORT_MAX", "29970"))

MAX_DGRAM = 16384
UDP_RECV_TIMEOUT = 0.05

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger("cod2-relay")


def parse_target(path: str, headers) -> tuple[str, int]:
    """Extract host= and port= from the WS request URI query string."""
    qs = urllib.parse.urlparse(path).query
    params = urllib.parse.parse_qs(qs)
    host_list = params.get("host")
    port_list = params.get("port")
    if not host_list or not port_list:
        raise ValueError("missing ?host=&port= query parameters")
    host = host_list[0].strip()
    port = int(port_list[0])
    if not host:
        raise ValueError("empty host")
    if port < PORT_MIN or port > PORT_MAX:
        raise ValueError(f"port {port} outside allowed range {PORT_MIN}-{PORT_MAX}")
    parts = host.split(".")
    if len(parts) != 4 or not all(p.isdigit() and 0 <= int(p) <= 255 for p in parts):
        raise ValueError(f"invalid IPv4: {host}")
    return host, port


async def relay_handler(ws):
    """Handle one browser client → one CoD2 server session."""
    try:
        host, port = parse_target(ws.request.path, ws.request.headers)
    except Exception as exc:
        log.warning("bad handshake: %s", exc)
        await ws.close(4400, str(exc))
        return

    target = (host, port)
    log.info("session open → %s:%d from %s", host, port,
             ws.remote_address if hasattr(ws, 'remote_address') else "?")

    loop = asyncio.get_running_loop()
    udp_transport = None
    udp_protocol = None

    class UdpRelay(asyncio.DatagramProtocol):
        def connection_made(self, transport):
            pass

        def datagram_received(self, data: bytes, addr: tuple):
            if addr[0] != target[0] or addr[1] != target[1]:
                return
            try:
                asyncio.ensure_future(ws.send(data))
            except Exception:
                pass

        def error_received(self, exc):
            log.debug("udp error: %s", exc)

    try:
        udp_transport, udp_protocol = await loop.create_datagram_endpoint(
            UdpRelay, family=0)

        async for message in ws:
            if not isinstance(message, (bytes, bytearray)):
                continue
            if len(message) > MAX_DGRAM:
                log.warning("oversized ws frame %d from %s", len(message),
                            ws.remote_address if hasattr(ws, 'remote_address') else "?")
                continue
            udp_transport.sendto(message, target)

    except websockets.exceptions.ConnectionClosed:
        pass
    except Exception as exc:
        log.error("session error: %s", exc)
    finally:
        if udp_transport:
            udp_transport.close()
        log.info("session closed → %s:%d", host, port)


async def main():
    stop = asyncio.get_running_loop().create_future()

    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            asyncio.get_running_loop().add_signal_handler(sig, stop.set_result, None)
        except NotImplementedError:
            pass

    async with websockets.server.serve(
        relay_handler,
        RELAY_HOST,
        RELAY_PORT,
        max_size=MAX_DGRAM + 256,
        compression=None,
        ping_interval=20,
        ping_timeout=30,
    ):
        log.info("cod2 game relay on ws://%s:%d/  (ports %d-%d)",
                 RELAY_HOST, RELAY_PORT, PORT_MIN, PORT_MAX)
        try:
            await stop
        except asyncio.CancelledError:
            pass

    log.info("relay stopped")


if __name__ == "__main__":
    asyncio.run(main())
