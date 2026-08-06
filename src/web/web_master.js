/**
 * Browser master discovery helper (linked via --post-js).
 * Fetches same-origin JSON masterlist and feeds C via CL_WebMaster_*.
 *
 * PROXY_TO_PTHREAD: post-js often runs on the worker Module, while fetch must
 * run on the browser UI thread (MAIN_THREAD_ASYNC_EM_ASM). Attach the helper to
 * globalThis so the main thread can find it even when Module.cod2MasterRequest
 * was never set there.
 */
(function () {
  function resolveUrl(defaultUrl) {
    try {
      if (typeof Module !== 'undefined' && Module.COD2_MASTER_URL)
        return Module.COD2_MASTER_URL;
    } catch (e) {}
    try {
      if (typeof self !== 'undefined' && self.COD2_MASTER_URL)
        return self.COD2_MASTER_URL;
    } catch (e) {}
    try {
      if (typeof globalThis !== 'undefined' && globalThis.COD2_MASTER_URL)
        return globalThis.COD2_MASTER_URL;
    } catch (e) {}
    return defaultUrl;
  }

  function avgPlayerPing(playerinfo) {
    if (!playerinfo || !playerinfo.length) return 999;
    var sum = 0;
    var n = 0;
    for (var i = 0; i < playerinfo.length; i++) {
      var p = parseInt(playerinfo[i].ping, 10);
      if (!isNaN(p) && p >= 0) {
        sum += p;
        n++;
      }
    }
    if (!n) return 999;
    var avg = Math.round(sum / n);
    return avg > 0 ? avg : 1;
  }

  /*
   * ping_ms is the proxy's measured VPS->server round trip. The player-ping
   * average is only a fallback: it is the connected players' latency, and it is
   * 999 for every empty server.
   */
  function serverPing(s) {
    var ms = parseInt(s.ping_ms, 10);
    if (!isNaN(ms) && ms > 0) return Math.min(ms, 999);
    return avgPlayerPing(s.playerinfo);
  }

  function packIPv4(ip) {
    var parts = String(ip).split('.');
    if (parts.length !== 4) return null;
    var packed = 0;
    for (var i = 0; i < 4; i++) {
      var b = parseInt(parts[i], 10);
      if (isNaN(b) || b < 0 || b > 255) return null;
      packed = (packed << 8) | b;
    }
    return packed | 0;
  }

  var pingTimer = null;

  function stopPingUpdates() {
    if (pingTimer !== null) {
      clearInterval(pingTimer);
      pingTimer = null;
    }
  }

  /*
   * The engine only re-reads the master list on Refresh, and rebuilding it drops
   * the selection and scroll position. So poll the proxy's cheap /pings route
   * and patch the latency of the already-listed servers in place.
   */
  function startPingUpdates(masterUrl, source) {
    stopPingUpdates();
    var pingsUrl = masterUrl.replace('/masterlist/', '/pings/');
    if (pingsUrl === masterUrl) return;

    pingTimer = setInterval(function () {
      fetch(pingsUrl, { credentials: 'same-origin', cache: 'no-cache' })
        .then(function (res) {
          if (!res.ok) throw new Error('HTTP ' + res.status);
          return res.json();
        })
        .then(function (data) {
          var pings = (data && data.pings) ? data.pings : null;
          if (!pings) return;
          for (var key in pings) {
            if (!Object.prototype.hasOwnProperty.call(pings, key)) continue;
            var sep = key.lastIndexOf(':');
            if (sep <= 0) continue;
            var packed = packIPv4(key.slice(0, sep));
            if (packed === null) continue;
            wasmCall('CL_WebMaster_UpdatePing', null,
                     ['number', 'number', 'number', 'number'],
                     [source, packed, parseInt(key.slice(sep + 1), 10) | 0,
                      pings[key] | 0]);
          }
        })
        .catch(function () {
          /* A missed poll just leaves the previous values on screen. */
        });
    }, 2000);
  }

  function wasmCall(name, ret, types, args) {
    var mod = (typeof Module !== 'undefined') ? Module : null;
    if (mod && typeof mod.ccall === 'function')
      return mod.ccall(name, ret, types, args);
    var fn = mod && mod['_' + name];
    if (typeof fn === 'function') {
      /* No string marshaling — only for numeric End/Begin fallbacks. */
      return fn.apply(null, args);
    }
    throw new Error('wasm export missing: ' + name);
  }

  function addOne(source, s) {
    if (!s || !s.ip || !s.port) return;
    var hostname = s.sv_hostname || s.hostname || '';
    var mapname = s.mapname || '';
    var gametype = s.g_gametype || s.gametype || '';
    var game = s.fs_game || s.game || '';
    var clients = s.clients | 0;
    var maxClients = s.sv_maxclients | 0;
    var pswrd = s.pswrd | 0;
    var pure = (typeof s.sv_pure !== 'undefined') ? (s.sv_pure | 0) : 1;
    var ping = serverPing(s);
    var hardware = (typeof s.hw !== 'undefined') ? (s.hw | 0) : 1;
    var mod = game ? 1 : 0;
    wasmCall(
      'CL_WebMaster_AddServer',
      null,
      ['number', 'string', 'number', 'string', 'string', 'string', 'string',
       'number', 'number', 'number', 'number', 'number', 'number', 'number'],
      [source, String(s.ip), s.port | 0, hostname, mapname, gametype, game,
       clients, maxClients, pswrd, pure, ping, hardware, mod]
    );
  }

  function cod2MasterRequest(defaultUrl, source) {
    var url = resolveUrl(defaultUrl);
    if (typeof console !== 'undefined' && console.log)
      console.log('[cod2-master] fetch', url, 'source=' + source);

    stopPingUpdates();

    fetch(url, { credentials: 'same-origin', cache: 'no-cache' })
      .then(function (res) {
        if (!res.ok) throw new Error('HTTP ' + res.status);
        return res.json();
      })
      .then(function (data) {
        var servers = (data && data.servers) ? data.servers : [];
        wasmCall('CL_WebMaster_Begin', null, ['number'], [source]);
        var limit = (source === 0) ? 128 : servers.length;
        for (var i = 0; i < servers.length && i < limit; i++)
          addOne(source, servers[i]);
        wasmCall('CL_WebMaster_End', null, ['number', 'number'], [source, 1]);
        startPingUpdates(url, source);
      })
      .catch(function (err) {
        if (typeof console !== 'undefined' && console.warn)
          console.warn('[cod2-master] failed:', err && err.message ? err.message : err);
        try {
          wasmCall('CL_WebMaster_Begin', null, ['number'], [source]);
        } catch (e) {}
        try {
          wasmCall('CL_WebMaster_End', null, ['number', 'number'], [source, 0]);
        } catch (e2) {}
      });
  }

  function attach(target) {
    if (!target) return;
    try { target.cod2MasterRequest = cod2MasterRequest; } catch (e) {}
  }

  attach(typeof Module !== 'undefined' ? Module : null);
  attach(typeof moduleArg !== 'undefined' ? moduleArg : null);
  attach(typeof globalThis !== 'undefined' ? globalThis : null);
  attach(typeof self !== 'undefined' ? self : null);
  attach(typeof window !== 'undefined' ? window : null);
})();
