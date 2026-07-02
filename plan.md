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
      `cgame` + `ui` as `SIDE_MODULE` `.wasm` files with loader-matching names
      (`cgame.mp.wasm32.wasm`, `ui.mp.wasm32.wasm`) — see `cmake/ETLBuildMod.cmake`.
- [ ] **Verify with a real emcc build** that the `MAIN_MODULE`/`SIDE_MODULE`
      link succeeds and that `Sys_LoadGameDll` resolves `dllEntry`/`vmMain` from
      the side modules at runtime. (Not verifiable in this environment — no emcc.)
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
- [ ] Add TLS (`wss://`) guidance/config so the relay works from HTTPS pages
      (browsers block `ws://` from `https://`).
- [ ] End-to-end test: browser client -> relay -> dedicated server connect.
- [ ] Verify two browser clients can join the same server simultaneously.
- [ ] (Optional/perf) Investigate WebRTC data channels to reduce latency.

### 4. Hosting the "link"

- [ ] Add a deploy step to `emscripten.yml` (e.g. GitHub Pages / static host)
      that publishes `etl.html`, `.js`, `.wasm`, and any `.data`.
- [ ] Ensure the page is served with the headers required by wasm threads/large
      memory if needed (COOP/COEP) and correct MIME types.
- [ ] Confirm the published URL loads and runs in a fresh browser.

### 5. Shell / UX

- [x] Web shell drives asset download + relay/connect via URL parameters
      (`?assets=`, `?relay=`, `?connect=`) in `src/web/shell.html`.
- [ ] Add an in-page server/relay connect UI (form) instead of URL-only config.
- [ ] Handle browser constraints: user-gesture required for audio, pointer lock
      for mouse look, fullscreen toggle.

### 6. CI / verification

- [x] `emscripten.yml` builds the client mod (`-DBUILD_MOD=ON`) and its artifact
      glob (`build-wasm/*.wasm`) now captures the `SIDE_MODULE` `.wasm` files.
- [ ] Add a smoke test (e.g. headless browser) that boots the wasm client and
      confirms it reaches the main menu without fatal errors.
- [ ] Document the full local workflow (build, run relay, run dedicated server,
      open page) in `tools/ws-relay/README.md` or a new `docs/web.md`.

---

## Definition of done

- [ ] A user opens a published URL and reaches the ET:Legacy main menu.
- [ ] The user can enter a relay/server address and connect to a running
      dedicated server.
- [ ] A second player can join the same server from their own browser.
- [ ] Gameplay (movement, shooting, map load) works end-to-end.
