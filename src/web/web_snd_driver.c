/*
 * Web Audio sound driver — replaces src/PC/win32/snd_driver.c (Miles/AIL) for
 * the Emscripten build. src/PC/win32/snd_driver.c is excluded from the web
 * build (see CMakeLists.txt) and this file is compiled in its place.
 *
 * Design: src/PC/snd.c (unchanged, shared with every platform) already
 * contains all the volume/pitch/attenuation/pan/blend/looping/master-vs-slave
 * math; it only calls out to a small "driver" API (SND_InitDriver,
 * SND_StartAlias2DSample/3DSample/StreamOnChannel, SND_Is*ChannelFree,
 * SND_Get/Set*PlaybackRate, ...) to actually produce sound. This file
 * implements that same API on top of the Web Audio engine in
 * src/web/web_audio.js (see the `cod2Audio.snd` object there), mirroring the
 * control flow of src/PC/win32/snd_driver.c function-by-function and
 * replacing each AIL_* call with the equivalent MAIN_THREAD_EM_ASM call into
 * that engine.
 *
 * Known, deliberate simplifications (see project plan for full rationale):
 *   - Reverb/room effects (SND_SetRoomtype, Update*ChannelReverb) drive a
 *     shared ConvolverNode reverb send bus in web_audio.js. The per-roomtype
 *     decay/pre-delay/hfRatio *timing* values are the real ones extracted
 *     from mss32.dll's own room-preset table (see ROOM_REVERB_PARAMS in
 *     web_audio.js for the extraction details/addresses); only the impulse
 *     response *synthesis* itself (decaying filtered noise) is a heuristic,
 *     since Miles' actual filter network is closed-source DSP we don't
 *     reproduce. See the comment above SND_SetRoomtype below and the reverb
 *     bus comment in web_audio.js.
 *   - SND_RawSamples (VOIP / cinematic raw PCM audio) is a no-op; those
 *     subsystems are out of scope for this pass.
 *   - 2D one-shot sounds are always centered (matches the original Miles
 *     path, which also always sets equal L/R levels for 2D samples).
 *   - 3D one-shot sample panning: the original relies on Miles' "Fast 2D
 *     Positional Audio" provider (opened with rolloff factor 0, i.e. Miles
 *     does *no* distance attenuation of its own — see SND_InitDriver in
 *     snd_driver.c) purely to turn a 3D position into a stereo pan; volume
 *     attenuation is fully computed by the game code (SND_Attenuate) and set
 *     explicitly. We reproduce this with the same direction-to-listener
 *     dot-product pan formula already used for 3D streams
 *     (MSS_SpatializeStreamImpl in snd_driver.c) instead of a real panner
 *     provider, since the volume math is unaffected either way.
 *   - Sound "loading" cannot synchronously decode on the web (no synchronous
 *     browser decode API); SND_LoadSoundFile kicks off a best-effort async
 *     pre-decode so the AudioBuffer is normally cached long before gameplay
 *     triggers that alias. If a duration/rate query races the decode, a
 *     short fallback is used (self-corrects on subsequent plays).
 */
#ifdef __EMSCRIPTEN__

#include "common_types.h"
#include "imports.h"
#include <emscripten.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

extern snd_local_t g_snd;

extern int FS_ReadFile(const char *qpath, void **buffer);
extern void FS_FreeFile(void *buffer);
extern void Com_Printf(const char *fmt, ...);
extern void Com_DPrintf(const char *fmt, ...);
extern const char *Com_GetSoundFileName(const snd_alias_t *pAlias);

extern float Vec3Normalize(float *v);
extern float Vec3Distance(const float *a, const float *b);
extern float Vec3DistanceSq(const float *a, const float *b);
extern int SND_GetListenerIndexNearestToOrigin(const float *org);
extern void SND_GetCurrent3DPosition(int entnum, const float *offset, float *org);
extern float SND_Attenuate(SndCurve *curve, float dist, float min, float max);
extern float SND_GetLerpedSlavePercentage(float slave);
extern int SND_FindFree2DChannel(int entnum, int channel);
extern int SND_FindFree3DChannel(int entnum, int channel);
extern Bool SND_IsAliasChannel3D(int channel);
extern Bool SND_UpdateBackgroundVolume(int track, int frametime);
extern const dvar_t *snd_enableReverb;
extern const dvar_t *Dvar_RegisterString(const char *name, const char *value, int flags);
/* Storage for this symbol comes from the reconstructed .bss blob
 * (src/blobs/bss.c: mss_3d_provider[128]) — same pattern
 * src/PC/win32/snd_driver.c uses; only excluded from the web build because
 * that file is. Registered here purely so the Options > Sound menu's
 * "Sound Provider" control (ui/options_sound.menu, bound to
 * "ui_mss_3d_provider" via setfromdvar) has a real dvar to read/write
 * instead of spamming "dvar 'mss_3d_provider' doesn't exist" and leaving
 * the control in an undefined state; Web Audio has no concept of
 * EAX/Miles 3D providers, so the value is otherwise unused here. */
extern const dvar_t *mss_3d_provider;

/* ---------------------------------------------------------------------- */
/* JS engine bridge (see src/web/web_audio.js: cod2Audio.snd)             */
/* ---------------------------------------------------------------------- */

static void WebSnd_BuildPath(char *out, int outSize, const snd_alias_t *alias)
{
    snprintf(out, outSize, "sound/%s", Com_GetSoundFileName(alias));
}

static int WebSnd_JS_IsFree(int index)
{
    return MAIN_THREAD_EM_ASM_INT({
        var a = globalThis.cod2Audio;
        return (a && a.snd && a.snd.isFree($0)) ? 1 : 0;
    },
                                   index);
}

static void WebSnd_JS_Stop(int index)
{
    MAIN_THREAD_EM_ASM(
        {
            var a = globalThis.cod2Audio;
            if (a && a.snd)
                a.snd.stop($0);
        },
        index);
}

