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

      The relay half of this is now tested automatically:
      `tools/ws-relay/test-relay.mjs` drives a real WebSocket client through
      `relay.js` to a UDP stand-in server that answers ET's out-of-band
      `getinfo`, and asserts the `infoResponse` comes back with the same
      challenge. It covers both URL forms, hostname targets (the DNS path the
      browser build depends on, because it cannot resolve names itself), a
      packet sent before the relay's UDP socket has finished binding, five
      packets in a row on one connection, six kinds of malformed target (close
      code 1008), the `--max-connections` limit (1013), the idle-connection
      reaper and a failed bind. It runs as the `relay-test` job in
      `emscripten.yml`, needs no toolchain and no game data.
      What is still untested is the *browser* end of the chain, which needs the
      retail paks and a real dedicated server.

      Found and fixed while writing it: the relay printed "Listening on ..."
      synchronously, before the socket was bound - and on `EADDRINUSE` (a
      restart while the old process still held the port) it *stayed running*,
      relaying nothing, while looking healthy to systemd/pm2 and to anything
      reading its log. The banner is now printed from the `listening` event and
      a failed bind exits with status 1; both are asserted by the test.
- [ ] Verify two browser clients can join the same server simultaneously.

      The relay half is covered by `test-relay.mjs` as well: two WebSocket
      clients on one target at the same time each get their own UDP source port
      (that is what lets the game server tell two players behind one relay
      apart), each sees only its own replies, a packet the server pushes to one
      of them does not leak to the other, and one client disconnecting leaves
      the other working. What is left is the browser end - two real clients,
      which needs the retail paks and a dedicated server.
- [x] (Optional/perf) Investigate WebRTC data channels to reduce latency.

      Outcome: **not worth doing yet**, and nothing about the current design
      blocks it later.

      The win would be real. An `RTCDataChannel` opened with
      `{ordered: false, maxRetransmits: 0}` has exactly the semantics ET's
      netchan already expects - it does its own sequencing and tolerates loss -
      so it removes the TCP head-of-line blocking and retransmit stalls that a
      WebSocket adds, which are the part that actually hurts, not the constant
      overhead.

      The cost is a whole second transport on both ends:

      - Emscripten's built-in `SOCKET_WEBRTC` backend is a dead end. It is
        marked `[deprecated]` in emscripten's `src/settings.js` and is listed as
        "under consideration for removal" in `tools/settings.py`, it is built on
        bundled copies of socket.io/wrtcp, and it plugs into the BSD-socket
        emulation (SOCKFS) - which this port deliberately does not use;
        `src/qcommon/net_web.c` talks to `emscripten/websocket.h` directly. So
        the engine side means a hand-written `--js-library` shim around
        `RTCPeerConnection`.
      - The relay would have to terminate DTLS+SCTP, which Node cannot do on
        its own: it needs a new dependency (`node-datachannel`/libdatachannel,
        or the pure-JS `werift`) next to the single, ubiquitous `ws` it has
        today.
      - WebRTC still needs a signalling channel for the offer/answer exchange,
        plus ICE (STUN, and TURN for symmetric NAT). The WebSocket path stays
        in place either way, as signalling and as the fallback.

      None of that can be validated in this environment (no browser end-to-end
      run, see the two items above), and shipping an unvalidated second
      transport is worse than a slightly slower one that works.

      What *was* done instead, because it is the cheap half of the same
      problem: the relay now disables Nagle's algorithm on every accepted
      connection (`setNoDelay(true)`). Small game packets were otherwise liable
      to be held back and coalesced, which adds tens of milliseconds - the same
      order as the head-of-line blocking WebRTC would remove.

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
- [x] Test the WebSocket relay end to end (`tools/ws-relay/test-relay.mjs`,
      `relay-test` job): a real WebSocket client through `relay.js` to a UDP
      stand-in game server, 19 assertions, no toolchain or game data needed. See
      section 3.
- [x] **Executed** (not just wired up): the whole section-6 pipeline was run
      locally against a real Emscripten toolchain — configure, full build,
      packaging of `dist/etlegacy-web`, `tools/web-smoke/verify-dist.mjs`
      (all structural checks pass) and `tools/web-smoke/boot-smoke.mjs` in
      headless Chromium ("PASS loading overlay dismissed: engine started").
      The two `-fPIC` fixes above were required to get there.
