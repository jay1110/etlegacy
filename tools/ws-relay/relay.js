/**
 * ET: Legacy WebSocket-to-UDP Relay Server
 *
 * This relay server bridges WebSocket connections from browser-based
 * ET: Legacy clients to UDP-based game servers.
 *
 * Usage:
 *   node relay.js [--port 8080] [--host 0.0.0.0]
 *                 [--tls-cert cert.pem --tls-key key.pem]
 *                 [--timeout 120] [--max-connections 128]
 *
 * The relay accepts WebSocket connections at:
 *   ws://host:port/<server-ip>:<server-port>    (plain)
 *   wss://host:port/<server-ip>:<server-port>   (with --tls-cert/--tls-key)
 *
 * The target may also be given as a query parameter, so the relay is a
 * drop-in replacement for simple "full UDP gateway" scripts:
 *   ws://host:port/?target=<server-ip>:<server-port>
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
const dns = require('dns');
const fs = require('fs');
const https = require('https');
const { WebSocketServer } = require('ws');

// Configuration
const DEFAULT_PORT = 8080;
const DEFAULT_HOST = '0.0.0.0';
const DEFAULT_CONNECTION_TIMEOUT_MS = 120000; // idle timeout (no traffic at all)
const DEFAULT_MAX_CONNECTIONS = 128;
const HEARTBEAT_INTERVAL_MS = 15000; // ping interval to detect dead peers
const TIMEOUT_CHECK_INTERVAL_MS = 5000;

// Parse command line arguments
const args = process.argv.slice(2);
let port = DEFAULT_PORT;
let host = DEFAULT_HOST;
let tlsCert = null;
let tlsKey = null;
let connectionTimeoutMs = DEFAULT_CONNECTION_TIMEOUT_MS;
let maxConnections = DEFAULT_MAX_CONNECTIONS;

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
    } else if ((args[i] === '--timeout' || args[i] === '--idle-timeout') && args[i + 1]) {
        const seconds = parseInt(args[i + 1], 10);
        if (isNaN(seconds) || seconds < 5) {
            console.error('Error: --timeout must be a number of seconds >= 5.');
            process.exit(1);
        }
        connectionTimeoutMs = seconds * 1000;
        i++;
    } else if (args[i] === '--max-connections' && args[i + 1]) {
        const limit = parseInt(args[i + 1], 10);
        if (isNaN(limit) || limit < 1) {
            console.error('Error: --max-connections must be a number >= 1.');
            process.exit(1);
        }
        maxConnections = limit;
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
        console.log('  --timeout <secs>    Idle timeout in seconds (default: 120)');
        console.log('  --max-connections <n>  Connection limit (default: 128)');
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
 * Tear down a single connection. Safe to call multiple times and from any
 * handler (close, error, idle timeout, shutdown) - a connection is only ever
 * cleaned up once, and neither a closed UDP socket nor an already closed
 * WebSocket can throw and take the whole relay process down.
 */
function closeConnection(conn, code, reason, logMessage) {
    if (conn.closed) {
        return;
    }
    conn.closed = true;

    connections.delete(conn.id);

    if (logMessage) {
        console.log(`[${conn.id}] ${logMessage}`);
    }

    console.log(`[${conn.id}] Connection closed (code: ${code}). ` +
                `Packets: ${conn.packetsSent} sent, ${conn.packetsReceived} received. ` +
                `Bytes: ${conn.bytesSent} sent, ${conn.bytesReceived} received.`);

    try {
        conn.udpSocket.close();
    } catch (err) {
        // Socket was never bound or is already closed - nothing to do.
    }

    try {
        if (conn.ws.readyState === conn.ws.OPEN || conn.ws.readyState === conn.ws.CONNECTING) {
            conn.ws.close(code >= 1000 && code <= 4999 ? code : 1000, reason);
        }
    } catch (err) {
        // Ignore - the socket is going away anyway.
    }
}

/**
 * Check that a string is a syntactically valid DNS hostname. The browser build
 * cannot resolve names itself, so it passes them here (see
 * src/qcommon/net_web.c) and the relay resolves them - that is what makes a
 * target like "etclan.de:27966" work.
 */