static void WebSnd_JS_Pause(int index)
{
    MAIN_THREAD_EM_ASM(
        {
            var a = globalThis.cod2Audio;
            if (a && a.snd)
                a.snd.pause($0);
        },
        index);
}

static void WebSnd_JS_Resume(int index)
{
    MAIN_THREAD_EM_ASM(
        {
            var a = globalThis.cod2Audio;
            if (a && a.snd)
                a.snd.resume($0);
        },
        index);
}

static void WebSnd_JS_SetVolume(int index, float volume)
{
    MAIN_THREAD_EM_ASM(
        {
            var a = globalThis.cod2Audio;
            if (a && a.snd)
                a.snd.setVolume($0, $1);
        },
        index, volume);
}

static void WebSnd_JS_SetPan(int index, float pan)
{
    MAIN_THREAD_EM_ASM(
        {
            var a = globalThis.cod2Audio;
            if (a && a.snd)
                a.snd.setPan($0, $1);
        },
        index, pan);
}

static void WebSnd_JS_SetRate(int index, int hz)
{
    MAIN_THREAD_EM_ASM(
        {
            var a = globalThis.cod2Audio;
            if (a && a.snd)
                a.snd.setRate($0, $1);
        },
        index, hz);
}

static int WebSnd_JS_GetRate(int index)
{
    return MAIN_THREAD_EM_ASM_INT({
        var a = globalThis.cod2Audio;
        return (a && a.snd) ? (a.snd.getRate($0) | 0) : 0;
    },
                                   index);
}

static float WebSnd_JS_GetVolume(int index)
{
    return (float)MAIN_THREAD_EM_ASM_DOUBLE({
        var a = globalThis.cod2Audio;
        return (a && a.snd) ? a.snd.getVolume($0) : 0;
    },
                                             index);
}

static int WebSnd_JS_GetPositionMs(int index)
{
    return MAIN_THREAD_EM_ASM_INT({
        var a = globalThis.cod2Audio;
        return (a && a.snd) ? (a.snd.getPositionMs($0) | 0) : 0;
    },
                                   index);
}

static int WebSnd_JS_GetChannelDurationMs(int index)
{
    return MAIN_THREAD_EM_ASM_INT({
        var a = globalThis.cod2Audio;
        return (a && a.snd) ? (a.snd.getChannelDurationMs($0) | 0) : -1;
    },
                                   index);
}

static int WebSnd_JS_GetPathDurationMs(const char *path)
{
    return MAIN_THREAD_EM_ASM_INT({
        var a = globalThis.cod2Audio;
        return (a && a.snd) ? (a.snd.getDurationMs(UTF8ToString($0)) | 0) : -1;
    },
                                   path);
}

static int WebSnd_JS_GetPathRate(const char *path)
{
    return MAIN_THREAD_EM_ASM_INT({
        var a = globalThis.cod2Audio;
        return (a && a.snd) ? (a.snd.getNativeRate(UTF8ToString($0)) | 0) : 0;
    },
                                   path);
}

static int WebSnd_JS_GetPathChannels(const char *path)
{
    return MAIN_THREAD_EM_ASM_INT({
        var a = globalThis.cod2Audio;
        return (a && a.snd) ? (a.snd.getNativeChannels(UTF8ToString($0)) | 0) : 0;
    },
                                   path);
}

static void WebSnd_JS_SetRoomType(int roomtype)
{
    MAIN_THREAD_EM_ASM(
        {
            var a = globalThis.cod2Audio;
            if (a && a.snd)
                a.snd.setRoomType($0);
        },
        roomtype);
}

static void WebSnd_JS_SetReverbSend(int index, float amount)
{
    MAIN_THREAD_EM_ASM(
        {
            var a = globalThis.cod2Audio;
            if (a && a.snd)
                a.snd.setReverbSend($0, $1);
        },
        index, amount);
}

static void WebSnd_JS_Preload(const char *path, const void *buf, int len)
{
    MAIN_THREAD_EM_ASM(
        {
            var a = globalThis.cod2Audio;
            if (!a || !a.snd)
                return;
            var ptr = $1 | 0;
            var len = $2 | 0;
            var copy = new Uint8Array(len);
            copy.set(HEAPU8.subarray(ptr, ptr + len));
            a.snd.preload(UTF8ToString($0), copy);
        },
        path, (int)(uintptr_t)buf, len);
}

static void WebSnd_JS_Start(int index, const char *path, const void *buf, int len, int loop, int rateHz,
                             float volume, float pan, float offsetSec, int startPaused)
{
    MAIN_THREAD_EM_ASM(
        {
            var a = globalThis.cod2Audio;
            if (!a || !a.snd)
                return;
            var ptr = $2 | 0;
            var len = $3 | 0;
            var copy = new Uint8Array(len);
            copy.set(HEAPU8.subarray(ptr, ptr + len));
            a.snd.start($0, UTF8ToString($1), copy, !!$4, $5 | 0, $6, $7, $8, !!$9);
        },
        index, path, (int)(uintptr_t)buf, len, loop, rateHz, volume, pan, offsetSec, startPaused);
}

static void WebSnd_JS_Start3D(int index, const char *path, const void *buf, int len, int loop, int rateHz,
                               float volume, float x, float y, float z, float offsetSec, int startPaused)
{
    MAIN_THREAD_EM_ASM(
        {
            var a = globalThis.cod2Audio;
            if (!a || !a.snd || !a.snd.start3D)
                return;
            var ptr = $2 | 0;
            var len = $3 | 0;
            var copy = new Uint8Array(len);
            copy.set(HEAPU8.subarray(ptr, ptr + len));
            a.snd.start3D($0, UTF8ToString($1), copy, !!$4, $5 | 0, $6, $7, $8, $9, $10, !!$11);
        },
        index, path, (int)(uintptr_t)buf, len, loop, rateHz, volume, x, y, z, offsetSec, startPaused);
}

static void WebSnd_JS_Set3DPosition(int index, float x, float y, float z)
{
    MAIN_THREAD_EM_ASM(
        {
            var a = globalThis.cod2Audio;
            if (a && a.snd && a.snd.set3DPosition)
                a.snd.set3DPosition($0, $1, $2, $3);
        },
        index, x, y, z);
}

