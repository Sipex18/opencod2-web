#include "common_types.h"
#include "imports.h"
extern refimport_t ri;
extern int alwaysfails;

#define DX_DEVICE_WRAP ((void *)imp_dx)
#define XMODEL_VTABLE ((char *)&ri)
#define GPU_FENCE_FLAG (*(volatile int *)&alwaysfails)

#define DEREF_PTR(ptr, off) (*(void **)((char *)(ptr) + (off)))

typedef int(__attribute__((cdecl)) * vtable_func_t)();
#define VTABLE(obj) (*(vtable_func_t **)((void *)(obj)))
#define VTABLE_CALL(obj, off, ...) (VTABLE(obj)[(off) / sizeof(vtable_func_t *)](__VA_ARGS__))

typedef int(__attribute__((cdecl)) * flat_func_t)();
#define FLAT_CALL(base, off, ...) ((*(flat_func_t *)((base) + (off)))(__VA_ARGS__))

extern void R_FinishStaticVertexBuffer(IDirect3DVertexBuffer9 *vb);
extern dvar_t *r_rendererInUse;
extern void *R_AllocStaticIndexBuffer(IDirect3DIndexBuffer9 **ib, int sizeInBytes);
extern void R_FinishStaticIndexBuffer(IDirect3DIndexBuffer9 *ib);
extern void R_FreeStaticIndexBuffer(IDirect3DIndexBuffer9 *ib);
extern int XSurfaceGetBoneOffset(const XSurface *surf);
extern void XSurfaceTransfer(const XVertexBuffer *surfVerts, void *verts, int vertCount);
extern void XSurfaceTransferDx7(const XVertexBuffer *surfVerts, void *verts, int vertCount);
extern void Com_Memcpy(void *dest, const void *src, int count);
extern int XModelGetNumLods(const struct XModel *model);
extern int XModelGetSurfaces(const struct XModel *model, struct XSurface_s ***surfaces, int lod, int **partBits);
#ifdef __EMSCRIPTEN__
extern void Com_Printf(const char *fmt, ...);
#endif

void XSurfaceOptimizeRigid(XModel *model, XSurface *surface, XVertexBuffer *surfVerts);
void XModelOptimize(XModel *model);
void XModelUnoptimize(XModel *model);

void XSurfaceOptimizeRigid(XModel *model, XSurface *surface, XVertexBuffer *surfVerts)
{
    IDirect3DVertexBuffer9 **surfRigid;
    void *vertexBuffer;
    int vertCount;
    int vertexStride;
    int hr;

    surfRigid = &surface->surfRigid.vb;

    if (surface->surfRigid.vb != NULL)
        return;

    vertCount = (int)surface->vertCount;

#ifdef __EMSCRIPTEN__
    if (vertCount <= 0 || !surfVerts)
        return;
#endif
    if (r_rendererInUse->current.integer == 2)
        vertexStride = 0x24;
    else
        vertexStride = 0x40;

#ifdef __EMSCRIPTEN__
    {
        extern HRESULT CDirect3DDevice_CreateVertexBuffer(const void *dev, unsigned int Length,
                                                           unsigned int Usage, unsigned int FVF, int Pool,
                                                           void **ppVertexBuffer, void *pSharedHandle);
        extern HRESULT CDirect3DVertexBuffer_Lock(const void *vb, unsigned int OffsetToLock,
                                                   unsigned int SizeToLock, void **ppbData, unsigned int Flags);
        void *deviceWrapper = DX_DEVICE_WRAP;
        void *device = DEREF_PTR(deviceWrapper, 8);

        hr = CDirect3DDevice_CreateVertexBuffer(device, vertCount * vertexStride, 8, 0, 0, (void **)surfRigid, 0);
        if (hr < 0) {
            surface->surfRigid.vb = NULL;
            return;
        }

        hr = CDirect3DVertexBuffer_Lock(surface->surfRigid.vb, 0, 0, &vertexBuffer, 0);
        if (hr < 0) {
            extern unsigned long CDirect3DVertexBuffer_Release(const void *);
            CDirect3DVertexBuffer_Release(surface->surfRigid.vb);
            surface->surfRigid.vb = NULL;
            return;
        }
    }
#else
    {
        void *deviceWrapper;
        void *device;
        void **deviceVtable;
        void **vbVtable;

        deviceWrapper = DX_DEVICE_WRAP;
        device = DEREF_PTR(deviceWrapper, 8);
        deviceVtable = *(void ***)device;

        hr = ((int(__attribute__((cdecl)) *)(void *, int, int, int, int, void *, void *))deviceVtable[0x68 / 4])(device, vertCount * vertexStride, 8, 0, 0, surfRigid, 0);

        if (hr < 0) {
            surface->surfRigid.vb = NULL;
            return;
        }

        vbVtable = *(void ***)surface->surfRigid.vb;
        hr = ((int(__attribute__((cdecl)) *)(void *, int, int, void *, int))vbVtable[0x2c / 4])(surface->surfRigid.vb, 0, 0, &vertexBuffer, 0);

        if (hr < 0) {
            vbVtable = *(void ***)surface->surfRigid.vb;
            ((void(__attribute__((cdecl)) *)(void *))vbVtable[8 / 4])(surface->surfRigid.vb);
            surface->surfRigid.vb = NULL;
            return;
        }
    }
#endif

    if (r_rendererInUse->current.integer == 2) {

        XSurfaceTransferDx7(surfVerts, vertexBuffer, (int)surface->vertCount);
    } else {

        XSurfaceTransfer(surfVerts, vertexBuffer, (int)surface->vertCount);
    }

    R_FinishStaticVertexBuffer(surface->surfRigid.vb);
}

