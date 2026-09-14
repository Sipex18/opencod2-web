# CoD2 web game relay (WebSocket ↔ UDP)

Browser WASM has no UDP sockets. This relay bridges each browser client's
WebSocket to a single CoD2 game server via UDP datagrams, enabling the full
connect → challenge → gameplay packet flow.

## Architecture

```
Browser (WS binary)  ──→  nginx /play/api/gamews  ──→  relay.py :20712
                                                          │
                                                          ↕ UDP
                                                     CoD2 server :28960
```

Each WebSocket connection is pinned to one `(host, port)` pair, passed as
query parameters: `wss://HOST/play/api/gamews?host=1.2.3.4&port=28960`.

## VPS setup

```bash
sudo mkdir -p /opt/cod2-game-relay
sudo cp tools/game_proxy/relay.py /opt/cod2-game-relay/
sudo pip3 install websockets
sudo cp tools/game_proxy/cod2-game-relay.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now cod2-game-relay

# Add the location block from nginx-gamews.conf.snippet
sudo nginx -t && sudo systemctl reload nginx
```

Smoke test:

```bash
# Check service
systemctl status cod2-game-relay

# Quick WS connect (requires wscat or websocat)
websocat "ws://127.0.0.1:20712/?host=1.2.3.4&port=28960" --binary
```

## Security

- Only IPv4 numeric addresses accepted (no DNS, no SSRF).
- Port range restricted to 28960-29970 (CoD2 default range).
- Each session bound to one server; cannot be redirected mid-session.

## Local dev

Preferred (static web + master + this relay, same-origin `/play/api/gamews`):

```bash
pip install websockets
python tools/serve_web_local.py
# open http://127.0.0.1:8080/cod2.html
```

Standalone:

```bash
pip install websockets
python3 tools/game_proxy/relay.py
# Browser JS will connect to ws://localhost:20712/?host=...&port=...
```
