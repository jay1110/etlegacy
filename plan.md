# Plan: Playable ET:Legacy in the Web Browser (Emscripten/WASM)

This plan tracks everything still required so that a user can open a link, run
ET:Legacy in a web browser, and play multiplayer (connecting to a server that
other players can also join).

## Current state (what already works)

Running `.github/workflows/emscripten.yml` builds a WebAssembly **client** that
now also builds the client game-logic modules (`cgame`, `ui`) and ships an asset
+ networking bootstrap. Remaining gaps are noted below.

Concrete decisions taken:

- **Retail assets** (`pak0.pk3`, `pak1.pk3`, `pak2.pk3`) are downloaded at
  runtime, by default same-origin from an `etmain/` folder served next to the
  page (no CORS needed); override the location with `?assets=<url>` (a
  cross-origin URL then requires the remote web space to allow CORS downloads).
  See `src/web/shell.html`. They are cached in IndexedDB.
- **Hosting model** (same approach as the Quake 3 / QuakeJS web port): the
  browser cannot host a server, so a **native dedicated server** is the host and
  browser clients join it through the **WebSocket->UDP relay** in
  `tools/ws-relay`. Connect via `?relay=<ws-url>&connect=<host:port>`.
- **Game modules** are built as Emscripten `SIDE_MODULE`s (`cgame`, `ui`) and
  loaded by the `MAIN_MODULE` engine via `dlopen`. They are embedded into the
  engine filesystem image (`etl.data`, via `--preload-file` in
  `cmake/ETLBuildMod.cmake`) at `/etlegacy/legacy/`, so they ship with the page
  and do not need to be served/fetched separately.

## Why it is not yet playable (gaps)

1. **Game-logic modules are not built.** The workflow uses `BUILD_MOD=OFF` and
   `ETLEmscripten.cmake` force-disables it, so `cgame`, `ui`, and `qagame` are
   never produced.
2. **No dynamic linking configured.** The engine loads `cgame`/`ui`/`qagame`
   through `Sys_LoadGameDll` -> `dlopen` (`src/sys/sys_main.c`). On wasm this
   needs Emscripten `MAIN_MODULE`/`SIDE_MODULE` linking; it is configured
   nowhere. QVM bytecode loading is disabled (`src/qcommon/vm.c`).
3. **No game assets packaged.** Nothing preloads/mounts pk3 data into the
   browser filesystem, so the client has no maps/media.
4. **No server/relay in the pipeline.** Browsers cannot host UDP servers; a
   native dedicated server + `ws-relay` is required, and neither is
   deployed/documented as part of the web workflow.
5. **No hosting/deploy.** The workflow only uploads artifacts, so there is no
   published link to open.
6. **Shell UI has no connect/relay controls.**

---

## Task checklist

Legend: `[ ]` = TODO, `[x]` = done.

### 1. Build the game-logic modules for WebAssembly

- [x] Decide on the wasm module strategy: Emscripten dynamic linking
      (`MAIN_MODULE` for the engine + `SIDE_MODULE` for each game lib, loaded via
      `dlopen`).
- [x] Add `MAIN_MODULE` (engine) and `EXPORTED_RUNTIME_METHODS`/`FORCE_FILESYSTEM`
      to `cmake/ETLEmscripten.cmake`.
- [x] Enable `BUILD_MOD`/`BUILD_CLIENT_MOD` for the wasm build and build
      `cgame` + `ui` as `SIDE_MODULE`s with loader-matching names. They use a
      `.so` suffix (`cgame.mp.wasm32.so`, `ui.mp.wasm32.so`) so Emscripten
      precompiles the preloaded modules and the engine's `dlopen` succeeds as a
      cache hit — see `cmake/ETLBuildMod.cmake`.
- [x] **Verify with a real emcc build** that the `MAIN_MODULE`/`SIDE_MODULE`
      link succeeds. Done: a full `emcmake cmake` + `cmake --build` run produced
      `etl.wasm`, `cgame.mp.wasm32.so`, `ui.mp.wasm32.so` and
      `qagame.mp.wasm32.so` (all with the `\0asm` magic). This uncovered two real
      build breakages that are now fixed: every object linked into a
      `MAIN_MODULE`/`SIDE_MODULE` must be compiled with `-fPIC`, both for the
      engine/mods (`cmake/ETLEmscripten.cmake`) and for the gl4es
      `ExternalProject` (`cmake/ETLGl4es.cmake`); without it `wasm-ld` aborts the
      engine link with "relocation R_WASM_MEMORY_ADDR_* cannot be used against
      symbol ...; recompile with -fPIC".
