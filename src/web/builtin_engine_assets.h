#ifndef COD2_BUILTIN_ENGINE_ASSETS_H
#define COD2_BUILTIN_ENGINE_ASSETS_H

#include "common_types.h"

typedef struct WebBuiltinAsset {
    const char *path;
    const byte *data;
    int size;
} WebBuiltinAsset;

const WebBuiltinAsset *FS_WebFindBuiltinAsset(const char *qpath);
qboolean FS_WebIsEngineBuiltinPath(const char *qpath);

#endif
