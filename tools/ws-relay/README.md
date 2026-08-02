# ET: Legacy WebSocket-to-UDP Relay Server

A lightweight Node.js relay server that bridges WebSocket connections from browser-based ET: Legacy clients to UDP-based game servers.

## Why is this needed?

Web browsers cannot send raw UDP packets. ET: Legacy game servers communicate via UDP. This relay server accepts WebSocket connections from the browser client and forwards the packets to/from the game server over UDP.

> For the full end-to-end web workflow (build the client, supply game data, run
> a server, run this relay, open the page), see [`docs/web.md`](../../docs/web.md).

## Installation

```bash
cd tools/ws-relay
npm install
```

## Usage

```bash
# Start with default settings (port 8080, plain ws://)
npm start

# Or with custom options
node relay.js --port 9090 --host 0.0.0.0

# Serve secure wss:// directly (required from HTTPS pages, see below)
node relay.js --tls-cert /path/cert.pem --tls-key /path/key.pem
```

## How it works

1. The browser client connects to the relay via WebSocket:
   ```
   ws://relay-server:8080/<game-server-ip>:<game-server-port>
   ```

   The target may be a numeric IP **or a hostname** (`ws://relay:8080/etclan.de:27966`).
   The browser cannot resolve names itself, so the relay does the DNS lookup.

2. The relay opens a UDP socket and forwards packets bidirectionally:
   - Browser → WebSocket → Relay → UDP → Game Server
   - Game Server → UDP → Relay → WebSocket → Browser

3. Each browser client gets its own UDP socket, so the game server sees individual connections.

## Configuration

| Option | Default | Description |
|--------|---------|-------------|
| `--port` | 8080 | WebSocket listen port |
| `--host` | 0.0.0.0 | WebSocket listen host |
| `--tls-cert` | _(none)_ | TLS certificate (PEM); enables `wss://` |
| `--tls-key` | _(none)_ | TLS private key (PEM); enables `wss://` |
| `--timeout` | 120 | Idle timeout in seconds (no traffic in either direction) |
| `--max-connections` | 128 | Maximum simultaneous connections |

Provide **both** `--tls-cert` and `--tls-key` to accept secure `wss://`
connections. With neither, the relay serves plain `ws://`.

## Secure `wss://` from HTTPS pages

Browsers refuse to open an insecure `ws://` socket from a page served over
`https://` (mixed-content blocking). Any HTTPS-hosted client — including the
GitHub Pages deploy produced by `.github/workflows/emscripten.yml` — therefore
needs the relay reachable over `wss://`. There are two ways to do this:

**A. Terminate TLS in the relay** (simplest for a single host):

```bash
node relay.js --port 8443 --tls-cert /etc/letsencrypt/live/EXAMPLE/fullchain.pem \
                          --tls-key  /etc/letsencrypt/live/EXAMPLE/privkey.pem
```

Then point the client at `?relay=wss://your-host:8443`.

**B. Terminate TLS in a reverse proxy** (nginx) and keep the relay on plain
`ws://` behind it:

```nginx
location /relay/ {
    proxy_pass http://127.0.0.1:8080/;
    proxy_http_version 1.1;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
}
```

Then point the client at `?relay=wss://your-host/relay`.

Use a certificate trusted by browsers (e.g. Let's Encrypt); self-signed
certificates are rejected unless manually trusted.

## Client Configuration

In the ET: Legacy web client, set the relay server address:

```
/set net_wsRelayServer "ws://your-relay-server:8080"
```

You can also configure everything from the page URL (no console needed). The web
shell (`src/web/shell.html`) reads these query parameters:

| Parameter | Purpose | Example |
|-----------|---------|---------|
| `assets`  | Base URL to download `pak0-2.pk3` from | `?assets=https://et.clan-etc.de/etmain/` |
| `relay`   | WebSocket relay URL (`net_wsRelayServer`) | `?relay=wss://relay.example.com` |
| `connect` | Game server `host:port` to auto-join | `?connect=etclan.de:27966` |
| `touch`   | Force the on-screen touch controls off/on | `?touch=1` |

`relay` is optional: the shell has a default relay built in (`DEFAULT_RELAY_HOST`
in `src/web/shell.html`) and picks `ws://` or `wss://` to match the page, so a
share link normally only needs the server:

```
etl.html?connect=etclan.de:27966
```

## Hosting a game others can join

The browser client **cannot host a server** (browsers have no raw/listening UDP
sockets). This works exactly like the Quake 3 / QuakeJS web port: the actual
host is a **native dedicated server**, and browser players reach it through this
relay.

1. Run a normal native ET: Legacy dedicated server (`etlded`) on a host with a
   public UDP port (default `27960`).
2. Run this relay next to it: `npm install && npm start` (see above). Put it
   behind TLS (`wss://`) if your page is served over HTTPS.
3. Share a link with the relay and server baked in, e.g.
   `https://your-page/etl.html?relay=wss://your-relay:8080&connect=<server-ip>:27960`.
4. Anyone who opens that link downloads the game data, connects through the
   relay, and joins the server — multiple browser players can join the same
   server at once (each gets its own UDP socket on the relay side).

## Reliability

The relay is built to stay up: a broken client must never take down the other
players.

- Every connection is torn down exactly once (WebSocket close, WebSocket error,
  UDP error, idle timeout and shutdown all share one idempotent teardown), so a
  UDP socket is never closed twice (this used to crash the process with
  `ERR_SOCKET_DGRAM_NOT_RUNNING`).
- Uncaught exceptions and unhandled promise rejections are logged, not fatal.
- A WebSocket ping/pong heartbeat (every 15s) drops half-open connections whose
  peer vanished without sending a close frame.
- UDP datagrams that do not come from the requested game server are ignored.
- Packets arriving before the UDP socket finished binding are queued instead of
  dropped, so the initial connection handshake is not lost.
- `SIGINT`/`SIGTERM` shut down cleanly, with a 5s fallback so shutdown cannot
  hang.

Raise `--timeout` if you want idle spectators to stay connected longer; lower it
to reclaim sockets faster.

## Deployment

For production use, consider:

- Running behind a reverse proxy (nginx) with TLS (`wss://`)
- Setting up CORS headers if needed
- Using a process manager (pm2, systemd) for reliability, e.g.
  `pm2 start relay.js -- --port 8080` or a systemd unit with `Restart=always`
- Deploying near your game servers to minimize latency

## Latency Considerations

The WebSocket relay adds latency compared to native UDP:
- WebSocket uses TCP, which adds ~10-30ms overhead from TCP handshake and head-of-line blocking
- The relay itself adds minimal processing time (<1ms)
- For lower latency, consider implementing WebRTC data channels (future work)

## License

GPL-3.0 (same as ET: Legacy)