- [ ] Follow-up found while running it: the side modules and the engine carry a
      very large wasm *data* section (`cgame`/`qagame` ~66-68 MB each, `etl.wasm`
      ~70 MB), because a side module has no BSS - zero-initialised statics are
      emitted as real data. Confirmed in isolation: a side module whose only
      content is a 32 MB zero-filled static array links to a 32 MB `.so`, while
      the same file built as a normal (non-PIC) module keeps it in BSS.

      It matters much less than the raw numbers suggest, because those bytes are
      zeros: that same 32 MB module is **30 KB** after `gzip -9`. So any host that
      sends `Content-Encoding: gzip`/`br` already makes the download small, and
      GitHub Actions artifacts are zipped anyway. `docs/web.md` now says so, and
      warns that `python3 -m http.server` does not compress. What is left is disk
      footprint and wasm compile time in the browser, worth attacking with
      `-sSIDE_MODULE=2`/`--strip-debug`, `-Os`, or by heap-allocating the big
      static buffers - none of which can be measured without the engine build.

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
      frontend, so this covers the compiler side of the port. Compiling is not
      enough though: `REGEX_OPTIONS` was `basic|icase|grep`, which is one valid
      grammar for Boost (its `grep` contains `basic`) but two conflicting
      grammar bits for `std::regex`, so every `FindAllFiles()` filter would have
      thrown and - because `Utils::RegexMatch` swallowed the exception - matched
      nothing, silently. Fixed and the swallowed error is now logged.
- [x] **Verified with `emcc`.** The emsdk downloads are still unreachable from
      the environment this was prepared in (`storage.googleapis.com` returns
      403), so the distribution's own `emscripten` package (3.1.6) was used
      instead. It was enough to configure, compile and link the bot library, and
      running the build for the first time turned up six real bugs - the port
      was "expected to work", and did not:
  1. **`add_library(omnibot-et MODULE ...)` silently became a static archive.**
     Emscripten's toolchain file sets `TARGET_SUPPORTS_SHARED_LIBS FALSE`, so
     CMake downgraded the module, archived it with `emar` and never applied
     `-sSIDE_MODULE=1`. The output was an `ar` archive that no `dlopen()` could
     ever load. Same trap `cmake/ETLEmscripten.cmake` already works around for
     cgame/ui/qagame.
  2. **`-DOMNIBOT_BOOST_INCLUDEDIR=/usr/include` (as documented below) could
     never have worked.** It ended up as `-isystem /usr/include`, putting the
     host glibc headers ahead of the Emscripten sysroot, and every translation
     unit died on `bits/libc-header-start.h`. The option now stages a directory
     containing only a `boost` symlink and puts that on the include path, so
     pointing it at a system include directory is safe.
  3. **Emscripten defines `__unix__` but not `__linux__`.** Seven files fell
     through their `#elif defined(__linux__) || (__MACH__ && __APPLE__)` arm
     into an `#error`, or lost `_stricmp`, `PATHDELIMITER` and
     `GM_DEFAULT_ALLOC_ALIGNMENT`.
  4. **`std::random_shuffle` was removed in C++17**, which
     `OMNIBOT_STD_FILESYSTEM` (the Emscripten default) requires - three call
     sites, replaced with a local Fisher-Yates helper.
  5. **The physfs LZMA glob pulled in duplicate symbols.** It matched both
     `LzmaDecode.c` and the alternative `LzmaDecodeSize.c` (both define
     `LzmaDecode`/`LzmaDecodeProperties`) plus `LzmaTest.c`, which defines
     `main`. A native static link only pulls the members it needs, so nobody
     ever noticed; a wasm side module links the archive with `--whole-archive`
     and fails outright.
  6. **The engine-side `vendor/Omnibot/Common/BotLoadLibrary.cpp` had the same
     `#error` platform guard**, so `FEATURE_OMNIBOT` could not have compiled for
     Emscripten at all - the `wasm32` case noted above only covered the library
     *suffix*, 300 lines further down.

      Native builds were re-checked afterwards with both `OMNIBOT_STD_FILESYSTEM`
      and compiled Boost: unchanged.

Remaining work, in order:

- [x] 1. Run the wasm build and fix whatever it turns up:

     ```sh
     emcmake cmake -S vendor/omni-bot/source/Omnibot -B build-omnibot-wasm \
         -DOMNIBOT_RTCW=OFF -DCMAKE_BUILD_TYPE=Release \
         -DOMNIBOT_BOOST_INCLUDEDIR=/usr/include
     cmake --build build-omnibot-wasm
     ```

     Produces `build-omnibot-wasm/ET/omnibot_et.wasm32.so`: a valid wasm binary
     with a `dylink.0` section that exports `ExportBotFunctionsFromDLL`, the
     symbol `BotLoadLibrary.cpp` resolves through `dlsym`.

- [x] 2. Preload `omnibot_et.wasm32.so` in `src/web/shell.html` with
     `FS.createPreloadedFile`, like `cgame`/`ui`/`qagame`: the browser will not
     compile a wasm module synchronously on the main thread, so a module that is
     only written to the virtual filesystem is inert data and `dlopen()` fails.
- [x] 3. Ship `vendor/omni-bot/data/` (~5 MB of `global_scripts/`, `et/scripts/`,
     `et/nav/`) into the browser filesystem where `Common/FileSystem.cpp` mounts
     it. Packed into `omni-bot-data.zip` (1.4 MB) by `cmake/ETLOmniBotWasm.cmake`
     and unpacked by the shell next to the library - `Utils::GetBaseFolder()`
     derives the data directory from the path of the loaded library, so the two
     must share a directory. It is fetched only when a game is actually hosted in
     the browser, so a player joining a dedicated server never pays for it.
- [x] 4. Build `qagame` with `FEATURE_OMNIBOT` for wasm (was force-disabled in
     `cmake/ETLEmscripten.cmake`). `cmake/ETLOmniBotWasm.cmake` builds the
     vendored tree as an `ExternalProject` with the same toolchain and publishes
     the library and data pack next to the other side modules.
- [x] 5. Drop the `+set omnibot_enable 0` that the web launcher passes for "Host
     game" and "Quick single game" in `src/web/shell.html`. It now passes
     `omnibot_enable 1` and an absolute `omnibot_path` (the cvar default,
     `legacy/omni-bot`, is relative to the working directory, which is `/` in a
     browser), and seeds a bot count into `omni-bot.cfg` in `fs_homepath` so a
     hosted game is actually populated.

Still open:

- [ ] The **full engine** wasm build with `FEATURE_OMNIBOT=ON` has not been run
      here: the distribution's emcc 3.1.6 fails `cmake/ETLPlatform.cmake`'s
      `SUPPORT_ERROR_IMPLICIT_FUNCTION_DECLARATION` probe, which the pinned emsdk
      4.0.23 passes. The two `FEATURE_OMNIBOT` sources were compiled with `em++`
      individually instead; CI is the first place the whole thing is linked.
- [ ] That a wasm side module (`qagame`) can `dlopen()` another side module
      (`omnibot_et`) has not been *observed* at runtime: `-sMAIN_MODULE` aborts
      in `initRuntime` with emcc 3.1.6 on Node 24 even for a hello-world, so no
      dynamic-linking test can run here.

      What *was* checked, statically, by reading the module tables with
      `WebAssembly.Module.imports()/exports()`: `omnibot_et.wasm32.so` has 531
      function imports and every one of them is satisfiable. 342 are imported
      *and* exported by the bot module itself - normal PIC interposition, they
      resolve against the module's own exports - 180 come from the main
      module's wasm exports, and the last 9 (`__cxa_throw`,
      `__cxa_allocate_exception`, `abort`, `exit`, `clock`, `time`, `strftime`,
      `getTempRet0`, `setTempRet0`) are JS-library functions that `MAIN_MODULE=1`
      always includes, because it implies `INCLUDE_FULL_LIBRARY`. A C-only main
      module is enough: `MAIN_MODULE=1` force-links libc++/libc++abi, so it
      exports the 2752 C++ symbols the bot needs.

      A failed `dlopen` is not fatal in any case - `Bot_Interface_Init()` returns
      false, `Omnibot_LoadLibrary` prints the reason and the match runs without
      bots.