/* ---------------------------------------------------------------------- */
/* SND_SetChannelInfo                                                     */
/*                                                                        */
/* The only other implementation of this symbol (src/stubs/link_stubs.c)  */
/* is a generic no-op placeholder ("int SND_SetChannelInfo() { return 0;*/
/* }") and that file is excluded from the web build entirely, so this     */
/* symbol would otherwise be undefined at runtime once the driver below   */
/* actually starts calling it. It is a plain struct-field setter fully    */
/* specified by its 3 call sites (see src/PC/win32/snd_driver.c) and by   */
/* snd_channel_info_t (common_types.h), so it is implemented here for     */
/* real rather than left stubbed.                                        */
/* ---------------------------------------------------------------------- */
void SND_SetChannelInfo(int index, int entnum, const snd_alias_t *pAlias0, const snd_alias_t *pAlias1, float lerp,
                         const vec_t *origin, float volume, float pitch, int srcChannelCount, int baserate,
                         int total_msec, int start_msec, int startDelay, int master, snd_alias_system_t system)
{
    snd_channel_info_t *ci = &g_snd.chaninfo[index];

    (void)start_msec;

    ci->entnum = entnum;
    ci->entchannel = (pAlias0->flags & 0x780) >> 7;
    ci->startDelay = startDelay;
    ci->looptime = g_snd.looptime;
    ci->endtime = g_snd.time + total_msec;
    ci->basevolume = volume;
    ci->baserate = baserate;
    ci->pitch = pitch;
    ci->srcChannelCount = srcChannelCount;
    ci->pAlias0 = pAlias0;
    ci->pAlias1 = pAlias1;
    ci->lerp = lerp;
    if (origin) {
        ci->org[0] = origin[0];
        ci->org[1] = origin[1];
        ci->org[2] = origin[2];
    }
    ci->paused = 0;
    ci->master = (Bool)master;
    ci->system = system;
}

/* ---------------------------------------------------------------------- */
/* Driver lifecycle                                                       */
/* ---------------------------------------------------------------------- */

Bool SND_InitDriver(void)
{
    Com_Printf("Attempting 44 kHz 16 bit stereo sound (Web Audio)\n");

    mss_3d_provider = Dvar_RegisterString("mss_3d_provider", "Miles Fast 2D Positional Audio", 0x1021);

    g_snd.Initialized2d = 1;
    g_snd.Initialized3d = 1;
    g_snd.max_2D_channels = 8;
    g_snd.max_3D_channels = 32;
    g_snd.max_stream_channels = 13;
    g_snd.playback_rate = 44100;
    g_snd.playback_bits = 16;
    g_snd.playback_channels = 2;
    g_snd.timescale = 1.0f;
    g_snd.ambient_track = 2;

    Com_Printf("Web Audio sound driver initialized (%i 2D / %i 3D / %i stream channels)\n", g_snd.max_2D_channels,
               g_snd.max_3D_channels, g_snd.max_stream_channels);
    return 1;
}

void SND_ShutdownDriver(void)
{
    int i;
    for (i = 0; i < 53; i++) {
        WebSnd_JS_Stop(i);
    }
}

int SND_GetDriverCPUPercentage(void)
{
    return 0;
}

void SND_DriverPreUpdate(int frametime)
{
    (void)frametime;
}

void SND_DriverPostUpdate(int frametime)
{
    (void)frametime;
}

/* ---------------------------------------------------------------------- */
/* Sound file loading                                                     */
/* ---------------------------------------------------------------------- */

static char s_webSoundFileLoadedMarker;
static int s_fallbackDuration[53];

void SND_LoadSoundFile(SoundFile *soundFile)
{
    char realname[256];
    void *buffer;
    int len;

    if (!g_snd.Initialized2d) {
        soundFile->fileMem = NULL;
        return;
    }

    snprintf(realname, sizeof(realname), "sound/%s", soundFile->soundName);

    len = FS_ReadFile(realname, &buffer);
    if (len <= 0 || !buffer) {
        Com_Printf("^1ERROR: Sound file '%s' not found\n", realname);
        soundFile->fileMem = NULL;
        return;
    }

    /* Best-effort async pre-decode; SND_StartAlias2DSample/3DSample/StreamOnChannel
     * re-supply the bytes at play time too, so this is not load-bearing for
     * correctness, only for having the AudioBuffer ready before gameplay. */
    WebSnd_JS_Preload(realname, buffer, len);
    FS_FreeFile(buffer);

    soundFile->fileMem = (struct MssSound *)&s_webSoundFileLoadedMarker;
}

int SND_GetSoundFileSize(const void *pSoundFile)
{
    (void)pSoundFile;
    return 0;
}

/* ---------------------------------------------------------------------- */
/* Reverb / room effects                                                  */
/*                                                                        */
/* src/PC/win32/snd_driver.c only ever calls AIL_set_x_reverb_levels(S,   */
/* dryLevel=1.0 always, wetLevel), so the game never crossfades dry vs    */
/* wet — reverb is purely an auxiliary "send" mixed in parallel           */
/* with the always-on dry signal. That maps directly onto a Web Audio     */
/* send-to-ConvolverNode bus (see cod2Audio.snd.setRoomType/setReverbSend */
/* in src/web/web_audio.js). The per-roomtype decay/pre-delay/hfRatio      */
/* values there are the real ones extracted from mss32.dll's own 26-entry */
/* EAX-preset table (matches CoD2's own snd_roomStrings name list 1:1);   */
/* only the synthetic impulse-response generator itself is a documented  */
/* heuristic, since Miles' internal filter network is closed-source DSP  */
/* we don't reproduce.                                                    */
/*                                                                        */
/* NOTE: the decompiled SND_Update2DChannelReverb/3DChannelReverb bodies  */
/* in snd_driver.c compute "reverbLevel = 0" exactly when                 */
/* (snd_enableReverb->current.enabled && !(flags & SA_NO_WET)) is true -  */
/* i.e. literally interpreted, reverb is silenced whenever it should be   */
/* audible. That is almost certainly a decompiler De Morgan sign-flip     */
/* (taken at face value the reverb feature could never have worked), so   */
/* we use the sane condition below instead of a literal port.             */
/* ---------------------------------------------------------------------- */

