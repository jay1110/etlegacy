#!/usr/bin/env node
/**
 * End-to-end test for the ET: Legacy WebSocket-to-UDP relay.
 *
 * Starts a fake UDP game server that answers ET's out-of-band `getinfo`
 * query, starts relay.js in front of it, and drives it through a real
 * WebSocket client - the same path a browser client takes:
 *
 *   WebSocket client  ->  relay.js  ->  UDP "game server"
 *                     <-           <-
 *
 * Run with:  npm --prefix tools/ws-relay install && node tools/ws-relay/test-relay.mjs
 *
 * License: GPL-3.0 (same as ET: Legacy)
 */

import { spawn } from 'node:child_process';
import dgram from 'node:dgram';
import net from 'node:net';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
let WebSocket;
try {
	WebSocket = require('ws');
} catch (err) {
	console.error('Cannot load "ws". Run: npm --prefix tools/ws-relay install');
	process.exit(2);
}

const HERE = path.dirname(fileURLToPath(import.meta.url));
const RELAY = path.join(HERE, 'relay.js');
const OOB = Buffer.from([0xff, 0xff, 0xff, 0xff]);
const TIMEOUT_MS = 10000;

let failures = 0;
const cleanups = [];

function check(ok, what) {
	console.log((ok ? 'PASS ' : 'FAIL ') + what);
	if (!ok) {
		failures++;
	}
}

function deadline(promise, what, ms = TIMEOUT_MS) {
	let timer;
	return Promise.race([
		promise.finally(() => clearTimeout(timer)),
		new Promise((_, reject) => {
			timer = setTimeout(() => reject(new Error('timed out waiting for ' + what)), ms);
		})
	]);
}

/** A free TCP port. Racy in principle, good enough for a local test. */
function freePort() {
	return new Promise((resolve, reject) => {
		const srv = net.createServer();
		srv.on('error', reject);
		srv.listen(0, '127.0.0.1', () => {
			const p = srv.address().port;
			srv.close(() => resolve(p));
		});
	});
}

/**
 * A minimal stand-in for a dedicated server: replies to the out-of-band
 * `getinfo <challenge>` with an `infoResponse` carrying the same challenge,
 * which is exactly the exchange a client uses to ping a server.
 */
function startFakeServer() {
	return new Promise((resolve, reject) => {
		const sock = dgram.createSocket('udp4');
		const received = [];

		sock.on('error', reject);
		sock.on('message', (msg, rinfo) => {
			received.push(msg);
			if (!msg.subarray(0, 4).equals(OOB)) {
				return;
			}
			const text = msg.subarray(4).toString('latin1');
			const m = /^getinfo\s+(\S+)/.exec(text);
			if (!m) {
				return;
			}
			const reply = Buffer.concat([
				OOB,
				Buffer.from('infoResponse\n\\challenge\\' + m[1] +
					'\\hostname\\test\\mapname\\oasis\\clients\\0', 'latin1')
			]);
			sock.send(reply, rinfo.port, rinfo.address);
		});

		sock.bind(0, '127.0.0.1', () => {
			const port = sock.address().port;
			cleanups.push(() => sock.close());
			resolve({ port, received });
		});
	});
}

/** Start relay.js and wait until it reports it is listening. */
async function startRelay(extraArgs = []) {
	const port = await freePort();
	const proc = spawn(process.execPath, [RELAY, '--host', '127.0.0.1', '--port', String(port), ...extraArgs], {
		stdio: ['ignore', 'pipe', 'pipe']
	});
	cleanups.push(() => proc.kill('SIGKILL'));

	const log = [];
	proc.stdout.setEncoding('utf8');
	proc.stderr.setEncoding('utf8');
	proc.stderr.on('data', d => log.push(d));

	await deadline(new Promise((resolve, reject) => {
		proc.on('exit', code => reject(new Error('relay exited early (code ' + code + '): ' + log.join(''))));
		proc.stdout.on('data', d => {
			log.push(d);
			if (d.includes('Listening on')) {
				resolve();
			}
		});
	}), 'the relay to start');

	// The banner is printed synchronously, before the listening socket is
	// actually bound, so wait for the port to accept connections.
	await deadline(waitForPort(port), 'the relay port to accept connections');

	return { port, proc, log };
}

/** Poll a TCP port until it accepts a connection. */
function waitForPort(port) {
	return new Promise((resolve, reject) => {
		let attempts = 0;
		const attempt = () => {
			const sock = net.connect(port, '127.0.0.1');
			sock.once('connect', () => { sock.destroy(); resolve(); });
			sock.once('error', () => {
				sock.destroy();
				if (++attempts > 200) {
					reject(new Error('port ' + port + ' never accepted a connection'));
					return;
				}
				setTimeout(attempt, 25);
			});
		};
		attempt();
	});
}

