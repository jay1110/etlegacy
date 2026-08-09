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
 * A reverse proxy may prefix that path with its own location
 * (wss://host/ws-relay/<server-ip>:<server-port>); only the last path segment
 * is read as the target.
 *
 * The target may also be given as a query parameter, so the relay is a
 * drop-in replacement for simple "full UDP gateway" scripts:
 *   ws://host:port/?target=<server-ip>:<server-port>
 *
 * The relay also passes HTTP downloads through:
 *   http://host:port/download?url=<url-of-a-pk3>
 * A browser may not fetch the mirror a server redirects it to with
 * sv_wwwBaseURL (mixed content, no Access-Control-Allow-Origin), which makes
 * cl_wwwDownload fail and the game fall back to its own, far slower transfer.
 * The relay fetches the file instead and serves it with the CORS header the
 * browser wants. Only public http(s) .pk3 URLs are passed through; see
 * --no-download-proxy and --allow-private-downloads.
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
const http = require('http');
const https = require('https');
const net = require('net');
const { WebSocketServer } = require('ws');

// Configuration
const DEFAULT_PORT = 8080;
const DEFAULT_HOST = '0.0.0.0';
const DEFAULT_CONNECTION_TIMEOUT_MS = 120000; // idle timeout (no traffic at all)
const DEFAULT_MAX_CONNECTIONS = 128;
const HEARTBEAT_INTERVAL_MS = 15000; // ping interval to detect dead peers
const TIMEOUT_CHECK_INTERVAL_MS = 5000;
const DEFAULT_MAX_DOWNLOAD_MB = 512; // a pk3 larger than this is not passed through
const DEFAULT_MAX_DOWNLOADS = 8; // proxied downloads at the same time
const MAX_DOWNLOADS_PER_CLIENT = 2;
const DOWNLOAD_RESPONSE_TIMEOUT_MS = 30000; // waiting for the mirror to answer
const MAX_DOWNLOAD_REDIRECTS = 4;

// Parse command line arguments
const args = process.argv.slice(2);
let port = DEFAULT_PORT;
let host = DEFAULT_HOST;
let tlsCert = null;
let tlsKey = null;
let connectionTimeoutMs = DEFAULT_CONNECTION_TIMEOUT_MS;
let maxConnections = DEFAULT_MAX_CONNECTIONS;
let downloadProxy = true;
let allowPrivateDownloads = false;
let maxDownloadBytes = DEFAULT_MAX_DOWNLOAD_MB * 1024 * 1024;
let maxDownloads = DEFAULT_MAX_DOWNLOADS;

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
    } else if (args[i] === '--no-download-proxy') {
        downloadProxy = false;
    } else if (args[i] === '--allow-private-downloads') {
        allowPrivateDownloads = true;
    } else if (args[i] === '--max-download-size' && args[i + 1]) {
        const megabytes = parseInt(args[i + 1], 10);
        if (isNaN(megabytes) || megabytes < 1) {
            console.error('Error: --max-download-size must be a number of megabytes >= 1.');
            process.exit(1);
        }
        maxDownloadBytes = megabytes * 1024 * 1024;
        i++;
    } else if (args[i] === '--max-downloads' && args[i + 1]) {
        const limit = parseInt(args[i + 1], 10);
        if (isNaN(limit) || limit < 1) {
            console.error('Error: --max-downloads must be a number >= 1.');
            process.exit(1);
        }
        maxDownloads = limit;
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
        console.log('  --no-download-proxy    Do not pass game file downloads through');
        console.log(`  --max-downloads <n>    Downloads at the same time (default: ${DEFAULT_MAX_DOWNLOADS})`);
        console.log(`  --max-download-size <mb>  Largest file to pass through (default: ${DEFAULT_MAX_DOWNLOAD_MB})`);
        console.log('  --allow-private-downloads  Allow downloads from private/loopback');
        console.log('                         addresses (local testing only)');
        console.log('  --help              Show this help');
        console.log('');
        console.log('Provide both --tls-cert and --tls-key to accept secure');
        console.log('wss:// connections (required from HTTPS pages). Otherwise');
        console.log('the relay serves plain ws://.');
        console.log('');
        console.log('The download proxy answers GET /download?url=<pk3-url> with');
        console.log('CORS headers so browser clients can use cl_wwwDownload.');
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
 *
 * Only the last path segment is looked at, so the relay also works behind a
 * reverse proxy that keeps its own location prefix in the forwarded path -
 * nginx's `proxy_pass http://127.0.0.1:8080;` (no trailing slash) hands
 * "/ws-relay/1.2.3.4:27960" through unchanged, while the trailing-slash form
 * strips the prefix. A target is "<host>:<port>" and never contains a slash,
 * so nothing valid is lost: an empty path still yields no target at all, and
 * every segment is validated by parseTargetSpec() below.
 */
function parseTargetAddress(pathname) {
    const segments = pathname.split('/').filter(function (part) {
        return part.length > 0;
    });

    return parseTargetSpec(segments.length ? segments[segments.length - 1] : '');
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
 * Download proxy
 *
 * A server with sv_wwwDownload 1 redirects clients to its sv_wwwBaseURL mirror
 * instead of sending a pk3 through the game connection, which is roughly an
 * order of magnitude faster. A browser client cannot follow that redirect on
 * its own: a page served over HTTPS may not fetch a http:// mirror at all
 * (mixed content) and a mirror on another host has to allow the page with an
 * Access-Control-Allow-Origin header, which a plain file mirror does not send.
 * Without help every www download fails and the game falls back to its own slow
 * transfer, so the relay fetches the file and serves it with that header.
 *
 * The relay must not become an open door into the network it runs in, so only
 * plain http(s) GETs of .pk3 files to public addresses are passed through, the
 * body is never interpreted, and the size, the number of redirects and the
 * number of downloads at the same time are all capped.
 */
let activeDownloads = 0;
const downloadsPerClient = new Map();

/**
 * Check whether an IPv4 address is one the relay must not fetch from: the
 * loopback, private and link-local ranges are where a cloud metadata service,
 * an admin interface or the game server's own control port live.
 */
function isPrivateIPv4(address) {
    const parts = address.split('.').map((part) => Number(part));

    if (parts.length !== 4 || parts.some((n) => !Number.isInteger(n) || n < 0 || n > 255)) {
        return true; // not an address we understand - refuse it
    }

    const [a, b] = parts;

    if (a === 0 || a === 10 || a === 127) {
        return true; // this host, private, loopback
    }
    if (a === 100 && b >= 64 && b <= 127) {
        return true; // carrier-grade NAT
    }
    if (a === 169 && b === 254) {
        return true; // link-local (cloud metadata)
    }
    if (a === 172 && b >= 16 && b <= 31) {
        return true; // private
    }
    if (a === 192 && (b === 0 || b === 168)) {
        return true; // protocol assignments, private
    }
    if (a === 198 && (b === 18 || b === 19)) {
        return true; // benchmarking
    }
    if (a >= 224) {
        return true; // multicast, reserved, broadcast
    }

    return false;
}

/**
 * Same for IPv6, including the IPv4-mapped form a dual-stack lookup returns.
 */
function isPrivateIPv6(address) {
    const ip = address.toLowerCase().split('%')[0]; // drop a zone index

    if (ip === '::' || ip === '::1') {
        return true;
    }

    if (ip.startsWith('::ffff:')) {
        const mapped = ip.slice('::ffff:'.length);
        return net.isIPv4(mapped) ? isPrivateIPv4(mapped) : true;
    }

    if (/^f[cd]/.test(ip)) {
        return true; // unique local fc00::/7
    }
    if (/^fe[89ab]/.test(ip)) {
        return true; // link-local fe80::/10
    }
    if (ip.startsWith('ff')) {
        return true; // multicast
    }

    return false;
}

function isPrivateAddress(ip) {
    if (net.isIPv4(ip)) {
        return isPrivateIPv4(ip);
    }
    if (net.isIPv6(ip)) {
        return isPrivateIPv6(ip);
    }
    return true;
}

/**
 * Decode a URL path without throwing on a malformed escape sequence.
 */
function safeDecode(value) {
    try {
        return decodeURIComponent(value);
    } catch (err) {
        return value;
    }
}

/**
 * Check that a URL is one the relay is willing to fetch. Anything but a plain
 * http(s) GET of a .pk3 file is refused - that is all a game download is, and
 * everything else only widens what the relay can be pointed at.
 *
 * @returns {string|null} the reason it was refused, or null when it is fine
 */
function checkDownloadUrl(url) {
    if (url.protocol !== 'http:' && url.protocol !== 'https:') {
        return 'only http and https URLs are passed through';
    }

    if (url.username || url.password) {
        return 'URLs with credentials are not passed through';
    }

    if (!/\.pk3$/i.test(safeDecode(url.pathname))) {
        return 'only .pk3 files are passed through';
    }

    return null;
}

/**
 * Resolve a hostname and return an address the relay may connect to. The
 * address is handed to the request itself, so a name that resolves to
 * something else on the second lookup cannot be used to slip past this check.
 */
function resolvePublicAddress(hostname) {
    return new Promise((resolve, reject) => {
        // A literal address needs no lookup - and dns.lookup would happily
        // hand back whatever it was given anyway.
        if (net.isIP(hostname)) {
            if (!allowPrivateDownloads && isPrivateAddress(hostname)) {
                reject(new Error(`${hostname} is not a public address`));
                return;
            }
            resolve({ address: hostname, family: net.isIPv6(hostname) ? 6 : 4 });
            return;
        }

        dns.lookup(hostname, { all: true }, (err, addresses) => {
            if (err || !addresses || !addresses.length) {
                reject(new Error(`cannot resolve ${hostname}`));
                return;
            }

            const usable = allowPrivateDownloads ?
                addresses : addresses.filter((entry) => !isPrivateAddress(entry.address));

            if (!usable.length) {
                reject(new Error(`${hostname} does not resolve to a public address`));
                return;
            }

            resolve(usable[0]);
        });
    });
}

/**
 * Send a single request to the mirror, connecting to the address that was
 * checked above.
 */
function requestUpstream(target, address, method) {
    return new Promise((resolve, reject) => {
        const client = target.protocol === 'https:' ? https : http;

        const request = client.request({
            protocol: target.protocol,
            hostname: target.hostname,
            port: target.port || (target.protocol === 'https:' ? 443 : 80),
            path: `${target.pathname}${target.search}`,
            method: method,
            headers: {
                'User-Agent': 'etlegacy-ws-relay',
                'Accept': '*/*'
            },
            lookup: (name, options, callback) => callback(null, address.address, address.family)
        }, resolve);

        request.setTimeout(DOWNLOAD_RESPONSE_TIMEOUT_MS, () => {
            request.destroy(new Error('the mirror did not answer in time'));
        });

        request.on('error', reject);
        request.end();
    });
}

/**
 * Fetch a file from a mirror, following redirects - a mirror answering a
 * http:// URL with a redirect to its https:// version is common - and checking
 * every hop again.
 */
async function fetchFromMirror(target, method) {
    let current = target;

    for (let redirects = 0; redirects <= MAX_DOWNLOAD_REDIRECTS; redirects++) {
        const address = await resolvePublicAddress(current.hostname);
        const upstream = await requestUpstream(current, address, method);
        const status = upstream.statusCode;

        if (status >= 300 && status < 400 && upstream.headers.location) {
            upstream.resume(); // discard the body of the redirect

            if (redirects === MAX_DOWNLOAD_REDIRECTS) {
                throw new Error('too many redirects');
            }

            let next;
            try {
                next = new URL(upstream.headers.location, current);
            } catch (err) {
                throw new Error('the mirror redirected to an invalid URL');
            }

            const refused = checkDownloadUrl(next);
            if (refused) {
                throw new Error(`the mirror redirected to a URL that is refused: ${refused}`);
            }

            current = next;
            continue;
        }

        return { upstream: upstream, url: current };
    }

    throw new Error('too many redirects');
}

const CORS_HEADERS = {
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Methods': 'GET, HEAD, OPTIONS',
    'Access-Control-Max-Age': '86400'
};

function sendPlainResponse(res, status, message) {
    if (res.headersSent) {
        res.destroy();
        return;
    }

    res.writeHead(status, Object.assign({
        'Content-Type': 'text/plain; charset=utf-8',
        'Cache-Control': 'no-store'
    }, CORS_HEADERS));
    res.end(`${message}\n`);
}

/**
 * Serve GET /download?url=<url of a pk3>
 */
async function handleDownloadRequest(req, res, requestUrl) {
    const clientKey = req.socket.remoteAddress || 'unknown';

    if (req.method === 'OPTIONS') {
        res.writeHead(204, CORS_HEADERS);
        res.end();
        return;
    }

    if (req.method !== 'GET' && req.method !== 'HEAD') {
        sendPlainResponse(res, 405, 'Only GET is supported.');
        return;
    }

    if (!downloadProxy) {
        sendPlainResponse(res, 403, 'This relay does not pass downloads through (--no-download-proxy).');
        return;
    }

    const raw = requestUrl.searchParams.get('url');

    if (!raw) {
        sendPlainResponse(res, 400, 'Missing url parameter. Use /download?url=<url of a pk3>.');
        return;
    }

    let target;
    try {
        target = new URL(raw);
    } catch (err) {
        sendPlainResponse(res, 400, 'The url parameter is not a valid URL.');
        return;
    }

    const refused = checkDownloadUrl(target);
    if (refused) {
        sendPlainResponse(res, 403, `Refused: ${refused}.`);
        return;
    }

    if (activeDownloads >= maxDownloads) {
        sendPlainResponse(res, 503, 'Too many downloads at the same time, try again shortly.');
        return;
    }

    const perClient = downloadsPerClient.get(clientKey) || 0;
    if (perClient >= MAX_DOWNLOADS_PER_CLIENT) {
        sendPlainResponse(res, 429, 'Too many downloads from this address at the same time.');
        return;
    }

    activeDownloads++;
    downloadsPerClient.set(clientKey, perClient + 1);

    let released = false;
    const release = () => {
        if (released) {
            return;
        }
        released = true;
        activeDownloads--;

        const left = (downloadsPerClient.get(clientKey) || 1) - 1;
        if (left > 0) {
            downloadsPerClient.set(clientKey, left);
        } else {
            downloadsPerClient.delete(clientKey);
        }
    };

    res.on('close', release);

    console.log(`[dl] ${clientKey} -> ${target.href}`);

    let upstream = null;

    try {
        const result = await fetchFromMirror(target, req.method);
        upstream = result.upstream;

        if (upstream.statusCode !== 200) {
            const status = upstream.statusCode;
            upstream.resume();
            console.log(`[dl] ${target.href} answered ${status}`);
            sendPlainResponse(res, status === 404 ? 404 : 502, `The mirror answered ${status}.`);
            return;
        }

        const length = Number(upstream.headers['content-length']);

        if (Number.isFinite(length) && length > maxDownloadBytes) {
            upstream.destroy();
            console.log(`[dl] ${target.href} is ${length} bytes, over the limit`);
            sendPlainResponse(res, 502, 'The file is larger than this relay passes through.');
            return;
        }

        const headers = Object.assign({
            // The relay serves this from its own origin, so it must never be
            // treated as anything but the opaque file it is.
            'Content-Type': 'application/octet-stream',
            'X-Content-Type-Options': 'nosniff',
            'Content-Disposition': 'attachment',
            'Cache-Control': 'no-store'
        }, CORS_HEADERS);

        if (Number.isFinite(length)) {
            headers['Content-Length'] = String(length);
        }

        res.writeHead(200, headers);

        if (req.method === 'HEAD') {
            upstream.destroy();
            res.end();
            return;
        }

        let received = 0;

        upstream.on('data', (chunk) => {
            received += chunk.length;

            if (received > maxDownloadBytes) {
                console.log(`[dl] ${target.href} exceeded the size limit while streaming`);
                upstream.destroy();
                res.destroy();
            }
        });

        upstream.on('error', (err) => {
            console.log(`[dl] ${target.href} failed while streaming: ${err.message}`);
            res.destroy();
        });

        upstream.pipe(res);
    } catch (err) {
        if (upstream) {
            upstream.destroy();
        }
        console.log(`[dl] ${target.href} failed: ${err.message}`);
        sendPlainResponse(res, 502, `Download failed: ${err.message}.`);
    }
}

/**
 * Handle a plain HTTP request. Everything the relay serves over HTTP is read
 * from the last path segment, the same way the WebSocket target is, so it also
 * works behind a reverse proxy that adds a location prefix of its own.
 */
function handleHttpRequest(req, res) {
    let requestUrl;

    try {
        requestUrl = new URL(req.url, 'http://relay');
    } catch (err) {
        sendPlainResponse(res, 400, 'Bad request.');
        return;
    }

    const segments = requestUrl.pathname.split('/').filter((part) => part.length);

    if (segments.length && segments[segments.length - 1] === 'download') {
        handleDownloadRequest(req, res, requestUrl).catch((err) => {
            console.log(`[dl] unexpected failure: ${err && err.message ? err.message : err}`);
            sendPlainResponse(res, 500, 'Download failed.');
        });
        return;
    }

    sendPlainResponse(res, 404,
                      'ET: Legacy WebSocket-to-UDP relay. Open a WebSocket to /<host>:<port> to play, ' +
                      'or GET /download?url=<url of a pk3> to fetch a game file.');
}

/**
 * Create the server. The WebSocket server is attached to an HTTP(S) server so
 * the relay can answer plain requests (the download proxy) next to the
 * WebSocket upgrades; with TLS configured that is an HTTPS server, which is
 * what a page served over HTTPS needs for both.
 */
let wss;
let webServer;

if (useTls) {
    let creds;
    try {
        creds = { cert: fs.readFileSync(tlsCert), key: fs.readFileSync(tlsKey) };
    } catch (err) {
        console.error(`Error: could not read TLS cert/key: ${err.message}`);
        process.exit(1);
    }

    webServer = https.createServer(creds);
} else {
    webServer = http.createServer();
}

webServer.on('request', handleHttpRequest);

wss = new WebSocketServer({
    server: webServer,
    maxPayload: 65536, // Max packet size
    perMessageDeflate: false // Disable compression for game packets
});

webServer.listen(port, host);

const scheme = useTls ? 'wss' : 'ws';

// The banner is printed from the "listening" event, not before it: until the
// socket is actually bound the relay may still fail, and a banner printed
// up front is a lie that anything scripting the relay (tests, a health check,
// an operator reading the log) has no way to tell from a working start.
const listeningServer = webServer;
let listening = false;
let startupFailed = false;

listeningServer.on('listening', () => {
    listening = true;

    const bound = typeof listeningServer.address === 'function' ? listeningServer.address() : null;
    const boundPort = bound && bound.port ? bound.port : port;

    console.log(`ET: Legacy WebSocket-to-UDP Relay Server`);
    console.log(`Listening on ${scheme}://${host}:${boundPort}`);
    console.log(`Max connections: ${maxConnections}`);
    console.log(`Connection timeout: ${connectionTimeoutMs / 1000}s`);
    if (downloadProxy) {
        console.log(`Download proxy: ${useTls ? 'https' : 'http'}://${host}:${boundPort}/download?url=<url of a pk3> ` +
                    `(max ${maxDownloads} at a time, ${maxDownloadBytes / (1024 * 1024)} MB each` +
                    `${allowPrivateDownloads ? ', private addresses allowed' : ''})`);
    } else {
        console.log('Download proxy: disabled');
    }
    console.log('');
});

wss.on('connection', (ws, req) => {
    // Game traffic is a stream of small packets. Nagle's algorithm holds those
    // back to coalesce them, which adds tens of milliseconds on top of the
    // relay - exactly the latency this relay is judged by. Node only defaults
    // this to true on newer releases and the relay supports Node >= 16, so set
    // it explicitly rather than relying on the default.
    try {
        req.socket.setNoDelay(true);
    } catch (err) {
        // Socket already gone - the connection handler below deals with it.
    }

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
    handleServerError('WebSocket server', err);
});

webServer.on('error', (err) => {
    handleServerError(useTls ? 'HTTPS server' : 'HTTP server', err);
});

if (useTls) {
    webServer.on('tlsClientError', () => {
        // Ignore - probes and plain-HTTP requests must not affect the relay.
    });
}

/**
 * A server error before the socket is listening (EADDRINUSE, EACCES,
 * EADDRNOTAVAIL, ...) means the relay never came up. Staying alive in that
 * state is worse than exiting: nothing can connect, yet a process manager,
 * container health check or operator sees a running relay. Fail fast instead,
 * so `Restart=always` retries and a wrapper script notices. Errors after a
 * successful bind stay non-fatal - one broken connection must not take the
 * relay, and everyone playing through it, down.
 */
function handleServerError(what, err) {
    if (listening) {
        console.error(`${what} error: ${err.message}`);
        return;
    }

    if (startupFailed) {
        return;
    }
    startupFailed = true;

    console.error(`Error: ${what} could not listen on ${host}:${port}: ${err.message}`);
    process.exit(1);
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
        webServer.close(done);
    });

    // Do not hang forever if a socket refuses to close.
    setTimeout(done, 5000).unref();
}

process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);
