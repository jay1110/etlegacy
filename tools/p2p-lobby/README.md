# ET: Legacy peer-to-peer lobby, signalling & fallback-relay server

A small Node.js server that lets a player **host a match inside the browser** (a
listen server) and other players **join that browser host over WebRTC data
channels**. It is the browser-hosting counterpart to the WebSocket-to-UDP relay
in [`tools/ws-relay`](../ws-relay/README.md): that one lets a browser join a
*native* dedicated server; this one lets browsers play with **each other** when
one of them is the host.

> For the full web workflow (build the client, supply game data, run servers,
> open the page) see [`docs/web.md`](../../docs/web.md).

## Why is this needed?

Browsers can talk to each other directly with **WebRTC data channels** (which
behave like UDP: unordered, unreliable — exactly what a game wants), but two
browsers cannot *find* each other or exchange the connection setup (SDP offers,
ICE candidates) on their own. They need a rendezvous point. This server is that
point, and it does three jobs on one port:

1. **Lobby** — keeps the list of open (public) rooms and hands it out, so a
   player can see who is hosting and click to join.
2. **Signalling** — forwards the WebRTC `offer`/`answer`/`candidate` messages
   between a host and each joining peer so they can open a direct data channel.
3. **Fallback relay** — when a direct WebRTC channel cannot be established (a
   symmetric/carrier-grade NAT with no TURN server, or a client with no WebRTC
   at all), game packets are relayed as binary WebSocket frames instead, so a
   match is never completely unreachable.

No game data, no IP addresses of players, and no match traffic are stored: the
server only ever holds the small public room list in memory.

## Installation

```bash
cd tools/p2p-lobby
npm install          # installs the single dependency, ws
```

Node.js **18 or newer** is required.

## Usage

```bash
# Default: plain ws:// + http:// on port 8081
npm start
# or
node lobby.js

# Custom port and limits
node lobby.js --port 8081 --max-connections 512 --max-rooms 128

# Advertise a TURN server (see "STUN / TURN" below)
node lobby.js --ice stun:stun.l.google.com:19302,turn:turn.example.com:3478 \
              --turn-user etl --turn-pass s3cret

# Terminate TLS directly (required from HTTPS pages, see below)
node lobby.js --tls-cert /etc/letsencrypt/live/EXAMPLE/fullchain.pem \
              --tls-key  /etc/letsencrypt/live/EXAMPLE/privkey.pem --port 8443
```

### Options

| Option | Default | Description |
|--------|---------|-------------|
| `--port <port>` | `8081` | Listen port (WebSocket **and** HTTP share it) |
| `--host <host>` | `0.0.0.0` | Listen address |
| `--tls-cert <file>` | _(none)_ | TLS certificate (PEM); enables `wss://`/`https://` |
| `--tls-key <file>` | _(none)_ | TLS private key (PEM); enables `wss://`/`https://` |
| `--max-connections <n>` | `512` | Maximum simultaneous connections (excess closed with code 1013) |
| `--max-rooms <n>` | `128` | Maximum hosted rooms (excess `host` gets `toomanyrooms`) |
| `--ice <list>` | `stun:stun.l.google.com:19302` | Comma-separated `stun:`/`turn:` URLs advertised to every client |
| `--turn-user <user>` | _(none)_ | Username applied to `turn:` URLs in `--ice` |
| `--turn-pass <pass>` | _(none)_ | Credential applied to `turn:` URLs in `--ice` |
| `--verbose` | off | Log every control message |
| `--help` | | Show help and exit |

Each option also has an environment-variable fallback: `ETL_LOBBY_PORT`,
`ETL_LOBBY_HOST`, `ETL_LOBBY_TLS_CERT`, `ETL_LOBBY_TLS_KEY`,
`ETL_LOBBY_MAX_CONNECTIONS`, `ETL_LOBBY_MAX_ROOMS`, `ETL_LOBBY_ICE`,
`ETL_LOBBY_TURN_USER`, `ETL_LOBBY_TURN_PASS`. Command-line flags win.