void SND_SetRoomtype(int roomtype)
{
    WebSnd_JS_SetRoomType(roomtype);
}

static float WebSnd_ComputeReverbSend(const snd_channel_info_t *ci)
{
    const snd_alias_t *pAlias0 = ci->pAlias0;

    if (pAlias0 == NULL)
        return 0.0f;
    if (snd_enableReverb && !snd_enableReverb->current.enabled)
        return 0.0f;
    if (pAlias0->flags & 0x10) /* SA_NO_WET */
        return 0.0f;
    return g_snd.effect ? g_snd.effect->wetlevel : 0.0f;
}

void SND_Update2DChannelReverb(int index)
{
    WebSnd_JS_SetReverbSend(index, WebSnd_ComputeReverbSend(&g_snd.chaninfo[index]));
}

void SND_Update3DChannelReverb(int index)
{
    WebSnd_JS_SetReverbSend(index, WebSnd_ComputeReverbSend(&g_snd.chaninfo[index]));
}

void SND_UpdateStreamChannelReverb(int index)
{
    WebSnd_JS_SetReverbSend(index, WebSnd_ComputeReverbSend(&g_snd.chaninfo[index]));
}

/* ---------------------------------------------------------------------- */
/* Raw PCM samples (VOIP / cinematics) — no-ops (see file header)         */
/* ---------------------------------------------------------------------- */

void SND_EndRawSamples(void)
{
}

int SND_RawSamplesTime(void)
{
    return 0;
}

void SND_RawSamples(int samples, int rate, int width, int s_channels, const byte *data)
{
    (void)samples;
    (void)rate;
    (void)width;
    (void)s_channels;
    (void)data;
}

/* ---------------------------------------------------------------------- */
/* 2D channels (indices 45..52)                                           */
/* ---------------------------------------------------------------------- */

void SND_Stop2DChannel(int index)
{
    WebSnd_JS_Stop(index);
    g_snd.chaninfo[index].paused = 0;
    g_snd.chaninfo[index].startDelay = 0;
    s_fallbackDuration[index] = 0;
}

void SND_Pause2DChannel(int index)
{
    WebSnd_JS_Pause(index);
    g_snd.chaninfo[index].paused = 1;
}

void SND_Unpause2DChannel(int index, int timeshift)
{
    if (g_snd.chaninfo[index].startDelay == 0) {
        WebSnd_JS_Resume(index);
    }
    g_snd.chaninfo[index].endtime += timeshift;
    g_snd.chaninfo[index].paused = 0;
}

Bool SND_Is2DChannelFree(int index)
{
    snd_channel_info_t *ci = &g_snd.chaninfo[index];
    if (ci->paused != 0)
        return 0;
    if (ci->startDelay != 0)
        return 0;
    if (ci->pAlias0 == NULL && ci->pAlias1 == NULL)
        return 1;
    return WebSnd_JS_IsFree(index) ? 1 : 0;
}

int SND_Get2DChannelPlaybackRate(int index)
{
    return WebSnd_JS_GetRate(index);
}

void SND_Set2DChannelPlaybackRate(int index, int rate)
{
    WebSnd_JS_SetRate(index, rate);
}

float SND_Get2DChannelVolume(int index)
{
    return WebSnd_JS_GetVolume(index);
}

int SND_Get2DChannelLength(int index)
{
    return WebSnd_JS_GetChannelDurationMs(index);
}

void SND_Get2DChannelSaveInfo(int index, snd_save_2D_sample_t *info)
{
    int totalMs = WebSnd_JS_GetChannelDurationMs(index);
    int posMs = WebSnd_JS_GetPositionMs(index);
    float vol = WebSnd_JS_GetVolume(index);

    info->fraction = (totalMs > 0) ? ((float)posMs / (float)totalMs) : 0.0f;
    info->pitch = g_snd.chaninfo[index].pitch;
    if (g_snd.volume != 0.0f) {
        info->volume = vol / g_snd.volume;
    } else {
        info->volume = g_snd.chaninfo[index].basevolume;
    }
    info->pan = 0.5f; /* 2D channels are always centered */
}

void SND_Set2DChannelFromSaveInfo(int index, snd_save_2D_sample_t *info)
{
    WebSnd_JS_SetVolume(index, info->volume * g_snd.volume);
}

void SND_Update2DChannel(int i, int frametime)
{
    snd_channel_info_t *ci = &g_snd.chaninfo[i];
    const snd_alias_t *pAlias0;
    int aliasFlags, channel, startDelay, newDelay;
    float volume, scaledVol, *volTable;

    if (ci->paused != 0)
        return;

    pAlias0 = ci->pAlias0;
    if (pAlias0 == NULL || ci->pAlias1 == NULL)
        return;

    volume = ci->basevolume;

    aliasFlags = pAlias0->flags;
    if (g_snd.slaveLerp != 0.0f && ci->master == 0 && (aliasFlags & 4)) {
        volume *= SND_GetLerpedSlavePercentage(pAlias0->fSlavePercentage);
    }

    volTable = (float *)g_snd.channelvol;
    channel = (aliasFlags & 0x780) >> 7;
    scaledVol = volume * volTable[channel * 3] * g_snd.volume;

    WebSnd_JS_SetVolume(i, scaledVol);

    startDelay = ci->startDelay;
    if (startDelay != 0) {
        newDelay = startDelay - frametime;
        if (frametime >= startDelay)
            newDelay = 0;
        ci->startDelay = newDelay;
        if (newDelay == 0)
            WebSnd_JS_Resume(i);
    }
}

