/**
 * Worker-side uncaught error reporter. A raw wasm trap (e.g. "memory access
 * out of bounds") in the pthread worker reaches the main thread as an
 * ErrorEvent WITHOUT a stack, so the faulting function is lost. Chrome
 * populates ev.error (with .stack, incl. wasm function names thanks to -g2)
 * on the worker-side error event. Forward it to the main thread: an unknown
 * cmd is logged there via err() and lands in the console log ring.
 */
(function () {
  if (typeof ENVIRONMENT_IS_WORKER === 'undefined' || !ENVIRONMENT_IS_WORKER)
    return;
  if (self.__cod2WorkerStackReporter)
    return;
  self.__cod2WorkerStackReporter = true;
  self.addEventListener('error', function (ev) {
    var err = ev && ev.error;
    var st = err && err.stack ? String(err.stack) : String(ev && ev.message);
    if (st.length > 6000) st = st.substring(0, 6000) + '...<truncated>';
    try {
      postMessage({ cmd: 'cod2-worker-stack >>> ' + st });
    } catch (e) {}
  });
})();

/**
 * Guard MAIN_THREAD_EM_ASM / proxied JS receive. A missing ASM_CONSTS entry
 * or proxiedFunctionTable slot otherwise throws TypeError: func is not a
 * function and aborts boot. Log once and return 0 so init can continue.
 *
 * NOTE: this must run before createWasm() binds imports. The build applies
 * the same guard via tools/patch_web_proxy_receive.py (POST_BUILD); this block
 * is a belt-and-suspenders live patch when the binding is still a JS var.
 */
(function () {
  function install() {
    if (typeof __emscripten_receive_on_main_thread_js !== 'function')
      return false;
    if (__emscripten_receive_on_main_thread_js.__cod2ProxyReceiveGuard)
      return true;
    var orig = __emscripten_receive_on_main_thread_js;
    function guarded(funcIndex, emAsmAddr, callingThread, numCallArgs, args) {
      var addr = emAsmAddr | 0;
      var idx = funcIndex | 0;
      var func = addr
        ? (typeof ASM_CONSTS !== 'undefined' ? ASM_CONSTS[addr] : null)
        : (typeof proxiedFunctionTable !== 'undefined' ? proxiedFunctionTable[idx] : null);
      if (typeof func !== 'function') {
        if (!guarded._logged) {
          guarded._logged = 1;
          var n = (typeof ASM_CONSTS !== 'undefined') ? Object.keys(ASM_CONSTS).length : -1;
          console.error('[cod2-proxy] missing main-thread target',
            { funcIndex: idx, emAsmAddr: addr, asmConsts: n, tableLen:
              (typeof proxiedFunctionTable !== 'undefined' ? proxiedFunctionTable.length : -1) });
        }
        return 0;
      }
      return orig(funcIndex, emAsmAddr, callingThread, numCallArgs, args);
    }
    guarded.__cod2ProxyReceiveGuard = true;
    __emscripten_receive_on_main_thread_js = guarded;
    try {
      if (typeof wasmImports === 'object' && wasmImports)
        wasmImports._emscripten_receive_on_main_thread_js = guarded;
    } catch (e) {}
    return true;
  }
  if (!install()) {
    var t = setInterval(function () {
      if (install()) clearInterval(t);
    }, 0);
  }
})();

/**
 * PROXY_TO_PTHREAD + OFFSCREEN_FRAMEBUFFER (PROXY_ALWAYS):
 * emscripten activates the worker-side GL context by assigning the raw
 * context *handle* to GL.currentContext (not GL.contexts[handle]). The
 * real context object — including tempVertexBufferCounters* — lives only
 * on the main browser thread.
 *
 * registerPreMainLoop still runs GL.newRenderingFrameStarted() on the
 * worker every frame, which then does:
 *   GL.currentContext.tempVertexBufferCounters1[i] = 0
 * → TypeError: Cannot set properties of undefined (setting '0')
 *
 * Skip the client-array temp-buffer swap when the context is proxied or
 * the counters were never allocated on this thread.
 *
 * Also patch resizeOffscreenFramebuffer: stock uses drawingBufferWidth which
 * stays at the OLD FBO size under renderViaOffscreenBackBuffer — soft
 * vid_restart then never leaves 640x480.
 */
