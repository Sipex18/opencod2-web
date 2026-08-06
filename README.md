# opencod2

> A reconstruction of the **Call of Duty 2** engine

> [!WARNING]
> **Work in progress — this does not fully work yet.** It is an early, incomplete
> reconstruction: it builds and boots, but expect crashes, missing functionality,
> and broken features. It is shared for the curious and for collaboration, not as
> a finished or playable port. No stability, no support, and the code may change
> shape at any time.

> [!IMPORTANT]
> **No game content is included — bring your own data.** This repository is
> *source code only*: no executables, archives, maps, models, textures, sounds,
> or scripts. To run anything you must supply data files from a copy of the game
> that **you legally own**.

## Platforms

| Target | Status | Notes |
|--------|--------|-------|
| Linux x86 (client + dedicated) | Primary | 32-bit multilib required |
| Windows (MinGW cross-compile)  | Supported | Dedicated server; client with `-DCOD2_WIN32_CLIENT=ON` |
| **Web (Emscripten / WebAssembly)** | **Experimental** | Runs in browser via WebGL2; server + client in single WASM binary |

## Building

All targets are driven by **CMake (≥ 3.16)**, each an out-of-source build into
its own directory.

### Linux (primary)

The engine is a 32-bit x86 binary; you need a multilib toolchain and 32-bit libs
(Debian/Ubuntu names shown):

```sh
sudo apt install build-essential gcc-multilib g++-multilib cmake \
     libsdl2-dev:i386 libgl1-mesa-dev:i386 libx11-dev:i386 \
     libcurl4-openssl-dev:i386 zlib1g-dev:i386 libstdc++6:i386
```

```sh
cmake -S . -B build-native
cmake --build build-native -j
# -> build-native/cod2_linux    (client)
# -> build-native/cod2_lnxded   (dedicated server)
```

### Windows (MinGW cross-compile)

```sh
cmake -S . -B build-win32 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw32.cmake
cmake --build build-win32 -j
# -> build-win32/cod2_win32_ded.exe   (dedicated server)
```

Add `-DCOD2_WIN32_CLIENT=ON` for the SDL2/GL client (supply SDL2 dev libs under
`src/win32/sdl2/lib/`; the dedicated server needs none).

#### Swap-in renderer DLL (optional, experimental)

The renderer can be built as a separate swap-in DLL — it exports `GetRefAPI`
and talks to the engine only through the `ri`/`re` tables. With the client
configured, run the `gfxdll` target:

```sh
cmake --build build-win32 --target gfxdll
# -> build-win32/gfx_d3d_mp_x86_s.dll   (renderer DLL)
# -> build-win32/cod2_win32_gfxdll.exe  (engine that loads it at runtime)
```

The renderer↔engine bridge is pre-generated and committed under `build/gfxdll/`;
the build is pure compile+link. Experimental — not exhaustively tested.

### Web / Emscripten (experimental)

Requires the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html).

```sh
source ~/emsdk/emsdk_env.sh
emcmake cmake -S . -B build/web
emmake make -C build/web -j$(nproc)
# -> build/web/cod2.html, cod2.js, cod2.wasm
```

The web build compiles the full engine (server + client) into a single
WebAssembly binary that runs in the browser using WebGL2.
Game assets must be served from a web server alongside the WASM files.

**Optimizations applied:** `-O2`, WASM SIMD (`-msimd128`), `emmalloc` allocator.
Pre-compress `cod2.wasm` with `gzip -9` and serve with `gzip_static on` in Nginx
for best load times.

Deploy to a VPS:

```sh
bash tools/deploy_web_to_vps.sh user@host build/web [version-tag]
```

## Running

This reconstructs the engine, not the content. Point it at data from a copy of
the game you legally own:

```sh
./build-native/cod2_linux +set fs_basepath /path/to/your/game
```

Without legally-obtained data the build runs but has nothing to load.

## Security

Older Call of Duty titles and game engines from this era have a history of
security-sensitive bugs, especially around networking, file parsing,
content-loading paths, and memory safety. This project should not currently be
treated as a hardened or production-safe engine.

One long-term goal of the reconstruction is to make those risks easier to audit
and fix: preserve compatibility where practical, but replace unsafe behavior and
close vulnerabilities as they are found. Until then, run it only with data you
trust and avoid exposing test servers to untrusted networks.

## Notice

This is an independent, source-level reconstruction of the Call of Duty
2 engine. It is not affiliated with, authorized by, sponsored by, or endorsed by
Activision Publishing, Inc., Infinity Ward, or any of their affiliates.

"Call of Duty" and "Call of Duty 2" are trademarks of Activision Publishing,
Inc. They are used in this repository only for identification and
interoperability, to describe what the code reconstructs. No claim is made to
those marks.

This project does not provide or help obtain copyrighted game data. To use it
with real game content, you must supply data files from a copy of Call of Duty 2
that you legally own.

The reconstructed engine source is a derivative work created for the purposes of
preservation, interoperability, research, and education. It is provided as-is,
without warranty of any kind, express or implied. The original port, build
system, and platform glue are separable original work.

If you are a rights holder and believe something here should not be distributed,
please open an issue or contact the maintainer and it will be addressed
promptly.
