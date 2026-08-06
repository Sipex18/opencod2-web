#include "web/web_vid_scale.h"

#ifdef __EMSCRIPTEN__

#include "common_types.h"
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>

/*
 * PROXY_TO_PTHREAD + OFFSCREEN_FRAMEBUFFER:
 * Stock emscripten_set_canvas_element_size fails (-4) because
 * canvas.controlTransferredOffscreen is set for the offscreen FBO path.
 * Worker-side JS patches also cannot reliably touch the DOM canvas / GL
 * object that lives on the browser UI thread.
 *
 * All real resize + CSS work runs via MAIN_THREAD_EM_ASM on the UI thread.
 */

void Web_GetDrawableSize(int *outW, int *outH)
{
    int w = 640;
    int h = 480;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_get_current_context();

    if (ctx) {
        double dw = 0.0;
        double dh = 0.0;

        if (emscripten_webgl_get_drawing_buffer_size(ctx, &dw, &dh) == EMSCRIPTEN_RESULT_SUCCESS) {
            w = (int)dw;
            h = (int)dh;
        }
    }

    /* Fallback: ask the UI thread for the live canvas / GL buffer size. */
    if (w < 2 || h < 2) {
        w = MAIN_THREAD_EM_ASM_INT({
            try {
                var c = (typeof document !== 'undefined')
                    ? (document.getElementById('canvas') || (typeof Module !== 'undefined' && Module['canvas']))
                    : null;
                if (!c) return 640;
                var gl = (c.GLctxObject && c.GLctxObject.GLctx) ? c.GLctxObject.GLctx : null;
                if (gl && gl.drawingBufferWidth > 0) return gl.drawingBufferWidth | 0;
                return (c.width | 0) || 640;
            } catch (e) { return 640; }
        });
        h = MAIN_THREAD_EM_ASM_INT({
            try {
                var c = (typeof document !== 'undefined')
                    ? (document.getElementById('canvas') || (typeof Module !== 'undefined' && Module['canvas']))
                    : null;
                if (!c) return 480;
                var gl = (c.GLctxObject && c.GLctxObject.GLctx) ? c.GLctxObject.GLctx : null;
                if (gl && gl.drawingBufferHeight > 0) return gl.drawingBufferHeight | 0;
                return (c.height | 0) || 480;
            } catch (e) { return 480; }
        });
    }

    if (w < 1)
        w = 640;
    if (h < 1)
        h = 480;

    if (outW)
        *outW = w;
    if (outH)
        *outH = h;
}

void Web_GetWindowPixelSize(int *outW, int *outH)
{
    int w = 1024;
    int h = 768;

    /*
     * Cap DPR and max drawable so HiDPI (dpr=2 @ 1080p → 4K) does not 4× fill-rate.
     * Softness trades for playable FPS under PROXY_ALWAYS + offscreen blit.
     */
    w = MAIN_THREAD_EM_ASM_INT({
        try {
            var dpr = window.devicePixelRatio || 1;
            if (dpr > 1.25) dpr = 1.25;
            var winW = window.innerWidth || (document.documentElement ? document.documentElement.clientWidth : 1024);
            var px = Math.round(winW * dpr);
            if (px > 1920) px = 1920;
            return Math.max(640, px);
        } catch (e) { return 1024; }
    });

    h = MAIN_THREAD_EM_ASM_INT({
        try {
            var dpr = window.devicePixelRatio || 1;
            if (dpr > 1.25) dpr = 1.25;
            var winH = window.innerHeight || (document.documentElement ? document.documentElement.clientHeight : 768);
            var px = Math.round(winH * dpr);
            if (px > 1080) px = 1080;
            return Math.max(480, px);
        } catch (e) { return 768; }
    });

    if (w < 1)
        w = 1024;
    if (h < 1)
        h = 768;

    if (outW)
        *outW = w;
    if (outH)
        *outH = h;
}