(function () {
  if (typeof GL === 'undefined') {
    return;
  }
  if (typeof GL.newRenderingFrameStarted === 'function' &&
      !GL.newRenderingFrameStarted.__cod2ProxiedGuard) {
    var orig = GL.newRenderingFrameStarted;
    GL.newRenderingFrameStarted = function () {
      var ctx = GL.currentContext;
      if (!ctx || GL.currentContextIsProxied || !ctx.tempVertexBufferCounters1) {
        return;
      }
      return orig.apply(this, arguments);
    };
    GL.newRenderingFrameStarted.__cod2ProxiedGuard = true;
  }
  if (typeof GL.resizeOffscreenFramebuffer === 'function' &&
      !GL.resizeOffscreenFramebuffer.__cod2CanvasSize) {
    var origResizeFbo = GL.resizeOffscreenFramebuffer;
    GL.resizeOffscreenFramebuffer = function (context) {
      if (!context || !context.GLctx) {
        return origResizeFbo.apply(this, arguments);
      }
      var gl = context.GLctx;
      var w = (gl.canvas && gl.canvas.width) | 0;
      var h = (gl.canvas && gl.canvas.height) | 0;
      if (w < 1 || h < 1) {
        return origResizeFbo.apply(this, arguments);
      }
      try {
        if (context.defaultColorTarget) {
          var prevTex = gl.getParameter(0x8069);
          gl.bindTexture(0x0DE1, context.defaultColorTarget);
          gl.texImage2D(0x0DE1, 0, 0x1908, w, h, 0, 0x1908, 0x1401, null);
          gl.bindTexture(0x0DE1, prevTex);
        }
        if (context.defaultDepthTarget) {
          var prevRb = gl.getParameter(0x8CA7);
          gl.bindRenderbuffer(0x8D41, context.defaultDepthTarget);
          gl.renderbufferStorage(0x8D41, 0x81A5, w, h);
          gl.bindRenderbuffer(0x8D41, prevRb);
        }
      } catch (e) {
        return origResizeFbo.apply(this, arguments);
      }
    };
    GL.resizeOffscreenFramebuffer.__cod2CanvasSize = true;
  }
})();

/**
 * Stock emscripten_set_canvas_element_size returns -4 under OFFSCREEN_FRAMEBUFFER
 * because controlTransferredOffscreen is set. The real resize is done from C via
 * MAIN_THREAD_EM_ASM in web_vid_scale.c. This patch is a belt-and-suspenders path
 * if anything still calls the stock export from the UI thread.
 */
