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
    ├── legacy_<ver>.pk3        # mod pk3 (ui .menu files + media)
    ├── cgame.mp.wasm32.so      # standalone side module (required)
    └── ui.mp.wasm32.so         # standalone side module (required)
```

Copy `pak0.pk3`, `pak1.pk3`, `pak2.pk3` from a retail Wolfenstein: Enemy
Territory install into `etmain/`. **These are not included and may not be
redistributed.**

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

Configure the relay and server from the page URL, or from the in-page
**Connect…** panel (bottom controls bar):

| Parameter | Purpose | Example |
|-----------|---------|---------|
| `assets`  | Base URL for `pak0-2.pk3` | `?assets=https://example.com/etmain/` |
| `legacy`  | Base URL for the mod pk3 | `?legacy=https://example.com/legacy/` |
| `mod`     | Override which mod pk3(s) to fetch | `?mod=legacy_2.84.0.pk3` |
| `relay`   | WebSocket relay URL (`net_wsRelayServer`) | `?relay=wss://relay.example.com:8443` |
| `connect` | Game server `host:port` to auto-join | `?connect=203.0.113.10:27960` |

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