Provide **both** `--tls-cert` and `--tls-key` to accept secure `wss://`.
Otherwise the server serves plain `ws://` and `http://`.

## HTTP endpoints

The same port answers a couple of plain HTTP GETs, so a page can show a host
count without opening a WebSocket:

| Request | Response |
|---------|----------|
| `GET /rooms` | `{"count":N,"rooms":[…]}`, `Content-Type: application/json`, `Access-Control-Allow-Origin: *` |
| `GET /health` | `ok` (200) — use for load-balancer / systemd health checks |
| anything else | `404` |

## Pointing the browser at it

The web shell (`src/web/shell.html`) loads `src/web/etl-p2p.js`, which is
configured with the lobby URL and then does everything else:

```js
ETLP2P.configure({
    lobbyUrl:   'wss://lobby.example.com/',        // this server
    playerName: 'Neo',
    inviteBase: location.origin + location.pathname // used to build invite links
});
```

The shell exposes it through the page URL, e.g. `?lobby=wss://lobby.example.com`
to override the built-in default, and `?join=<id>` on an invite link to join a
specific host. A joining player is handed a synthetic address
(`241.0.0.1:27960`) that the engine (`src/qcommon/net_web.c`) connects to; the
`241.0.0.0/8` block is reserved, so it can never collide with a real server.

## Wire protocol reference

WebSocket, default port **8081**, default path `/`. **Text** frames are JSON
control messages; **binary** frames are game packets (the fallback relay path).

### Client → server (JSON)

| Message | Reply / effect |
|---------|----------------|
| `{"t":"hello","name":"<name>","version":1}` | `{"t":"welcome","peer":<uint32>,"ice":[<RTCIceServer>…],"rooms":<public count>}`. `peer` is this connection's unique id. |
| `{"t":"host","room":{name,map,maxPlayers,bots,timeLimit,private}}` | `{"t":"hosted","roomId":"<6-8 chars>","room":{…normalised…}}`. One room per connection; re-hosting replaces the old room. |
| `{"t":"update","room":{<subset of fields, plus players>}}` | `{"t":"updated","room":{…}}` and a coalesced push to subscribers. |
| `{"t":"unhost"}` | `{"t":"unhosted"}`; the room disappears. Disconnecting does the same. |
| `{"t":"list"}` | `{"t":"rooms","rooms":[…],"count":<n>}` (public rooms only). |
| `{"t":"subscribe"}` / `{"t":"unsubscribe"}` | After subscribing, `{"t":"rooms",…}` is pushed whenever the public list changes (add/remove/update), coalesced to at most ~1 push per 250 ms. Subscribing also sends an immediate snapshot. |
| `{"t":"join","roomId":"<id>"}` | Joiner gets `{"t":"joined","roomId","host":<host peer>,"room":{…}}`; the host gets `{"t":"peer","peer":<joiner>,"name":"<name>"}`. Errors: `noroom`, `full`, `self`, `badrequest`. Private rooms are joinable by id but never listed. |
| `{"t":"signal","to":<peer>,"data":<any JSON>}` | Forwarded verbatim to that peer as `{"t":"signal","from":<sender>,"data":<…>}`. Only between peers with an active join relationship, else `{"t":"error","code":"nopeer"}`. |
| `{"t":"bye","to":<peer>}` | The target receives `{"t":"peerleft","peer":<sender>}`. |
| `{"t":"ping"}` | `{"t":"pong"}` |

### Server → client pushes

`rooms`, `signal`, `peer`, `peerleft`, `roomclosed`
(`{"t":"roomclosed","roomId":"…"}` sent to everyone joined to a room whose host
went away), and `error` (`{"t":"error","code":"…","message":"…"}`).

### Binary frames — the data fallback

Used when WebRTC cannot be established (or is unavailable, e.g. in Node):

- **client → server:** bytes `0..3` = destination peer id (uint32 little-endian),
  bytes `4..` = payload.
- **server → client:** bytes `0..3` = **source** peer id (uint32 LE),
  bytes `4..` = payload.

