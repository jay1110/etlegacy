/**
 * ET: Legacy WebSocket-to-UDP Relay Server
 *
 * This relay server bridges WebSocket connections from browser-based
 * ET: Legacy clients to UDP-based game servers.
 *
 * Usage:
 *   node relay.js [--port 8080] [--host 0.0.0.0]
 *                 [--tls-cert cert.pem --tls-key key.pem]
 *
 * The relay accepts WebSocket connections at:
 *   ws://host:port/<server-ip>:<server-port>    (plain)
 *   wss://host:port/<server-ip>:<server-port>   (with --tls-cert/--tls-key)
 *
 * Browsers served over HTTPS may only open secure (wss://) WebSockets, so a
 * page hosted on GitHub Pages / any HTTPS host needs the relay behind TLS -
 * either terminate TLS here with --tls-cert/--tls-key or in front of it with a
 * reverse proxy (nginx). See README.md.
 *
 * For each WebSocket connection, it opens a UDP socket and forwards
 * packets bidirectionally between the WebSocket client and the UDP
 * game server.
 *
 * License: GPL-3.0 (same as ET: Legacy)
 */

'use strict';

const dgram = require('dgram');
const fs = require('fs');
const https = require('https');
const { WebSocketServer } = require('ws');
const url = require('url');

// Configuration
const DEFAULT_PORT = 8080;
const DEFAULT_HOST = '0.0.0.0';
const CONNECTION_TIMEOUT_MS = 30000; // 30 seconds idle timeout
const MAX_CONNECTIONS = 128;

// Parse command line arguments
const args = process.argv.slice(2);
let port = DEFAULT_PORT;
let host = DEFAULT_HOST;
let tlsCert = null;
let tlsKey = null;

for (let i = 0; i < args.length; i++) {
    if (args[i] === '--port' && args[i + 1]) {
        port = parseInt(args[i + 1], 10);
        i++;
    } else if (args[i] === '--host' && args[i + 1]) {
        host = args[i + 1];
        i++;
    } else if (args[i] === '--tls-cert' && args[i + 1]) {
        tlsCert = args[i + 1];
        i++;
    } else if (args[i] === '--tls-key' && args[i + 1]) {
        tlsKey = args[i + 1];
        i++;
    } else if (args[i] === '--help') {
        console.log('ET: Legacy WebSocket-to-UDP Relay Server');
        console.log('');
        console.log('Usage: node relay.js [options]');
        console.log('');
        console.log('Options:');
        console.log('  --port <port>       Listen port (default: 8080)');
        console.log('  --host <host>       Listen host (default: 0.0.0.0)');
        console.log('  --tls-cert <file>   TLS certificate (PEM) to serve wss://');
        console.log('  --tls-key <file>    TLS private key (PEM) to serve wss://');
        console.log('  --help              Show this help');
        console.log('');
        console.log('Provide both --tls-cert and --tls-key to accept secure');
        console.log('wss:// connections (required from HTTPS pages). Otherwise');
        console.log('the relay serves plain ws://.');
        process.exit(0);
    }
}

// TLS is enabled only when both a certificate and a key are supplied.
if ((tlsCert && !tlsKey) || (!tlsCert && tlsKey)) {
    console.error('Error: --tls-cert and --tls-key must be provided together.');
    process.exit(1);
}
const useTls = Boolean(tlsCert && tlsKey);

// Active connections
const connections = new Map();
let connectionIdCounter = 0;

/**
 * Parse the target server address from the WebSocket URL path.
 * Expected format: /<ip>:<port>
 */