- [ ] Verify at runtime that `Sys_LoadGameDll` resolves `dllEntry`/`vmMain` from
      the side modules (needs the retail paks; the boot smoke test only gets as
      far as the asset bootstrap).
- [ ] Confirm the client reaches the main menu (ui module loads).

### 2. Package and mount game assets

- [x] Asset-delivery method chosen: runtime `fetch` into the browser FS with
      IndexedDB (IDBFS) caching — see `src/web/shell.html`.
- [x] Mount assets at the engine-expected path (`/etlegacy/etmain`,
      `/etlegacy/legacy`); matches `Sys_SetDefaultInstallPath("/etlegacy")`.
- [x] Retail `pak0-2.pk3` fetched from a configurable location (default
      same-origin `etmain/`, override with `?assets=`) instead of being
      redistributed.
- [x] Game side-modules fetched same-origin into `/etlegacy/legacy`.
- [ ] Verify a map loads and renders in the browser (needs emcc build + a live
      web space that serves the paks with CORS enabled).

### 3. Server + relay so other players can join

- [x] Document that hosting happens **outside** the browser (native `etlded`)
      and that browser clients join via the relay — see `tools/ws-relay/README.md`
      and `plan.md`.
- [x] Make the WebSocket-to-UDP relay reproducibly runnable: added
      `tools/ws-relay/package.json` (`npm install` / `npm start`). Smoke-tested
      that it accepts a WebSocket connection.
- [x] Expose relay/connect entry points to the client via URL parameters
      (`?relay=`, `?connect=`) that set `net_wsRelayServer` and `+connect`.
- [x] Add TLS (`wss://`) guidance/config so the relay works from HTTPS pages
      (browsers block `ws://` from `https://`): `relay.js` now serves `wss://`
      directly via `--tls-cert`/`--tls-key` (smoke-tested), and the README
      documents both that and an nginx reverse-proxy setup.
- [ ] End-to-end test: browser client -> relay -> dedicated server connect.
- [ ] Verify two browser clients can join the same server simultaneously.
- [ ] (Optional/perf) Investigate WebRTC data channels to reduce latency.

### 4. Hosting the "link"

- [x] Add a deploy step to `emscripten.yml` (GitHub Pages) that publishes
      `etl.html`, `.js`, `.wasm`, the mod `.pk3` and side modules on the default
      branch (`deploy-pages` job + `upload-pages-artifact`; `.nojekyll` added).
- [x] Ensure the page is served with correct MIME types and document header
      requirements: the build uses no wasm threads/`SharedArrayBuffer`, so no
      COOP/COEP is required; GitHub Pages serves `.wasm` as `application/wasm`.
      Documented in `docs/web.md` and the workflow.
- [ ] Confirm the published URL loads and runs in a fresh browser.

### 5. Shell / UX

- [x] Web shell drives asset download + relay/connect via URL parameters
      (`?assets=`, `?relay=`, `?connect=`) in `src/web/shell.html`.
- [x] Add an in-page server/relay connect UI (form) instead of URL-only config:
      a "Connect…" panel writes `?relay`/`?connect` (preserving other params)
      and reloads through the same bootstrap.
- [x] Handle browser constraints: user-gesture required for audio (AudioContext
      resume on first gesture), pointer lock for mouse look (click-to-capture),
      fullscreen toggle (enter/exit).

### 6. CI / verification

- [x] `emscripten.yml` builds the client mod (`-DBUILD_MOD=ON`) and its artifact
      glob (`build-wasm/*.wasm`) now captures the `SIDE_MODULE` `.wasm` files.
- [x] Add a smoke test that boots the wasm client and confirms it reaches its
      asset-bootstrap stage without fatal errors (`tools/web-smoke/boot-smoke.mjs`,
      headless Chromium via Playwright), plus a deterministic structural check
      (`tools/web-smoke/verify-dist.mjs`). A full "reaches the main menu" run is
      not possible in CI because the retail paks are not redistributable.
- [x] Document the full local workflow (build, run relay, run dedicated server,
      open page) in `docs/web.md`.
- [x] **Executed** (not just wired up): the whole section-6 pipeline was run
      locally against a real Emscripten toolchain — configure, full build,
      packaging of `dist/etlegacy-web`, `tools/web-smoke/verify-dist.mjs`
      (all structural checks pass) and `tools/web-smoke/boot-smoke.mjs` in
      headless Chromium ("PASS loading overlay dismissed: engine started").
      The two `-fPIC` fixes above were required to get there.
