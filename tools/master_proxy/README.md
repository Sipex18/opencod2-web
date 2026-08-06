# CoD2 web masterlist proxy

Browser Join Game cannot use UDP `getservers` / `getinfo`. This tiny HTTP proxy
fetches [api.cod.pm](https://api.cod.pm) server-side and exposes JSON at
`/play/api/masterlist/cod2/1.3` (same origin as the COEP game page).

## VPS setup

```bash
sudo mkdir -p /opt/cod2-master-proxy
sudo cp tools/master_proxy/server.py /opt/cod2-master-proxy/
sudo cp tools/master_proxy/cod2-master-proxy.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now cod2-master-proxy

# Add location /play/api/ from nginx-play-api.conf.snippet (or tools/nginx-cod2-web.conf)
sudo nginx -t && sudo systemctl reload nginx
```

Smoke test:

```bash
curl -sS http://127.0.0.1:20711/health
curl -sS http://127.0.0.1:20711/masterlist/cod2/1.3 | head -c 200
curl -sS https://HOST/play/api/masterlist/cod2/1.3 | head -c 200
```

## Local dev

```bash
python3 tools/master_proxy/server.py
# point Module.COD2_MASTER_URL at http://127.0.0.1:20711/masterlist/cod2/1.3
# (local page without COEP can call it directly; production uses /play/api/)
```
