#include "common_types.h"
#include "imports.h"
#ifdef __EMSCRIPTEN__
#include <stdio.h>
#endif

extern void MacDisplay_GetVideoMemoryInfo(int *vidMemMB, int *totalVidMem);
/* Direct call — D3D vtable slots are typed as void(*)(void); call_indirect through
 * that table traps with "function signature mismatch" under Emscripten/wasm. */
extern unsigned int CDirect3DDevice_GetAvailableTextureMem(void *device);

extern unsigned char dx[];
extern refimport_t ri;

unsigned int R_AvailableTextureMemory(void)
{
    typedef void (*ri_Printf_fn)(int, const char *, ...);
    ri_Printf_fn Printf = (ri_Printf_fn)ri.Printf;
    int vidMem;
    int textureMemBytes;
    unsigned int texMemInMegs;
    char *device;

    MacDisplay_GetVideoMemoryInfo(&vidMem, &textureMemBytes);

    device = *(char **)(dx + 8);
#ifdef __EMSCRIPTEN__
    fprintf(stderr, "webdbg: R_AvailableTextureMemory device=%p vidMem=%d texBytes=%d\n",
            (void *)device, vidMem, textureMemBytes);
    fflush(stderr);
#endif
    if (device) {
        texMemInMegs = CDirect3DDevice_GetAvailableTextureMem(device) >> 20;
    } else {
        texMemInMegs = textureMemBytes > 0 ? (unsigned int)textureMemBytes >> 20 : 0;
    }
#ifdef __EMSCRIPTEN__
    /* WebGL has no reliable VRAM query; keep picmip from falling into tiny-tex path. */
    if (texMemInMegs < 128u)
        texMemInMegs = 512u;
#endif

    if (vidMem == 0) {
        Printf(0, "DirectX reports %i MB of available texture memory, but wouldn't tell available video memory.\n", texMemInMegs);
    } else {
        Printf(0, "DirectX reports %i MB of video memory and %i MB of available texture memory.\n", vidMem, texMemInMegs);
        if ((unsigned int)vidMem < texMemInMegs) {
            texMemInMegs = vidMem - 16;
            Printf(0, "Using video memory size to cap used texture memory at %i MB.\n", texMemInMegs);
        }
    }
    return texMemInMegs;
}
