#include "common_types.h"
#include "imports.h"

extern void MacDisplay_GetVideoMemoryInfo(int *vidMemMB, int *totalVidMem);
/* Direct call — D3D vtable slots are typed as void(*)(void); call_indirect through
 * that table traps with "function signature mismatch" under Emscripten/wasm. */
extern unsigned int CDirect3DDevice_GetAvailableTextureMem(void *device);
extern void Com_Printf(const char *fmt, ...);

extern unsigned char dx[];
extern refimport_t ri;

unsigned int R_AvailableTextureMemory(void)
{
    int vidMem;
    int textureMemBytes;
    unsigned int texMemInMegs;
    char *device;

    MacDisplay_GetVideoMemoryInfo(&vidMem, &textureMemBytes);

    device = *(char **)(dx + 8);
#ifdef __EMSCRIPTEN__
    /* Do not call CDirect3DDevice_GetAvailableTextureMem or ri.Printf here.
     * Log evidence: R_Init stops immediately after the pre-query debug line —
     * a bad dx.device pointer or ri.Printf signature trap freezes the tab. */
    (void)device;
    (void)ri;
    texMemInMegs = textureMemBytes > 0 ? (unsigned int)textureMemBytes >> 20 : 512u;
    if (texMemInMegs < 128u)
        texMemInMegs = 512u;
    Com_Printf("webdbg: R_AvailableTextureMemory web path vidMem=%d texMem=%u MB\n",
               vidMem, texMemInMegs);
    Com_Printf("DirectX reports %i MB of video memory and %i MB of available texture memory.\n",
               vidMem > 0 ? vidMem : (int)texMemInMegs, texMemInMegs);
#else
    {
        typedef void (*ri_Printf_fn)(int, const char *, ...);
        ri_Printf_fn Printf = (ri_Printf_fn)ri.Printf;

        if (device) {
            texMemInMegs = CDirect3DDevice_GetAvailableTextureMem(device) >> 20;
        } else {
            texMemInMegs = textureMemBytes > 0 ? (unsigned int)textureMemBytes >> 20 : 0;
        }

        if (vidMem == 0) {
            Printf(0, "DirectX reports %i MB of available texture memory, but wouldn't tell available video memory.\n", texMemInMegs);
        } else {
            Printf(0, "DirectX reports %i MB of video memory and %i MB of available texture memory.\n", vidMem, texMemInMegs);
            if ((unsigned int)vidMem < texMemInMegs) {
                texMemInMegs = vidMem - 16;
                Printf(0, "Using video memory size to cap used texture memory at %i MB.\n", texMemInMegs);
            }
        }
    }
#endif
    return texMemInMegs;
}
