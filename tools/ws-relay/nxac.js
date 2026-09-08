/* ET: Legacy - GPL-3.0-or-later. NxAC streams bound to a game WebSocket. */
'use strict';
const dgram = require('dgram');
const net = require('net');
const crypto = require('crypto');
const MAX_CHUNK = 16384;
const MAX_QUEUE = 128 * 1024;
const MAX_TOTAL = 8 * 1024 * 1024 + 4096;
const advertisements = new Map();

function advertisedPort(target) {
    const key = `${target.address}:${target.port}`;
    const prior = advertisements.get(key);
    if (prior && prior.expires > Date.now()) return prior.promise;
    // Bound cache and deduplicate simultaneous requests behind the same relay.
    if (advertisements.size >= 128) advertisements.delete(advertisements.keys().next().value);
    const entry = { expires: Date.now() + 5000 };
    entry.promise = new Promise((resolve, reject) => {
        const nonce = crypto.randomBytes(16).toString('hex');
        const socket = dgram.createSocket('udp4');
        let done = false;
        const finish = (err, port) => {
            if (done) return;
            done = true; clearTimeout(timer);
            try { socket.close(); } catch (_) {}
            if (err) { advertisements.delete(key); reject(err); }
            else resolve(port);
        };
        const timer = setTimeout(() => finish(new Error('status-timeout')), 3000);
        socket.on('error', () => finish(new Error('status-failed')));
        socket.on('message', (data, peer) => {
            if (peer.address !== target.address || peer.port !== target.port || data.length > 65507) return;
            const prefix = Buffer.from('\xff\xff\xff\xffstatusResponse\n', 'latin1');
            if (!data.subarray(0, prefix.length).equals(prefix)) return;
            const line = data.subarray(prefix.length).toString('latin1').split('\n', 1)[0];
            const parts = line.split('\\'); const info = Object.create(null);
            for (let i = 1; i + 1 < parts.length; i += 2) {
                if (Object.prototype.hasOwnProperty.call(info, parts[i])) return;
                info[parts[i]] = parts[i + 1];
            }
            if (info.challenge !== nonce) return;
            const port = Number(info.nport);
            if (String(info.gamename).toLowerCase() !== 'nitmod' ||
                !/^\d{1,5}$/.test(info.nport || '') || port < 1 || port > 65535) {
                finish(new Error('not-nitmod-listener')); return;
            }
            finish(null, port);
        });
        socket.bind(0, () => {
            if (done) return;
            const query = Buffer.from(`\xff\xff\xff\xffgetstatus ${nonce}\n`, 'latin1');
            socket.send(query, target.port, target.address, err => {
                if (err) finish(new Error('status-send-failed'));
            });
        });
    });
    advertisements.set(key, entry);
    return entry.promise;
}

class NxACRelay {
    constructor(connection) {
        this.connection = connection;
        this.stream = null;
        this.closed = false;
        this.lastOpen = 0;
        this.send('nxac1 ready');
    }
    send(text) {
        const ws = this.connection.ws;
        if (this.closed || this.connection.closed || ws.readyState !== ws.OPEN || ws.bufferedAmount > MAX_QUEUE) return false;
        try { ws.send(text, { binary: false }, () => {}); return true; }
        catch (_) { return false; }
    }
    finish(stream, reason, notify = true) {
        if (!stream || stream.closed) return;
        stream.closed = true;
        clearInterval(stream.timer);
        if (stream.socket) stream.socket.destroy();
        if (this.stream === stream) this.stream = null;
        if (notify) this.send(`nxac1 ${reason ? 'error' : 'eof'} ${stream.id}${reason ? ' ' + reason : ''}`);
    }
    close() { this.closed = true; this.finish(this.stream, '', false); }
    async open(id, port) {
        const conn = this.connection;
        if (this.closed || conn.closed) return;
        if (this.stream || Date.now() - this.lastOpen < 1000 || !conn.sequenced ||
            !conn.receivedSequenced || !conn.resolved || !conn.udpReady) {
            this.send(`nxac1 error ${id} game-session-not-ready`); return;
        }
        this.lastOpen = Date.now();
        const stream = { id, closed: false, socket: null, start: Date.now(), progress: Date.now(), sent: 0, received: 0 };
        this.stream = stream;
        stream.timer = setInterval(() => {
            if (Date.now() - stream.start > 60000 || Date.now() - stream.progress > 10000) this.finish(stream, 'timeout');
        }, 1000);
        try {
            const published = await advertisedPort(conn.target);
            if (stream.closed || this.closed) return;
            if (published !== port) { this.finish(stream, 'unadvertised-port'); return; }
            // The peer IP comes exclusively from this WebSocket's resolved game target.
            const socket = net.createConnection({ host: conn.target.address, port });
            stream.socket = socket;
            socket.setNoDelay(true);
            socket.on('connect', () => {
                if (stream.closed) return;
                stream.progress = Date.now();
                if (!this.send(`nxac1 opened ${id} ${socket.localPort}`)) this.finish(stream, 'backpressure');
            });
            socket.on('data', data => {
                stream.received += data.length; stream.progress = Date.now();
                if (stream.received > MAX_TOTAL) { this.finish(stream, 'receive-limit'); return; }
                for (let i = 0; i < data.length; i += MAX_CHUNK) {
                    if (!this.send(`nxac1 data ${id} ${data.subarray(i, i + MAX_CHUNK).toString('base64')}`)) {
                        this.finish(stream, 'backpressure'); return;
                    }
                }
            });
            socket.on('end', () => this.finish(stream, ''));
            socket.on('error', () => this.finish(stream, 'tcp-error'));
            socket.on('close', () => this.finish(stream, ''));
        } catch (_) { this.finish(stream, 'server-verification-failed'); }
    }
    onText(data) {
        if (data.length > 22000) return;
        const text = data.toString('utf8');
        if (!text.startsWith('nxac1 ')) return;
        const fields = text.split(' ');
        if (fields.length < 3 || !/^[1-9][0-9]{0,8}$/.test(fields[2])) return;
        const id = Number(fields[2]);
        if (fields[1] === 'open' && fields.length === 4 && /^\d{1,5}$/.test(fields[3])) {
            const port = Number(fields[3]);
            if (port > 0 && port <= 65535) this.open(id, port);
            return;
        }
        const stream = this.stream;
        if (!stream || stream.id !== id || stream.closed) return;
        if (fields[1] === 'close' && fields.length === 3) { this.finish(stream, '', false); return; }
        if (fields[1] !== 'data' || fields.length !== 4 || !stream.socket || stream.socket.connecting ||
            !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(fields[3])) {
            this.finish(stream, 'invalid-data'); return;
        }
        const bytes = Buffer.from(fields[3], 'base64');
        if (!bytes.length || bytes.length > MAX_CHUNK || bytes.toString('base64') !== fields[3] ||
            stream.sent + bytes.length > MAX_TOTAL || stream.socket.writableLength + bytes.length > MAX_QUEUE) {
            this.finish(stream, 'send-limit'); return;
        }
        stream.sent += bytes.length; stream.progress = Date.now();
        try { stream.socket.write(bytes); } catch (_) { this.finish(stream, 'tcp-write-failed'); }
    }
}
module.exports = { NxACRelay };
