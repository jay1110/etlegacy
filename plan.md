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
- [x] Accept the target as a query parameter as well
      (`ws://relay/?target=host:port`) next to the path form, so the relay is a
      drop-in replacement for simple UDP gateway scripts.
- [x] Fix "Join a server does nothing / client stays in the main menu":
      `src/qcommon/net_web.c` only had `MAX_WS_CONNECTIONS 4` sockets and never
      recycled them. The in-game server browser pings one address per server and
      exhausted every slot, so `WS_GetConnection()` returned `NULL` for the
      server the player actually clicked *Join* on and `Sys_SendPacket()`
      silently dropped the `getchallenge`. There are now 64 slots and the least
      recently used one is closed and reused when they run out.
- [x] Follow-up hardening for the same area: a WebSocket that has only ever
      carried out-of-band traffic (server-browser ping, master query) is now
      closed again after 30 s, and slot recycling prefers those over connections
      that carried netchan traffic. One browser WebSocket - and one UDP socket on
      the relay - is held per remote address, so without this every server-list
      refresh pinned the sockets of every server it touched, and a refresh while
      playing could evict the connection the player was actually using.
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
- [x] Touch overlay fixes: the look area covered the whole viewport at
      `z-index: 900` while the controls bar sat at `z-index: 50`, so once the
      overlay was switched on its own off-switch (and everything else) was
      unreachable. The controls bar is now above the overlay, the overlay itself
      carries a close (X) and a keyboard button, its state is no longer
      persisted in `localStorage` (so F5 always starts with it off) and it is
      force-disabled whenever a launcher panel is shown.
