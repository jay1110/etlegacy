# Running ET: Legacy in a web browser (WebAssembly)

ET: Legacy can be compiled to WebAssembly with Emscripten and played in a
browser. Because a browser cannot open raw UDP sockets or host a server, and
because the retail game data may not be redistributed, a playable setup has a
few moving parts. This document describes the full local workflow: build the
web client, supply the game data, run a server, run the WebSocket relay, and
open the page.

## Overview

```
 Browser (etl.html + etl.wasm)                Native host machine
 ┌───────────────────────────┐               ┌──────────────────────────┐
 │ engine (MAIN_MODULE)       │  wss:// / ws  │ ws-relay (tools/ws-relay)│
 │  + cgame/ui (SIDE_MODULE)  │◀────────────▶│        │                 │
 │  downloads pak0-2.pk3      │               │        │ UDP             │
 │  + legacy_<ver>.pk3        │               │   etlded (dedicated srv) │
 └───────────────────────────┘               └──────────────────────────┘
```

- The **engine** is built as an Emscripten `MAIN_MODULE`; the game logic
  (`cgame`, `ui`) is built as `SIDE_MODULE`s loaded with `dlopen`.
- The **retail paks** (`pak0.pk3`, `pak1.pk3`, `pak2.pk3`) and the **mod pk3**
  (`legacy_<version>.pk3`) are downloaded by the page at startup and cached in
  IndexedDB. They are not embedded in the build.
- The browser joins a **native dedicated server** through the
  **WebSocket→UDP relay** in `tools/ws-relay`.

## 1. Build the web client

Requires the [Emscripten SDK](https://emscripten.org/) (pinned to a version
verified to work; see `.github/workflows/emscripten.yml`, currently `4.0.23`).

```bash
emcmake cmake -B build-wasm \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_CLIENT=ON -DBUILD_SERVER=OFF \
  -DBUILD_MOD=ON -DBUILD_CLIENT_MOD=ON -DBUILD_SERVER_MOD=OFF \
  -DFEATURE_RENDERER1=ON -DFEATURE_RENDERER2=OFF -DFEATURE_GL4ES=ON
cmake --build build-wasm --parallel "$(nproc)"
```

This produces `etl.html`, `etl.js`, `etl.wasm` and the side modules
`cgame.mp.wasm32.so` / `ui.mp.wasm32.so` in `build-wasm/`. The exact CMake flags
CI uses are in `.github/workflows/emscripten.yml` — copy them for an exact match.

## 2. Lay out the web directory

Serve a directory with this layout (the CI "Package web release" step builds
exactly this):

```
etlegacy-web/
├── etl.html            # game page
├── index.html          # copy of etl.html
├── etl.js
├── etl.wasm
├── etmain/             # put pak0.pk3, pak1.pk3, pak2.pk3 here
└── legacy/
    ├── legacy_<ver>.pk3        # mod pk3 (cgame/ui game logic + ui menus + media)
    ├── cgame.mp.wasm32.so      # standalone side module (fallback)
    └── ui.mp.wasm32.so         # standalone side module (fallback)
```

The game logic (`cgame`/`ui`) is loaded from the mod pk3: the page reads the
side modules straight out of it and compiles them up front (into both
`fs_homepath/legacy` and `fs_basepath/legacy`, the two locations the engine
`dlopen()`s them from) so the engine's `dlopen()` is a cache hit on its first
attempt. The page searches **any** `*.pk3` present in the
`legacy/` folder for the modules (just like the engine scans `fs_game` for
paks), so a pk3 served under a name other than `legacy_<ver>.pk3` still works.
The standalone `cgame.mp.wasm32.so` / `ui.mp.wasm32.so` next to it are only a
fallback used when a module is missing from every pk3, so at least one pk3 that
contains the modules must be present.

Copy `pak0.pk3`, `pak1.pk3`, `pak2.pk3` from a retail Wolfenstein: Enemy
Territory install into `etmain/`. **These are not included and may not be
redistributed.**

