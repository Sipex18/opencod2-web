# Web Build

This target builds the port with Emscripten, SDL2, WASM, and WebGL2. With the
[Emscripten SDK](https://emscripten.org) active (`emcc` on PATH):

```sh
emcmake cmake -S . -B build-web -DCOD2_WEB_WASMFS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-web -j
```

Output is written to `build-web/` (`cod2.html`, `cod2.js`, `cod2.wasm`, and the
asset-loader `cod2_fs.js`). Serve that directory over HTTP and supply your own
data via the in-page folder picker.

The build keeps no extra state; Emscripten's cache lives under the ignored
build directory.

The target requests a WebGL2/ES3 context with:

```text
-sUSE_SDL=2 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1
```

## Filesystem backends

There are three ways assets reach the C filesystem. Pick one with the CMake
options below; the default copies assets, the other two do not.

| Backend | CMake flags | Assets | Needs COOP/COEP |
|---------|-------------|--------|-----------------|
| MEMFS + IDBFS | *(default)* | copied into wasm heap RAM | no |
| WASMFS + OPFS | `-DCOD2_WEB_WASMFS=ON` | read on demand from OPFS | yes |
| WORKERFS | `-DCOD2_WEB_WORKERFS=ON` | read on demand from the picked folder | yes |

### Default — MEMFS + IDBFS

On boot the OPFS-cached files are streamed once into the in-memory FS at
`/cod2`. Works on any static server (no special headers). Trade-off: the assets
occupy wasm heap RAM (`-sALLOW_MEMORY_GROWTH=1` covers it), so trim to the maps
you need. Config/savegames persist through IDBFS.

### WASMFS + OPFS

`-DCOD2_WEB_WASMFS=ON` backs the C filesystem with WASMFS and mounts the OPFS
tree directly at `/cod2`, so the engine reads iwd bytes on demand — the archives
are never copied into RAM. It adds `-sWASMFS`, `-DWEB_WASMFS` (activates
`cod2_mount_opfs()` in `src/web/cod2_wasmfs_mount.c`), and `-pthread` (the OPFS
backend proxies blocking I/O to a dedicated worker).

Because of `-pthread`, the page **must be served cross-origin isolated**:

```text
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

Without those headers a `-pthread` build will not start.

### WORKERFS — zero copy

`-DCOD2_WEB_WORKERFS=ON` reads the iwd archives directly from the picked disk
folder — no copy into OPFS, no full-RAM load. The folder is the live source;
bytes are sliced on demand as the engine seeks the ZIP central directory and
per-file data.

How it works:

1. The shell calls `Cod2Fs.enumerateInstall(handle)`, which validates the folder
   and returns live `File` objects from the picked `FileSystemDirectoryHandle`
   (it also remembers the handle in IndexedDB). No bytes are read or copied.
2. It mounts those File objects read-only with WORKERFS:
   `FS.mount(WORKERFS, { files: [{name:'main/iw_00.iwd', data:File}, …] }, '/cod2')`.
   WORKERFS serves synchronous slice reads via `FileReaderSync`, which only
   exists on a Worker thread — so `main()` must run on a worker.
3. `-sPROXY_TO_PTHREAD=1` (+ `-pthread`, SharedArrayBuffer) moves `main()` to a
   worker; SDL2's canvas/GL then uses OffscreenCanvas
   (`-sOFFSCREENCANVAS_SUPPORT=1`, `-sOFFSCREENCANVASES_TO_PTHREAD=#canvas`).
4. The game launches with `+set fs_basepath /cod2 +set fs_homepath /cod2home`.
   `/cod2/main/*.iwd` resolves to the WORKERFS-mounted File objects.
5. OPFS is used **only** for `/cod2home` (savegames, config, `players/`): the
   shell mirrors the small OPFS `home/` subtree into the writable FS on boot and
   flushes it back on an interval and at `pagehide`. The read-only assets are
   never duplicated.

On reload the remembered handle is restored from IndexedDB and re-granted
(silent if `granted`, one click if `prompt`), then re-mounted — no re-pick, no
copy. There is no "play from cached assets" because nothing is cached.

COOP/COEP is required here too (`-pthread`).

## Asset loading (point the browser at your CoD2 install)

The page never bundles game assets. On first run it shows a landing screen with
**"Select your Call of Duty 2 folder"**. The loader (`src/web/cod2_fs.js`) then:

1. **Picks the folder** with the File System Access API
   (`showDirectoryPicker`, Chromium-only). Pick either the install root (the
   folder that contains `main/`) or `main/` itself. The folder is validated to
   look like a CoD2 install (it must contain `main/*.iwd`).
2. **Caches assets into OPFS** (Origin Private File System) — the iwd archives
   plus loose `*.cfg` / localization / config files the engine reads off disk.
   OPFS is disk-backed and persistent, so the data survives reloads and is never
   uploaded. A per-file freshness check (same size ⇒ skip) avoids re-copying.
   `navigator.storage.persist()` is requested so the cache is not evicted.
   Progress (per file and total MB) is shown.
3. **Remembers the folder**: the `FileSystemDirectoryHandle` is stored in
   IndexedDB. On reload the handle is read back and `queryPermission({mode:'read'})`
   is checked — if `granted`, the game resumes silently; if `prompt`, a
   **"Resume from …"** button calls `requestPermission`. If the disk handle is
   gone or permission is refused, the game boots straight from the OPFS cache
   (**"Play from cached assets"**). **Change folder** re-runs the picker;
   **Clear cache** wipes OPFS + the saved handle.
4. **Mounts the assets into the engine FS** and launches with
   `+set fs_basepath /cod2 +set fs_homepath /cod2home`, so `FS_Startup` finds
   `/cod2/main/*.iwd` and config/savegames persist under `/cod2home`.

Non-Chromium browsers (no File System Access API) fall back to a
`<input webkitdirectory>` picker that streams the selected assets straight into
the engine FS for that session (no folder memory — that needs Chromium).

### Remote assets (HTTP manifest)

Instead of a folder picker the page can download a prebuilt pack from the same
origin. `src/web/remote-config.js` sets the endpoints, or pass them in the URL:

```text
cod2.html?assets=/play/assets.json&base=/
```

The manifest shape is `{ "baseUrl": "/", "files": [{ "path": "main/iw_00.iwd", "size": 614351028 }] }`.
Generate one with `tools/gen_assets_manifest.py` and serve the tree it points at.
Files are streamed into OPFS and the same freshness check skips what is already
cached. If no manifest is configured the loader probes a sibling `assets.json`
and otherwise falls back to the folder picker.

## Local development server

`tools/serve_web_local.py` serves the build directory and reverse-proxies the
same-origin Join Game endpoints, so no VPS is needed:

```sh
pip install websockets          # once, for the WS↔UDP relay
python tools/serve_web_local.py --coop
# http://127.0.0.1:8080/cod2.html
```

It starts `tools/master_proxy/server.py` (HTTP masterlist) and
`tools/game_proxy/relay.py` (WebSocket↔UDP) as children and exposes:

```text
/play/api/masterlist/...  →  127.0.0.1:20711
/play/api/gamews          →  127.0.0.1:20712
/play/api/dev-status      →  local health of both backends
```

`--coop` sends the COOP/COEP headers; it is required for the WASMFS and WORKERFS
variants and unnecessary for the default one.

To serve your install tree without copying it, put a symlink or directory
junction named `main` inside the served directory — the static handler follows
it, while `../` traversal is still rejected.

## Manual browser test

1. Build and serve (see above).
2. Open `http://127.0.0.1:8080/cod2.html` in **Chrome or Edge**.
3. Click **"Select your Call of Duty 2 folder"** and choose your CoD2 install
   (the folder containing `main/`). Approve the read permission.
4. Watch the copy progress (per-file + total MB). When it finishes the canvas
   appears and the game starts with `fs_basepath=/cod2`.
5. **Reload the page.** It should resume **without re-picking** — either silently
   (permission still granted) or via the **"Resume from …"** button. Disconnect
   the drive / move the folder and reload: it should offer **"Play from cached
   assets"** and boot from OPFS.
6. Use **Clear cache** to reset, and **Change folder** to point at a different
   install.
7. (Optional) Verify the OPFS cache exists: DevTools → Application → Storage →
   *Origin Private File System* should show `cod2/main/*.iwd`.
8. For the WASMFS on-demand variant, confirm it boots without copying the iwds
   into RAM. For WORKERFS, confirm OPFS contains only `home/` and the wasm heap
   does not grow by the size of the iwds.