Frames are forwarded only between peers with an active join relationship;
anything else is dropped silently. Payload limit **16384 bytes** (`MAX_MSGLEN`).

### Room record (as published in `rooms` / `GET /rooms`)

```json
{ "roomId", "name", "map", "players", "maxPlayers", "bots",
  "timeLimit", "private": false, "created" }
```

Nothing else is ever leaked — in particular **no IP addresses**. Every field is
validated and clamped server-side: `name`/`map` trimmed and stripped of control
characters (`name` ≤ 64, `map` ≤ 64 and restricted to `[A-Za-z0-9_-]`),
`maxPlayers` 2–32, `bots` 0–31 and never more than `maxPlayers-1`, `timeLimit`
0–1440, `players` 0–`maxPlayers`, `private` coerced to a boolean.

## Deploying on an Ubuntu dedicated root

This is meant to run next to the existing `ws-relay` on the same host. All you
need is Node.js and a way to reach it over TLS from the (HTTPS) game page.

### 1. Install Node.js and the server

```bash
# Node.js 18+ (NodeSource, adjust the major version as you like)
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt-get install -y nodejs

# Put the server somewhere stable and install its one dependency
sudo mkdir -p /opt/etlegacy-p2p-lobby
sudo cp tools/p2p-lobby/lobby.js tools/p2p-lobby/package.json /opt/etlegacy-p2p-lobby/
cd /opt/etlegacy-p2p-lobby && sudo npm install --omit=dev
```

### 2. Run it under systemd

A hardened unit ships next to this file:
[`etlegacy-p2p-lobby.service`](./etlegacy-p2p-lobby.service). Copy it in, then
enable it:

```bash
sudo cp tools/p2p-lobby/etlegacy-p2p-lobby.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now etlegacy-p2p-lobby
systemctl status etlegacy-p2p-lobby
journalctl -u etlegacy-p2p-lobby -f
```

The unit runs as a throwaway `DynamicUser`, with `NoNewPrivileges=yes`,
`PrivateTmp=yes`, `ProtectSystem=strict`, `Restart=always` and a tight syscall
filter (see the file for the full list). The server needs nothing writable on
disk. If you prefer a fixed account over `DynamicUser`, swap in:

```ini
DynamicUser=no
User=etl-lobby
Group=etl-lobby
```

after `sudo adduser --system --group --no-create-home etl-lobby`.

The complete unit, ready to copy:

```ini
[Unit]
Description=ET: Legacy p2p lobby / WebRTC signalling / fallback relay
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
WorkingDirectory=/opt/etlegacy-p2p-lobby
ExecStart=/usr/bin/node /opt/etlegacy-p2p-lobby/lobby.js --port 8081 --max-rooms 128
Restart=always
RestartSec=2
DynamicUser=yes
NoNewPrivileges=yes
PrivateTmp=yes
ProtectSystem=strict
ProtectHome=yes
ProtectControlGroups=yes
ProtectKernelModules=yes
ProtectKernelTunables=yes
ProtectKernelLogs=yes
ProtectClock=yes
ProtectHostname=yes
ProtectProc=invisible
RestrictNamespaces=yes
RestrictRealtime=yes
RestrictSUIDSGID=yes
LockPersonality=yes
MemoryDenyWriteExecute=yes
SystemCallArchitectures=native
SystemCallFilter=@system-service
SystemCallErrorNumber=EPERM
RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX
AmbientCapabilities=CAP_NET_BIND_SERVICE
CapabilityBoundingSet=CAP_NET_BIND_SERVICE
ReadOnlyPaths=/opt/etlegacy-p2p-lobby
LimitNOFILE=65536
MemoryMax=256M
TasksMax=64

[Install]
WantedBy=multi-user.target
```

### 3. TLS: reverse proxy (recommended) or built-in

A page served over `https://` may only open **secure** `wss://` sockets
(mixed-content blocking), so the lobby must be reachable over TLS.

**A. Terminate TLS in nginx** and keep the lobby on plain `ws://` behind it —
this is the usual setup, and it lets one certificate cover the relay, the lobby
and the page:

```nginx
# https://lobby.example.com/  ->  ws://127.0.0.1:8081/
server {
    listen 443 ssl http2;
    server_name lobby.example.com;

    ssl_certificate     /etc/letsencrypt/live/lobby.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/lobby.example.com/privkey.pem;

    location / {
        proxy_pass http://127.0.0.1:8081/;
        proxy_http_version 1.1;
        proxy_set_header Upgrade    $http_upgrade;   # WebSocket upgrade
        proxy_set_header Connection "upgrade";
        proxy_set_header Host       $host;
        proxy_read_timeout 3600s;                    # keep idle lobbies open
    }
}
```

Point the client at `wss://lobby.example.com/`.

**B. Terminate TLS in the lobby itself** (no proxy):

```bash
node lobby.js --port 8443 \
  --tls-cert /etc/letsencrypt/live/lobby.example.com/fullchain.pem \
  --tls-key  /etc/letsencrypt/live/lobby.example.com/privkey.pem
```

Give the `DynamicUser` read access to the certificate (e.g. an ACL:
`sudo setfacl -R -m u:$(systemctl show -p User --value etlegacy-p2p-lobby):rX
/etc/letsencrypt/live /etc/letsencrypt/archive`), or use the reverse proxy,
which keeps private keys out of the service entirely.