Alternatively, you do not have to host the retail paks at all: the loading
screen has a **"Load local game files (pak0-2.pk3)"** button that lets each
player pick `pak0.pk3`, `pak1.pk3` and `pak2.pk3` from their own installation
directly in the browser. The picked paks are written into the in-browser cache
(IndexedDB), so they are only chosen once. This is also offered on the error
screen if a network download fails, alongside a **"Retry download"** button.

## 3. Serve the page

Any static web server works, as long as `.wasm` is served with the
`application/wasm` MIME type and every file returns HTTP 200:

```bash
cd etlegacy-web
python3 -m http.server 8000
# open http://localhost:8000/
```

The build does not use wasm threads / `SharedArrayBuffer`, so **no COOP/COEP
cross-origin-isolation headers are required**. (If a future build enables
pthreads, the page must then be served with
`Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`.)

> **Upload the `.pk3` and `.so` files in binary mode.** They are binary
> WebAssembly data. If they are transferred over FTP/SFTP in *ASCII*/*text*
> mode (or rewritten by a server content filter), their bytes get mangled and
> the browser aborts at startup with **`need to see wasm magic number`** /
> `VM_Create on ui failed`. The shell now detects this and reports the real
> cause (e.g. *"most likely corrupted in upload (FTP/SFTP in ASCII/text mode)"*
> or *"the server returned an HTML page (HTTP 200), not the binary"*). If you
> see it: re-upload `legacy/*.pk3` and `legacy/*.so` (and `etmain/*.pk3`) in
> **binary** mode and confirm each URL returns the raw file with HTTP 200.

### Hosting on GitHub Pages

The `emscripten.yml` workflow publishes the web client to GitHub Pages on pushes
to the default branch (enable Pages with "GitHub Actions" as the source under
Settings → Pages). The retail paks are **not** deployed, so either host an
`etmain/` folder next to the page yourself or point the client at another host
with `?assets=` (that host must allow CORS).

## 4. Run a dedicated server

Browsers cannot host a server. Run a normal native ET: Legacy dedicated server
on a machine with a public UDP port (default `27960`):

```bash
etlded +set dedicated 2 +set net_port 27960 +map oasis
```

## 5. Run the WebSocket→UDP relay

The relay bridges the browser's WebSocket to the server's UDP. See
`tools/ws-relay/README.md`.

```bash
cd tools/ws-relay
npm install
npm start                      # plain ws:// on :8080
# or, for an HTTPS page, serve wss:// directly:
node relay.js --tls-cert cert.pem --tls-key key.pem --port 8443
```

A page served over `https://` (e.g. GitHub Pages) can only open `wss://`
sockets, so the relay must be reachable over TLS — either terminate TLS in the
relay (above) or behind an nginx reverse proxy (see the relay README).

## 6. Open the game and connect

On first load the page asks how to provide the game data: **download
pak0.pk3** (fetched together with pak1/pak2 and cached in the browser) or
**use a local pak0.pk3** picked from your own installation. Once the data is
set, a **Run game** menu offers: starting the game to the main menu without
connecting anywhere, joining the preconfigured ETc server (a
different `fs_game`, xmod — missing pk3s are downloaded from the server),
a quick single game (`+map oasis`), a manually maintained server list
(`SERVER_LIST` in `src/web/shell.html`), and hosting a listen server in the
browser (other players join through the relay).

Alternatively, configure everything from the page URL (which skips the menu)
or from the in-page **Connect…** panel (bottom controls bar):

| Parameter | Purpose | Example |
|-----------|---------|---------|
| `assets`  | Base URL for `pak0-2.pk3` | `?assets=https://example.com/etmain/` |
| `legacy`  | Base URL for the mod pk3 | `?legacy=https://example.com/legacy/` |
| `mod`     | Override which mod pk3(s) to fetch | `?mod=legacy_2.84.0.pk3` |
| `relay`   | WebSocket relay URL (`net_wsRelayServer`) | `?relay=wss://relay.example.com:8443` |
| `connect` | Game server `host:port` to auto-join | `?connect=203.0.113.10:27960` |
| `map`     | Start a local game on this map | `?map=oasis` |

Full example:

```
https://your-page/etl.html?relay=wss://relay.example.com:8443&connect=203.0.113.10:27960
```

Multiple browser players can open the same link and join the same server; each
gets its own UDP socket on the relay side.

## Verification / smoke tests

`tools/web-smoke/` contains two smoke tests (run in CI after the build):

- `verify-dist.mjs <dir>` — structural check of the packaged build (engine files
  present, valid wasm header, mod pk3 contains the side modules). No browser
  needed.
- `boot-smoke.mjs <dir>` — boots the build in headless Chromium (Playwright) and
  confirms the wasm engine initializes and reaches its asset-bootstrap stage
  without a fatal error. Because the retail paks are not redistributable, it
  cannot assert a full "reaches the main menu" run in CI; it verifies the client
  boots cleanly up to the point where the (missing) paks would be loaded.

```bash
node tools/web-smoke/verify-dist.mjs dist/etlegacy-web
(cd tools/web-smoke && npm install && npx playwright install chromium)
node tools/web-smoke/boot-smoke.mjs dist/etlegacy-web
```

## Known limitations

- Latency is higher than native UDP (the relay uses TCP/WebSocket).
- The retail paks must be supplied by the user; they are never redistributed.
- WebRTC data channels (lower latency than WebSocket) are possible future work.
- The browser console logs `The ScriptProcessorNode is deprecated. Use
  AudioWorkletNode instead.` once at startup. This comes from Emscripten's
  bundled SDL2 audio backend, not from ET: Legacy, and is a harmless
  deprecation notice — sound still works.

## Troubleshooting

- **"The game appears to be stuck at: Downloading pakX.pk3 …"** — a large pak
  (`pak0.pk3` is ~228 MB) can take a while on a slow link. The loader only shows
  this once the download has made **no progress for a full minute**; while the
  byte counter keeps moving it will keep waiting. You can also click **"Load
  local game files"** to pick the paks from your own installation instead of
  waiting for the download.
- **WebGL `no ARRAY_BUFFER is bound and offset is non-zero` errors / a black
  screen** — the renderer draws from client-side vertex arrays, which WebGL does
  not allow directly. The web build links with `-s FULL_ES2=1` (see
  `cmake/ETLEmscripten.cmake`) so Emscripten uploads those arrays into buffer
  objects automatically. If you build without it you will see a flood of these
  `INVALID_OPERATION` messages and nothing renders.
- **Black screen with no GL errors while the engine log looks healthy (gl4es
  build)** — the renderer draws the whole frame into its own FBO and presents
  it by drawing a fullscreen quad (gamma/blit shader) to the canvas. gl4es
  defers `glBegin`/`glEnd` geometry into a pending render list
  (`LIBGL_BEGINEND=1` default) and draws it on the *next* flush with the GL
  state current at flush time — and `gl4es_glUseProgram()` does not flush. The
  engine unbinds the present shader right after the quad, so the quad used to
  be drawn later by the fixed-function emulator with the 2D pixel-space ortho
  projection applied to its NDC coordinates, collapsing it off-screen and
  leaving only the black clear color on the canvas. `GL_FullscreenQuad()`
  (`src/renderer/tr_backend.c`) now issues an explicit `glFlush()` while the
  program is still bound when built with `FEATURE_GL4ES`. Additionally, the
  browser build now forces `r_ignorehwgamma 1` (ROM, `src/sdl/sdl_glimp.c`) so
  the FBO/gamma present pass is skipped entirely and the scene is rendered
  straight to the canvas; gamma is baked into texture uploads instead
  (`r_gamma` defaults to `2.2` on the web, like the Wwasm reference port).
- **Connecting to a server does nothing — no `Awaiting challenge` /
  `connectResponse` in the console** — two historical bugs in
  `src/qcommon/net_web.c`, both fixed: `NET_Sleep()`/`NET_Event()` were no-ops,
  so packets received from the WebSocket relay were queued but *never
  dispatched* to the client (the handshake replies were silently discarded);
  and packets sent while the WebSocket was still `CONNECTING` (the very first
  `getchallenge`) were dropped instead of being buffered until `onopen`. Also
  note the browser cannot resolve hostnames for game servers — use a numeric
  IP (`203.0.113.10:27960`), and make sure the relay
  (`tools/ws-relay`) is running and reachable (`net_wsRelayServer`, `?relay=`).
- **No sound until (or even after) clicking** — browsers keep an
  `AudioContext` suspended until a user gesture. SDL2's emscripten backend
  resumes it once `navigator.userActivation.hasBeenActive` is true, and the
  shell (`src/web/shell.html`) additionally tracks every context created and
  resumes it on `pointerdown`/`keydown`/`touchstart` — including contexts
  created *after* the first gesture (SDL opens the audio device long after the
  setup-menu click). If sound is still missing, check the console for
  `memory access out of bounds` first: once the wasm traps, the audio callback
  dies with it (see below).
- **`memory access out of bounds` (at `doRewind` / `__synccall` /
  `silence_callback`, often followed by an endless stream of the same error
  from `SDL2.audio.scriptProcessorNode.onaudioprocess`)** — an Asyncify state
  corruption or resource-limit problem. The first trap (usually at `doRewind`)
  poisons the Asyncify state, after which *every* re-entry into the wasm (e.g.
  the SDL2 audio callback) traps with the same error, forever — only the first
  error matters for diagnosis. Causes, all handled by the build:
  1. **dlopen() of a side module from deep inside the running engine (the
     root cause of the field crash at `__dlopen_js` → `doRewind`).** Under
     `-sASYNCIFY`, Emscripten's `_dlopen_js` is *always* asynchronous: it
     unwinds and rewinds the entire wasm call stack, even when the module is
     already precompiled in the `preloadedWasm` cache (the cache only skips
     the fetch/compile, not the unwind). The engine used to first dlopen
     cgame/ui deep inside `Com_Frame` (client init → `VM_Create` →
     `Sys_LoadGameDll`); the dlopen mutated the dynamic-linking state while
     dozens of frames were unwound and the rewind trapped. None of the
     working browser ports (Qwasm2, jdarpinian/ioq3) ever dlopen from inside
     the running engine. The engine now dlopen()s both side modules at the
     very top of `main()` (`Sys_PreloadGameDlls` in `src/sys/sys_web.c`),
     the officially supported Asyncify+dlopen pattern; Emscripten caches
     loaded DSOs by path and its `dlclose()` is a no-op, so every later
     `Sys_LoadLibrary()` of the same path is a synchronous cache hit and no
     mid-frame unwind ever happens.
  2. **Stale cgame/ui side modules from an old build.** The engine is linked
     with `-sASYNCIFY`; every dlopen()ed
     side module must be Asyncify-instrumented too (`-sASYNCIFY` in
     `cmake/ETLBuildMod.cmake`, requires Emscripten ≥ 3.1.17 for shared
     Asyncify globals across dynamic linking; CI uses 4.0.23). A module built
     without it cannot save/restore its frames when Asyncify unwinds through
     them, which corrupts the rewind. Because the mod pk3 and the standalone
     `.so` files are cached in IndexedDB and the browser HTTP cache, a site
     update used to leave old modules paired with a new engine. The shell now
     (a) revalidates the mod pk3/`.so` against the server on every load,
     (b) deletes cached `legacy_*.pk3` from other builds, and (c) refuses to
     preload any side module that lacks the `asyncify_start_rewind` export,
     reporting a clear error instead of crashing later. If you see that error,
     redeploy matching `etl.wasm`/`etl.js`/pk3/`.so` artifacts from one build,
     and clear the site data if it persists.
  3. Asyncify/native stack overflow — the web build sets a generous `-s
     ASYNCIFY_STACK_SIZE` (16 MiB) and native `-s STACK_SIZE` (8 MiB) in
     `cmake/ETLEmscripten.cmake`; the engine's deep call stacks overflow the
     Emscripten defaults.
  4. The heap hitting its growth cap — the build starts at 2 GiB (`-s
     INITIAL_MEMORY`) and raises the cap to the wasm32 maximum (`-s
     MAXIMUM_MEMORY=4gb`, growing on demand); large maps plus downloaded pk3s
     overflow the 2 GiB Emscripten default cap.