- [x] "Quick single game" / "Host game" (and "Join ETc server", and picking a
      server from the launcher's server list) all ended up in the main menu.
      Root cause was **not** in the engine: the launcher built the extra command
      line (`+connect …` / `+map …`) only after the player had picked a game mode
      and then assigned it to `Module.arguments` from inside `Module.preRun`.
      Emscripten copies `Module.arguments` into its internal `arguments_`
      variable while `etl.js` is being loaded (`makeModuleReceive('arguments_',
      'arguments')` in emscripten's `src/postlibrary.js`, emitted before the wasm
      module is created) and `run()`/`callMain()` only ever read that local, so
      the later assignment was silently dropped and the engine booted with the
      URL-derived command line alone. `src/web/shell.html` now *appends* to the
      array Emscripten captured (`addEngineArgs`), and
      `tools/web-smoke/verify-dist.mjs` fails the build if the assignment ever
      comes back.

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

All Omni-bot sources and data are now vendored in one place,
`vendor/omni-bot/` - see `vendor/omni-bot/README.md` for the provenance of each
subtree and what was pruned:

| Path | From |
| --- | --- |
| `vendor/omni-bot/source/` | `omni-bot-0.93.zip` (buildable 0.8x/0.93 tree) |
| `vendor/omni-bot/source/Omnibot/dependencies/gmscriptex/` | <https://github.com/jswigart/gmscriptex> |
| `vendor/omni-bot/upstream/` | <https://github.com/jswigart/omni-bot> (reference only) |
| `vendor/omni-bot/data/` | `omnibot.zip` (scripts + navigation meshes) |

Status of "build it as wasm":

- [x] Establish where bots would run: Omni-bot is a **server-side** library. The
      game module (`qagame`) loads `omnibot_et.<arch>.so` through `dlopen`
      (`vendor/Omnibot/Common/BotLoadLibrary.cpp`, enabled by `FEATURE_OMNIBOT`).
      The browser build is a **client** that joins a native dedicated server
      (see section 3), and that server already runs the native Omni-bot - so a
      wasm Omni-bot only matters for the in-browser listen server ("Host game" /
      "Quick single game").
- [x] Check whether the supplied archive can be built at all: not as delivered -
      `0.83/Omnibot/dependencies/gmscriptex` is an empty git submodule. Vendored
      separately, see the table above; the tree builds natively.
- [x] Remove the **compiled** Boost dependency (`system`, `filesystem`, `regex`,
      `date_time`), which was the single biggest blocker because Emscripten ships
      no Boost port. The new `OMNIBOT_STD_FILESYSTEM` option (default ON for
      Emscripten) points the `fs`/`obre` aliases in `Common/common.h` at
      `std::filesystem`/`std::regex` instead. What is left of Boost is header
      only and therefore architecture independent, and
      `OMNIBOT_BOOST_INCLUDEDIR` lets a cross build point straight at any host
      header tree (Emscripten's toolchain restricts `find_package` to its own
      sysroot).
- [x] Re-check the "shared memory / threads / sockets" blockers: they were
      already compiled out. `ENABLE_FILE_DOWNLOADER`, `ENABLE_REMOTE_DEBUGGER`,
      `ENABLE_DEBUG_WINDOW` and `ENABLE_REMOTE_DEBUGGING` are commented out even
      for Windows in `Common/common.h`, and `INTERPROCESS` is never defined, so
      `boost::thread`, `boost::asio` and `boost::interprocess::message_queue` are
      unreachable. The one real problem was the *non*-interprocess debug-draw
      path in `Common/Interprocess.cpp`, which finds the loaded `cgame` module
      via `<link.h>`/`dl_iterate_phdr`; it now asks Emscripten's dynamic loader
      for `cgame.mp.wasm32.so` by name.
- [x] Correct the `-ffriend-injection` finding: it was never actually passed.
      `Common/CMakeLists.txt` handed three flags to `set_target_properties`,
      which takes property/value *pairs*, so only `-pthread` reached the
      compiler and `-ffriend-injection`/`-fno-strict-aliasing` became bogus
      property names. That is now a proper `target_compile_options`, without the
      GCC-only flag and without `-pthread` for Emscripten.
- [x] Teach `dependencies/physfs/physfs_platforms.h` about Emscripten (it
      `#error`ed on unknown platforms) and select the POSIX/UNIX backend.
- [x] Packaging: `MODULE_SUFFIX` is `.wasm32` for Emscripten, `omnibot-et` links
      with `-sSIDE_MODULE=1`, and `BotLoadLibrary.cpp` has the matching `wasm32`
      case - so the engine looks for exactly what the build produces,
      `omnibot_et.wasm32.so`.
- [x] Verified natively for all four combinations of {GCC 13, clang 18} x
      {Boost, `OMNIBOT_STD_FILESYSTEM`}. The `std` builds link no Boost shared
      library and contain no `boost::filesystem` symbols. clang is emcc's
      frontend, so this covers the compiler side of the port.
- [ ] **Not verified with `emcc`.** The emsdk downloads are unreachable from the
      environment this was prepared in (`storage.googleapis.com` returns 403), so
      the wasm build has never actually been run. It is expected to work, not
      known to.

Remaining work, in order:

1. Run the wasm build and fix whatever it turns up:

   ```sh
   emcmake cmake -S vendor/omni-bot/source/Omnibot -B build-omnibot-wasm \
       -DOMNIBOT_RTCW=OFF -DCMAKE_BUILD_TYPE=Release \
       -DOMNIBOT_BOOST_INCLUDEDIR=/usr/include
   cmake --build build-omnibot-wasm
   ```

2. Preload `omnibot_et.wasm32.so` in `src/web/shell.html` with
   `FS.createPreloadedFile`, like `cgame`/`ui`/`qagame`: the browser will not
   compile a wasm module synchronously on the main thread, so a module that is
   only written to the virtual filesystem is inert data and `dlopen()` fails.
3. Ship `vendor/omni-bot/data/` (~5 MB of `global_scripts/`, `et/scripts/`,
   `et/nav/`) into the browser filesystem where `Common/FileSystem.cpp` mounts
   it.
4. Build `qagame` with `FEATURE_OMNIBOT` for wasm (force-disabled in
   `cmake/ETLEmscripten.cmake`), which also requires a side module to be able to
   `dlopen()` another side module.
5. Drop the `+set omnibot_enable 0` that the web launcher passes for "Host game"
   and "Quick single game" in `src/web/shell.html`.

---

## Definition of done

- [ ] A user opens a published URL and reaches the ET:Legacy main menu.
- [ ] The user can enter a relay/server address and connect to a running
      dedicated server.
- [ ] A second player can join the same server from their own browser.
- [ ] Gameplay (movement, shooting, map load) works end-to-end.
