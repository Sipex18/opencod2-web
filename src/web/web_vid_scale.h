#ifndef WEB_VID_SCALE_H
#define WEB_VID_SCALE_H

#ifdef __EMSCRIPTEN__

/* Query current WebGL drawing-buffer size (actual render pixels). */
void Web_GetDrawableSize(int *outW, int *outH);

/* Query browser window / container pixel size (width * devicePixelRatio, height * devicePixelRatio). */
void Web_GetWindowPixelSize(int *outW, int *outH);

/* Query detected monitor max refresh rate in Hz. */
int Web_GetMonitorMaxHz(void);

/* Resize #canvas backing store on the browser main thread; returns 1 on exact match. */
int Web_TryResizeCanvas(int wantW, int wantH);

/* Keep #canvas CSS full-viewport (Video Mode must not shrink the on-screen window). */
void Web_ForceCanvasFullBleedCss(void);

/* Triggered by JS window resize event; runs vid_restart only if current r_mode == Auto (7). */
void Web_OnWindowResize(void);

/* Rebuild r_displayRefresh enum to rates <= detected monitor Hz. */
void Web_ApplyMonitorRefreshLimit(void);

/* Map SDL/canvas mouse pixels → UI virtual 640x480 (full-bleed / drawable scale). */
void Web_MapMouseToVirtual(int sdlX, int sdlY, int *outX, int *outY);

/* Active WebGL antialias sample count used at last create/recreate (>1 => MSAA on). */
int Web_GetActiveAASamples(void);

/* Destroy+recreate PROXY_ALWAYS WebGL context when AA on/off changes. Returns 1 on success. */
int Web_RecreateGLContextForAA(int aaSamples);

#else

static inline void Web_GetDrawableSize(int *outW, int *outH)
{
    if (outW)
        *outW = 640;
    if (outH)
        *outH = 480;
}

static inline void Web_GetWindowPixelSize(int *outW, int *outH)
{
    if (outW)
        *outW = 1024;
    if (outH)
        *outH = 768;
}

static inline int Web_GetMonitorMaxHz(void)
{
    return 240;
}

static inline int Web_TryResizeCanvas(int wantW, int wantH)
{
    (void)wantW;
    (void)wantH;
    return 0;
}

static inline void Web_ForceCanvasFullBleedCss(void)
{
}

static inline void Web_OnWindowResize(void)
{
}

static inline void Web_ApplyMonitorRefreshLimit(void)
{
}

static inline void Web_MapMouseToVirtual(int sdlX, int sdlY, int *outX, int *outY)
{
    if (outX)
        *outX = sdlX;
    if (outY)
        *outY = sdlY;
}

static inline int Web_GetActiveAASamples(void)
{
    return 1;
}

static inline int Web_RecreateGLContextForAA(int aaSamples)
{
    (void)aaSamples;
    return 0;
}

#endif

#endif
