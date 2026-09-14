#include "common_types.h"
#include "imports.h"
extern int alwaysfails;

extern const char *va(const char *fmt, ...);
extern void R_FatalInitError(const char *msg);
extern const char *R_ErrorDescription(HRESULT hr);

void *R_AllocStaticVertexBuffer(IDirect3DVertexBuffer9 **vb, int sizeInBytes);
void R_FinishStaticVertexBuffer(IDirect3DVertexBuffer9 *vb);
void R_FreeStaticVertexBuffer(IDirect3DVertexBuffer9 *vb);
void *R_AllocStaticIndexBuffer(IDirect3DIndexBuffer9 **ib, int sizeInBytes);
void R_FinishStaticIndexBuffer(IDirect3DIndexBuffer9 *ib);
void R_FreeStaticIndexBuffer(IDirect3DIndexBuffer9 *ib);

#define VTABLE(obj) (*(void ***)((void *)(obj)))

#ifdef __EMSCRIPTEN__
extern HRESULT CDirect3DDevice_CreateVertexBuffer(const void *, unsigned int, unsigned int, unsigned int, int, void **, void *);
extern HRESULT CDirect3DDevice_CreateIndexBuffer(const void *, unsigned int, unsigned int, unsigned int, int, void **, void *);
extern HRESULT CDirect3DVertexBuffer_Lock(const void *, unsigned int, unsigned int, void **, unsigned int);
extern HRESULT CDirect3DVertexBuffer_Unlock(const void *);
extern unsigned long CDirect3DVertexBuffer_Release(const void *);
extern HRESULT CDirect3DIndexBuffer_Lock(const void *, unsigned int, unsigned int, void **, unsigned int);
extern HRESULT CDirect3DIndexBuffer_Unlock(const void *);
extern unsigned long CDirect3DIndexBuffer_Release(const void *);
#else
typedef HRESULT(D3DVTCC *CreateVertexBufferFn)(void *device, UINT length, DWORD usage, DWORD fvf, int pool, IDirect3DVertexBuffer9 **ppVB, void *sharedHandle);
typedef HRESULT(D3DVTCC *CreateIndexBufferFn)(void *device, UINT length, DWORD usage, DWORD fvf, int format, IDirect3DIndexBuffer9 **ppIB, void *sharedHandle);
typedef HRESULT(D3DVTCC *BufferLockFn)(void *buffer, UINT offsetToLock, UINT sizeToLock, void **ppbData, DWORD flags);
typedef HRESULT(D3DVTCC *BufferUnlockFn)(void *buffer);
typedef ULONG(D3DVTCC *ReleaseFn)(void *obj);
#endif

void *R_AllocStaticVertexBuffer(IDirect3DVertexBuffer9 **vb, int sizeInBytes)
{
    void *vertexBufferData;
    byte *dxPtr;
    void *device;
    HRESULT hr;

    dxPtr = (byte *)imp_dx;
    device = *(void **)(dxPtr + 8);

#ifdef __EMSCRIPTEN__
    hr = CDirect3DDevice_CreateVertexBuffer(device, sizeInBytes, 8, 0, 0, (void **)vb, NULL);
#else
    {
        void **vtable = VTABLE(device);
        hr = ((CreateVertexBufferFn)vtable[0x68 / 4])(device, sizeInBytes, 8, 0, 0, vb, NULL);
    }
#endif
    if (hr < 0) {
        R_FatalInitError(va("DirectX didn't create a %i-byte vertex buffer: %s\n", sizeInBytes, R_ErrorDescription(hr)));
    }

#ifdef __EMSCRIPTEN__
    hr = CDirect3DVertexBuffer_Lock(*vb, 0, 0, &vertexBufferData, 0);
#else
    hr = ((BufferLockFn)VTABLE(*vb)[0x2c / 4])((void *)*vb, 0, 0, &vertexBufferData, 0);
#endif
    if (hr < 0) {
        R_FatalInitError(va("DirectX didn't lock a vertex buffer: %s\n", R_ErrorDescription(hr)));
    }

    return vertexBufferData;
}

void R_FinishStaticVertexBuffer(IDirect3DVertexBuffer9 *vb)
{
#ifdef __EMSCRIPTEN__
    CDirect3DVertexBuffer_Unlock(vb);
#else
    do {
        ((BufferUnlockFn)VTABLE(vb)[0x30 / 4])((void *)vb);
    } while (*(volatile int *)&alwaysfails != 0);
#endif
}

void R_FreeStaticVertexBuffer(IDirect3DVertexBuffer9 *vb)
{
#ifdef __EMSCRIPTEN__
    CDirect3DVertexBuffer_Release(vb);
#else
    do {
        ((ReleaseFn)VTABLE(vb)[0x08 / 4])((void *)vb);
        vb = NULL;
    } while (*(volatile int *)&alwaysfails != 0);
#endif
}

void *R_AllocStaticIndexBuffer(IDirect3DIndexBuffer9 **ib, int sizeInBytes)
{
    void *indexBufferData;
    byte *dxPtr;
    void *device;
    HRESULT hr;

    dxPtr = (byte *)imp_dx;
    device = *(void **)(dxPtr + 8);

#ifdef __EMSCRIPTEN__
    hr = CDirect3DDevice_CreateIndexBuffer(device, sizeInBytes, 8, 0x65, 0, (void **)ib, NULL);
#else
    {
        void **devVtable = VTABLE(device);
        hr = ((CreateIndexBufferFn)devVtable[0x6c / 4])(device, sizeInBytes, 8, 0x65, 0, ib, NULL);
    }
#endif
    if (hr < 0) {
        return NULL;
    }

#ifdef __EMSCRIPTEN__
    hr = CDirect3DIndexBuffer_Lock(*ib, 0, 0, &indexBufferData, 0);
#else
    hr = ((BufferLockFn)VTABLE(*ib)[0x2c / 4])((void *)*ib, 0, 0, &indexBufferData, 0);
#endif
    if (hr < 0) {
#ifdef __EMSCRIPTEN__
        CDirect3DIndexBuffer_Release(*ib);
#else
        ((ReleaseFn)VTABLE(*ib)[0x08 / 4])((void *)*ib);
#endif
        return NULL;
    }

    return indexBufferData;
}

void R_FinishStaticIndexBuffer(IDirect3DIndexBuffer9 *ib)
{
#ifdef __EMSCRIPTEN__
    CDirect3DIndexBuffer_Unlock(ib);
#else
    do {
        ((BufferUnlockFn)VTABLE(ib)[0x30 / 4])((void *)ib);
    } while (*(volatile int *)&alwaysfails != 0);
#endif
}

void R_FreeStaticIndexBuffer(IDirect3DIndexBuffer9 *ib)
{
#ifdef __EMSCRIPTEN__
    CDirect3DIndexBuffer_Release(ib);
#else
    do {
        ((ReleaseFn)VTABLE(ib)[0x08 / 4])((void *)ib);
        ib = NULL;
    } while (*(volatile int *)&alwaysfails != 0);
#endif
}
