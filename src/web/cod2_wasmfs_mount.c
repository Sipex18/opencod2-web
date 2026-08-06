#include <emscripten/emscripten.h>

#if defined(WEB_WASMFS)
#include <emscripten/wasmfs.h>
#include <emscripten/threading.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

/*
 * wasmfs OPFS uses a ProxyWorker that must not be constructed on the main
 * browser thread. Call this from PROXY_TO_PTHREAD main() (worker thread).
 */
static int cod2_mount_opfs_impl(void) {
    backend_t opfs = wasmfs_create_opfs_backend();
    if (!opfs) {
        return -1;
    }

    if (wasmfs_create_directory("/opfs", 0777, opfs) != 0) {
        if (errno != EEXIST) {
            return -2;
        }
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE int cod2_mount_opfs(void) {
    if (emscripten_is_main_browser_thread()) {
        fprintf(stderr,
                "cod2_mount_opfs: must run off the browser main thread "
                "(PROXY_TO_PTHREAD)\n");
        return -3;
    }
    return cod2_mount_opfs_impl();
}

#else

#endif