void XModelOptimize(XModel *model)
{
    int lodCount;
    int lodIndex;
    int surfCount;
    int surfIndex;
    struct XSurface_s **surfaces;
    int *partBits;
    XSurface *surf;
    int indexDataSize;
    int indexBytes;
    void *indexBuffer;

#ifdef __EMSCRIPTEN__
    if (!model)
        return;
#endif

    lodCount = XModelGetNumLods(model);
    if (lodCount <= 0)
        return;

    for (lodIndex = 0; lodIndex < lodCount; lodIndex++) {

#ifdef __EMSCRIPTEN__
        {
            XModelSurfs *surfData = model->lodInfo[lodIndex].surfs;
            if (!surfData) {
                continue;
            }
        }
#endif

        surfaces = NULL;
        partBits = NULL;
        surfCount = XModelGetSurfaces(model, &surfaces, lodIndex, &partBits);
        if (surfCount <= 0)
            continue;
        if (surfaces == NULL) {
            continue;
        }

        for (surfIndex = 0; surfIndex < surfCount; surfIndex++) {
            surf = surfaces[surfIndex];
            if (surf == NULL) {
                continue;
            }

#ifdef __EMSCRIPTEN__
            if ((unsigned int)surf < 0x1000u) {
                continue;
            }
            if (surf->vertCount <= 0 || surf->triCount <= 0) {
                continue;
            }
#endif

            if (surf->indexBuffer != NULL) {
                if (XSurfaceGetBoneOffset(surf) != -1) {
                    XSurfaceOptimizeRigid(model, surf, surf->verts);
                }
                continue;
            }

            indexDataSize = (int)surf->triCount * 6;

#ifdef __EMSCRIPTEN__
            if (indexDataSize <= 0 || indexDataSize > 0x400000) {
                continue;
            }
            if (!surf->triIndices) {
                continue;
            }
#endif

            indexBytes = (indexDataSize + 0x1f) & ~0x1f;

            indexBuffer = R_AllocStaticIndexBuffer(&surf->indexBuffer, indexBytes);
            if (indexBuffer == NULL)
                continue;

            Com_Memcpy(indexBuffer, surf->triIndices, indexDataSize);

            memset((char *)indexBuffer + indexDataSize, 0, indexBytes - indexDataSize);

            R_FinishStaticIndexBuffer(surf->indexBuffer);

            if (XSurfaceGetBoneOffset(surf) != -1) {
                XSurfaceOptimizeRigid(model, surf, surf->verts);
            }
        }
    }
}

void XModelUnoptimize(XModel *model)
{
    int lodCount;
    int lodIndex;
    int surfCount;
    int surfIndex;
    struct XSurface_s **surfaces;
    int *partBits;
    XSurface *surf;
    void **vbVtable;

    lodCount = XModelGetNumLods(model);
    if (lodCount <= 0)
        return;

    for (lodIndex = 0; lodIndex < lodCount; lodIndex++) {

        surfaces = NULL;
        partBits = NULL;
        surfCount = XModelGetSurfaces(model, &surfaces, lodIndex, &partBits);
        if (surfCount <= 0)
            continue;
        if (!surfaces)
            continue;

        for (surfIndex = 0; surfIndex < surfCount; surfIndex++) {
            surf = surfaces[surfIndex];
            if (!surf)
                continue;

            if (surf->indexBuffer != NULL) {
                R_FreeStaticIndexBuffer(surf->indexBuffer);
                surf->indexBuffer = NULL;
            }

            if (surf->surfRigid.vb != NULL) {
#ifdef __EMSCRIPTEN__
                {
                    extern unsigned long CDirect3DVertexBuffer_Release(const void *);
                    CDirect3DVertexBuffer_Release(surf->surfRigid.vb);
                    surf->surfRigid.vb = NULL;
                }
#else
                do {

                    vbVtable = *(void ***)surf->surfRigid.vb;
                    ((void(__attribute__((cdecl)) *)(void *))vbVtable[8 / 4])(surf->surfRigid.vb);
                    surf->surfRigid.vb = NULL;
                } while (GPU_FENCE_FLAG != 0);
#endif
            }
        }
    }
}