int SND_StartAlias2DSample(const snd_alias_t *pAlias0, const snd_alias_t *pAlias1, float lerp, int entnum,
                            float volume, float pitch, int timeshift, float fraction, int startDelay, int master,
                            int *pChannel, snd_alias_system_t system)
{
    int index, channel, aliasFlags, numCh, baseRateHz, rateHz, totalMs, startMs;
    char path[256];
    void *buf;
    int len;
    float scaledVol, volMul, startOffsetSec, *volTable;

    aliasFlags = pAlias0->flags;
    channel = (aliasFlags & 0x780) >> 7;
    index = SND_FindFree2DChannel(entnum, channel);
    if (pChannel)
        *pChannel = index;
    if (index < 0)
        return 0;

    WebSnd_BuildPath(path, sizeof(path), pAlias0);

    buf = NULL;
    len = FS_ReadFile(path, &buf);
    if (len <= 0 || !buf) {
        Com_DPrintf("Tried to play sound '%s' from alias '%s', but it was not successfully loaded.\n", path,
                     pAlias0->pszAliasName);
        if (pChannel)
            *pChannel = -1;
        return 0;
    }

    baseRateHz = WebSnd_JS_GetPathRate(path);
    if (baseRateHz <= 0)
        baseRateHz = 44100;
    rateHz = (int)floorf((float)baseRateHz * pitch * g_snd.timescale + 0.5f);

    numCh = WebSnd_JS_GetPathChannels(path);
    if (numCh <= 0)
        numCh = 1;

    totalMs = WebSnd_JS_GetPathDurationMs(path);
    if (totalMs <= 0) {
        totalMs = 500;
        s_fallbackDuration[index] = 500;
    } else {
        s_fallbackDuration[index] = 0;
    }

    if (totalMs <= timeshift) {
        FS_FreeFile(buf);
        return 0;
    }

    if (fraction != 0.0f) {
        startMs = (int)floorf((float)totalMs * fraction + 0.5f);
    } else {
        startMs = timeshift;
    }
    if (startMs != 0) {
        startDelay = 0;
    }
    startOffsetSec = (float)startMs / 1000.0f;

    volTable = (float *)g_snd.channelvol;
    scaledVol = volume * g_snd.volume * volTable[channel * 3];
    if (g_snd.slaveLerp != 0.0f && !master && (aliasFlags & 4)) {
        scaledVol *= SND_GetLerpedSlavePercentage(pAlias0->fSlavePercentage);
    }
    volMul = (numCh == 2) ? 1.0f : 0.5f;
    scaledVol *= volMul;

    WebSnd_JS_Start(index, path, buf, len, (aliasFlags & 1) ? 1 : 0, rateHz, scaledVol, 0.0f, startOffsetSec,
                     startDelay != 0 ? 1 : 0);
    FS_FreeFile(buf);

    totalMs += startDelay;
    if (aliasFlags & 1)
        totalMs = 0;

    SND_SetChannelInfo(index, entnum, pAlias0, pAlias1, lerp, NULL, volume, pitch, numCh, baseRateHz, totalMs,
                        startMs, startDelay, master, system);
    return totalMs;
}

/* ---------------------------------------------------------------------- */
/* 3D channels (indices 0..31)                                            */
/* ---------------------------------------------------------------------- */

void SND_Stop3DChannel(int index)
{
    WebSnd_JS_Stop(index);
    g_snd.chaninfo[index].paused = 0;
    g_snd.chaninfo[index].startDelay = 0;
    s_fallbackDuration[index] = 0;
}

void SND_Pause3DChannel(int index)
{
    WebSnd_JS_Pause(index);
    g_snd.chaninfo[index].paused = 1;
}

void SND_Unpause3DChannel(int index, int timeshift)
{
    if (g_snd.chaninfo[index].startDelay == 0) {
        WebSnd_JS_Resume(index);
    }
    g_snd.chaninfo[index].endtime += timeshift;
    g_snd.chaninfo[index].paused = 0;
}

Bool SND_Is3DChannelFree(int index)
{
    snd_channel_info_t *ci = &g_snd.chaninfo[index];
    if (ci->paused != 0)
        return 0;
    if (ci->startDelay != 0)
        return 0;
    if (ci->pAlias0 == NULL && ci->pAlias1 == NULL)
        return 1;
    return WebSnd_JS_IsFree(index) ? 1 : 0;
}

int SND_Get3DChannelPlaybackRate(int index)
{
    return WebSnd_JS_GetRate(index);
}

void SND_Set3DChannelPlaybackRate(int index, int rate)
{
    WebSnd_JS_SetRate(index, rate);
}

float SND_Get3DChannelVolume(int index)
{
    return WebSnd_JS_GetVolume(index);
}

int SND_Get3DChannelLength(int index)
{
    return WebSnd_JS_GetChannelDurationMs(index);
}

void SND_Get3DChannelSaveInfo(int index, snd_save_3D_sample_t *info)
{
    int totalMs = WebSnd_JS_GetChannelDurationMs(index);
    int posMs = WebSnd_JS_GetPositionMs(index);
    float vol = WebSnd_JS_GetVolume(index);

    info->fraction = (totalMs > 0) ? ((float)posMs / (float)totalMs) : 0.0f;
    info->pitch = g_snd.chaninfo[index].pitch;
    if (g_snd.volume != 0.0f) {
        info->volume = vol / g_snd.volume;
    } else {
        info->volume = g_snd.chaninfo[index].basevolume;
    }
    info->org[0] = g_snd.chaninfo[index].org[0];
    info->org[1] = g_snd.chaninfo[index].org[1];
    info->org[2] = g_snd.chaninfo[index].org[2];
}

/* Compute listener-local 3D coordinates from a world-space origin.
 * This is the exact same matrix transform Miles uses in SND_Set3DPosition
 * (snd_driver.c line 713-738): projects world delta onto listener's
 * right/up/forward axes to get listener-local (x, y, z) where:
 *   x = right, y = -up (down), z = forward.
 * These listener-local coords are passed to the Web Audio PannerNode which
 * provides real HRTF spatialization. */
