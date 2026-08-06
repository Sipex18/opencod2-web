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
 */
(function () {
  if (typeof GL === 'undefined' || typeof GL.newRenderingFrameStarted !== 'function') {
    return;
  }
  if (GL.newRenderingFrameStarted.__cod2ProxiedGuard) {
    return;
  }
  var orig = GL.newRenderingFrameStarted;
  GL.newRenderingFrameStarted = function () {
    var ctx = GL.currentContext;
    if (!ctx || GL.currentContextIsProxied || !ctx.tempVertexBufferCounters1) {
      return;
    }
    return orig.apply(this, arguments);
  };
  GL.newRenderingFrameStarted.__cod2ProxiedGuard = true;
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
    canvas.style.setProperty('width', '100%', 'important');
    canvas.style.setProperty('height', '100%', 'important');
    canvas.style.setProperty('max-width', '100%', 'important');
    canvas.style.setProperty('max-height', '100%', 'important');
    canvas.style.setProperty('object-fit', 'contain', 'important');
    canvas.style.setProperty('object-position', 'center', 'important');
    canvas.style.setProperty('border', '0', 'important');
    canvas.style.setProperty('display', 'block', 'important');
    canvas.style.setProperty('margin', '0', 'important');
    canvas.style.setProperty('padding', '0', 'important');
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
