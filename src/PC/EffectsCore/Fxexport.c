#include "common_types.h"
#include "imports.h"

extern volatile qboolean fx_camera_valid;

extern void *Com_GetClientDObj(int entNum, int localClientNum);
extern int DObjGetBoneIndex(void *dobj, unsigned int bone);
extern void MakeNormalVectors(const vec_t *forward, vec_t *right, vec_t *up);
/* Fixed 5-arg form must match FxScheduler.c — variadic/arity mismatch → WASM unreachable. */
extern void FxScheduler_PlayEffect(void *scheduler, EffectTemplate *fx, const vec_t *org, const void *axis, const void *bolt);
extern int FX_Init(int rendererExists);
extern void FX_Free(int freeAll);
extern void FxHelper_AdjustCamera(void *helper, void *refdef, float zfar);
extern void FxHelper_AdjustTime(void *helper, int time);
extern void FxHelper_WarpTime(void *helper, int time);
extern float FxScheduler_GetEffectLength(void *scheduler, EffectTemplate *fx);

extern byte *fx_scheduler_ptr;
extern byte *fx_helper_ptr;

extern int effectActiveCountBolt;
extern int privateEffectActiveCountBolt;
extern int effectActiveCountNonBolt;
extern int privateEffectActiveCountNonBolt;

int FX_GetBoneIndex(const int entNum, unsigned int bone);
void FX_PlaySimpleEffect(EffectTemplate *fx, const vec_t *org);
void FX_PlayEffect(EffectTemplate *fx, const vec_t *org, const vec_t *fwd);
void FX_PlayEntityEffect(EffectTemplate *fx, const vec_t *org, vec3_t *axis, const FxBoltInfo *bolt);
int FX_InitSystem(int rendererExists);
void FX_FreeSystem(void);
void FX_FreeActive(void);
void FX_AdjustCamera(PrimType (*refdef)[256], float zfar);
void FX_AdjustTime(int time);
void FX_WarpTime(int time);
float FX_GetEffectLength(EffectTemplate *fx);
void Server_SwitchToValidFxScheduler(void);

int FX_GetBoneIndex(const int entNum, unsigned int bone)
{
    void *pObj = Com_GetClientDObj(entNum, 0);
    if (pObj == NULL)
        return -1;
    return DObjGetBoneIndex(pObj, bone);
}

void FX_PlaySimpleEffect(EffectTemplate *fx, const vec_t *org)
{
    /* Vanilla FxScheduler::PlayEffect(this, fx, origin): identity axis. */
    vec3_t axis[3];
    axis[0][0] = 0.0f;
    axis[0][1] = 0.0f;
    axis[0][2] = 1.0f;
    axis[1][0] = 1.0f;
    axis[1][1] = 0.0f;
    axis[1][2] = 0.0f;
    axis[2][0] = 0.0f;
    axis[2][1] = 1.0f;
    axis[2][2] = 0.0f;
    FxScheduler_PlayEffect(*(void **)*(void **)&fx_scheduler_ptr, fx, org, axis, NULL);
}

void FX_PlayEffect(EffectTemplate *fx, const vec_t *org, const vec_t *fwd)
{
    /* Vanilla cdecl PlayEffect(fx, org, fwd) builds a 3x3 via MakeNormalVectors. */
    vec3_t axis[3];
    if (fwd) {
        axis[0][0] = fwd[0];
        axis[0][1] = fwd[1];
        axis[0][2] = fwd[2];
        MakeNormalVectors(axis[0], axis[1], axis[2]);
    } else {
        axis[0][0] = 0.0f;
        axis[0][1] = 0.0f;
        axis[0][2] = 1.0f;
        axis[1][0] = 1.0f;
        axis[1][1] = 0.0f;
        axis[1][2] = 0.0f;
        axis[2][0] = 0.0f;
        axis[2][1] = 1.0f;
        axis[2][2] = 0.0f;
    }
    FxScheduler_PlayEffect(*(void **)*(void **)&fx_scheduler_ptr, fx, org, axis, NULL);
}

void FX_PlayEntityEffect(EffectTemplate *fx, const vec_t *org, vec3_t *axis, const FxBoltInfo *bolt)
{
    FxScheduler_PlayEffect(*(void **)*(void **)&fx_scheduler_ptr, fx, org, axis, bolt);
}

int FX_InitSystem(int rendererExists)
{
    return FX_Init((unsigned char)rendererExists);
}

void FX_FreeSystem(void)
{
    FX_Free(1);
}

void FX_FreeActive(void)
{
    FX_Free(0);
}

void FX_AdjustCamera(PrimType (*refdef)[256], float zfar)
{
    FxHelper_AdjustCamera(*(void **)*(void **)&fx_helper_ptr, refdef, zfar);
    fx_camera_valid = 1;
}

extern void *imp_effectActiveCountBolt;
extern void *imp_effectActiveCountNonBolt;
extern void *imp_privateEffectActiveCountBolt;
extern void *imp_privateEffectActiveCountNonBolt;
void FX_AdjustTime(int time)
{
    fx_camera_valid = 0;
    FxHelper_AdjustTime(*(void **)*(void **)&fx_helper_ptr, time);
    *(int *)imp_privateEffectActiveCountBolt = *(int *)imp_effectActiveCountBolt;
    *(int *)imp_privateEffectActiveCountNonBolt = *(int *)imp_effectActiveCountNonBolt;
}

void FX_WarpTime(int time)
{
    FxHelper_WarpTime(*(void **)*(void **)&fx_helper_ptr, time);
}

float FX_GetEffectLength(EffectTemplate *fx)
{
    return FxScheduler_GetEffectLength(*(void **)*(void **)&fx_scheduler_ptr, fx);
}

void Server_SwitchToValidFxScheduler(void)
{

}
