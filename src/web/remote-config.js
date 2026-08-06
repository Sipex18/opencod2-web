// Optional override. On VPS deploy this file sets remote assets.
// Locally, missing or empty config falls back to folder picker / assets.json probe.
// window.COD2_REMOTE_ASSETS = { manifestUrl: '/play/assets.json', baseUrl: '/' };
//
// Join Game masterlist (same-origin proxy; see tools/master_proxy/):
// window.COD2_MASTER_URL = '/play/api/masterlist/cod2/1.3';
//
// WebSocket↔UDP game connect relay (see tools/game_proxy/):
// window.COD2_RELAY_URL = 'wss://' + location.host + '/play/api/gamews';
