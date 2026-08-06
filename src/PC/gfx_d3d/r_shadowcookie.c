#include "common_types.h"
#include "imports.h"

#include <math.h>

extern const dvar_t *sc_blur;
extern const dvar_t *sc_debugCasterCount;
extern const dvar_t *sc_debugReceiverCount;
extern const dvar_t *sc_shadowInRate;
extern const dvar_t *sc_shadowOutRate;

static ShadowCookieGlob shadowCookieGlob;

static float s_shadowCookieProj[16];
static float s_shadowCookieAdaptDist = 100.0f;
static int   s_shadowCookieAdaptLastTime = -1;

#define SHADOW_COOKIE_PICK_CAP    24u

static float SC_Dot3(const float *a, const float *b)
{
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

static void SC_Cross3(const float *a, const float *b, float *out)
{
    out[0] = a[1]*b[2] - a[2]*b[1];
    out[1] = a[2]*b[0] - a[0]*b[2];
    out[2] = a[0]*b[1] - a[1]*b[0];
}

static void SC_Normalize3(float *v)
{
    float lenSq = SC_Dot3(v, v);
    if (lenSq < 1e-12f)
        return;
    float inv = 1.0f / sqrtf(lenSq);
    v[0] *= inv; v[1] *= inv; v[2] *= inv;
}

void R_BuildShadowCookieProjection(float *aabbMin, float *aabbMax, const float *sunDir)
{
    float negSun[3];
    float right[3], up[3], forward[3];
    float corners[8][3];
    float minP[3], maxP[3];
    int i;

    if (!aabbMin || !aabbMax || !sunDir)
        return;

    negSun[0] = -sunDir[0];
    negSun[1] = -sunDir[1];
    negSun[2] = -sunDir[2];

    if ((sunDir[0]*sunDir[0] + sunDir[1]*sunDir[1]) >= 0.1f) {
        up[0] = 0.0f; up[1] = 0.0f; up[2] = 1.0f;
    } else {
        up[0] = 1.0f; up[1] = 0.0f; up[2] = 0.0f;
    }
    SC_Cross3(up, negSun, right);
    SC_Normalize3(right);
    SC_Cross3(sunDir, right, up);
    SC_Normalize3(up);
    forward[0] = negSun[0];
    forward[1] = negSun[1];
    forward[2] = negSun[2];

    for (i = 0; i < 8; i++) {
        corners[i][0] = (i & 1) ? aabbMax[0] : aabbMin[0];
        corners[i][1] = (i & 2) ? aabbMax[1] : aabbMin[1];
        corners[i][2] = (i & 4) ? aabbMax[2] : aabbMin[2];
    }

    minP[0] = maxP[0] = SC_Dot3(corners[0], right);
    minP[1] = maxP[1] = SC_Dot3(corners[0], up);
    minP[2] = maxP[2] = SC_Dot3(corners[0], forward);
    for (i = 1; i < 8; i++) {
        float xr = SC_Dot3(corners[i], right);
        float yu = SC_Dot3(corners[i], up);
        float zf = SC_Dot3(corners[i], forward);
        if (xr < minP[0]) minP[0] = xr;
        if (xr > maxP[0]) maxP[0] = xr;
        if (yu < minP[1]) minP[1] = yu;
        if (yu > maxP[1]) maxP[1] = yu;
        if (zf < minP[2]) minP[2] = zf;
        if (zf > maxP[2]) maxP[2] = zf;
    }

    {
        const float inset = 15.0f / 16.0f;
        float dx = maxP[0] - minP[0];
        float dy = maxP[1] - minP[1];
        float dz = maxP[2] - minP[2];
        float sx = (dx > 1e-12f) ? (2.0f / dx) * inset : 1.0f;
        float sy = (dy > 1e-12f) ? (2.0f / dy) * inset : 1.0f;
        float sz = (dz > 1e-12f) ? (1.0f / dz) : 1.0f;
        float tx = (dx > 1e-12f) ? -(maxP[0] + minP[0]) / dx * inset : 0.0f;
        float ty = (dy > 1e-12f) ? -(maxP[1] + minP[1]) / dy * inset : 0.0f;
        float tz = (dz > 1e-12f) ? -(minP[2]) / dz : 0.0f;
        int j;

        for (j = 0; j < 16; j++)
            s_shadowCookieProj[j] = 0.0f;
        s_shadowCookieProj[0]  = sx;
        s_shadowCookieProj[5]  = sy;
        s_shadowCookieProj[10] = sz;
        s_shadowCookieProj[12] = tx;
        s_shadowCookieProj[13] = ty;
        s_shadowCookieProj[14] = tz;
        s_shadowCookieProj[15] = 1.0f;
    }
}

void R_GetShadowCookieProjection(float *out16)
{
    if (!out16)
        return;
    memcpy(out16, s_shadowCookieProj, sizeof(s_shadowCookieProj));
}

int R_CullAabbBy5Planes(const float *mins, const float *maxs,
                                 const float *planes)
{
    int i;
    for (i = 0; i < 5; i++) {
        const float *p = planes + i * 4;
        float px = (p[0] >= 0.0f) ? maxs[0] : mins[0];
        float py = (p[1] >= 0.0f) ? maxs[1] : mins[1];
        float pz = (p[2] >= 0.0f) ? maxs[2] : mins[2];
        if (p[0]*px + p[1]*py + p[2]*pz + p[3] < 0.0f)
            return 0;
    }
    return 1;
}

void R_PickCastersByDistance(float *outPairs, int maxCasters,
                              const float *eyePos,
                              const void *visibleList, int visibleCount,
                              int stride,
                              void (*getBounds)(int handle, float *mins, float *maxs))
{
    int i, j;
    int pick = 0;
    int remain;
    int *worst;
    int *scan;

    if (!outPairs || !eyePos || maxCasters <= 0)
        return;

    for (i = 0; i < maxCasters && i < (int)SHADOW_COOKIE_PICK_CAP; i++) {
        outPairs[i * 2]     = -1;
        *(float *)&outPairs[i * 2 + 1] = -999999.0f;
    }

    if (visibleCount <= 0 || !visibleList || !getBounds)
        return;

    worst = (int *)outPairs;
    remain = maxCasters - 1;
    if (remain < 0) remain = 0;

    for (pick = 0; pick < visibleCount; pick++) {
        const unsigned char *vis = (const unsigned char *)visibleList + pick * stride;
        int visFlags = *(const int *)(vis + 4);

        if ((*(const int *)vis == 0) && ((visFlags & 8) == 0)) {
            unsigned int tier = (unsigned int)visFlags & 0xe00u;
            if (tier != 0x200u) {
                float localMin[4], localMax[4];
                float delta[4];
                float extentSq[3];
                float maxExtent;
                float distSq;
                float score;
                unsigned int axis;

                getBounds(*(const int *)vis, localMin, localMax);
                delta[0] = localMax[0] - localMin[0];
                delta[1] = localMax[1] - localMin[1];
                delta[2] = localMax[2] - localMin[2];
                extentSq[0] = delta[0] * delta[0];
                extentSq[1] = delta[1] * delta[1];
                extentSq[2] = delta[2] * delta[2];
                axis = (extentSq[0] < extentSq[1]) ? 1u : 0u;
                if (extentSq[axis] < extentSq[2])
                    axis = 2u;
                maxExtent = delta[axis];
                if (maxExtent != 0.0f) {
                    float ex = *(const float *)(vis + 0x3c) - eyePos[0];
                    float ey = *(const float *)(vis + 0x40) - eyePos[1];
                    float ez = *(const float *)(vis + 0x44) - eyePos[2];
                    distSq = ex * ex + ey * ey + ez * ez;
                    score = distSq / maxExtent;
                    if (score < 1.0e8f) {
                        if (tier == 0x800u)
                            score = 0.0f;
                        else if (tier == 0x600u)
                            score *= 0.25f;
                        else if (tier == 0x400u)
                            score *= 4.0f;
                        if (score <= *(float *)&worst[1]) {
                            worst[1] = *(int *)&score;
                            worst[0] = pick;
                            scan = (int *)outPairs;
                            worst = (int *)outPairs;
                            for (j = 0; j < remain; j++) {
                                if (*(float *)&scan[1] < *(float *)&worst[3])
                                    worst = scan + 2;
                                scan += 2;
                            }
                        }
                    }
                }
            }
        }
    }
}

void R_FlushShadowCookieAndAdapt(int currentTimeMs,
                                  const int *casterPairs, int maxCasters,
                                  void (*renderCaster)(int view, int casterIdx, float blend))
{
    int i;
    int adaptDir = 0;
    float blurFrac;
    float floorDist, ceilDist;
    float dtSec;

    if (!casterPairs || maxCasters <= 0)
        return;

    blurFrac = (float)sc_blur->current.integer * 0.25f;
    floorDist = (1.0f - blurFrac) * s_shadowCookieAdaptDist;
    ceilDist = s_shadowCookieAdaptDist * blurFrac;

    for (i = 0; i < maxCasters && i < (int)SHADOW_COOKIE_PICK_CAP; i++) {
        int caster = casterPairs[i * 2];
        float dist = *(const float *)&casterPairs[i * 2 + 1];

        if (caster == -1) {
            adaptDir = -1;
            break;
        }
        if (s_shadowCookieAdaptDist < dist) {
            int dbgC = sc_debugCasterCount->current.integer;
            int dbgR = sc_debugReceiverCount->current.integer;
            if (dbgC + dbgR < i)
                adaptDir = -1;
            else if (i < dbgC - dbgR)
                adaptDir = 1;
            break;
        }
        {
            float blend;
            if (dist <= floorDist || dist == 0.0f)
                blend = 1.0f;
            else
                blend = (s_shadowCookieAdaptDist - dist) * (1.0f / ceilDist);
            if (renderCaster)
                renderCaster(0, caster, blend);
        }
    }

    if (s_shadowCookieAdaptLastTime == -1)
        dtSec = 0.0f;
    else {
        dtSec = (float)(currentTimeMs - s_shadowCookieAdaptLastTime) * 0.001f;
        if (dtSec < 0.0f)
            dtSec = 0.0f;
    }
    s_shadowCookieAdaptLastTime = currentTimeMs;

    if (adaptDir < 1) {
        if (adaptDir < 0)
            s_shadowCookieAdaptDist -= dtSec * sc_shadowInRate->current.value;
    } else
        s_shadowCookieAdaptDist += dtSec * sc_shadowOutRate->current.value;

    if (s_shadowCookieAdaptDist < 0.0001f)
        s_shadowCookieAdaptDist = 0.0001f;
}

void R_ResetShadowCookies(void)
{
    shadowCookieGlob.weightCap = 10.0f;
    shadowCookieGlob.lastTime = -1;
    s_shadowCookieAdaptDist = 100.0f;
    s_shadowCookieAdaptLastTime = -1;
    memset(s_shadowCookieProj, 0, sizeof(s_shadowCookieProj));
}
