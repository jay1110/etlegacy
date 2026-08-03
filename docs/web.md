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
verified to work; see `.github/workflows/emscripten.yml`, currently `4.0.23`)
and the Boost headers (`libboost-dev`) for Omni-bot — header-only, so no
cross-compiled Boost is needed.

```bash
emcmake cmake -B build-wasm \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_CLIENT=ON -DBUILD_SERVER=OFF \
  -DBUILD_MOD=ON -DBUILD_CLIENT_MOD=ON -DBUILD_SERVER_MOD=ON \
  -DFEATURE_RENDERER1=ON -DFEATURE_RENDERER2=OFF -DFEATURE_GL4ES=ON \
  -DFEATURE_OMNIBOT=ON
cmake --build build-wasm --parallel "$(nproc)"
```

This produces `etl.html`, `etl.js`, `etl.wasm`, the side modules
`cgame.mp.wasm32.so` / `ui.mp.wasm32.so` / `qagame.mp.wasm32.so`, and — with
`FEATURE_OMNIBOT` — `omnibot_et.wasm32.so` plus `omni-bot-data.zip`, all in
`build-wasm/`. The exact CMake flags CI uses are in
`.github/workflows/emscripten.yml` — copy them for an exact match.

## 2. Lay out the web directory

Serve a directory with this layout (the CI "Package web release" step builds
exactly this):