function isValidHostname(name) {
    if (!name || name.length > 253) {
        return false;
    }
    if (!/^[A-Za-z0-9.-]+$/.test(name)) {
        return false;
    }
    if (/^[-.]|[-.]$/.test(name)) {
        return false;
    }
    // must contain a letter - a purely numeric name is a malformed IP
    return /[A-Za-z]/.test(name);
}

/**
 * Parse the target server address from the WebSocket URL path.
 * Expected format: /<ip-or-hostname>:<port>
 */
function parseTargetAddress(pathname) {
    // Remove leading slash
    const addr = pathname.replace(/^\//, '');

    return parseTargetSpec(addr);
}

/**
 * Parse a "<ip-or-hostname>:<port>" target specification.
 */
function parseTargetSpec(addr) {
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

    // Numeric IPv4 target
    const ipParts = targetHost.split('.');
    if (ipParts.length === 4 &&
        ipParts.every(function (part) {
            return /^\d{1,3}$/.test(part) && Number(part) <= 255;
        })) {
        return { host: targetHost, port: targetPort, isIp: true };
    }

    // Hostname target - resolved via DNS when the connection is set up
    if (isValidHostname(targetHost)) {
        return { host: targetHost, port: targetPort, isIp: false };
    }

    return null;
}

/**
 * Determine the target for a WebSocket upgrade request. Two URL forms are
 * accepted, so the relay is a drop-in replacement for simple UDP gateways:
 *   ws://relay/<host>:<port>          (path form - what the engine emits)
 *   ws://relay/?target=<host>:<port>  (query form)
 */
function parseTargetFromRequestUrl(requestUrl) {
    const url = new URL(requestUrl, 'http://relay');

    if (url.searchParams.has('target')) {
        return parseTargetSpec(url.searchParams.get('target'));
    }

    return parseTargetAddress(url.pathname);
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
console.log(`Max connections: ${maxConnections}`);
console.log(`Connection timeout: ${connectionTimeoutMs / 1000}s`);
console.log('');

wss.on('connection', (ws, req) => {
    // Check connection limit
    if (connections.size >= maxConnections) {
        console.log(`Connection rejected: max connections (${maxConnections}) reached`);
        ws.close(1013, 'Server is full');
        return;
    }

    // Parse target address from URL
    let target = null;
    try {
        target = parseTargetFromRequestUrl(req.url);
    } catch (err) {
        target = null;
    }

    if (!target) {
        console.log(`Connection rejected: invalid target address in URL: ${req.url}`);
        ws.close(1008, 'Invalid target address. Use ws://relay/<ip>:<port> or ws://relay/?target=<ip>:<port>');
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
        closed: false,
        udpReady: false,
        resolved: false,
        pendingPackets: [],
        isAlive: true,
        packetsSent: 0,
        packetsReceived: 0,
        bytesSent: 0,
        bytesReceived: 0
    };

    connections.set(connId, connection);

    // Forward WebSocket messages to UDP
    const sendToServer = (buffer) => {
        if (connection.closed) {
            return;
        }

        try {
            udpSocket.send(buffer, 0, buffer.length, target.port, target.address, (err) => {
                if (err) {
                    console.log(`[${connId}] UDP send error: ${err.message}`);
                } else {
                    connection.packetsSent++;
                    connection.bytesSent += buffer.length;
                }
            });
        } catch (err) {
            // e.g. the socket was closed between the check and the send
            console.log(`[${connId}] UDP send failed: ${err.message}`);
        }
    };

    ws.on('message', (data) => {
        connection.lastActivity = Date.now();
        connection.isAlive = true;

        if (Buffer.isBuffer(data) || data instanceof ArrayBuffer || ArrayBuffer.isView(data)) {
            const buffer = Buffer.isBuffer(data) ? data : Buffer.from(data);

            // Packets that arrive before bind() completed are queued instead of
            // being dropped, so the initial handshake is never lost.
            if (!connection.udpReady || !connection.resolved) {
                if (connection.pendingPackets.length < 64) {
                    connection.pendingPackets.push(buffer);
                }
                return;
            }

            sendToServer(buffer);
        }
    });

    ws.on('pong', () => {
        connection.isAlive = true;
    });

    function flushPending() {
        if (connection.closed || !connection.udpReady || !connection.resolved) {
            return;
        }

        const queued = connection.pendingPackets;
        connection.pendingPackets = [];
        for (const buffer of queued) {
            sendToServer(buffer);
        }
    }

    // Forward UDP responses back to WebSocket
    udpSocket.on('message', (msg, rinfo) => {
        // Ignore traffic that does not come from the target game server.
        if (rinfo.address !== target.address || rinfo.port !== target.port) {
            return;
        }

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
        closeConnection(connection, 1011, 'UDP error', `UDP error: ${err.message}`);
    });

    udpSocket.on('listening', () => {
        connection.udpReady = true;
        flushPending();
    });

    // Resolve a hostname target once, then start forwarding. Until both the
    // DNS lookup and the bind() have completed, packets stay queued above.
    if (!target.isIp) {
        dns.lookup(target.host, { family: 4 }, (err, address) => {
            if (connection.closed) {
                return;
            }
            if (err) {
                closeConnection(connection, 1011, 'DNS lookup failed',
                                `Cannot resolve ${target.host}: ${err.message}`);
                return;
            }

            console.log(`[${connId}] Resolved ${target.host} -> ${address}`);
            target.address = address;
            connection.resolved = true;
            flushPending();
        });
    } else {
        target.address = target.host;
        connection.resolved = true;
    }

    // Bind UDP socket to any available port
    try {
        udpSocket.bind(0);
    } catch (err) {
        closeConnection(connection, 1011, 'UDP bind failed', `UDP bind failed: ${err.message}`);
        return;
    }

    // Handle WebSocket close
    ws.on('close', (code) => {
        closeConnection(connection, code || 1000, 'Closed');
    });

    ws.on('error', (err) => {
        closeConnection(connection, 1011, 'WebSocket error', `WebSocket error: ${err.message}`);
    });
});

wss.on('error', (err) => {
    console.error(`WebSocket server error: ${err.message}`);
});

if (httpsServer) {
    httpsServer.on('error', (err) => {
        console.error(`HTTPS server error: ${err.message}`);
    });

    httpsServer.on('tlsClientError', () => {
        // Ignore - probes and plain-HTTP requests must not affect the relay.
    });
}

// Timeout checker - close idle connections
const timeoutTimer = setInterval(() => {
    const now = Date.now();

    for (const conn of [...connections.values()]) {
        if (now - conn.lastActivity > connectionTimeoutMs) {
            closeConnection(conn, 1000, 'Idle timeout',
                            `Connection timed out (idle for ${connectionTimeoutMs / 1000}s)`);
        }
    }
}, TIMEOUT_CHECK_INTERVAL_MS);

// Heartbeat - drop connections whose peer vanished without a close frame
// (half-open TCP connections would otherwise linger until the idle timeout).
const heartbeatTimer = setInterval(() => {
    for (const conn of [...connections.values()]) {
        if (!conn.isAlive) {
            closeConnection(conn, 1001, 'No heartbeat response', 'Connection dropped (no pong response)');
            continue;
        }

        conn.isAlive = false;
        try {
            conn.ws.ping();
        } catch (err) {
            closeConnection(conn, 1011, 'Ping failed', `Ping failed: ${err.message}`);
        }
    }
}, HEARTBEAT_INTERVAL_MS);

// A single failing connection must never take the relay down. Errors are
// logged and the relay keeps serving every other player.
process.on('uncaughtException', (err) => {
    console.error(`Uncaught exception (relay keeps running): ${err && err.stack ? err.stack : err}`);
});

process.on('unhandledRejection', (reason) => {
    console.error(`Unhandled rejection (relay keeps running): ${reason}`);
});

// Graceful shutdown
let shuttingDown = false;

function shutdown() {
    if (shuttingDown) {
        return;
    }
    shuttingDown = true;

    console.log('\nShutting down relay server...');

    clearInterval(timeoutTimer);
    clearInterval(heartbeatTimer);

    for (const conn of [...connections.values()]) {
        closeConnection(conn, 1001, 'Server shutting down');
    }

    const done = () => {
        console.log('Relay server stopped.');
        process.exit(0);
    };

    wss.close(() => {
        if (httpsServer) {
            httpsServer.close(done);
        } else {
            done();
        }
    });

    // Do not hang forever if a socket refuses to close.
    setTimeout(done, 5000).unref();
}

process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);
