/**
 * Web Audio backend for the sound engine (linked via --post-js).
 * PROXY_TO_PTHREAD: AudioContext must live on the browser UI thread; C calls
 * MAIN_THREAD_EM_ASM which invokes globalThis.cod2Audio.*.
 *
 * post-js is also evaluated inside pthread workers — never touch window/document
 * there (ReferenceError aborts the worker and cascades into wasmfs_mkdir asserts).
 *
 * `cod2Audio.snd` is the channel-oriented engine used by src/web/web_snd_driver.c
 * (the Web Audio backend for the real alias-based sound engine in src/PC/snd.c).
 * Every alias-driven sound (menu clicks/music, weapons, footsteps, ambient loops,
 * in-game music, ...) flows through this single engine/AudioContext.
 */
(function () {
  /* Workers have no window/document; MAIN_THREAD_EM_ASM uses the main copy. */
  if (typeof window === "undefined" || typeof document === "undefined") return;

  function attach(api) {
    try {
      if (typeof globalThis !== "undefined") globalThis.cod2Audio = api;
    } catch (e) {}
    try {
      if (typeof self !== "undefined") self.cod2Audio = api;
    } catch (e) {}
    try {
      if (typeof Module !== "undefined") Module.cod2Audio = api;
    } catch (e) {}
  }

  var ctx = null;
  var unlocked = false;
  var decodeCache = Object.create(null); /* path -> AudioBuffer | Promise */
  var masterGain = null;
  var pending = [];
  var nextGeneration = 1;

  /*
   * Browsers (observed on Chrome) can suspend a running AudioContext on
   * their own — e.g. around a tab visibility/focus blip, which the web
   * build's vid_restart soft-apply (triggered together with snd_restart by
   * CL_Snd_Restart_f in src/PC/client_mp/cl_main_mp.c) can cause — with no
   * exception thrown anywhere. `.start()` on a suspended context's source
   * node doesn't throw either; it just silently never produces audio until
   * something resumes the context, which is the "sound just goes away, no
   * console errors, F5 fixes it" symptom the user reported. `unlock()`
   * previously only ever ran once (the first user gesture); this keeps
   * re-resuming any time the context drifts back to "suspended" instead of
   * relying solely on that one-shot gesture unlock.
   */
  function autoResume() {
    if (ctx && ctx.state === "suspended") {
      ctx.resume().catch(function () {});
    }
  }

  function getCtx() {
    if (ctx) return ctx;
    var AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) return null;
    ctx = new AC();
    masterGain = ctx.createGain();
    masterGain.gain.value = 1.0;
    masterGain.connect(ctx.destination);
    ctx.onstatechange = autoResume;
    document.addEventListener(
      "visibilitychange",
      function () {
        if (!document.hidden) autoResume();
      },
      true,
    );
    return ctx;
  }

  function unlock() {
    var c = getCtx();
    if (!c) return;
    autoResume();
    unlocked = c.state === "running" || c.state === "suspended";
    if (pending.length) {
      var q = pending.slice();
      pending.length = 0;
      for (var i = 0; i < q.length; i++) q[i]();
    }
  }

  function bindUnlock() {
    var evs = ["pointerdown", "mousedown", "touchstart", "keydown"];
    function once() {
      unlock();
      for (var i = 0; i < evs.length; i++) {
        window.removeEventListener(evs[i], once, true);
        document.removeEventListener(evs[i], once, true);
      }
    }
    for (var i = 0; i < evs.length; i++) {
      window.addEventListener(evs[i], once, true);
      document.addEventListener(evs[i], once, true);
    }
  }

  function decodeBytes(path, bytes) {
    var cached = decodeCache[path];
    if (cached) return Promise.resolve(cached);
    var c = getCtx();
    if (!c) return Promise.reject(new Error("no AudioContext"));
    if (!bytes || !bytes.byteLength) return Promise.reject(new Error("no bytes for " + path));
    var ab = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
    var p = c
      .decodeAudioData(ab)
      .then(function (buf) {
        decodeCache[path] = buf;
        return buf;
      })
      .catch(function (err) {
        delete decodeCache[path];
        throw err;
      });
    decodeCache[path] = p;
    return p;
  }

  /*
   * Channel-oriented engine mirroring the abstract driver contract that
   * src/PC/win32/snd_driver.c implements on top of Miles/AIL: a fixed set of
   * indexed channels (see g_snd.chaninfo[53] / snd.c), each independently
   * started/stopped/paused/volumed/panned/rate-shifted. src/web/web_snd_driver.c
   * calls these by channel index (0..52) exactly as the C engine does.
   */
  var channels = Object.create(null); /* index -> state */

  function chanEntry(index) {
    return channels[index] || null;
  }

  /*
   * Shared reverb send bus (ConvolverNode), driven by src/web/web_snd_driver.c
   * (SND_SetRoomtype / SND_Update2DChannelReverb / SND_Update3DChannelReverb /
   * SND_UpdateStreamChannelReverb).
   *
   * ROOM_REVERB_PARAMS below is not guessed: it is the actual 26-entry,
   * 16-byte-stride float table Miles Sound System ships inside mss32.dll at
   * VA 0x2115583c (module base 0x21100000; confirmed by static analysis of a
   * real mss32.dll, see the Ghidra-based reverse-engineering project the user
   * pointed at). _AIL_set_digital_master_room_type (VA 0x21115930) indexes
   * this table as `&table + roomtype*0x10` and forwards
   * {table[+0]=decayTime, table[+8]=preDelay, table[+4]=hfRatio} into the
   * (closed-source) DSP setup at FUN_2112b0f0/FUN_2112ace0, plus
   * table[+0xc]=a default wet/send level. The 26 entries line up 1:1, in
   * this exact order, with CoD2's own room name list (src/PC/qcommon/
   * common.c: "generic".."psychotic" — the standard EAX/I3DL2 environment
   * presets), which is what confirms the table is genuine and not
   * misattributed.
   *
   * We reuse decayTime/preDelay/hfRatio to size and color a *synthetic*
   * impulse response (decaying filtered noise) fed into a ConvolverNode —
   * Miles' actual internal filter network (FUN_2112ace0 and beyond) is
   * closed-source DSP we do not reproduce, so the reverb *character* is
   * still a documented heuristic even though the *timing* now matches the
   * original per-room values exactly.
   */
  var ROOM_REVERB_PARAMS = [
    /* [decayTimeSeconds, preDelaySeconds, hfRatio] */
    [1.493, 0.011, 0.5], // 0  generic
    [0.1, 0.002, 0.0], // 1  paddedcell
    [0.4, 0.003, 0.666], // 2  room
    [1.499, 0.011, 0.166], // 3  bathroom
    [0.478, 0.004, 0.0], // 4  livingroom
    [2.309, 0.017, 0.68], // 5  stoneroom
    [4.279, 0.03, 0.5], // 6  auditorium
    [3.961, 0.029, 0.5], // 7  concerthall
    [2.886, 0.022, 1.304], // 8  cave
    [7.284, 0.03, 0.332], // 9  arena
    [10.0, 0.03, 0.3], // 10 hangar
    [0.259, 0.03, 0.55], // 11 carpetedhallway
    [1.493, 0.011, 0.5], // 12 hallway
    [2.697, 0.02, 0.638], // 13 stonecorridor
    [1.752, 0.011, 0.776], // 14 alley
    [3.145, 0.088, 0.472], // 15 forest
    [2.767, 0.011, 0.224], // 16 city
    [0.8, 0.3, 0.2], // 17 mountains
    [3.0, 0.043, 0.55], // 18 quarry
    [0.8, 0.1, 0.18], // 19 plain
    [1.652, 0.012, 1.5], // 20 parkinglot
    [2.886, 0.021, 0.25], // 21 sewerpipe
    [1.499, 0.011, 0.1], // 22 underwater
    [8.392, 0.011, 1.388], // 23 drugged
    [10.0, 0.011, 1.5], // 24 dizzy
    [7.563, 0.011, 4.0], // 25 psychotic
  ];

  var reverbConvolver = null;
  var reverbWetGain = null;
  var reverbIRCache = Object.create(null); /* roomtype -> AudioBuffer */
  var currentRoomType = -1;

  function ensureReverbBus() {
    var c = getCtx();
    if (!c) return null;
    if (!reverbConvolver) {
      reverbConvolver = c.createConvolver();
      reverbConvolver.normalize = true;
      reverbWetGain = c.createGain();
      reverbWetGain.gain.value = 1.0;
      reverbConvolver.connect(reverbWetGain);
      reverbWetGain.connect(masterGain);
      setRoomType(0);
    }
    return reverbConvolver;
  }

  function buildImpulseResponse(roomtype) {
    var c = getCtx();
    var params = ROOM_REVERB_PARAMS[roomtype] || ROOM_REVERB_PARAMS[0];
    var decay = params[0];
    var preDelay = params[1];
    var hfRatio = params[2];
    /* hfRatio ~0 -> darker/more damped tail, ~4 -> brighter; mapped onto the
     * noise smoothing coefficient. This specific mapping (as opposed to the
     * timing values above) is a heuristic: we know hfRatio feeds Miles' real
     * filter, but not that filter's exact response curve. */
    var smoothing = Math.max(0.35, Math.min(0.92, 0.75 - hfRatio * 0.08));
    var preDelaySamples = Math.round(c.sampleRate * preDelay);
    var tailLength = Math.max(1, Math.round(c.sampleRate * decay));
    var length = preDelaySamples + tailLength;
    var buf = c.createBuffer(2, length, c.sampleRate);
    for (var ch = 0; ch < 2; ch++) {
      var data = buf.getChannelData(ch);
      var prev = 0;
      for (var i = preDelaySamples; i < length; i++) {
        var t = (i - preDelaySamples) / tailLength;
        var envelope = Math.pow(1 - t, 2.2);
        var white = Math.random() * 2 - 1;
        prev = prev * smoothing + white * (1 - smoothing);
        data[i] = prev * envelope;
      }
    }
    return buf;
  }

  function setRoomType(roomtype) {
    ensureReverbBus();
    if (roomtype === currentRoomType) return;
    currentRoomType = roomtype;
    var cached = reverbIRCache[roomtype];
    if (!cached) {
      cached = buildImpulseResponse(roomtype);
      reverbIRCache[roomtype] = cached;
    }
    reverbConvolver.buffer = cached;
  }

  function setReverbSend(index, amount) {
    var chan = channels[index];
    if (!chan) return;
    chan.pendingReverbSend = amount;
    if (chan.send) {
      chan.send.gain.value = amount;
    }
  }

  function stop(index) {
    var chan = channels[index];
    if (!chan) return;
    if (chan.source) {
      try {
        chan.source.onended = null;
        chan.source.stop();
      } catch (e) {}
      try {
        chan.source.disconnect();
      } catch (e) {}
    }
    if (chan.gain) {
      try {
        chan.gain.disconnect();
      } catch (e) {}
    }
    if (chan.pan) {
      try {
        chan.pan.disconnect();
      } catch (e) {}
    }
    if (chan.panner) {
      try {
        chan.panner.disconnect();
      } catch (e) {}
    }
    if (chan.send) {
      try {
        chan.send.disconnect();
      } catch (e) {}
    }
    delete channels[index];
  }

  function start(index, path, bytes, loop, rateHz, volume, pan, offsetSec, startPaused) {
    var c = getCtx();
    if (!c) return;
    autoResume();
    stop(index);

    var gen = nextGeneration++;
    var chan = {
      path: path,
      loop: !!loop,
      generation: gen,
      source: null,
      gain: null,
      pan: null,
      send: null,
      pendingReverbSend: 0,
      buffer: null,
      baseRate: 0,
      rateHz: rateHz > 0 ? rateHz : 0,
      startedCtxTime: c.currentTime,
      pendingOffset: offsetSec || 0,
      pausedAt: startPaused ? (offsetSec || 0) : null,
      ended: false,
    };
    channels[index] = chan;

    function begin() {
      decodeBytes(path, bytes)
        .then(function (buffer) {
          var cur = channels[index];
          if (!cur || cur.generation !== gen) return;

          var gainNode = c.createGain();
          gainNode.gain.value = volume;

          var panNode = c.createStereoPanner ? c.createStereoPanner() : null;
          if (panNode) panNode.pan.value = Math.max(-1, Math.min(1, pan || 0));

          var effectiveRate = rateHz > 0 ? rateHz : buffer.sampleRate;

          var sendGain = c.createGain();
          sendGain.gain.value = cur.pendingReverbSend || 0;

          cur.gain = gainNode;
          cur.pan = panNode;
          cur.send = sendGain;
          cur.buffer = buffer;
          cur.baseRate = effectiveRate;
          cur.rateHz = effectiveRate;

          if (panNode) {
            gainNode.connect(panNode);
            panNode.connect(masterGain);
          } else {
            gainNode.connect(masterGain);
          }
          (panNode || gainNode).connect(sendGain);
          sendGain.connect(ensureReverbBus());

          if (cur.pausedAt !== null) return;

          var source = c.createBufferSource();
          source.buffer = buffer;
          source.loop = !!loop;
          source.playbackRate.value = effectiveRate / cur.baseRate;
          source.connect(gainNode);

          var startOffset = Math.max(0, Math.min(offsetSec || 0, Math.max(0, buffer.duration - 0.001)));
          try {
            source.start(0, startOffset);
          } catch (e) {
            console.warn("[cod2Audio] source.start() failed for " + path + ":", e);
            cur.ended = true;
            return;
          }

          cur.source = source;
          cur.startedCtxTime = c.currentTime - startOffset;

          source.onended = function () {
            if (cur.source === source) cur.ended = true;
          };
        })
        .catch(function (err) {
          console.warn("[cod2Audio] decode failed for " + path + ":", err);
          var cur = channels[index];
          if (cur && cur.generation === gen) cur.ended = true;
        });
    }

    if (c.state === "suspended" && !unlocked) {
      pending.push(begin);
    } else {
      begin();
    }
  }

  function pause(index) {
    var chan = channels[index];
    if (!chan || chan.pausedAt !== null) return;
    if (!chan.source) {
      chan.pausedAt = chan.pendingOffset || 0;
      return;
    }
    var c = getCtx();
    var dur = chan.buffer ? chan.buffer.duration : 0;
    var elapsed = c ? c.currentTime - chan.startedCtxTime : 0;
    if (chan.loop && dur > 0) elapsed = elapsed % dur;
    chan.pausedAt = Math.max(0, elapsed);
    try {
      chan.source.onended = null;
      chan.source.stop();
    } catch (e) {}
    chan.source = null;
  }

  function resume(index) {
    var chan = channels[index];
    if (!chan || chan.pausedAt === null) return;
    /* Decode still pending: keep pausedAt so begin()/decode.then starts later. */
    if (!chan.buffer || !chan.gain) return;
    var c = getCtx();
    if (!c) return;
    var source = c.createBufferSource();
    source.buffer = chan.buffer;
    source.loop = chan.loop;
    source.playbackRate.value = chan.baseRate ? chan.rateHz / chan.baseRate : 1.0;
    source.connect(chan.gain);
    var offset = chan.pausedAt;
    try {
      source.start(0, offset);
    } catch (e) {
      return;
    }
    chan.source = source;
    chan.startedCtxTime = c.currentTime - offset;
    chan.pausedAt = null;
    chan.ended = false;
    source.onended = function () {
      if (chan.source === source) chan.ended = true;
    };
  }

  function setVolume(index, v) {
    var chan = channels[index];
    if (chan && chan.gain) chan.gain.gain.value = v;
  }

  function setPan(index, p) {
    var chan = channels[index];
    if (!chan) return;
    if (chan.panner) return; /* 3D channel — ignore legacy pan calls */
    if (chan.pan) chan.pan.pan.value = Math.max(-1, Math.min(1, p));
  }

  /*
   * 3D positional audio using PannerNode (HRTF or equalpower).
   *
   * Miles' "Fast 2D Positional Audio" provider (rolloff factor 0) provides
   * only directional panning — volume attenuation is fully controlled by the
   * game's own SND_Attenuate(). We replicate this: PannerNode does HRTF
   * spatialization for direction, but with rolloffFactor=0 so it contributes
   * no distance-based gain changes. The game drives volume via setVolume().
   *
   * AudioListener stays at origin facing default direction (0,0,-1) with up
   * (0,1,0). Source positions are provided in listener-local coordinates
   * (pre-transformed by the C code using the same matrix Miles uses in
   * SND_Set3DPosition), converted to Web Audio coords:
   *   WebAudio X = miles_x (right)
   *   WebAudio Y = -miles_y (up; Miles Y is down)
   *   WebAudio Z = -miles_z (Web Audio forward is -Z)
   */
  function set3DPosition(index, x, y, z) {
    var chan = channels[index];
    if (!chan || !chan.panner) return;
    var p = chan.panner;
    /* Web Audio coords from Miles listener-local: */
    var wx = x;
    var wy = -y;
    var wz = -z;
    if (p.positionX) {
      p.positionX.value = wx;
      p.positionY.value = wy;
      p.positionZ.value = wz;
    } else {
      p.setPosition(wx, wy, wz);
    }
  }

  function start3D(index, path, bytes, loop, rateHz, volume, x, y, z, offsetSec, startPaused) {
    var c = getCtx();
    if (!c) return;
    autoResume();
    stop(index);

    var gen = nextGeneration++;
    var chan = {
      path: path,
      loop: !!loop,
      generation: gen,
      source: null,
      gain: null,
      pan: null,
      panner: null,
      send: null,
      pendingReverbSend: 0,
      buffer: null,
      baseRate: 0,
      rateHz: rateHz > 0 ? rateHz : 0,
      startedCtxTime: c.currentTime,
      pendingOffset: offsetSec || 0,
      pausedAt: startPaused ? (offsetSec || 0) : null,
      ended: false,
    };
    channels[index] = chan;

    function begin() {
      decodeBytes(path, bytes)
        .then(function (buffer) {
          var cur = channels[index];
          if (!cur || cur.generation !== gen) return;

          var gainNode = c.createGain();
          gainNode.gain.value = volume;

          /* PannerNode with HRTF, rolloff disabled (game controls volume) */
          var pannerNode = c.createPanner();
          /* equalpower is much cheaper than HRTF under many concurrent 3D sources. */
          pannerNode.panningModel = "equalpower";
          pannerNode.distanceModel = "inverse";
          pannerNode.refDistance = 1;
          pannerNode.maxDistance = 10000;
          pannerNode.rolloffFactor = 0;
          pannerNode.coneInnerAngle = 360;
          pannerNode.coneOuterAngle = 360;
          pannerNode.coneOuterGain = 1;

          /* Set initial position (Miles listener-local → Web Audio coords) */
          var wx = x;
          var wy = -y;
          var wz = -z;
          if (pannerNode.positionX) {
            pannerNode.positionX.value = wx;
            pannerNode.positionY.value = wy;
            pannerNode.positionZ.value = wz;
          } else {
            pannerNode.setPosition(wx, wy, wz);
          }

          /* Dry: gain → panner → master. Wet send from gain (pre-panner). */
          gainNode.connect(pannerNode);
          pannerNode.connect(masterGain);

          var effectiveRate = rateHz > 0 ? rateHz : buffer.sampleRate;

          var sendGain = c.createGain();
          sendGain.gain.value = cur.pendingReverbSend || 0;
          gainNode.connect(sendGain);
          sendGain.connect(ensureReverbBus());

          cur.gain = gainNode;
          cur.pan = null;
          cur.panner = pannerNode;
          cur.send = sendGain;
          cur.buffer = buffer;
          cur.baseRate = effectiveRate;
          cur.rateHz = effectiveRate;

          /* Miles holds until startDelay==0; pause-before-decode must not start. */
          if (cur.pausedAt !== null) return;

          var source = c.createBufferSource();
          source.buffer = buffer;
          source.loop = !!loop;
          source.playbackRate.value = effectiveRate / cur.baseRate;
          source.connect(gainNode);

          var startOffset = Math.max(0, Math.min(offsetSec || 0, Math.max(0, buffer.duration - 0.001)));
          try {
            source.start(0, startOffset);
          } catch (e) {
            console.warn("[cod2Audio] source.start() failed for " + path + ":", e);
            cur.ended = true;
            return;
          }

          cur.source = source;
          cur.startedCtxTime = c.currentTime - startOffset;

          source.onended = function () {
            if (cur.source === source) cur.ended = true;
          };
        })
        .catch(function (err) {
          console.warn("[cod2Audio] decode failed for " + path + ":", err);
          var cur = channels[index];
          if (cur && cur.generation === gen) cur.ended = true;
        });
    }

    if (c.state === "suspended" && !unlocked) {
      pending.push(begin);
    } else {
      begin();
    }
  }

  function setRate(index, hz) {
    var chan = channels[index];
    if (!chan) return;
    chan.rateHz = hz;
    if (chan.source && chan.baseRate) chan.source.playbackRate.value = hz / chan.baseRate;
  }

  function getRate(index) {
    var chan = chanEntry(index);
    return chan ? chan.rateHz || 0 : 0;
  }

  function getVolume(index) {
    var chan = chanEntry(index);
    return chan && chan.gain ? chan.gain.gain.value : 0;
  }

  function isFree(index) {
    var chan = chanEntry(index);
    return !chan || chan.ended;
  }

  function getPositionMs(index) {
    var chan = chanEntry(index);
    if (!chan) return 0;
    if (chan.pausedAt !== null) return Math.round(chan.pausedAt * 1000);
    var c = getCtx();
    if (!c) return 0;
    var dur = chan.buffer ? chan.buffer.duration : 0;
    var elapsed = c.currentTime - chan.startedCtxTime;
    if (chan.loop && dur > 0) elapsed = elapsed % dur;
    return Math.round(Math.max(0, elapsed) * 1000);
  }

  function getChannelDurationMs(index) {
    var chan = chanEntry(index);
    return chan && chan.buffer ? Math.round(chan.buffer.duration * 1000) : -1;
  }

  function preload(path, bytes) {
    decodeBytes(path, bytes).catch(function () {});
  }

  function getDurationMs(path) {
    var cached = decodeCache[path];
    if (cached && typeof cached.duration === "number") return Math.round(cached.duration * 1000);
    return -1;
  }

  function getNativeRate(path) {
    var cached = decodeCache[path];
    if (cached && typeof cached.sampleRate === "number") return cached.sampleRate;
    return 0;
  }

  function getNativeChannels(path) {
    var cached = decodeCache[path];
    if (cached && typeof cached.numberOfChannels === "number") return cached.numberOfChannels;
    return 0;
  }

  var snd = {
    preload: preload,
    getDurationMs: getDurationMs,
    getNativeRate: getNativeRate,
    getNativeChannels: getNativeChannels,
    start: start,
    start3D: start3D,
    set3DPosition: set3DPosition,
    stop: stop,
    pause: pause,
    resume: resume,
    setVolume: setVolume,
    setPan: setPan,
    setRate: setRate,
    getRate: getRate,
    getVolume: getVolume,
    isFree: isFree,
    getPositionMs: getPositionMs,
    getChannelDurationMs: getChannelDurationMs,
    setRoomType: setRoomType,
    setReverbSend: setReverbSend,
  };

  bindUnlock();
  attach({
    unlock: unlock,
    snd: snd,
  });
})();