int Web_GetMonitorMaxHz(void)
{
    int hz = 0;
#ifdef __EMSCRIPTEN__
    hz = MAIN_THREAD_EM_ASM_INT({
        try {
            if (typeof window !== 'undefined' && window.detectedScreenHz) {
                return window.detectedScreenHz | 0;
            }
            return 0;
        } catch (e) { return 0; }
    });
#endif
    if (hz < 60)
        return 0; /* unknown / not measured yet */
    return hz;
}

static void Web_ForceCanvasFullBleedCss(void)
{
    MAIN_THREAD_EM_ASM({
        function force(c) {
            if (!c || !c.style) return;
            c.style.setProperty('width', '100%', 'important');
            c.style.setProperty('height', '100%', 'important');
            c.style.setProperty('max-width', '100%', 'important');
            c.style.setProperty('max-height', '100%', 'important');
            c.style.setProperty('object-fit', 'contain', 'important');
            c.style.setProperty('object-position', 'center', 'important');
            c.style.setProperty('border', '0', 'important');
            c.style.setProperty('display', 'block', 'important');
            c.style.setProperty('margin', '0', 'important');
            c.style.setProperty('padding', '0', 'important');
        }
        if (typeof document !== 'undefined') {
            force(document.getElementById('canvas'));
            try {
                if (typeof Module !== 'undefined' && Module['canvas'])
                    force(Module['canvas']);
            } catch (e) {}
        }
    });
}

int Web_TryResizeCanvas(int wantW, int wantH)
{
    int gotW;
    int gotH;
    int ok;

    if (wantW < 1)
        wantW = 640;
    if (wantH < 1)
        wantH = 480;

    /*
     * Resize DOM canvas + offscreen FBO on the browser UI thread, then report
     * drawingBuffer size. Do not use stock emscripten_set_canvas_element_size
     * (returns -4 when controlTransferredOffscreen is set for OFFSCREEN_FRAMEBUFFER).
     */
    ok = MAIN_THREAD_EM_ASM_INT({
        var wantW = $0 | 0;
        var wantH = $1 | 0;
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
            if (ctxObj && typeof GL !== 'undefined' && typeof GL.resizeOffscreenFramebuffer === 'function') {
                try { GL.resizeOffscreenFramebuffer(ctxObj); } catch (e1) {
                    console.warn('[cod2-vid] resizeOffscreenFramebuffer', e1);
                }
            }

            /* Keep display full-bleed; backing store is wantW x wantH (sharp). */
            if (c.style) {
                c.style.setProperty('width', '100%', 'important');
                c.style.setProperty('height', '100%', 'important');
                c.style.setProperty('max-width', '100%', 'important');
                c.style.setProperty('max-height', '100%', 'important');
                c.style.setProperty('object-fit', 'contain', 'important');
                c.style.setProperty('object-position', 'center', 'important');
                c.style.setProperty('display', 'block', 'important');
                c.style.setProperty('margin', '0', 'important');
                c.style.setProperty('padding', '0', 'important');
            }

            var gl = (ctxObj && ctxObj.GLctx) ? ctxObj.GLctx : null;
            var dw = gl && gl.drawingBufferWidth ? (gl.drawingBufferWidth | 0) : (c.width | 0);
            var dh = gl && gl.drawingBufferHeight ? (gl.drawingBufferHeight | 0) : (c.height | 0);
            outOk = (dw === wantW && dh === wantH) ? 1 : 0;
            if (!outOk)
                console.warn('[cod2-vid] resize mismatch want=' + wantW + 'x' + wantH +
                             ' got=' + dw + 'x' + dh + ' canvas=' + c.width + 'x' + c.height);
            else
                console.log('[cod2-vid] resized drawable to ' + dw + 'x' + dh);
        } catch (e) {
            console.warn('[cod2-vid] resize threw', e);
            outOk = 0;
        }
        return outOk;
    }, wantW, wantH);

    Web_GetDrawableSize(&gotW, &gotH);
    if (ok && gotW == wantW && gotH == wantH)
        return 1;

    /* One more CSS pass even on failure so we never leave a 640px CSS box. */
    Web_ForceCanvasFullBleedCss();
    return (gotW == wantW && gotH == wantH) ? 1 : 0;
}

