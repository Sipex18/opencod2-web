/**
 * Proxied (main-thread) canvas resize for PROXY_TO_PTHREAD + OFFSCREEN_FRAMEBUFFER.
 *
 * Critical: stock GL.resizeOffscreenFramebuffer() sizes the FBO from
 * gl.drawingBufferWidth/Height. With renderViaOffscreenBackBuffer those values
 * stay at the OLD FBO size after canvas.width is assigned — so a 1920x1080
 * apply silently keeps a 640x480 drawable. Resize color/depth targets to the
 * requested size (canvas attributes) explicitly.
 */
mergeInto(LibraryManager.library, {
  $cod2RestoreCanvasFullBleed: function (c) {
    if (!c || !c.style) return;
    c.style.setProperty('position', 'fixed', 'important');
    c.style.setProperty('left', '0', 'important');
    c.style.setProperty('top', '0', 'important');
    c.style.setProperty('right', '0', 'important');
    c.style.setProperty('bottom', '0', 'important');
    c.style.setProperty('width', '100vw', 'important');
    c.style.setProperty('height', '100vh', 'important');
    c.style.setProperty('max-width', '100vw', 'important');
    c.style.setProperty('max-height', '100vh', 'important');
    c.style.setProperty('object-fit', 'contain', 'important');
    c.style.setProperty('object-position', 'center', 'important');
    c.style.setProperty('border', '0', 'important');
    c.style.setProperty('display', 'block', 'important');
    c.style.setProperty('margin', '0', 'important');
    c.style.setProperty('padding', '0', 'important');
    c.style.setProperty('box-sizing', 'border-box', 'important');
  },

  $cod2ResizeOffscreenTo: function (context, wantW, wantH) {
    if (!context || !context.GLctx) return 0;
    var gl = context.GLctx;
    wantW = wantW | 0;
    wantH = wantH | 0;
    if (wantW < 1 || wantH < 1) return 0;
    try {
      if (context.defaultColorTarget) {
        var prevTex = gl.getParameter(0x8069); /* GL_TEXTURE_BINDING_2D */
        gl.bindTexture(0x0DE1, context.defaultColorTarget); /* GL_TEXTURE_2D */
        gl.texImage2D(0x0DE1, 0, 0x1908 /*RGBA*/, wantW, wantH, 0, 0x1908, 0x1401 /*UNSIGNED_BYTE*/, null);
        gl.bindTexture(0x0DE1, prevTex);
      }
      if (context.defaultDepthTarget) {
        var prevRb = gl.getParameter(0x8CA7); /* GL_RENDERBUFFER_BINDING */
        gl.bindRenderbuffer(0x8D41, context.defaultDepthTarget); /* GL_RENDERBUFFER */
        gl.renderbufferStorage(0x8D41, 0x81A5 /*DEPTH_COMPONENT16*/, wantW, wantH);
        gl.bindRenderbuffer(0x8D41, prevRb);
      }
      /* Keep default viewport in sync with the new drawable. */
      try { gl.viewport(0, 0, wantW, wantH); } catch (eV) {}
      return 1;
    } catch (e) {
      console.warn('[cod2-vid] offscreen resize failed', e);
      return 0;
    }
  },

  web_force_canvas_fullbleed_main__deps: ['$cod2RestoreCanvasFullBleed'],
  web_force_canvas_fullbleed_main__proxy: 'sync',
  web_force_canvas_fullbleed_main__sig: 'v',
  web_force_canvas_fullbleed_main: function () {
    if (typeof document === 'undefined') return;
    var c = document.getElementById('canvas');
    if (!c && typeof Module !== 'undefined') c = Module['canvas'];
    cod2RestoreCanvasFullBleed(c);
  },

  web_try_resize_canvas_main__deps: ['$cod2RestoreCanvasFullBleed', '$cod2ResizeOffscreenTo'],
  web_try_resize_canvas_main__proxy: 'sync',
  web_try_resize_canvas_main__sig: 'iii',
  web_try_resize_canvas_main: function (wantW, wantH) {
    wantW = wantW | 0;
    wantH = wantH | 0;
    var outOk = 0;
    try {
      var c = null;
      if (typeof document !== 'undefined')
        c = document.getElementById('canvas');
      if (!c && typeof Module !== 'undefined')
        c = Module['canvas'];
      if (!c) {
        console.warn('[cod2-vid] resize: no canvas on main thread');
        return 0;
      }

      c.width = wantW;
      c.height = wantH;

      var ctxObj = c.GLctxObject || null;
      if (!ctxObj && typeof GL !== 'undefined' && GL.currentContext) {
        var cur = GL.currentContext;
        if (cur && typeof cur === 'object' && cur.GLctx)
          ctxObj = cur;
        else if (typeof cur === 'number' && GL.contexts && GL.contexts[cur])
          ctxObj = GL.contexts[cur];
      }

      if (ctxObj) {
        /* Prefer explicit size — stock resizeOffscreenFramebuffer is a no-op here. */
        if (ctxObj.defaultFbo || ctxObj.defaultColorTarget) {
          cod2ResizeOffscreenTo(ctxObj, wantW, wantH);
        } else if (typeof GL !== 'undefined' && typeof GL.resizeOffscreenFramebuffer === 'function') {
          try { GL.resizeOffscreenFramebuffer(ctxObj); } catch (e1) {
            console.warn('[cod2-vid] resizeOffscreenFramebuffer', e1);
          }
        }
      }

      cod2RestoreCanvasFullBleed(c);

      var gl = (ctxObj && ctxObj.GLctx) ? ctxObj.GLctx : null;
      /*
       * With offscreen backbuffer, drawingBufferWidth can lag. Treat matching
       * canvas attributes as success — the FBO was sized to wantW/wantH above.
       */
      var cw = c.width | 0;
      var ch = c.height | 0;
      var dw = gl && gl.drawingBufferWidth ? (gl.drawingBufferWidth | 0) : cw;
      var dh = gl && gl.drawingBufferHeight ? (gl.drawingBufferHeight | 0) : ch;
      outOk = (cw === wantW && ch === wantH) ? 1 : 0;
      if (!outOk) {
        console.warn('[cod2-vid] resize mismatch want=' + wantW + 'x' + wantH +
                     ' canvas=' + cw + 'x' + ch + ' db=' + dw + 'x' + dh);
      } else {
        console.log('[cod2-vid] resized drawable to ' + wantW + 'x' + wantH +
                    ' (canvas ok, db=' + dw + 'x' + dh + ', CSS full-bleed)');
      }
    } catch (e) {
      console.warn('[cod2-vid] resize threw', e);
      outOk = 0;
    }
    return outOk;
  }
});
