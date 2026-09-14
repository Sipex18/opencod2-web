# opencod2

<p align="center">
  <strong>Source-level reconstruction of the Call of Duty 2 multiplayer engine</strong><br />
  Native Linux &middot; Windows &middot; Web (WebAssembly)
</p>

<p align="center">
  <img alt="C" src="https://img.shields.io/badge/C-99-A8B9CC?logo=c&logoColor=white" />
  <img alt="WebAssembly" src="https://img.shields.io/badge/WebAssembly-Emscripten-654FF0?logo=webassembly&logoColor=white" />
  <img alt="OpenGL" src="https://img.shields.io/badge/OpenGL-WebGL2-5586A4?logo=opengl&logoColor=white" />
  <img alt="CMake" src="https://img.shields.io/badge/CMake-≥3.16-064F8C?logo=cmake&logoColor=white" />
  <img alt="CoD2" src="https://img.shields.io/badge/Game-Call%20of%20Duty%202-b91c1c" />
  <img alt="Status" src="https://img.shields.io/badge/Status-Work%20in%20Progress-f59e0b" />
</p>

---

> [!WARNING]
> **Work in progress.** The engine boots, connects, and starts loading maps —
> but expect crashes, missing features, and rough edges. Shared for
> collaboration and the curious, not as a finished product.

> [!IMPORTANT]
> **No game content is included.** This is source code only. You must supply
> IWD / map / asset files from a copy of the game that **you legally own**.

---

## Overview

**opencod2** is a ground-up reconstruction of the CoD2 1.3 multiplayer engine,
written in C. The goal is a portable, auditable engine that can run the
original game content on modern platforms — including the browser.

This repository is a derivative of [opencod2/opencod2](https://github.com/opencod2/opencod2),
which reconstructs the engine for native Linux and Windows. The work here adds
the Emscripten/WebAssembly target and the browser plumbing around it: a WebGL2
compatibility layer, Web Audio, an OPFS-backed filesystem, and a WebSocket↔UDP
relay so a browser client can reach real servers.

The entire server + client compiles into a single binary (or `.wasm` for web),
with no external game logic DLLs.

---

## Features

| Area | What's included |
|------|-----------------|
| **Game logic** | Entity system, weapons, spawning, movement, script VM (GSC) |
| **Networking** | Netchan, snapshots, loopback, WebSocket relay for web |
| **Renderer** | OpenGL / WebGL2, material system, lightmaps, BSP loading |
| **Script VM** | GSC compiler, virtual machine, 65k+ variable pool, built-in tables |
| **UI** | Menu system, HUD, scoreboard, server browser, loading screens |
| **Sound** | Native ALSA / DirectSound abstraction (stubbed for web) |
| **Collision** | Full CM trace, AABB trees, capsule traces |
| **Animation** | XAnim system, skeletal animation, blend trees |

---

## Platforms

| Target | Status | Output |
|--------|--------|--------|
| **Linux x86** | Primary | `cod2_linux` (client) · `cod2_lnxded` (dedicated) |
| **Windows** (MinGW) | Supported | `cod2_win32_ded.exe` · optional client |
| **Web / WASM** | Experimental | `cod2.wasm` + `cod2.js` — runs in browser |

---

## Building

All targets use **CMake ≥ 3.16** with out-of-source builds.

### Linux

```sh
sudo apt install build-essential gcc-multilib g++-multilib cmake \
     libsdl2-dev:i386 libgl1-mesa-dev:i386 libx11-dev:i386 \
     libcurl4-openssl-dev:i386 zlib1g-dev:i386 libstdc++6:i386
```

```sh
cmake -S . -B build-native
cmake --build build-native -j
```

### Windows (MinGW cross-compile)

```sh
cmake -S . -B build-win32 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw32.cmake
cmake --build build-win32 -j
```

Add `-DCOD2_WIN32_CLIENT=ON` for the SDL2/GL client.

<details>
<summary>Swap-in renderer DLL (optional)</summary>

```sh
cmake --build build-win32 --target gfxdll
# -> gfx_d3d_mp_x86_s.dll + cod2_win32_gfxdll.exe
```

Exports `GetRefAPI`; talks to the engine through `ri`/`re` tables. Pre-generated bridge under `build/gfxdll/`.
</details>

### Web / Emscripten

Requires the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html):

```sh
source ~/emsdk/emsdk_env.sh
emcmake cmake -S . -B build/web
emmake make -C build/web -j$(nproc)
```

| Optimization | Flag |
|-------------|------|
| Compiler | `-O2` |
| WASM SIMD | `-msimd128` |
| Allocator | `-sMALLOC=emmalloc` |
| Transfer | Pre-gzip `.wasm` + Nginx `gzip_static` |

**Deploy to a VPS:**

```sh
bash tools/deploy_web_to_vps.sh user@host build/web [version-tag]
```

---

## Running

Point the engine at your legally-obtained game data:

```sh
./build-native/cod2_linux +set fs_basepath /path/to/your/cod2
```

For the web build, assets are fetched from the web server at runtime.
See `src/web/remote-config.js` for the asset path configuration.

Local browser client + Join Game (HTTP masterlist + WebSocket↔UDP relay), no VPS:

```sh
pip install websockets
python tools/serve_web_local.py
# http://127.0.0.1:8080/cod2.html
# optional: --coop  (COOP/COEP for pthread / WASMFS / WORKERFS only)
```

---

## Project structure

```
src/
├── PC/
│   ├── client_mp/       Client: networking, parsing, input, screen
│   ├── server_mp/       Server: game VM, snapshots, netchan
│   ├── game_mp/         Game logic: entities, weapons, spawning
│   ├── cgame_mp/        Client game: prediction, effects, HUD
│   ├── script/          GSC compiler and virtual machine
│   ├── gfx_d3d/         Renderer: materials, shaders, BSP, images
│   ├── qcommon/         Shared: collision, networking, filesystem
│   ├── bgame/           Shared physics and animation
│   ├── ui_mp/           UI menu system
│   └── xanim/           Skeletal animation
├── web/                 Browser: WebGL, WebSocket, audio, FS
├── headers/             Type definitions and declarations
├── stubs/               Platform stubs and shims
└── blobs/               BSS segment and import tables
tools/                   Build, deploy, and diagnostic scripts
build/web_gen/           Auto-generated WASM glue code
```

---

## Security

Game engines from this era were not designed with modern security in mind.
Networking, file parsing, and content loading paths may contain exploitable
bugs. **Do not expose test servers to untrusted networks.**

A long-term goal is to audit and harden these paths while preserving
compatibility with the original game.

---

## Credits

The engine reconstruction itself comes from
[opencod2/opencod2](https://github.com/opencod2/opencod2) — the native Linux
and Windows port this web target is built on top of. Thanks to its authors and
contributors.

The browser layer, the WASM ABI work, and the web tooling live in this
repository; see the changelog for what each one covers.

---

## Notice

This is an independent reconstruction. It is not affiliated with, authorized
by, or endorsed by Activision Publishing, Inc. or Infinity Ward.

"Call of Duty" and "Call of Duty 2" are trademarks of Activision Publishing,
Inc., used here only for identification and interoperability.

No copyrighted game data is included or distributed. You must supply your own
legally-obtained copy of the game.

The reconstructed source is a derivative work for preservation,
interoperability, research, and education — provided as-is without warranty.
If you are a rights holder with concerns, please open an issue.

---

<p align="center">
  Made by <strong>Sipex</strong>
</p>