static void WebSnd_Compute3DPositionFromOrg(const vec_t *org, float *outX, float *outY, float *outZ)
{
    int listenerIdx;
    snd_listener *listener;
    float *listenerOrigin, *right, *up, *forward;
    float dx, dy, dz;

    listenerIdx = SND_GetListenerIndexNearestToOrigin(org);
    listener = &g_snd.listeners[listenerIdx];
    listenerOrigin = listener->orient.origin;
    right = listener->orient.axis[0];
    up = listener->orient.axis[1];
    forward = listener->orient.axis[2];

    dx = org[0] - listenerOrigin[0];
    dy = org[1] - listenerOrigin[1];
    dz = org[2] - listenerOrigin[2];

    *outX = dx * right[0] + dy * right[1] + dz * right[2];
    *outZ = dx * forward[0] + dy * forward[1] + dz * forward[2];
    *outY = -(dx * up[0] + dy * up[1] + dz * up[2]);
}

/* Legacy pan fallback for stream channels that haven't been upgraded to
 * PannerNode yet (also used by WebSnd_ComputeStreamPan). */
static float WebSnd_ComputePanFromOrg(const vec_t *org)
{
    int listenerIdx;
    snd_listener *listener;
    vec3_t delta;
    float dot;

    listenerIdx = SND_GetListenerIndexNearestToOrigin(org);
    listener = &g_snd.listeners[listenerIdx];

    delta[0] = org[0] - listener->orient.origin[0];
    delta[1] = org[1] - listener->orient.origin[1];
    delta[2] = org[2] - listener->orient.origin[2];
    Vec3Normalize(delta);

    dot = delta[0] * listener->orient.axis[0][0] + delta[1] * listener->orient.axis[0][1] +
          delta[2] * listener->orient.axis[0][2];
    if (dot < -1.0f)
        dot = -1.0f;
    if (dot > 1.0f)
        dot = 1.0f;
    return dot;
}

void SND_Set3DPosition(int index, const vec_t *org)
{
    float x, y, z;
    WebSnd_Compute3DPositionFromOrg(org, &x, &y, &z);
    WebSnd_JS_Set3DPosition(index, x, y, z);
}

void SND_Update3DChannel(int i, int frametime)
{
    snd_channel_info_t *ci = &g_snd.chaninfo[i];
    const snd_alias_t *pAlias0, *pAlias1;
    float lerp, volume, fDistMin, fDistMax, dist, attenuation, scaledVol, *volTable;
    vec3_t org;
    int aliasFlags, channel, listenerIdx, startDelay, newDelay;

    ci = &g_snd.chaninfo[i];
    if (ci->paused != 0)
        return;

    pAlias0 = ci->pAlias0;
    pAlias1 = ci->pAlias1;
    if (pAlias0 == NULL || pAlias1 == NULL)
        return;

    lerp = ci->lerp;
    volume = ci->basevolume;

    SND_GetCurrent3DPosition(ci->entnum, ci->offset, org);
    SND_Set3DPosition(i, org);

    fDistMin = (1.0f - lerp) * pAlias0->fDistMin + lerp * pAlias1->fDistMin;
    fDistMax = (1.0f - lerp) * pAlias0->fDistMax + lerp * pAlias1->fDistMax;

    listenerIdx = SND_GetListenerIndexNearestToOrigin(org);
    dist = Vec3Distance(org, g_snd.listeners[listenerIdx].orient.origin);
    attenuation = SND_Attenuate(pAlias0->volumeFalloffCurve, dist, fDistMin, fDistMax);
    volume *= attenuation;

    aliasFlags = pAlias0->flags;
    if (g_snd.slaveLerp != 0.0f && ci->master == 0 && (aliasFlags & 4)) {
        volume *= SND_GetLerpedSlavePercentage(pAlias0->fSlavePercentage);
    }

    volTable = (float *)g_snd.channelvol;
    channel = (aliasFlags & 0x780) >> 7;
    scaledVol = volume * volTable[channel * 3] * g_snd.volume;
    WebSnd_JS_SetVolume(i, scaledVol);

    startDelay = ci->startDelay;
    if (startDelay != 0) {
        newDelay = startDelay - frametime;
        if (frametime >= startDelay)
            newDelay = 0;
        ci->startDelay = newDelay;
        if (newDelay == 0)
            WebSnd_JS_Resume(i);
    }
}