Use a browser-trusted certificate (Let's Encrypt); self-signed ones are
rejected unless manually trusted.

### 4. Firewall ports

| Port | Proto | Who | Why |
|------|-------|-----|-----|
| 443 (or 8443/8081) | TCP | public | the lobby / signalling WebSocket + HTTP |
| 3478 | TCP **and** UDP | public | STUN/TURN control (if you run coturn) |
| 49152–65535 | UDP | public | TURN relay media ports (coturn default range) |

With `ufw`:

```bash
sudo ufw allow 443/tcp
sudo ufw allow 3478
sudo ufw allow 49152:65535/udp
```

The WebRTC data channels themselves open **directly between the two players'
machines** (or through your TURN server); they do not go through the lobby port,
so you do not need to open anything else for the peer-to-peer path.

### 5. STUN / TURN (coturn) — and why TURN matters

WebRTC needs to discover a network path between the two browsers:

- **STUN** just tells each peer its public address. That is enough when at least
  one side has a cooperative NAT. A public STUN server (the built-in default,
  `stun:stun.l.google.com:19302`) is fine for this and costs you nothing.
- **TURN** is a *relay* the media falls back to when a direct path cannot be
  found — most importantly behind **symmetric NAT** and **carrier-grade NAT**
  (common on mobile networks and many home ISPs), where STUN alone fails. In
  practice a noticeable fraction of players cannot connect peer-to-peer without
  a TURN server. TURN traffic costs you bandwidth (it flows through your host),
  which is why it is not a free public service — you run your own.

Without TURN, players who can't get a direct WebRTC path still aren't stranded:
the lobby's **binary fallback relay** carries their packets over the WebSocket.
TURN is preferable when available because it keeps the low-latency,
head-of-line-blocking-free UDP behaviour of a data channel; the WebSocket
fallback is TCP. Offer both — the browser uses the best that works.

Install and configure coturn on the same host:

```bash
sudo apt-get install -y coturn
sudo sed -i 's/^#TURNSERVER_ENABLED=1/TURNSERVER_ENABLED=1/' /etc/default/coturn
```

Minimal `/etc/turnserver.conf`:

```conf
listening-port=3478
fingerprint
lt-cred-mech
user=etl:s3cret                       # matches --turn-user / --turn-pass
realm=lobby.example.com
# The public IP clients should relay through:
external-ip=YOUR.PUBLIC.IP
# Keep the relay port range in step with the firewall rule above:
min-port=49152
max-port=65535
no-cli
# Do not relay to private ranges - a TURN server is a tempting open proxy:
no-multicast-peers
denied-peer-ip=10.0.0.0-10.255.255.255
denied-peer-ip=192.168.0.0-192.168.255.255
denied-peer-ip=172.16.0.0-172.31.255.255
```

```bash
sudo systemctl enable --now coturn
```

Then advertise it to clients (the lobby just passes `ice` through to every
browser in the `welcome` message):

```bash
node lobby.js --ice stun:stun.l.google.com:19302,turn:lobby.example.com:3478 \
              --turn-user etl --turn-pass s3cret
```

For production, prefer coturn's time-limited credentials
(`use-auth-secret` + a shared secret) over a static username/password so a
leaked credential expires on its own; generate the ephemeral credential in your
page and pass it to `ETLP2P.configure({ iceServers })`.

## Reliability & hardening

Built to stay up — a broken client must never take down the other players.

- **Fail-fast startup.** `Listening on …` is printed only from the socket's
  `listening` event, and a server that cannot bind (`EADDRINUSE` after a restart
  that was too quick, `EACCES` on a privileged port) exits with status 1 instead
  of staying up serving nothing. `Restart=always` then retries, and the banner
  in the log means it really is accepting connections. (The `ws-relay` once had
  the opposite bug; this deliberately does not repeat it.)
- **Every field from a client is validated and clamped** (see the room record
  above). Control characters are stripped, `map` is restricted to a safe
  identifier charset, bots can never fill the host's own slot.
- **Rate limiting.** JSON control messages are limited to a burst of 60 per 10 s
  per connection; an abuser is closed with code 1008. The binary data path is
  **not** rate limited (it carries the game) but is strictly size limited.
- **Size limits.** JSON control frames over 8 KB and binary frames over
  16 KB + 4 are ignored, not fatal.
- **Connection & room caps.** `--max-connections` (close 1013) and `--max-rooms`
  (`toomanyrooms`) bound resource use.
- **Heartbeat.** A `ws.ping()` every 30 s terminates connections that miss two
  pongs; a peer disappearing removes its room and notifies its partners.
- Uncaught exceptions and unhandled rejections are logged, not fatal.
- `SIGINT`/`SIGTERM` shut down cleanly, with a 5 s fallback so shutdown can't
  hang.
- No `eval`, no dynamic `require`, no dependencies beyond `ws`, and no player IP
  addresses are ever exposed to other players or stored.

## Tests

Two executable Node tests, no browser and no game data:

```bash
npm --prefix tools/p2p-lobby install
node tools/p2p-lobby/test-lobby.mjs        # protocol/server level, raw ws clients
node tools/p2p-lobby/test-p2p-client.mjs   # loads src/web/etl-p2p.js against a real lobby
# or both:  npm --prefix tools/p2p-lobby test
```

`test-lobby.mjs` drives a real `lobby.js` with raw `ws` clients and asserts the
whole control protocol (hello/welcome, unique peer ids, ICE delivery), hosting
and listing (including `GET /rooms`, and private rooms that are hidden but
joinable by id), updates and coalesced subscriber pushes, `roomclosed` on host
disconnect, the join errors (`noroom`/`full`/`self`), verbatim signalling
between partners and its refusal between strangers, the binary relay both ways
with correct source ids and its silent drop between strangers, all the field
validation/clamping, oversized-frame handling, the control-message rate limit,
`--max-connections`, `--max-rooms`, and the fail-fast bind.

`test-p2p-client.mjs` loads the browser transport `src/web/etl-p2p.js` in Node
(with a `ws` `WebSocket` shim; Node has no `RTCPeerConnection`, so it exercises
the relay fallback) and runs a host and a joining client end to end against a
real lobby: `host()`/`join()`, `send`/`receive` both ways with the right peer
indices, `addressForPeer`, `getPeers`, `stopHosting()` throwing the joined
client out (the `closed` event), the receive-queue cap, and
`getRoomCount`/`subscribeRooms`.

Both use ephemeral ports and clean up after themselves; each prints `PASS …`
per assertion and exits non-zero on failure, matching
[`tools/ws-relay/test-relay.mjs`](../ws-relay/test-relay.mjs).

## License

GPL-3.0-or-later (same as ET: Legacy).
