# opencod2

**A source-level reconstruction of the Call of Duty 2 multiplayer engine.**

Built from scratch to be portable: runs natively on Linux and Windows,
and experimentally **in the browser** via WebAssembly.

> [!WARNING]
> **Work in progress.** The engine boots, connects, and loads maps — but expect
> crashes, missing features, and rough edges. Shared for collaboration and the
> curious, not as a finished product.

> [!IMPORTANT]
> **No game content is included.** This is source code only. You must supply
> IWD/map/asset files from a copy of the game that **you legally own**.

---

## Features

- Full multiplayer server and client in a single codebase
- Script VM (GSC) with compiler, virtual machine, and built-in function tables
- Collision detection, entity system, weapons, animation, and game logic
- OpenGL / WebGL2 renderer with material system, lightmaps, and shader pipeline
- UI menu system with HUD, scoreboard, and server browser
- Network layer with netchan, loopback, and WebSocket relay for web builds
- Sound system abstraction (native ALSA/DirectSound; stubbed for web)

## Platforms

| Target | Status | Notes |
|--------|--------|-------|
| **Linux x86** | Primary | 32-bit multilib; client + dedicated server |
| **Windows** (MinGW) | Supported | Cross-compiled; dedicated + optional client |
| **Web / WASM** | Experimental | Full engine in a single `.wasm`; WebGL2 renderer |

---

## Building

All targets use **CMake ≥ 3.16** with out-of-source builds.

### Linux

Requires a 32-bit multilib toolchain:

```sh
sudo apt install build-essential gcc-multilib g++-multilib cmake \
     libsdl2-dev:i386 libgl1-mesa-dev:i386 libx11-dev:i386 \
     libcurl4-openssl-dev:i386 zlib1g-dev:i386 libstdc++6:i386
```

```sh
cmake -S . -B build-native
cmake --build build-native -j
```

Produces `build-native/cod2_linux` (client) and `build-native/cod2_lnxded` (dedicated).

### Windows (MinGW cross-compile)

```sh
cmake -S . -B build-win32 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw32.cmake
cmake --build build-win32 -j
```

Produces `build-win32/cod2_win32_ded.exe`. Add `-DCOD2_WIN32_CLIENT=ON` for the
SDL2/GL client (supply SDL2 dev libs under `src/win32/sdl2/lib/`).

<details>
<summary>Swap-in renderer DLL (optional)</summary>

The renderer can be built as a standalone DLL that exports `GetRefAPI`:

```sh
cmake --build build-win32 --target gfxdll
# -> gfx_d3d_mp_x86_s.dll + cod2_win32_gfxdll.exe
```

The renderer↔engine bridge is pre-generated under `build/gfxdll/`.
</details>

### Web / Emscripten

Requires the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html):

```sh
source ~/emsdk/emsdk_env.sh
emcmake cmake -S . -B build/web
emmake make -C build/web -j$(nproc)
```

Produces `build/web/cod2.html`, `cod2.js`, and `cod2.wasm` — the full engine
compiled to WebAssembly. Serve alongside game assets from any HTTPS-capable
web server.

**Build optimizations:** `-O2`, WASM SIMD (`-msimd128`), `emmalloc` allocator.

**Deployment:**

```sh
bash tools/deploy_web_to_vps.sh user@host build/web [version-tag]
```

The deploy script pre-compresses the `.wasm` with gzip and configures Nginx
with `gzip_static` for optimal transfer sizes.

---

## Running

Point the engine at your legally-obtained game data:

```sh
./build-native/cod2_linux +set fs_basepath /path/to/your/cod2
```

For the web build, game assets (IWDs, maps) are fetched from the web server at
runtime. See `tools/deploy_web_to_vps.sh` and `src/web/remote-config.js` for
the asset serving configuration.

---

## Project Structure

```
src/
├── PC/                  # Platform-specific engine code
│   ├── client_mp/       # Client: networking, parsing, input, screen
│   ├── server_mp/       # Server: game VM, snapshots, netchan
│   ├── game_mp/         # Game logic: entities, weapons, spawning
│   ├── cgame_mp/        # Client-side game: prediction, effects, HUD
│   ├── script/          # GSC script compiler and virtual machine
│   ├── gfx_d3d/         # Renderer: materials, shaders, BSP, images
│   ├── qcommon/         # Shared: collision, networking, filesystem
│   ├── bgame/           # Shared client/server: physics, animation
│   ├── ui_mp/           # UI menu system
│   └── xanim/           # Animation system
├── web/                 # Emscripten/browser: WebGL, networking, audio, FS
├── headers/             # Type definitions and function declarations
├── stubs/               # Platform stubs and compatibility shims
└── blobs/               # BSS segment and import pointer tables
tools/                   # Build, deploy, and diagnostic scripts
build/web_gen/           # Auto-generated WASM glue (data, stubs, BSS)
```

---

## Security

Game engines from this era were not built with modern security in mind.
Networking, file parsing, and content loading paths may have exploitable bugs.
Do not expose test servers to untrusted networks.

A long-term goal is to audit and harden these paths while preserving
compatibility with the original game.

---

## Notice

This is an independent reconstruction. It is not affiliated with, authorized by,
or endorsed by Activision Publishing, Inc. or Infinity Ward.

"Call of Duty" and "Call of Duty 2" are trademarks of Activision Publishing, Inc.,
used here only for identification and interoperability.

No copyrighted game data is included or distributed. You must supply your own
legally-obtained copy of the game.

The reconstructed source is a derivative work for preservation, interoperability,
research, and education — provided as-is without warranty. If you are a rights
holder with concerns, please open an issue.