- [x] **`BotInitialise()` trapped with `RuntimeError: unreachable`.** Starting a
      quick single game aborted the frame right after `Omnibot_LoadLibrary`
      printed "Attempting to Initialize", inside `ScriptManager::Init()`.

      Cause: `gmbinder2.h` declares the primary `ToGmVar` template
      `__attribute__((noreturn))` *with an empty body* on non-Windows,
      non-Apple targets - a deliberate trick so that using an unspecialised type
      fails. gcc ignores it for the explicit specialisations that follow; clang
      propagates the attribute to them, so every `gmBind2::Global(...).var(...)`
      call - the first one being `MapGoal::Bind()` - becomes UB and is emitted as
      a bare `unreachable`. The Apple branch (also clang) already only *declares*
      the primary template, so the fix is to take that branch for `__clang__` as
      well, which keeps "instantiating the primary template is a link error".

      Verified by building `vendor/omni-bot` with `em++` and driving
      `BotInitialise()` from a standalone Node/wasm harness with a stub
      `IEngineInterface`: it trapped before, and after the change reports
      "Omni-bot 0.93 initialized" and returns `BOT_ERROR_NONE`.

- [ ] **The bot's `catch` blocks are compiled away.** Emscripten defaults to
      `DISABLE_EXCEPTION_CATCHING=1`, which passes `-fignore-exceptions`: `throw`
      still works, landing pads do not. Confirmed on the built artifact - it
      imports `__cxa_throw` and `__cxa_allocate_exception` but no
      `__cxa_begin_catch` or `invoke_*`. So the 25 `catch (const std::exception &)`
      handlers in `Common/` (`FileSystem::GetRealDir`, `Utils::RegexMatch`,
      `ScriptManager`, `gmSystemLibApp`, ...) no longer run; a `std::regex_error`
      or a bad `fs::path` escapes as a JS exception instead of being logged and
      swallowed. These are all defensive paths, so a normal match is unaffected,
      but a malformed script or filter kills the frame rather than printing a
      warning.

      The fix is *not* `-fexceptions` (JS exceptions): that makes the side module
      import `invoke_ii`, `invoke_viiii`, `__cxa_find_matching_catch_2/3`, ... and
      those wrappers are emitted per call signature into the *main* module's JS,
      which cannot know the signatures of a module it will `dlopen` later. They
      come out unresolved.

      It has to be native Wasm exception handling on **both** modules. Verified
      by building a representative side module both ways: with
      `-fwasm-exceptions` on the side module and on the main module there are
      **zero** unresolved imports - no `invoke_*` at all, the `env.__cpp_exception`
      tag is created by the main module's JS (`new WebAssembly.Tag(...)`, present
      even in a default `MAIN_MODULE=1` build) and the two remaining symbols,
      `_Unwind_CallPersonality` and the `__wasm_lpad_context` data symbol, come
      from libunwind, which only gets linked when the main module is built with
      `-fwasm-exceptions` too. Building only the bot that way leaves both
      unresolved and `dlopen` fails.

      Not done here because it cannot be validated: the engine wasm build does not
      run in this sandbox, and the change raises the browser floor - Wasm EH needs
      Chrome 95+, Safari 15.2+ and Firefox 131+, and a browser without it fails to
      compile the main module at all, which would break the page for everyone
      rather than just disabling bots. Worth doing once someone can build and load
      the result: add `-fwasm-exceptions` to the Emscripten compile *and* link
      flags in `cmake/ETLEmscripten.cmake`, pass
      `-DCMAKE_CXX_FLAGS=-fwasm-exceptions` through `OMNIBOT_WASM_CMAKE_ARGS` in
      `cmake/ETLOmniBotWasm.cmake`, and document the minimum browser versions in
      `docs/web.md`.

### 8. Host a game in the browser: settings, map list and peer-to-peer play

- [x] **`maplist.json` in the root of the release** (`maplist.json`, copied to
      `dist/etlegacy-web/` by `.github/workflows/emscripten.yml` and next to
      `etl.html` in the build directory by `cmake/ETLBuildMod.cmake`). Maps a
      map's bsp name to the pk3 it is downloaded from; an empty link marks a
      stock map. Both sides use it: the host installs the map before the server
      starts (and before a map change), a joining player installs the same pk3
      before connecting. Downloads go through the existing `downloadInto()`
      path, so they are cached in IndexedDB like the retail paks and only
      fetched once. Sanitised on load (bsp name charset, `http(s)` + `.pk3`
      only), with the stock maps as a fallback if the file cannot be read.