int SND_StartAlias3DSample(const snd_alias_t *pAlias0, const snd_alias_t *pAlias1, float lerp, int entnum,
                            const vec_t *org, float volume, float pitch, int timeshift, float fraction,
                            int startDelay, int master, int *pChannel, snd_alias_system_t system)
{
    int index, channel, aliasFlags, numCh, baseRateHz, rateHz, totalMs, startMs, listenerIdx;
    char path[256];
    void *buf;
    int len;
    float scaledVol, startOffsetSec, *volTable;
    float fDistMin, fDistMax, oneMinusLerp, dist, attenuation;
    float posX, posY, posZ;

    aliasFlags = pAlias0->flags;
    channel = (aliasFlags & 0x780) >> 7;
    index = SND_FindFree3DChannel(entnum, channel);
    if (pChannel)
        *pChannel = index;
    if (index < 0)
        return 0;

    WebSnd_BuildPath(path, sizeof(path), pAlias0);

    buf = NULL;
    len = FS_ReadFile(path, &buf);
    if (len <= 0 || !buf) {
        Com_DPrintf("Tried to play sound '%s' from alias '%s', but it was not successfully loaded.\n", path,
                     pAlias0->pszAliasName);
        if (pChannel)
            *pChannel = -1;
        return 0;
    }

    oneMinusLerp = 1.0f - lerp;
    fDistMin = oneMinusLerp * pAlias0->fDistMin + lerp * pAlias1->fDistMin;
    fDistMax = oneMinusLerp * pAlias0->fDistMax + lerp * pAlias1->fDistMax;

    listenerIdx = SND_GetListenerIndexNearestToOrigin(org);
    dist = Vec3Distance(org, g_snd.listeners[listenerIdx].orient.origin);
    attenuation = SND_Attenuate(pAlias0->volumeFalloffCurve, dist, fDistMin, fDistMax);

    volTable = (float *)g_snd.channelvol;
    channel = (aliasFlags & 0x780) >> 7;
    scaledVol = attenuation * volume * volTable[channel * 3] * g_snd.volume;
    if (g_snd.slaveLerp != 0.0f && !master && (aliasFlags & 4)) {
        scaledVol *= SND_GetLerpedSlavePercentage(pAlias0->fSlavePercentage);
    }

    baseRateHz = WebSnd_JS_GetPathRate(path);
    if (baseRateHz <= 0)
        baseRateHz = 44100;
    rateHz = (int)floorf((float)baseRateHz * pitch * g_snd.timescale + 0.5f);

    numCh = WebSnd_JS_GetPathChannels(path);
    if (numCh <= 0)
        numCh = 1;

    totalMs = WebSnd_JS_GetPathDurationMs(path);
    if (totalMs <= 0)
        totalMs = 500;

    if (totalMs <= timeshift) {
        FS_FreeFile(buf);
        return 0;
    }

    if (fraction != 0.0f) {
        startMs = (int)floorf((float)totalMs * fraction + 0.5f);
    } else {
        startMs = timeshift;
    }
    if (startMs != 0) {
        startDelay = 0;
    }
    startOffsetSec = (float)startMs / 1000.0f;

    /* Compute listener-local 3D position for HRTF panning */
    WebSnd_Compute3DPositionFromOrg(org, &posX, &posY, &posZ);
    WebSnd_JS_Start3D(index, path, buf, len, (aliasFlags & 1) ? 1 : 0, rateHz, scaledVol,
                       posX, posY, posZ, startOffsetSec, startDelay != 0 ? 1 : 0);
    FS_FreeFile(buf);

    totalMs += startDelay;
    if (aliasFlags & 1)
        totalMs = 0;

    SND_SetChannelInfo(index, entnum, pAlias0, pAlias1, lerp, org, volume, pitch, numCh, baseRateHz, totalMs, startMs,
                        startDelay, master, system);
    return totalMs;
}

/* ---------------------------------------------------------------------- */
/* Stream channels (indices 32..44): ambient loops + music                */
/* ---------------------------------------------------------------------- */

void SND_StopStreamChannel(int index)
{
    WebSnd_JS_Stop(index);
    g_snd.chaninfo[index].paused = 0;
    g_snd.chaninfo[index].startDelay = 0;
    s_fallbackDuration[index] = 0;
}

void SND_PauseStreamChannel(int index)
{
    WebSnd_JS_Pause(index);
    g_snd.chaninfo[index].paused = 1;
}

void SND_UnpauseStreamChannel(int index, int timeshift)
{
    if (g_snd.chaninfo[index].startDelay == 0) {
        WebSnd_JS_Resume(index);
    }
    g_snd.chaninfo[index].endtime += timeshift;
    g_snd.chaninfo[index].paused = 0;
}

Bool SND_IsStreamChannelFree(int index)
{
    snd_channel_info_t *ci = &g_snd.chaninfo[index];
    if (ci->paused != 0)
        return 0;
    if (ci->startDelay != 0)
        return 0;
    if (ci->pAlias0 == NULL && ci->pAlias1 == NULL)
        return 1;
    return WebSnd_JS_IsFree(index) ? 1 : 0;
}

int SND_GetStreamChannelPlaybackRate(int index)
{
    return WebSnd_JS_GetRate(index);
}

void SND_SetStreamChannelPlaybackRate(int index, int rate)
{
    WebSnd_JS_SetRate(index, rate);
}

float SND_GetStreamChannelVolume(int index)
{
    return WebSnd_JS_GetVolume(index);
}

int SND_GetStreamChannelLength(int index)
{
    return WebSnd_JS_GetChannelDurationMs(index);
}

void SND_GetStreamChannelSaveInfo(int index, snd_save_stream_t *info)
{
    int totalMs = WebSnd_JS_GetChannelDurationMs(index);
    int posMs = WebSnd_JS_GetPositionMs(index);
    float vol = WebSnd_JS_GetVolume(index);

    info->fraction = (totalMs > 0) ? ((float)posMs / (float)totalMs) : 0.0f;
    info->rate = WebSnd_JS_GetRate(index);
    info->basevolume = g_snd.chaninfo[index].basevolume;
    if (g_snd.volume != 0.0f) {
        info->volume = vol / g_snd.volume;
    } else {
        info->volume = g_snd.chaninfo[index].basevolume;
    }
    info->pan = 0.5f;
    info->org[0] = g_snd.chaninfo[index].org[0];
    info->org[1] = g_snd.chaninfo[index].org[1];
    info->org[2] = g_snd.chaninfo[index].org[2];
}

void SND_SetStreamChannelFromSaveInfo(int index, snd_save_stream_t *info)
{
    WebSnd_JS_SetVolume(index, info->volume * g_snd.volume);
}

/* Mirrors MSS_SpatializeStreamImpl in snd_driver.c (see file header for the
 * pass-savedVolume-as-distance quirk we deliberately reproduce as-is). */
static void WebSnd_ComputeStreamPan(const vec_t *orgVec, float lerp, const snd_alias_t *pAlias0,
                                     const snd_alias_t *pAlias1, float *volume, float *pan)
{
    int listenerIdx;
    snd_listener *listener;
    vec3_t delta;
    float dot, oneMinusLerp, fDistMin, fDistMax, savedVolume, attenuation;

    listenerIdx = SND_GetListenerIndexNearestToOrigin(orgVec);
    listener = &g_snd.listeners[listenerIdx];

    delta[0] = orgVec[0] - listener->orient.origin[0];
    delta[1] = orgVec[1] - listener->orient.origin[1];
    delta[2] = orgVec[2] - listener->orient.origin[2];
    Vec3Normalize(delta);

    dot = delta[0] * listener->orient.axis[0][0] + delta[1] * listener->orient.axis[0][1] +
          delta[2] * listener->orient.axis[0][2];

    oneMinusLerp = 1.0f - lerp;
    fDistMin = oneMinusLerp * pAlias0->fDistMin + lerp * pAlias1->fDistMin;
    fDistMax = oneMinusLerp * pAlias0->fDistMax + lerp * pAlias1->fDistMax;

    savedVolume = *volume;
    attenuation = SND_Attenuate(pAlias0->volumeFalloffCurve, savedVolume, fDistMin, fDistMax);
    *volume = savedVolume * attenuation;
    *pan = dot;
}

