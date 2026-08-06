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
 */ (function() {
  if (typeof GL === "undefined" || typeof GL.newRenderingFrameStarted !== "function") {
    return;
  }
  if (GL.newRenderingFrameStarted.__cod2ProxiedGuard) {
    return;
  }
  var orig = GL.newRenderingFrameStarted;
  GL.newRenderingFrameStarted = function() {
    var ctx = GL.currentContext;
    if (!ctx || GL.currentContextIsProxied || !ctx.tempVertexBufferCounters1) {
      return;
    }
    return orig.apply(this, arguments);
  };
  GL.newRenderingFrameStarted.__cod2ProxiedGuard = true;
})();

/**
 * PROXY_TO_PTHREAD + OFFSCREEN_FRAMEBUFFER: stock emscripten_set_canvas_element_size
 * returns -4 when controlTransferredOffscreen (main-thread HTML canvas) or when
 * called from the worker where document.querySelector("#canvas") is null and the
 * proxied main-thread path still hits -4. Soft vid_restart then kept drawable at
 * 640x480 while lying about 1024 — UI hit-tests / OOB broke.
 *
 * Resize whichever canvas actually backs the GL context (Module.canvas /
 * GLctx.canvas / GLctxObject), then resizeOffscreenFramebuffer.
 */ (function() {
  function cod2RestoreCanvasCss(canvas) {
    if (!canvas || !canvas.style) {
      return;
    }
    canvas.style.width = "100%";
    canvas.style.height = "100%";
    canvas.style.objectFit = "fill";
    canvas.style.border = "0";
    canvas.style.display = "block";
  }
  function cod2ResolveResizeCanvas(mod) {
    var canvas = null;
    if (typeof document !== "undefined") {
      canvas = document.getElementById("canvas") || mod.canvas;
    } else {
      canvas = mod.canvas;
    }
    if (typeof GL !== "undefined") {
      var cur = GL.currentContext;
      if (cur && typeof cur === "object" && cur.GLctx && cur.GLctx.canvas) {
        return cur.GLctx.canvas;
      }
      if (typeof cur === "number" && GL.contexts && GL.contexts[cur] && GL.contexts[cur].GLctx && GL.contexts[cur].GLctx.canvas) {
        return GL.contexts[cur].GLctx.canvas;
      }
      if (canvas && canvas.GLctxObject && canvas.GLctxObject.GLctx && canvas.GLctxObject.GLctx.canvas) {
        return canvas.GLctxObject.GLctx.canvas;
      }
    }
    return canvas;
  }
  function cod2ResolveCtxObject(canvas) {
    if (canvas && canvas.GLctxObject) {
      return canvas.GLctxObject;
    }
    if (typeof GL === "undefined") {
      return null;
    }
    var cur = GL.currentContext;
    if (cur && typeof cur === "object" && cur.GLctx) {
      return cur;
    }
    if (typeof cur === "number" && GL.contexts && GL.contexts[cur]) {
      return GL.contexts[cur];
    }
    return null;
  }
  function cod2PatchCanvasResize() {
    var mod = typeof Module !== "undefined" ? Module : null;
    if (!mod || mod.__cod2CanvasResizePatched) {
      return !!mod;
    }
    if (typeof GL === "undefined" || !mod._emscripten_set_canvas_element_size) {
      return false;
    }
    var orig = mod._emscripten_set_canvas_element_size;
    mod._emscripten_set_canvas_element_size = function(target, width, height) {
      var resizeTarget = cod2ResolveResizeCanvas(mod);
      if (resizeTarget && typeof resizeTarget.width === "number") {
        try {
          resizeTarget.width = width | 0;
          resizeTarget.height = height | 0;
        } catch (e) {
          return orig(target, width, height);
        }
        if (typeof document !== "undefined") {
          cod2RestoreCanvasCss(resizeTarget);
          var dom = document.getElementById("canvas");
          if (dom && dom !== resizeTarget) {
            cod2RestoreCanvasCss(dom);
          }
        }
        var ctxObj = cod2ResolveCtxObject(resizeTarget) || cod2ResolveCtxObject(mod.canvas);
        if (ctxObj && typeof GL.resizeOffscreenFramebuffer === "function") {
          try {
            GL.resizeOffscreenFramebuffer(ctxObj);
          } catch (e2) {}
        }
        /* ignore */ return 0;
      }
      var r = orig(target, width, height);
      if (typeof document !== "undefined") {
        cod2RestoreCanvasCss(document.getElementById("canvas") || mod.canvas);
      }
      return r;
    };
    mod.__cod2CanvasResizePatched = true;
    return true;
  }
  if (!cod2PatchCanvasResize()) {
    var t = setInterval(function() {
      if (cod2PatchCanvasResize()) {
        clearInterval(t);
      }
    }, 50);
  }
})();