void Web_MapMouseToVirtual(int sdlX, int sdlY, int *outX, int *outY)
{
    int fbW = 640;
    int fbH = 480;
    int cssW, cssH;
    int uiX, uiY;

    Web_GetDrawableSize(&fbW, &fbH);
    if (fbW < 1)
        fbW = 640;
    if (fbH < 1)
        fbH = 480;

    /*
     * With object-fit: contain the canvas content is letterboxed inside the
     * CSS element.  Emscripten scales mouse events linearly from CSS coords
     * to backing-store (canvas.width) coords, ignoring object-fit.  We undo
     * that scaling, subtract the contain inset, then map through the
     * displayed content rect to virtual 640x480.
     */
    cssW = MAIN_THREAD_EM_ASM_INT({
        var c = document.getElementById('canvas');
        return c ? (c.clientWidth | 0) : 0;
    });
    cssH = MAIN_THREAD_EM_ASM_INT({
        var c = document.getElementById('canvas');
        return c ? (c.clientHeight | 0) : 0;
    });

    if (cssW > 0 && cssH > 0) {
        double contentAspect = (double)fbW / (double)fbH;
        double cssAspect     = (double)cssW / (double)cssH;
        double displayW, displayH, offX, offY;
        double cssMouseX, cssMouseY;

        if (contentAspect > cssAspect) {
            displayW = (double)cssW;
            displayH = (double)cssW / contentAspect;
            offX = 0.0;
            offY = ((double)cssH - displayH) * 0.5;
        } else {
            displayH = (double)cssH;
            displayW = (double)cssH * contentAspect;
            offX = ((double)cssW - displayW) * 0.5;
            offY = 0.0;
        }

        cssMouseX = (double)sdlX * (double)cssW / (double)fbW;
        cssMouseY = (double)sdlY * (double)cssH / (double)fbH;

        uiX = (int)((cssMouseX - offX) * 640.0 / displayW + 0.5);
        uiY = (int)((cssMouseY - offY) * 480.0 / displayH + 0.5);
    } else {
        uiX = (int)((double)sdlX * 640.0 / (double)fbW + 0.5);
        uiY = (int)((double)sdlY * 480.0 / (double)fbH + 0.5);
    }

    if (uiX < 0)
        uiX = 0;
    else if (uiX > 640)
        uiX = 640;
    if (uiY < 0)
        uiY = 0;
    else if (uiY > 480)
        uiY = 480;

    if (outX)
        *outX = uiX;
    if (outY)
        *outY = uiY;
}

EMSCRIPTEN_KEEPALIVE void Web_OnWindowResize(void)
{
    extern const dvar_t *Dvar_FindVar(const char *name);
    extern void Cbuf_AddText(const char *text);
    const dvar_t *modeDvar = Dvar_FindVar("r_mode");
    if (modeDvar) {
        /*
         * Only auto-resize when the CURRENT (active) mode is Auto (7).
         * Do NOT read the latched value: if the user has latched Auto but
         * hasn't applied yet, a window resize shouldn't pre-empt that.
         * Conversely, if current is Auto but latched is a fixed mode,
         * the user hasn't applied the fixed mode yet — keep following
         * the window size under Auto until they do.
         */
        if (modeDvar->current.integer == 7) {
            Cbuf_AddText("vid_restart\n");
        }
    }
}

EMSCRIPTEN_KEEPALIVE void Web_ApplyMonitorRefreshLimit(void)
{
    extern void R_ApplyMonitorRefreshLimit(void);
    R_ApplyMonitorRefreshLimit();
}

#endif