void SND_UpdateStreamChannel(int i, int frametime)
{
    snd_channel_info_t *ci = &g_snd.chaninfo[i];
    const snd_alias_t *pAlias0;
    float volume, pan, scaledVol, *volTable;
    int aliasFlags, channel, startDelay, newDelay;

    if (ci->paused != 0)
        return;

    if (i <= 0x22) {
        if (!SND_UpdateBackgroundVolume(i - 0x20, frametime))
            return;
    }

    pAlias0 = ci->pAlias0;
    if (pAlias0 == NULL || ci->pAlias1 == NULL)
        return;

    volume = ci->basevolume;
    pan = 0.0f;

    aliasFlags = pAlias0->flags;
    channel = (aliasFlags & 0x780) >> 7;

    if (SND_IsAliasChannel3D(channel)) {
        SND_GetCurrent3DPosition(ci->entnum, ci->offset, ci->org);
        WebSnd_ComputeStreamPan(ci->org, ci->lerp, pAlias0, ci->pAlias1, &volume, &pan);
    }

    if (g_snd.slaveLerp != 0.0f && ci->master == 0 && (aliasFlags & 4)) {
        volume *= SND_GetLerpedSlavePercentage(pAlias0->fSlavePercentage);
    }

    volTable = (float *)g_snd.channelvol;
    scaledVol = volume * volTable[channel * 3] * g_snd.volume;

    WebSnd_JS_SetVolume(i, scaledVol);
    WebSnd_JS_SetPan(i, pan);

    startDelay = ci->startDelay;
    if (startDelay != 0) {
        newDelay = startDelay - frametime;
        if (frametime >= startDelay)
            newDelay = 0;
        ci->startDelay = newDelay;
        if (newDelay == 0)
            WebSnd_JS_Resume(i);
    }
}

int SND_StartAliasStreamOnChannel(const snd_alias_t *pAlias0, const snd_alias_t *pAlias1, float lerp, int entnum,
                                   const vec_t *org, float volume, float pitch, int timeshift, float fraction,
                                   int startDelay, int master, int index, snd_alias_system_t system)
{
    char path[256];
    void *buf;
    int len, aliasFlags, channel, numCh, baseRateHz, rateHz, totalMs, startMs;
    float scaledVol, startOffsetSec, *volTable;
    int is3D;

    if (pAlias0->soundFile->isStreamFound == 0) {
        Com_DPrintf("Tried to play streamed sound '%s' from alias '%s', but it was not found at load time.\n",
                     Com_GetSoundFileName(pAlias0), pAlias0->pszAliasName);
        return 0;
    }

    WebSnd_BuildPath(path, sizeof(path), pAlias0);

    buf = NULL;
    len = FS_ReadFile(path, &buf);
    if (len <= 0 || !buf) {
        Com_Printf("Couldn't play stream '%s' from alias '%s' - file not found\n", path, pAlias0->pszAliasName);
        return 0;
    }

    baseRateHz = WebSnd_JS_GetPathRate(path);
    if (baseRateHz <= 0)
        baseRateHz = 44100;
    rateHz = (int)floorf((float)baseRateHz * pitch * g_snd.timescale + 0.5f);

    numCh = WebSnd_JS_GetPathChannels(path);
    if (numCh <= 0)
        numCh = 1;

    aliasFlags = pAlias0->flags;
    channel = (aliasFlags & 0x780) >> 7;
    volTable = (float *)g_snd.channelvol;
    scaledVol = g_snd.volume * volume * volTable[channel * 3];
    if (g_snd.slaveLerp != 0.0f && !master && (aliasFlags & 4)) {
        scaledVol *= SND_GetLerpedSlavePercentage(pAlias0->fSlavePercentage);
    }

    is3D = SND_IsAliasChannel3D(channel) && org;

    totalMs = WebSnd_JS_GetPathDurationMs(path);
    if (totalMs <= 0)
        totalMs = 1000;

    if (totalMs <= timeshift) {
        FS_FreeFile(buf);
        return 0;
    }

    if (fraction != 0.0f) {
        startMs = (int)floorf((float)totalMs * fraction + 0.5f);
    } else {
        startMs = timeshift;
    }
    if (startMs != 0) {
        startDelay = 0;
    }
    startOffsetSec = (float)startMs / 1000.0f;

    if (is3D) {
        float posX, posY, posZ;
        WebSnd_Compute3DPositionFromOrg(org, &posX, &posY, &posZ);
        WebSnd_JS_Start3D(index, path, buf, len, (aliasFlags & 1) ? 1 : 0, rateHz, scaledVol,
                           posX, posY, posZ, startOffsetSec, startDelay != 0 ? 1 : 0);
    } else {
        WebSnd_JS_Start(index, path, buf, len, (aliasFlags & 1) ? 1 : 0, rateHz, scaledVol, 0.0f, startOffsetSec,
                         startDelay != 0 ? 1 : 0);
    }
    FS_FreeFile(buf);

    totalMs += startDelay;
    if (aliasFlags & 1)
        totalMs = 0;

    if (org) {
        g_snd.chaninfo[index].org[0] = org[0];
        g_snd.chaninfo[index].org[1] = org[1];
        g_snd.chaninfo[index].org[2] = org[2];
    }

    SND_SetChannelInfo(index, entnum, pAlias0, pAlias1, lerp, org, volume, pitch, numCh, baseRateHz, totalMs, startMs,
                        startDelay, master, system);
    return totalMs;
}

#endif /* __EMSCRIPTEN__ */