(function () {
  function cod2RestoreCanvasCss(canvas) {
    if (!canvas || !canvas.style) {
      return;
    }
    canvas.style.setProperty('position', 'fixed', 'important');
    canvas.style.setProperty('left', '0', 'important');
    canvas.style.setProperty('top', '0', 'important');
    canvas.style.setProperty('right', '0', 'important');
    canvas.style.setProperty('bottom', '0', 'important');
    canvas.style.setProperty('width', '100vw', 'important');
    canvas.style.setProperty('height', '100vh', 'important');
    canvas.style.setProperty('max-width', '100vw', 'important');
    canvas.style.setProperty('max-height', '100vh', 'important');
    canvas.style.setProperty('object-fit', 'contain', 'important');
    canvas.style.setProperty('object-position', 'center', 'important');
    canvas.style.setProperty('border', '0', 'important');
    canvas.style.setProperty('display', 'block', 'important');
    canvas.style.setProperty('margin', '0', 'important');
    canvas.style.setProperty('padding', '0', 'important');
    canvas.style.setProperty('box-sizing', 'border-box', 'important');
  }

  function cod2ForceResize(width, height) {
    width = width | 0;
    height = height | 0;
    if (typeof document === 'undefined') {
      return 0;
    }
    var c = document.getElementById('canvas');
    var mod = typeof Module !== 'undefined' ? Module : null;
    if (!c && mod) {
      c = mod.canvas;
    }
    if (!c) {
      return 0;
    }
    try {
      c.width = width;
      c.height = height;
    } catch (e) {
      return 0;
    }
    var ctxObj = c.GLctxObject || null;
    if (!ctxObj && typeof GL !== 'undefined' && GL.currentContext) {
      var cur = GL.currentContext;
      if (cur && typeof cur === 'object' && cur.GLctx) {
        ctxObj = cur;
      } else if (typeof cur === 'number' && GL.contexts && GL.contexts[cur]) {
        ctxObj = GL.contexts[cur];
      }
    }
    if (ctxObj && typeof GL !== 'undefined' && typeof GL.resizeOffscreenFramebuffer === 'function') {
      try {
        GL.resizeOffscreenFramebuffer(ctxObj);
      } catch (e2) { /* ignore */ }
    }
    cod2RestoreCanvasCss(c);
    var gl = ctxObj && ctxObj.GLctx;
    var dw = gl && gl.drawingBufferWidth ? (gl.drawingBufferWidth | 0) : (c.width | 0);
    var dh = gl && gl.drawingBufferHeight ? (gl.drawingBufferHeight | 0) : (c.height | 0);
    return (dw === width && dh === height) ? 1 : 0;
  }

  function cod2PatchCanvasResize() {
    var mod = typeof Module !== 'undefined' ? Module : null;
    if (!mod || mod.__cod2CanvasResizePatched) {
      return !!mod;
    }
    mod.cod2ForceResizeCanvas = cod2ForceResize;
    if (typeof globalThis !== 'undefined') {
      globalThis.cod2ForceResizeCanvas = cod2ForceResize;
    }

    /* SDL_SetWindowSize → this would set style.width = "640px" and shrink the page. */
    function patchCssSize(fn) {
      if (typeof fn !== 'function' || fn.__cod2FullBleed) return fn;
      function wrapped(target, width, height) {
        var r = fn(target, width, height);
        try {
          var c = (typeof document !== 'undefined')
            ? (document.getElementById('canvas') || (mod && mod.canvas))
            : null;
          if (c) cod2RestoreCanvasCss(c);
        } catch (e) {}
        return r;
      }
      wrapped.__cod2FullBleed = true;
      return wrapped;
    }
    if (typeof _emscripten_set_element_css_size === 'function') {
      _emscripten_set_element_css_size = patchCssSize(_emscripten_set_element_css_size);
    }
    if (typeof mod._emscripten_set_element_css_size === 'function') {
      mod._emscripten_set_element_css_size = patchCssSize(mod._emscripten_set_element_css_size);
    }
    try {
      if (typeof wasmImports === 'object' && wasmImports &&
          typeof wasmImports._emscripten_set_element_css_size === 'function') {
        wasmImports._emscripten_set_element_css_size =
          patchCssSize(wasmImports._emscripten_set_element_css_size);
      }
    } catch (e2) {}
    /* proxiedFunctionTable entry used by PROXY_TO_PTHREAD sync calls */
    try {
      if (typeof proxiedFunctionTable !== 'undefined') {
        for (var pi = 0; pi < proxiedFunctionTable.length; pi++) {
          if (proxiedFunctionTable[pi] &&
              proxiedFunctionTable[pi].name === '_emscripten_set_element_css_size') {
            proxiedFunctionTable[pi] = patchCssSize(proxiedFunctionTable[pi]);
          }
        }
      }
    } catch (e3) {}

    if (typeof GL === 'undefined' || !mod._emscripten_set_canvas_element_size) {
      return false;
    }

    var orig = mod._emscripten_set_canvas_element_size;
    mod._emscripten_set_canvas_element_size = function (target, width, height) {
      if (typeof document !== 'undefined') {
        if (cod2ForceResize(width, height)) {
          return 0;
        }
      }
      var r = orig(target, width, height);
      if (typeof document !== 'undefined') {
        cod2RestoreCanvasCss(document.getElementById('canvas') || mod.canvas);
      }
      return r;
    };
    mod.__cod2CanvasResizePatched = true;
    return true;
  }

  if (!cod2PatchCanvasResize()) {
    var t = setInterval(function () {
      if (cod2PatchCanvasResize()) {
        clearInterval(t);
      }
    }, 50);
  }
})();
