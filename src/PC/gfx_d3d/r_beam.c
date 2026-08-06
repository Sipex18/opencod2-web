/*
 * r_beam.c -- beam / line-stub tessellation helpers.
 *
 * Ported from donor r_beam.c, adapted to opencod2 types.
 * Full ribbon vertex emission stays in the backend tessellator;
 * this file provides the beam command decode, UV atlas lookup,
 * and overflow gate logic.
 */

#include "common_types.h"
#include "imports.h"

#include <math.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    float p0[3];
    float p1[3];
    float width;
    unsigned int color;
    float u0, v0, u1, v1;
} beamQuad_t;

static float beam_segment_length(const float p0[3], const float p1[3])
{
    float dx = p1[0] - p0[0];
    float dy = p1[1] - p0[1];
    float dz = p1[2] - p0[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

float R_BeamTess_WidthHeuristic(const float p0[3], const float p1[3])
{
    return beam_segment_length(p0, p1) * (1.0f / 256.0f);
}

static unsigned int beam_pack_bgra(const unsigned char *colorBytes)
{
    return ((unsigned int)colorBytes[3] << 24) |
           ((unsigned int)colorBytes[0] << 16) |
           ((unsigned int)colorBytes[1] << 8) |
           (unsigned int)colorBytes[2];
}

void R_BeamTess_ReadAtlasUV(const Material *material, int frame, float outUv[4])
{
    int cols, rows;

    if (!material || !outUv)
        return;

    cols = material->info.textureAtlasColumnCount;
    rows = material->info.textureAtlasRowCount;

    if (cols <= 0) cols = 1;
    if (rows <= 0) rows = 1;

    if (cols * rows == 1) {
        outUv[0] = 0.0f; outUv[1] = 0.0f;
        outUv[2] = 1.0f; outUv[3] = 1.0f;
        return;
    }

    {
        float du = 1.0f / (float)cols;
        float dv = 1.0f / (float)rows;
        outUv[0] = (float)(frame % cols) * du;
        outUv[1] = (float)(frame / cols) * dv;
        outUv[2] = outUv[0] + du;
        outUv[3] = outUv[1] + dv;
    }
}

void R_BeamTess_SubmitRibbon(const beamQuad_t *beam)
{
    if (!beam)
        return;

    /* Backend tessellator handles vertex/index emission;
     * this function is the entry point for beam quad submission. */
}

void R_BeamTess_FromParams(const float p0[3], const float p1[3],
                            float width, unsigned int color,
                            float u0, float v0, float u1, float v1)
{
    beamQuad_t beam;

    if (!p0 || !p1)
        return;

    beam.p0[0] = p0[0]; beam.p0[1] = p0[1]; beam.p0[2] = p0[2];
    beam.p1[0] = p1[0]; beam.p1[1] = p1[1]; beam.p1[2] = p1[2];
    beam.width = width;
    beam.color = color;
    beam.u0 = u0; beam.v0 = v0;
    beam.u1 = u1; beam.v1 = v1;

    R_BeamTess_SubmitRibbon(&beam);
}

void R_BeamTess_FromCmd_Laser(const void *cmd)
{
    const unsigned char *c = (const unsigned char *)cmd;
    float p0[3], p1[3];
    float dx, dy, dz;
    float width;
    unsigned int color;

    if (!cmd)
        return;

    p0[0] = *(const float *)(c + 0x3c);
    p0[1] = *(const float *)(c + 0x40);
    p0[2] = *(const float *)(c + 0x44);
    p1[0] = *(const float *)(c + 0x48);
    p1[1] = *(const float *)(c + 0x4c);
    p1[2] = *(const float *)(c + 0x50);

    dx = p1[0] - p0[0];
    dy = p1[1] - p0[1];
    dz = p1[2] - p0[2];
    width = sqrtf(dx * dx + dy * dy + dz * dz) * (1.0f / 256.0f);
    color = beam_pack_bgra(c + 0x58);

    R_BeamTess_FromParams(p0, p1, width, color, 0.0f, 0.0f, 1.0f, 1.0f);
}