- [ ] Follow-up found while running it: the side modules and the engine carry a
      very large wasm *data* section (`cgame`/`qagame` ~66-68 MB each, `etl.wasm`
      ~70 MB), because a side module has no BSS - zero-initialised statics are
      emitted as real data. That makes the page download huge; worth shrinking
      (e.g. `-sSIDE_MODULE=2`/`--strip-debug` for the modules, `-Os`, or moving
      the large static buffers to heap allocations) before promoting the link.

### 7. Omni-bot (bots) for the web build

Source under review: `omni-bot-0.93.zip` in the repository root (the Omni-bot
0.83/0.93 tree with `Omnibot/Common`, `Omnibot/ET`, `dependencies/`).

Analysis of "build it as wasm":

- [x] Establish where bots would run: Omni-bot is a **server-side** library. The
      game module (`qagame`) loads `omnibot_et.<arch>.so` through `dlopen`
      (`vendor/Omnibot/Common/BotLoadLibrary.cpp`, enabled by `FEATURE_OMNIBOT`).
      The browser build is a **client** that joins a native dedicated server
      (see section 3), and that server already runs the native Omni-bot - so a
      wasm Omni-bot adds no gameplay today. It would only matter if the wasm
      `qagame` were ever run as an in-browser/host-side server.
- [x] Check whether the supplied archive can be built at all:
      **it cannot as-is.** `0.83/Omnibot/dependencies/gmscriptex` is an empty
      directory - the GameMonkey script engine is a git submodule
      (`.gitmodules`: `path = 0.83/Omnibot/dependencies/gmscriptex`) and the zip
      contains no submodule content, while `Omnibot/Common/CMakeLists.txt` globs
      its entire `gmsrc_ex` tree into `omnibot-common`.
- [x] Check the remaining dependencies against the browser target:
      - Boost **compiled** libraries are required (`find_package(Boost COMPONENTS
        system filesystem regex date_time REQUIRED)`); Emscripten ships no Boost
        port, so they would have to be cross-built for wasm first.
      - `Omnibot/Common/Interprocess.cpp` uses `boost::interprocess::message_queue`
        (shared-memory IPC) and `Common/common.h` pulls in `boost/thread.hpp` and
        `boost/asio.hpp`. Shared memory and threads need
        `SharedArrayBuffer`/pthreads, which this build deliberately does not use
        (no COOP/COEP, see section 4), and `boost::asio` sockets do not exist in
        the browser.
      - `omnibot-common` is compiled with `-pthread -ffriend-injection`;
        `-ffriend-injection` is a GCC-only, long-removed flag that clang/emcc
        rejects.
- [ ] Consequently **not started**: wiring a wasm Omni-bot build into
      `cmake/ETLEmscripten.cmake` (`FEATURE_OMNIBOT` is force-disabled there).

If it is still wanted, the order of work would be:

1. Obtain the missing `gmscriptex` submodule and vendor it next to the Omni-bot
   sources (the zip alone is not a complete source tree).
2. Cross-build the needed Boost libraries for wasm, or replace their uses
   (`filesystem` -> `std::filesystem`, `regex` -> `std::regex`, drop
   `date_time`).
3. Stub out the browser-impossible parts: `Interprocess.cpp` (debug-draw shared
   memory queue), the `boost::asio` remote-debugger/`FileDownloader` paths and
   all threading (the bot would have to run on the engine's own frame loop).
4. Build `omnibot-et` as an Emscripten `SIDE_MODULE` named
   `omnibot_et.wasm32.so` (matching the `SUFFIX`/`POSTFIX` logic in
   `BotLoadLibrary.cpp`, which needs a wasm32 case), and preload it in
   `src/web/shell.html` like the other side modules so `qagame`'s `dlopen`
   resolves synchronously.
5. Ship the Omni-bot script/nav data (`0.83/Omnibot/ET/scripts`, nav meshes)
   into the browser filesystem, and only then enable `FEATURE_OMNIBOT` for the
   wasm build.

---

## Definition of done

- [ ] A user opens a published URL and reaches the ET:Legacy main menu.
- [ ] The user can enter a relay/server address and connect to a running
      dedicated server.
- [ ] A second player can join the same server from their own browser.
- [ ] Gameplay (movement, shooting, map load) works end-to-end.
