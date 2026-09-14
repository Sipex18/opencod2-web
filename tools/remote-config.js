// VPS variant of src/web/remote-config.js — copy next to cod2.html to enable
// the remote asset pack and the same-origin Join Game endpoints behind nginx.
// tools/deploy_web_to_vps.sh writes this shape automatically.
window.COD2_REMOTE_ASSETS = {
  manifestUrl: "/play/assets.json",
  baseUrl: "/"
};
// Same-origin HTTP master proxy (tools/master_proxy on VPS :20711).
window.COD2_MASTER_URL = "/play/api/masterlist/cod2/1.3";