function parseTargetAddress(pathname) {
    // Remove leading slash
    const addr = pathname.replace(/^\//, '');

    if (!addr) {
        return null;
    }

    const colonIndex = addr.lastIndexOf(':');
    if (colonIndex === -1) {
        return null;
    }

    const targetHost = addr.substring(0, colonIndex);
    const targetPort = parseInt(addr.substring(colonIndex + 1), 10);

    if (!targetHost || isNaN(targetPort) || targetPort < 1 || targetPort > 65535) {
        return null;
    }

    // Basic IP address validation
    const ipParts = targetHost.split('.');
    if (ipParts.length !== 4) {
        return null;
    }

    for (const part of ipParts) {
        const num = parseInt(part, 10);
        if (isNaN(num) || num < 0 || num > 255) {
            return null;
        }
    }

    return { host: targetHost, port: targetPort };
}

/**
 * Create the WebSocket server. When TLS is configured, attach the WebSocket
 * server to an HTTPS server so it accepts secure wss:// connections; otherwise
 * listen directly for plain ws://.
 */
let wss;
let httpsServer = null;

if (useTls) {
    let creds;
    try {
        creds = { cert: fs.readFileSync(tlsCert), key: fs.readFileSync(tlsKey) };
    } catch (err) {
        console.error(`Error: could not read TLS cert/key: ${err.message}`);
        process.exit(1);
    }

    httpsServer = https.createServer(creds);
    wss = new WebSocketServer({
        server: httpsServer,
        maxPayload: 65536, // Max packet size
        perMessageDeflate: false // Disable compression for game packets
    });
    httpsServer.listen(port, host);
} else {
    wss = new WebSocketServer({
        host: host,
        port: port,
        maxPayload: 65536, // Max packet size
        perMessageDeflate: false // Disable compression for game packets
    });
}

const scheme = useTls ? 'wss' : 'ws';

console.log(`ET: Legacy WebSocket-to-UDP Relay Server`);
console.log(`Listening on ${scheme}://${host}:${port}`);
console.log(`Max connections: ${MAX_CONNECTIONS}`);
console.log(`Connection timeout: ${CONNECTION_TIMEOUT_MS / 1000}s`);
console.log('');

wss.on('connection', (ws, req) => {
    // Check connection limit
    if (connections.size >= MAX_CONNECTIONS) {
        console.log(`Connection rejected: max connections (${MAX_CONNECTIONS}) reached`);
        ws.close(1013, 'Server is full');
        return;
    }

    // Parse target address from URL
    const parsed = url.parse(req.url);
    const target = parseTargetAddress(parsed.pathname);

    if (!target) {
        console.log(`Connection rejected: invalid target address in URL: ${req.url}`);
        ws.close(1008, 'Invalid target address. Use ws://relay/<ip>:<port>');
        return;
    }

    const connId = ++connectionIdCounter;
    const clientAddr = req.socket.remoteAddress;

    console.log(`[${connId}] New connection from ${clientAddr} -> ${target.host}:${target.port}`);

    // Create UDP socket for this connection
    const udpSocket = dgram.createSocket('udp4');

    const connection = {
        id: connId,
        ws: ws,
        udpSocket: udpSocket,
        target: target,
        clientAddr: clientAddr,
        lastActivity: Date.now(),
        packetsSent: 0,
        packetsReceived: 0,
        bytesSent: 0,
        bytesReceived: 0
    };

    connections.set(connId, connection);

    // Forward WebSocket messages to UDP
    ws.on('message', (data) => {
        connection.lastActivity = Date.now();

        if (Buffer.isBuffer(data) || data instanceof ArrayBuffer) {
            const buffer = Buffer.isBuffer(data) ? data : Buffer.from(data);

            udpSocket.send(buffer, 0, buffer.length, target.port, target.host, (err) => {
                if (err) {
                    console.log(`[${connId}] UDP send error: ${err.message}`);
                } else {
                    connection.packetsSent++;
                    connection.bytesSent += buffer.length;
                }
            });
        }
    });

    // Forward UDP responses back to WebSocket
    udpSocket.on('message', (msg, rinfo) => {
        connection.lastActivity = Date.now();

        if (ws.readyState === ws.OPEN) {
            ws.send(msg, { binary: true }, (err) => {
                if (err) {
                    console.log(`[${connId}] WebSocket send error: ${err.message}`);
                } else {
                    connection.packetsReceived++;
                    connection.bytesReceived += msg.length;
                }
            });
        }
    });

    udpSocket.on('error', (err) => {
        console.log(`[${connId}] UDP error: ${err.message}`);
    });

    // Bind UDP socket to any available port
    udpSocket.bind(0);

    // Handle WebSocket close
    ws.on('close', (code, reason) => {
        console.log(`[${connId}] Connection closed (code: ${code}). ` +
                    `Packets: ${connection.packetsSent} sent, ${connection.packetsReceived} received. ` +
                    `Bytes: ${connection.bytesSent} sent, ${connection.bytesReceived} received.`);

        udpSocket.close();
        connections.delete(connId);
    });

    ws.on('error', (err) => {
        console.log(`[${connId}] WebSocket error: ${err.message}`);
        udpSocket.close();
        connections.delete(connId);
    });
});

// Timeout checker - close idle connections
setInterval(() => {
    const now = Date.now();

    for (const [id, conn] of connections) {
        if (now - conn.lastActivity > CONNECTION_TIMEOUT_MS) {
            console.log(`[${id}] Connection timed out (idle for ${CONNECTION_TIMEOUT_MS / 1000}s)`);
            conn.ws.close(1000, 'Idle timeout');
            conn.udpSocket.close();
            connections.delete(id);
        }
    }
}, 5000);

// Graceful shutdown
process.on('SIGINT', () => {
    console.log('\nShutting down relay server...');

    for (const [id, conn] of connections) {
        conn.ws.close(1001, 'Server shutting down');
        conn.udpSocket.close();
    }

    wss.close(() => {
        if (httpsServer) {
            httpsServer.close(() => {
                console.log('Relay server stopped.');
                process.exit(0);
            });
        } else {
            console.log('Relay server stopped.');
            process.exit(0);
        }
    });
});

process.on('SIGTERM', () => {
    process.emit('SIGINT');
});
