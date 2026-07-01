# ET: Legacy WebSocket-to-UDP Relay Server

A lightweight Node.js relay server that bridges WebSocket connections from browser-based ET: Legacy clients to UDP-based game servers.

## Why is this needed?

Web browsers cannot send raw UDP packets. ET: Legacy game servers communicate via UDP. This relay server accepts WebSocket connections from the browser client and forwards the packets to/from the game server over UDP.

## Installation

```bash
cd tools/ws-relay
npm install
```

## Usage

```bash
# Start with default settings (port 8080)
npm start

# Or with custom options
node relay.js --port 9090 --host 0.0.0.0
```

## How it works

1. The browser client connects to the relay via WebSocket:
   ```
   ws://relay-server:8080/<game-server-ip>:<game-server-port>
   ```

2. The relay opens a UDP socket and forwards packets bidirectionally:
   - Browser → WebSocket → Relay → UDP → Game Server
   - Game Server → UDP → Relay → WebSocket → Browser

3. Each browser client gets its own UDP socket, so the game server sees individual connections.

## Configuration

| Option | Default | Description |
|--------|---------|-------------|
| `--port` | 8080 | WebSocket listen port |
| `--host` | 0.0.0.0 | WebSocket listen host |

## Client Configuration

In the ET: Legacy web client, set the relay server address:

```
/set net_wsRelayServer "ws://your-relay-server:8080"
```

## Deployment

For production use, consider:

- Running behind a reverse proxy (nginx) with TLS (`wss://`)
- Setting up CORS headers if needed
- Using a process manager (pm2, systemd) for reliability
- Deploying near your game servers to minimize latency

## Latency Considerations

The WebSocket relay adds latency compared to native UDP:
- WebSocket uses TCP, which adds ~10-30ms overhead from TCP handshake and head-of-line blocking
- The relay itself adds minimal processing time (<1ms)
- For lower latency, consider implementing WebRTC data channels (future work)

## License

GPL-3.0 (same as ET: Legacy)
