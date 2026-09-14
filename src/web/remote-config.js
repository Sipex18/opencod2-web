// Optional override. On VPS deploy this file sets remote assets.
// Locally, missing or empty config falls back to folder picker / assets.json probe.
// window.COD2_REMOTE_ASSETS = { manifestUrl: '/play/assets.json', baseUrl: '/' };
//
// Same-origin Join Game endpoints. tools/serve_web_local.py (and VPS nginx)
// expose these at the page origin — no VPS required for local play.
window.COD2_MASTER_URL = '/play/api/masterlist/cod2/1.3';
window.COD2_RELAY_URL = (typeof location !== 'undefined' && location.protocol === 'https:' ? 'wss://' : 'ws://')
  + (typeof location !== 'undefined' && location.host ? location.host : '127.0.0.1:8080')
  + '/play/api/gamews';

// Cheap launcher connectivity line (does not touch in-game Join Game menus).
(function probeJoinGameServices() {
  if (typeof document === 'undefined' || typeof fetch !== 'function') return;

  function paint(text) {
    var el = document.getElementById('mpStatus');
    if (!el) {
      var host = document.getElementById('diag') || document.getElementById('panel');
      if (!host) return;
      el = document.createElement('div');
      el.id = 'mpStatus';
      el.style.cssText = 'margin-top:8px;font-size:12px;color:#5a5648';
      if (host.parentNode) host.parentNode.insertBefore(el, host.nextSibling);
      else host.appendChild(el);
    }
    el.textContent = text;
  }

  function fromDevStatus(data) {
    var m = data && data.master && data.master.ok ? 'ok' : 'down';
    var r = data && data.relay && data.relay.ok ? 'ok' : 'down';
    paint('Join Game: master ' + m + ' · relay ' + r);
  }

  function run() {
    paint('Join Game: checking…');
    fetch('/play/api/dev-status', { credentials: 'same-origin', cache: 'no-cache' })
      .then(function (res) {
        if (!res.ok) throw new Error('HTTP ' + res.status);
        return res.json();
      })
      .then(fromDevStatus)
      .catch(function () {
        fetch('/play/api/health', { credentials: 'same-origin', cache: 'no-cache' })
          .then(function (res) {
            paint(res.ok
              ? 'Join Game: master ok · relay ?'
              : 'Join Game: master down — run python tools/serve_web_local.py');
          })
          .catch(function () {
            paint('Join Game: master/relay unreachable — run python tools/serve_web_local.py');
          });
      });
  }

  if (document.readyState === 'loading')
    document.addEventListener('DOMContentLoaded', run);
  else
    run();
})();