```
etlegacy-web/
├── etl.html            # game page
├── index.html          # copy of etl.html
├── etl.js
├── etl.wasm
├── etl.data            # preloaded virtual-filesystem image (browser default config)
├── etl-p2p.js          # lobby client + WebRTC transport (browser-hosted games)
├── maplist.json        # maps offered when hosting, + download link per map
├── etmain/             # put pak0.pk3, pak1.pk3, pak2.pk3 here
└── legacy/
    ├── legacy_<ver>.pk3        # mod pk3 (cgame/ui game logic + ui menus + media)
    ├── cgame.mp.wasm32.so      # standalone side module (fallback)
    ├── ui.mp.wasm32.so         # standalone side module (fallback)
    ├── qagame.mp.wasm32.so     # standalone side module (fallback)
    └── omni-bot/
        ├── omnibot_et.wasm32.so   # bot library (loaded on demand)
        └── omni-bot-data.zip      # bot scripts + navigation meshes
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

`legacy/omni-bot/` holds the bot library and its data. Unlike the game logic it
is fetched **on demand**, the first time a game is hosted in the browser
("Quick single game" / "Host game"), so a player who only joins a dedicated
server never downloads it. The library is compiled up front like the other side
modules; the data pack is cached in IndexedDB and unpacked next to it, because
Omni-bot resolves its data directory from the location of the loaded library.
If either cannot be fetched the game still starts, just without bots.

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

> **Serve the `.wasm` files compressed.** They are big on disk - the engine and
> each side module carry a wasm *data* section of tens of megabytes, because a
> wasm side module has no BSS and zero-initialised statics are emitted as literal
> zero bytes. Those bytes compress away almost entirely (measured on a test
> module: 32 MB → 30 KB with `gzip -9`), so any server that sends
> `Content-Encoding: gzip` or `br` makes the download small. `python3 -m
> http.server` does **not** compress and will transfer the full size; use it for
> a quick local check only.

> **Upload the `.pk3` and `.so` files in binary mode.** They are binary
> WebAssembly data. If they are transferred over FTP/SFTP in *ASCII*/*text*
> mode (or rewritten by a server content filter), their bytes get mangled and
> the browser aborts at startup with **`need to see wasm magic number`** /
> `VM_Create on ui failed`. The shell now detects this and reports the real
> cause (e.g. *"most likely corrupted in upload (FTP/SFTP in ASCII/text mode)"*
> or *"the server returned an HTML page (HTTP 200), not the binary"*). If you
> see it: re-upload `legacy/*.pk3` and `legacy/*.so` (and `etmain/*.pk3`) in
> **binary** mode and confirm each URL returns the raw file with HTTP 200.

### Start page layout

The start page ships with three layouts. Which one is used is decided by the
operator of the page, not by the players: open `etl.html` (or `index.html`) in
an editor and set the value near the top of `<body>`:

```html
<script>window.ETL_HOME_LAYOUT = 'classic';</script>
```

| Value | Layout |
|-------|--------|
| `classic` | The familiar single column of wide buttons with descriptions |
| `cards` | A two-column grid of tiles with icons (one column on narrow screens) |
| `hero` | A large title over a compact, borderless button column with icons |

Anything else falls back to `classic`. For a quick look at the alternatives
before editing the file, `?home=cards` / `?home=hero` switches the layout for
one page load.

### Hosting on GitHub Pages

The `emscripten.yml` workflow publishes the web client to GitHub Pages on pushes
to the default branch (enable Pages with "GitHub Actions" as the source under
Settings → Pages). The retail paks are **not** deployed, so either host an
`etmain/` folder next to the page yourself or point the client at another host
with `?assets=` (that host must allow CORS).

## 4. Run a dedicated server

A browser cannot open a listening socket, so it cannot be reached by native
clients. It *can* host a game for other browser players over WebRTC data
channels (see [section 7](#7-host-games-in-the-browser-lobby--webrtc)); for
native clients, run a normal native ET: Legacy dedicated server on a machine
with a public UDP port (default `27960`):

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

Targets may be hostnames (`?connect=etclan.de:27966`): the browser cannot
resolve names, so the engine passes the name to the relay, which resolves it.
The shell also has a **default relay** built in (`DEFAULT_RELAY_HOST` in
`src/web/shell.html`, `ws://`/`wss://` chosen to match the page), so a share
link only needs `?connect=`.

The relay keeps running when a single connection fails (all connection errors
are logged, never fatal) and drops dead peers via a WebSocket heartbeat. Idle
timeout and connection limit are tunable with `--timeout <secs>` and
`--max-connections <n>`. For unattended hosting still run it under a process
manager (systemd with `Restart=always`, or pm2).

## 6. Open the game and connect

On first load the page asks how to provide the game data: **download
pak0.pk3** (fetched together with pak1/pak2 and cached in the browser) or
**use a local pak0.pk3** picked from your own installation. Once the data is
set, a **Run game** menu offers: starting the game to the main menu without
connecting anywhere, joining the preconfigured ETc server (a
different `fs_game`, xmod — missing pk3s are downloaded from the server),
a quick single game (`+map oasis`), a manually maintained server list
(`SERVER_LIST` in `src/web/shell.html`), **hosting a game** in the browser and
**joining a game** somebody else hosts (both described in section 7).

Locally hosted games fill the server with Omni-bot bots. The bot count is the
one chosen in the launcher; it is written to `omni-bot.cfg` in `fs_homepath`
(persisted in the browser like the rest of the home directory) and can be
changed at any time in the game's settings panel or in-game with
`/bot minbots <n>` and `/bot maxbots <n>`.

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
| `lobby`   | Lobby server for browser-hosted games | `?lobby=wss://lobby.example.com:8443` |
| `join`    | Join a browser-hosted game (invite link) | `?join=7f3a91` |
| `maplist` | Use another map list | `?maplist=https://example.com/maplist.json` |

Full example:

```
https://your-page/etl.html?relay=wss://relay.example.com:8443&connect=203.0.113.10:27960
```

Multiple browser players can open the same link and join the same server; each
gets its own UDP socket on the relay side.

### Mouse capture (desktop)

The game only captures the mouse when you ask for it: click into the game area
(or use **Capture Mouse** in the controls bar). Simply moving the pointer over
the page never captures it, and `Esc` gives the pointer back and keeps it back
until you click into the game again — so switching to another window or tab is
always possible.

### Phones and tablets

Touch devices are detected automatically (force it either way with `?touch=1`
or `?touch=0`). There, the bottom controls bar is replaced by icons in the slim
left sidebar — **⛶** fullscreen, **⇄** connect, **›_** console and **◎** touch
controls — and the game area starts next to that sidebar instead of below it,
so nothing covers the picture.

The touch controls are laid out on a fixed grid that keeps every button clear of
its neighbours: a movement stick in the bottom left corner (with **RUN** above
it), the fire button and the action cluster (jump, duck, prone, reload, weapon
switch, use) in the bottom right corner, and chat/scores/menu as small buttons
in the top left corner. Anywhere else on the screen is the look area: drag to
aim, and the on-screen keyboard button (**⌨**, top right) types into the game
(console, chat, name entry).

## 7. Host games in the browser (lobby / WebRTC)

**Host game** in the launcher starts a listen server inside the browser and
announces it on a lobby server, so other players find it under **Join games**
(the button shows how many games are running) or through the invite link the
host can share. Players connect directly to the host's browser with a WebRTC
data channel — the lobby only introduces them to each other, no game traffic
passes through it.

### What the host can set

| Setting | Meaning |
|---------|---------|
| Room name | Name shown in the game list and as `sv_hostname` |
| Map rotation | Up to 10 maps from `maplist.json` (or **Random map**), played one after another; a single map simply restarts |
| Max players | 2 – 32 (`sv_maxclients`) |
| Bots | 0 – 31, never more than the free slots (Omni-bot) |
| Time limit | Minutes; `0` keeps the time the map itself sets (`g_userTimeLimit`) |
| Private room | The game is not listed; it can only be joined with the invite link |

The rotation is programmed into the server as a chain of `nextmap` cvars, which
ET runs when a match ends, and it wraps around to the first map after the last
one. It can be changed while the game runs (**⚙** in the sidebar), and it is
announced in the lobby together with the rest of the room.

Once the game runs, a narrow column on the left has an **✕** button (leave the
game — if the host leaves, everybody is returned to the launcher), a **⚙**
button (current map, player count and the map rotation; every setting above can
be changed and any map of the rotation can be played immediately while the game
runs) and a **🔗** button that copies the invite link.

**A hosted game only lives as long as its tab is in the foreground.** Browsers
throttle background tabs, which stops the server from stepping and disconnects
everybody who joined, so the host is warned about this in the host form and
again with a banner over the game — switching tabs, switching windows or
minimizing the browser has to wait until the game is over.

### maplist.json

`maplist.json` sits in the root of the web directory, next to `etl.html`, and
decides which maps can be hosted:

```json
{
    "oasis": "",
    "etl_supply": "https://et.clan-etc.de/etmain/etl_supply_v14.pk3"
}
```

The key is the map's bsp name, the value is the download link of the pk3 the
map ships in. An **empty** link marks a stock map that needs no download.
Everything else is downloaded — by the host when the game starts (or when the
map is switched) and by every player who joins that game — into the browser's
`etmain` and cached in IndexedDB, so it is fetched only once. The file the URL
points at must be a `.pk3` and the server hosting it must allow CORS
(`Access-Control-Allow-Origin`), otherwise the browser cannot read it. Use
`?maplist=<url>` to point the page at a different list.

### Run the lobby server

The lobby is a small Node service (one dependency, `ws`) that keeps the list of
open games and forwards the WebRTC offers/answers between the players. See
`tools/p2p-lobby/README.md`. On a plain Ubuntu root server:

```bash
sudo apt install -y nodejs npm
cd tools/p2p-lobby
npm install
npm start                       # plain ws:// on :8081
# or, for an HTTPS page, serve wss:// directly:
node lobby.js --tls-cert /etc/letsencrypt/live/example.com/fullchain.pem \
              --tls-key  /etc/letsencrypt/live/example.com/privkey.pem \
              --port 8443
```

A page served over `https://` (e.g. GitHub Pages) may only open `wss://`
sockets, so terminate TLS in the lobby (above) or put it behind nginx, exactly
like the relay. `tools/p2p-lobby/README.md` contains a ready-made systemd unit
and an nginx location block. The shell has a default lobby built in
(`DEFAULT_LOBBY_HOST` in `src/web/shell.html`); `?lobby=<ws-url>` overrides it.

Open ports: **8081/tcp** (or 8443/tcp with TLS) for the lobby, plus the UDP
range WebRTC uses if the host runs a TURN server (below).

### When a direct connection is not possible (TURN)

Most players connect directly once the lobby has introduced them (the lobby
hands out a public STUN server for that). Behind a symmetric NAT or a strict
firewall this fails, and a relay is needed — that is what TURN is. On the same
Ubuntu machine:

```bash
sudo apt install -y coturn
# /etc/turnserver.conf
#   listening-port=3478
#   fingerprint
#   lt-cred-mech
#   user=etl:<password>
#   realm=example.com
sudo systemctl enable --now coturn
```

Then start the lobby with the TURN server, which it passes on to the players:

```bash
node lobby.js --ice stun:stun.l.google.com:19302 \
              --ice turn:example.com:3478 \
              --turn-user etl --turn-pass <password>
```

Open **3478/tcp+udp** and coturn's relay range (`min-port`/`max-port`,
49152–65535 by default).

### Why not HumbleNet?

HumbleNet solves the same problem, but it is a C++ library that has to be built
into the engine *and* needs its own peer server; its Emscripten socket
emulation also expects to own the whole socket layer, which collides with the
WebSocket relay this port already uses for dedicated servers. The transport
here is plain JavaScript (`src/web/etl-p2p.js`, `RTCPeerConnection` +
`RTCDataChannel`) behind the same tiny interface the relay uses in
`src/qcommon/net_web.c`: a peer becomes a synthetic address (`241.0.x.y:27960`)
the engine treats like any other, so no engine subsystem had to change. It is
also testable without a browser — `tools/p2p-lobby/test-p2p-client.mjs` runs the
whole handshake in Node.

## Verification / smoke tests

`tools/web-smoke/` contains two smoke tests (run in CI after the build):

- `verify-dist.mjs <dir>` — structural check of the packaged build (engine files
  present, valid wasm header, mod pk3 contains the side modules, Omni-bot module
  and data pack shipped). No browser needed.
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

The relay has its own end-to-end test, which needs neither the build nor the
game data and runs as a separate CI job:

```bash
npm --prefix tools/ws-relay install
node tools/ws-relay/test-relay.mjs
```

It drives a real WebSocket client through `relay.js` to a stand-in UDP game
server that answers ET's out-of-band `getinfo` query, covering both URL forms,
hostname targets, packets sent before the UDP socket is bound, two clients on
the same server at once (own UDP source port each, no cross-talk between them),
datagrams from a foreign source, malformed targets, the connection limit, the
idle-connection reaper and a failed bind.

## Known limitations

- Latency is higher than native UDP (the relay uses TCP/WebSocket; Nagle is
  disabled on the relay side, but TCP head-of-line blocking remains).
- The retail paks must be supplied by the user; they are never redistributed.
- A browser-hosted game reaches other **browser** players only (WebRTC data
  channels, section 7). Native clients cannot join it, because a browser has no
  listening UDP socket — use a native dedicated server for that.
- A browser-hosted game depends on the host's browser tab: closing it ends the
  game for everybody (the players are returned to the launcher).
- `vid_restart` (and the settings menu's "apply" that issues it) is disabled in
  the browser: a canvas WebGL context and gl4es cannot be torn down and
  re-created within the same page (the Wwasm reference port suppresses it the
  same way). Latched video cvar changes take effect on the next page reload.
  `etl.data` preloads `com_recommendedSet 1` so the first-run "apply
  recommended settings + vid_restart" path is never taken.
- The cgame/ui/qagame side modules are never unloaded: Emscripten's `dlclose()`
  is a no-op, so a later `dlopen()` of the same path hands back the instance that
  is already loaded. A VM restart - which happens on every map change - therefore
  reuses the module *with all of its globals still set*, unlike native platforms
  where the library is genuinely reloaded. Mod code that keeps "already
  initialised" flags in module globals while the matching state lives in
  `cgs`/`cg` (wiped by `CG_Init()`) has to cope with that; see the loading screen
  font restore in `CG_DrawConnectScreen()`.
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
- **Black screen right after the menu should appear (first visit only, or after
  clearing site data)** — on a first run (`com_recommendedSet 0`) `Com_Init`
  used to queue `exec preset_high.cfg` + `vid_restart`; the restart tore down
  and re-created the WebGL context and re-initialised gl4es, neither of which
  the browser supports, leaving only the clear color. Fixed three ways: the
  browser build no longer queues that first-run `vid_restart`
  (`src/qcommon/common.c`), `vid_restart` itself is suppressed on the web
  (`src/client/cl_main.c`, like the Wwasm reference port), and `etl.data`
  preloads an `autoexec.cfg` with `com_recommendedSet 1`
  (`src/web/fs/legacy/autoexec.cfg`).
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
- **`memory access out of bounds` or `indirect call to null` (at `doRewind`,
  `__synccall`, `silence_callback`, or the
  `SDL2.audio.scriptProcessorNode.onaudioprocess` handler)** — this whole crash
  family came from **Asyncify** and no longer occurs: the web build is
  deliberately **not** linked with `-sASYNCIFY` (see the note in
  `cmake/ETLEmscripten.cmake`, and `-sSIDE_MODULE=1` without Asyncify in
  `cmake/ETLBuildMod.cmake`). Asyncify unwinds and rewinds the entire wasm call
  stack, and any JS callback that re-entered the module while a rewind was
  pending corrupted the saved state and trapped; once the Asyncify state was
  poisoned, *every* later re-entry — most visibly SDL2's audio callback, which
  fires on the browser event loop independently of the frame — trapped the same
  way forever. Removing Asyncify removes the rewind state entirely, so the audio
  callback re-entering wasm is now harmless, exactly how the other q3 wasm ports
  (jdarpinian/ioq3, Qwasm2/Wwasm) run. The engine never needed Asyncify:
  - it drives the browser main loop itself, non-blocking, via `setMainLoop()`
    (`Sys_GameLoop` in `src/sys/sys_main.c`) instead of a blocking loop that
    yields with `emscripten_sleep`;
  - it downloads asynchronously through `emscripten_fetch` callbacks
    (`src/qcommon/dl_main_web.c`), never a synchronous fetch;
  - it loads the cgame/ui/qagame side modules from the shell's `preloadedWasm`
    cache, so `dlopen()` only has to *instantiate* an already-compiled module —
    synchronous on the main thread, needing no async unwind (`preloadSideModule`
    in `src/web/shell.html`, `Sys_PreloadGameDlls` in `src/sys/sys_web.c`).

  A `memory access out of bounds` now only means a genuine resource-limit or
  module-mismatch problem (not an Asyncify rewind). Check:
  1. **Stale cgame/ui/qagame side modules from an old build.** The mod pk3 and
     the standalone `.so` files are cached in IndexedDB and the browser HTTP
     cache, so a site update can leave old game modules paired with a new
     engine; a mismatched VM syscall ABI then traps (e.g. `table index is out of
     bounds`). The shell (a) revalidates the mod pk3/`.so` against the server on
     every load and (b) deletes cached `legacy_*.pk3` from other builds. If it
     persists, redeploy matching `etl.wasm`/`etl.js`/pk3/`.so` artifacts from one
     build and clear the site data.
  2. **Native stack overflow** — the build sets a generous `-s STACK_SIZE`
     (8 MiB) in `cmake/ETLEmscripten.cmake`; the engine's deep call stacks
     overflow the 64 KiB Emscripten default.
  3. **The heap hitting its growth cap** — the build starts at 2 GiB (`-s
     INITIAL_MEMORY`) and raises the cap to the wasm32 maximum (`-s
     MAXIMUM_MEMORY=4gb`, growing on demand); large maps plus downloaded pk3s
     overflow the 2 GiB Emscripten default cap.
- **`Bad cgame system trap: 24` (or another unimplemented trap) right after
  connecting to a third-party mod server (xmod, …)** — trap 24 is **not** a
  cgame trap in any ET engine (`CG_CM_LOADMODEL` is an unused enum slot that
  neither 2.60b nor ET: Legacy implements); it is `UI_R_REGISTERSHADERNOMIP`,
  a **ui** trap number. It arrives in the cgame dispatcher because the mod's
  wasm side modules were built **without `-fvisibility=hidden`**: `cgame` and
  `ui` define hundreds of identically named symbols (`trap_*`), and Emscripten
  resolves address-taken ones through a single global name-keyed GOT, so the
  module loaded first wins. The ui module is loaded first, so the cgame's
  `cgDC.registerShaderNoMip = &trap_R_RegisterShaderNoMip` (`cg_main.c`) binds
  to **ui's** copy; the connect/loading screen (`DC->registerShaderNoMip` in
  `cg_loadpanel.c`) then sends UI trap 24 while the cgame VM is current. This
  is the same bug that used to break ET: Legacy's own modules ("table index is
  out of bounds" during `UI_Init`) and is why `etl_configure_wasm_side_module`
  in `cmake/ETLBuildMod.cmake` compiles them with `-fvisibility=hidden`.
  **The fix belongs in the mod's own build, not in the engine.** A mod that
  ships wasm game logic for this client must:
  1. compile `cgame`/`ui` with `-fvisibility=hidden` and link them with
     `-sSIDE_MODULE=1`, leaving only `vmMain`/`dllEntry` (`Q_EXPORT`) exported;
  2. use the array-based syscall ABI — under `__EMSCRIPTEN__` `dllEntry`
     receives `intptr_t (*)(intptr_t *args)` and each trap passes one argument
     array (see `src/cgame/cg_syscalls.c` and `SystemCall_*` in
     `src/qcommon/q_shared.h`); wasm `call_indirect` requires an exact
     signature match, so the variadic ABI cannot be used;
  3. name the modules `cgame.mp.wasm32.so` / `ui.mp.wasm32.so` (`ARCH_STRING` +
     `DLL_EXT`, see `src/qcommon/q_platform.h`), and make them reachable in the
     mod's own game directory — every module must be run through Emscripten's
     wasm preload plugin before the engine `dlopen()`s it (see the next entry;
     the shipped shell only preloads the `legacy` game directory).
- **`VM_Create on cgame/ui/qagame failed` / `dlopen` errors** — a side module
  was not compiled into `preloadedWasm` before the engine `dlopen()`ed it.
  Without Asyncify the browser cannot compile a wasm module synchronously on the
  main thread, so each side module must first be run through Emscripten's wasm
  preload plugin (`FS.createPreloadedFile`, see `createPreloadedSideModule` in
  `src/web/shell.html`); `dlopen()` then instantiates the cached module
  synchronously. The shell extracts the modules from the mod pk3 (falling back to
  the standalone `.so`) and preloads them into every directory the engine may
  load them from before `main()` runs.