- [x] **Host settings in the launcher** (`src/web/shell.html`): room name, map
      (or *Random map*), max players 2-32, bots 0-31 (clamped to the free
      slots, both fields limit each other while editing), time limit in minutes
      (`0` = whatever the map script sets, via `g_userTimeLimit`) and a
      *private room* checkbox. They map onto `sv_hostname`, `sv_maxclients`,
      Omni-bot's `omni-bot.cfg` + `/bot minbots|maxbots`, `g_userTimeLimit` and
      the lobby's room record.
- [x] **In-game sidebar** (`#game-sidebar`): narrow column on the left of a
      running game with *leave* (two clicks; the host leaving closes the room,
      so every player is returned to the launcher), *settings* (a panel showing
      the current map and player count, all host settings and a map switch) and
      *invite link* (copied to the clipboard). Settings are applied to the
      running game through the existing `window.etlPendingCommands` bridge
      (`Sys_WebPumpConsoleCommands`), so no engine change was needed;
      `sv_maxclients` and the time limit are followed by a `map_restart`
      because they only take effect when a match starts.
- [x] **Lobby / signalling server** (`tools/p2p-lobby/`): a small Node service
      (one dependency, `ws`) that keeps the list of open games, hands out the
      ICE configuration and forwards offers/answers between players. HTTP
      `GET /rooms` and `/health` for monitoring, TLS or nginx in front of it,
      systemd unit in the README. Covered by `test-lobby.mjs` in CI.
- [x] **Browser transport** (`src/web/etl-p2p.js`): `RTCPeerConnection` +
      unreliable/unordered `RTCDataChannel`, exposed as `window.ETLP2P`. Covered
      by `test-p2p-client.mjs`, which runs the full handshake in Node against
      a real lobby instance.
- [x] **Engine side** (`src/qcommon/net_web.c`): a peer is mapped to a
      synthetic address (`241.0.<hi>.<lo>:27960`) that the engine treats like
      any other UDP address, so `Sys_SendPacket()` routes those to the data
      channel and `NET_Event()` pumps received packets into the normal packet
      queue. Nothing above the socket layer knows the difference between a
      relayed dedicated server and a peer.
- [x] **"Join games" button**: shows how many games are running (live, from the
      lobby) and lists them with name, map, player count and a *Join* button.
      An invite link (`?join=<room-id>`) joins directly and also works for a
      private room, which is not listed.
- [ ] **Play-test with two browsers on the published page.** Blocked by the same
      three external blockers as the rest of the plan (no retail paks here, no
      published deployment of this branch); the handshake itself is asserted
      end to end in Node, but the WebRTC path itself can only be exercised in a
      real browser.

---

## What is still open, and why

Everything that can be built, run and asserted without the retail game data is
done and covered by CI. The open boxes above all sit behind one of three
external blockers:

1. **The retail paks (`pak0-2.pk3`) are not redistributable.** Blocks every
   "does it actually load/render/play" item: `Sys_LoadGameDll` resolving
   `dllEntry`/`vmMain` at runtime, reaching the main menu, a map rendering, the
   browser end of the connect path, two browser clients on one server, and the
   whole "Definition of done". The automated boot smoke test deliberately stops
   where the missing paks would be loaded, and every layer *below* that is
   asserted structurally (`tools/web-smoke/verify-dist.mjs`) or end to end on
   the relay side (`tools/ws-relay/test-relay.mjs`).
2. **The pinned Emscripten SDK cannot be downloaded here** -
   `storage.googleapis.com` answers 403, so only the distribution's emcc 3.1.6
   is available, and that one fails `cmake/ETLPlatform.cmake`'s
   `SUPPORT_ERROR_IMPLICIT_FUNCTION_DECLARATION` probe. Blocks the full engine
   build with `FEATURE_OMNIBOT=ON`, the side-module-`dlopen`s-side-module
   runtime check, the `-fwasm-exceptions` switch (which must be validated in a
   real build before it is turned on, because a browser without Wasm EH would
   fail to load the page at all) and measuring the wasm data-section
   improvements. CI (`emscripten.yml`, emsdk 4.0.23) is the place these get
   exercised.
3. **No published deployment to open.** `deploy-pages` only runs on the default
   branch, so "confirm the published URL loads in a fresh browser" needs this
   branch merged first.

## Definition of done

- [ ] A user opens a published URL and reaches the ET:Legacy main menu.
- [ ] The user can enter a relay/server address and connect to a running
      dedicated server.
- [ ] A second player can join the same server from their own browser.
- [ ] Gameplay (movement, shooting, map load) works end-to-end.
