/*
 * Keep CG_EntityEvent / view / render callees alive under Binaryen / wasm-ld GC.
 *
 * Stock web link uses -sERROR_ON_UNDEFINED_SYMBOLS=0 and aggressive DCE.
 * Helpers that are only reached from reconstructed CG_EntityEvent (or from
 * thin wrappers that Binaryen may drop) can disappear from the final
 * cod2.wasm; call sites then become `unreachable` and the client traps.
 *
 * A used pointer table is enough for the linker to retain the definitions.
 */
void CG_OutOfAmmoChange(void);
void CG_SwitchOffHandCmd(void);
void CG_PrepOffHand(void *es, int weaponId, int eventParm);
void CG_UseOffHand(void *cent, int weaponId, int eventParm);
void CG_BulletHitEvent(int otherEntNum, void *position, void *dir, void *reflect,
                       int surfType, int event);
void CG_BulletHitClientEvent(int otherEntNum, void *position, int surfType,
                             int event);
int CG_PlaySoundAliasAsMasterByName(int entitynum, const float *origin,
                                    const char *aliasname);
void CL_DeathMessagePrint(const char *attackerName, float *attackerColor,
                          const char *targetName, float *victimColor,
                          const char *iconShader, float iconWidth,
                          float iconHeight, float *iconColor, int iconHorzFlip);

void CG_DrawActive(void);
void CL_RenderScene(const void *refdef);
void R_RenderScene(const void *refdef);
void R_SkinGfxEntityDelayed(void *ent);
void R_SkinGfxEntity(void *ent);

__attribute__((used)) static void *const cod2_cgevent_keepalive[] = {
    (void *)CG_OutOfAmmoChange,
    (void *)CG_SwitchOffHandCmd,
    (void *)CG_PrepOffHand,
    (void *)CG_UseOffHand,
    (void *)CG_BulletHitEvent,
    (void *)CG_BulletHitClientEvent,
    (void *)CG_PlaySoundAliasAsMasterByName,
    (void *)CL_DeathMessagePrint,
    /* View / scene path — DCE left HUD drawing but dropped named scene entry. */
    (void *)CG_DrawActive,
    (void *)CL_RenderScene,
    (void *)R_RenderScene,
    (void *)R_SkinGfxEntityDelayed,
    (void *)R_SkinGfxEntity,
};