function open(relayPort, urlPath) {
	const ws = new WebSocket('ws://127.0.0.1:' + relayPort + urlPath);
	ws.binaryType = 'nodebuffer';
	cleanups.push(() => ws.terminate());
	return ws;
}

function opened(ws) {
	return new Promise((resolve, reject) => {
		ws.on('open', resolve);
		ws.on('error', reject);
	});
}

function nextMessage(ws) {
	return new Promise((resolve, reject) => {
		ws.once('message', resolve);
		ws.once('error', reject);
		ws.once('close', (code) => reject(new Error('closed (' + code + ') before a message arrived')));
	});
}

function closedWith(ws) {
	return new Promise((resolve) => {
		ws.once('close', (code, reason) => resolve({ code, reason: String(reason) }));
		ws.once('error', () => { /* a rejected upgrade also surfaces as an error */ });
	});
}

function getinfo(challenge) {
	return Buffer.concat([OOB, Buffer.from('getinfo ' + challenge, 'latin1')]);
}

/** One round trip through the relay: send getinfo, expect the matching infoResponse. */
async function roundTrip(relayPort, urlPath, challenge, what) {
	const ws = open(relayPort, urlPath);
	await deadline(opened(ws), 'the WebSocket to open');
	// Sent immediately: the relay has to queue it until its UDP socket is
	// bound (and the hostname resolved) instead of dropping it.
	ws.send(getinfo(challenge));
	const reply = await deadline(nextMessage(ws), 'the infoResponse');
	const text = Buffer.from(reply).toString('latin1');
	check(Buffer.from(reply).subarray(0, 4).equals(OOB), what + ': reply is an out-of-band packet');
	check(text.includes('infoResponse'), what + ': reply is an infoResponse');
	check(text.includes('\\challenge\\' + challenge), what + ': reply carries the challenge back');
	ws.close();
}

async function main() {
	const server = await startFakeServer();
	const relay = await startRelay();

	await roundTrip(relay.port, '/127.0.0.1:' + server.port, 'aaa111', 'path form');
	await roundTrip(relay.port, '/?target=127.0.0.1:' + server.port, 'bbb222', 'query form');
	// The browser build cannot resolve names itself and passes them here.
	await roundTrip(relay.port, '/localhost:' + server.port, 'ccc333', 'hostname form');

	// Several sequential packets on one connection keep working (the UDP
	// socket and its queue are reused).
	{
		const ws = open(relay.port, '/127.0.0.1:' + server.port);
		await deadline(opened(ws), 'the WebSocket to open');
		let ok = true;
		for (let i = 0; i < 5; i++) {
			ws.send(getinfo('seq' + i));
			const reply = await deadline(nextMessage(ws), 'infoResponse ' + i);
			ok = ok && Buffer.from(reply).toString('latin1').includes('\\challenge\\seq' + i);
		}
		check(ok, 'five packets in a row are all relayed in order');
		ws.close();
	}

	// A syntactically invalid target must be refused, not silently dropped.
	for (const [urlPath, what] of [
		['/', 'empty target'],
		['/127.0.0.1', 'target without a port'],
		['/127.0.0.1:0', 'target with port 0'],
		['/127.0.0.1:70000', 'target with an out-of-range port'],
		['/999.1.1.1:27960', 'target with an out-of-range octet'],
		['/?target=' + encodeURIComponent('bad host:27960'), 'target with an invalid hostname']
	]) {
		const ws = open(relay.port, urlPath);
		const closed = await deadline(closedWith(ws), 'the relay to reject ' + what);
		check(closed.code === 1008, what + ' is rejected with close code 1008 (got ' + closed.code + ')');
	}

	// The connection limit is enforced.
	{
		const limited = await startRelay(['--max-connections', '1']);
		const first = open(limited.port, '/127.0.0.1:' + server.port);
		await deadline(opened(first), 'the first connection');
		const second = open(limited.port, '/127.0.0.1:' + server.port);
		const closed = await deadline(closedWith(second), 'the second connection to be refused');
		check(closed.code === 1013, 'a connection past --max-connections is refused with 1013 (got ' + closed.code + ')');
		first.close();
		limited.proc.kill('SIGTERM');
	}

	check(server.received.length > 0, 'the fake game server actually received UDP traffic');

	// The relay survived all of it.
	check(relay.proc.exitCode === null, 'the relay is still running at the end');
	relay.proc.kill('SIGTERM');
}

main().then(() => {
	for (const fn of cleanups) {
		try { fn(); } catch (err) { /* best effort */ }
	}
	if (failures) {
		console.error('\n' + failures + ' check(s) failed.');
		process.exit(1);
	}
	console.log('\nAll relay checks passed.');
	process.exit(0);
}).catch(err => {
	for (const fn of cleanups) {
		try { fn(); } catch (e) { /* best effort */ }
	}
	console.error('\nTest error: ' + (err && err.stack ? err.stack : err));
	process.exit(1);
});
