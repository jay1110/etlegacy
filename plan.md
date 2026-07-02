# Plan: Playable ET:Legacy in the Web Browser (Emscripten/WASM)

This plan tracks everything still required so that a user can open a link, run
ET:Legacy in a web browser, and play multiplayer (connecting to a server that
other players can also join).

## Current state (what already works)

Running `.github/workflows/emscripten.yml` today builds a WebAssembly **client
shell** only. It produces `etl.html` + `.js` + `.wasm`, but the result is **not
yet playable**:

- Client engine compiles/links for wasm (renderer OpenGL1 via gl4es, SDL2 port,
  WebGL). See `cmake/ETLEmscripten.cmake`.
- WebSocket networking layer exists (`src/qcommon/net_web.c`) and is wired into
  the build in place of `net_ip.c` (`cmake/ETLSources.cmake`).
- A WebSocket-to-UDP relay exists as a tool (`tools/ws-relay/relay.js`).
- An HTML shell exists (`src/web/shell.html`).

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

- [ ] Decide on the wasm module strategy: Emscripten dynamic linking
      (`MAIN_MODULE` for the engine + `SIDE_MODULE` for each game lib, loaded via
      `dlopen`). Confirm the duplicate-symbol constraints noted in prior work
      (llvm-objcopy/wasm-ld cannot localize symbols) are handled by
      `SIDE_MODULE` isolation.
- [ ] Add `MAIN_MODULE`/`SIDE_MODULE` (and required `EXPORTED_FUNCTIONS` /
      `dlopen` support flags) to `cmake/ETLEmscripten.cmake` /
      `cmake/ETLSetupFeatures.cmake`.
- [ ] Enable `BUILD_MOD=ON` for the wasm build and make `cgame_mp`, `ui_mp`, and
      `qagame_mp` compile and link as `SIDE_MODULE` `.wasm` files.
- [ ] Verify `Sys_LoadGameDll`/`Sys_LoadDll` resolve `dllEntry`/`vmMain` from the
      wasm side modules at runtime.
- [ ] Confirm the client reaches the main menu (ui module loads).

### 2. Package and mount game assets

- [ ] Choose an asset-delivery method: `--preload-file`, lazy `emscripten_fetch`
      (FETCH is already enabled), or IDBFS + runtime download.
- [ ] Mount assets at the paths the engine expects
      (`Sys_SetDefaultInstallPath("/etlegacy")` in `src/sys/sys_main.c`).
- [ ] Bundle the freely-distributable ET:Legacy paks (`legacy/*.pk3`). Do **not**
      commit or redistribute the original retail `pak0.pk3`/`pak1.pk3`/`pak2.pk3`
      (copyrighted) — document that users must supply their own `etmain` data.
- [ ] Provide a documented mechanism for the user to load their retail `etmain`
      data into the browser FS (upload or fetch from a URL they control).
- [ ] Verify a map loads and renders in the browser.

### 3. Server + relay so other players can join

- [ ] Document clearly that hosting happens **outside** the browser: a native
      (or containerized) `etlded` dedicated server is the actual game host.
- [ ] Package/document running the WebSocket-to-UDP relay
      (`tools/ws-relay`) next to the dedicated server, including a
      `package.json`/lockfile so `npm install` works reproducibly.
- [ ] Add TLS (`wss://`) guidance/config so the relay works from HTTPS-hosted
      pages (browsers block `ws://` from `https://`).
- [ ] Expose a `net_wsRelayServer` cvar UI/entry in `src/web/shell.html` (or the
      in-game console) so users can point the client at a relay.
- [ ] End-to-end test: browser client -> relay -> dedicated server connect.
- [ ] Verify two browser clients can join the same server simultaneously.
- [ ] (Optional/perf) Investigate WebRTC data channels to reduce TCP/WebSocket
      latency (noted as future work in `tools/ws-relay/README.md`).

### 4. Hosting the "link"

- [ ] Add a deploy step to `emscripten.yml` (e.g. GitHub Pages / static host)
      that publishes `etl.html`, `.js`, `.wasm`, and any `.data`.
- [ ] Ensure the page is served with the headers required by wasm threads/large
      memory if needed (COOP/COEP) and correct MIME types.
- [ ] Confirm the published URL loads and runs in a fresh browser.

### 5. Shell / UX

- [ ] Update `src/web/shell.html` with a server/relay connect UI and basic
      instructions (controls, pointer-lock, audio-enable gesture).
- [ ] Handle browser constraints: user-gesture required for audio, pointer lock
      for mouse look, fullscreen toggle.

### 6. CI / verification

- [ ] Update `emscripten.yml` so the artifact bundle includes the game
      `SIDE_MODULE` `.wasm` files and any packaged `.data`.
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
