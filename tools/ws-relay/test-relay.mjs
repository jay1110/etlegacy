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
 * Covered: both URL forms, a proxied path prefix, hostname targets (the relay's
 * DNS lookup), packets sent before the UDP socket is bound, several packets in
 * a row, two clients on one server at the same time (own UDP port each, no
 * cross-talk), the "only the target may answer" filter, malformed targets, the
 * connection limit, the idle-connection reaper and a failed bind.
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
 *
 * Every datagram is recorded with the source address the relay sent it from,
 * so a test can tell two relayed clients apart and push a packet back to one
 * of them specifically - the way a real server addresses each connected
 * player.
 */
function startFakeServer() {
	return new Promise((resolve, reject) => {
		const sock = dgram.createSocket('udp4');
		const received = [];

		sock.on('error', reject);
		sock.on('message', (msg, rinfo) => {
			received.push({ msg, address: rinfo.address, port: rinfo.port });
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
			resolve({
				port,
				received,
				/** Push an unsolicited packet to one relayed client. */
				sendTo(client, buffer) {
					sock.send(buffer, client.port, client.address);
				}
			});
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

	// The banner is printed from the server's "listening" event, so the port
	// has to accept a connection right away - no polling. The relay used to
	// announce itself synchronously, before (and even without) a successful
	// bind, which made the banner meaningless.
	await deadline(connectOnce(port), 'the relay port to accept a connection right after its banner');

	return { port, proc, log };
}

/** Open one TCP connection, without retrying. */
function connectOnce(port) {
	return new Promise((resolve, reject) => {
		const sock = net.connect(port, '127.0.0.1');
		sock.once('connect', () => { sock.destroy(); resolve(); });
		sock.once('error', (err) => { sock.destroy(); reject(err); });
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
		if (ws.readyState === ws.CLOSED) {
			resolve({ code: ws._closeCode || 1000, reason: '' });
			return;
		}
		ws.once('close', (code, reason) => resolve({ code, reason: String(reason) }));
		ws.once('error', () => { /* a rejected upgrade also surfaces as an error */ });
	});
}

function getinfo(challenge) {
	return Buffer.concat([OOB, Buffer.from('getinfo ' + challenge, 'latin1')]);
}

function oob(text) {
	return Buffer.concat([OOB, Buffer.from(text, 'latin1')]);
}

function text(buffer) {
	return Buffer.from(buffer).toString('latin1');
}

/** Resolve to false if a message arrives within `ms`, true if none does. */
function noMessageWithin(ws, ms) {
	return new Promise((resolve) => {
		const onMessage = () => { cleanup(); resolve(false); };
		const cleanup = () => {
			clearTimeout(timer);
			ws.off('message', onMessage);
		};
		const timer = setTimeout(() => { cleanup(); resolve(true); }, ms);
		ws.on('message', onMessage);
	});
}

/** One round trip through the relay: send getinfo, expect the matching infoResponse. */
async function roundTrip(relayPort, urlPath, challenge, what) {
	const ws = open(relayPort, urlPath);
	await deadline(opened(ws), 'the WebSocket to open');
	// Sent immediately: the relay has to queue it until its UDP socket is
	// bound (and the hostname resolved) instead of dropping it.
	ws.send(getinfo(challenge));
	const reply = await deadline(nextMessage(ws), 'the infoResponse');
	check(Buffer.from(reply).subarray(0, 4).equals(OOB), what + ': reply is an out-of-band packet');
	check(text(reply).includes('infoResponse'), what + ': reply is an infoResponse');
	check(text(reply).includes('\\challenge\\' + challenge), what + ': reply carries the challenge back');
	ws.close();
}

/**
 * Two clients on one game server at the same time - the relay half of "two
 * players join from their own browser". Each connection must get its own UDP
 * socket, and traffic must never cross over between them: the relay is the
 * only thing that knows which browser a datagram belongs to, because from the
 * game server's point of view both players share the relay's IP.
 */
async function testTwoClientsAtOnce(relay, server) {
	const a = open(relay.port, '/127.0.0.1:' + server.port);
	const b = open(relay.port, '/127.0.0.1:' + server.port);

	await deadline(Promise.all([opened(a), opened(b)]), 'both WebSockets to open');

	const before = server.received.length;

	a.send(getinfo('playerA'));
	b.send(getinfo('playerB'));

	const [replyA, replyB] = await deadline(
		Promise.all([nextMessage(a), nextMessage(b)]),
		'both infoResponses'
	);

	check(text(replyA).includes('\\challenge\\playerA'), 'two clients: the first client gets its own reply');
	check(text(replyB).includes('\\challenge\\playerB'), 'two clients: the second client gets its own reply');

	const seen = server.received.slice(before);
	const fromA = seen.find(p => text(p.msg).includes('playerA'));
	const fromB = seen.find(p => text(p.msg).includes('playerB'));

	check(Boolean(fromA && fromB), 'two clients: the server saw both requests');
	check(Boolean(fromA && fromB) && fromA.port !== fromB.port,
		'two clients: the relay uses a separate UDP source port per client');

	// A server-initiated packet (a snapshot, in a real match) must reach only
	// the player it was addressed to.
	server.sendTo(fromA, oob('print\nfor the first client'));

	const pushed = await deadline(nextMessage(a), 'the server push to the first client');
	check(text(pushed).includes('for the first client'), 'two clients: a server push reaches the addressed client');
	check(await noMessageWithin(b, 300), 'two clients: a server push does not leak to the other client');

	// One player leaving must not disturb the other.
	a.close();
	await deadline(closedWith(a), 'the first client to close');

	b.send(getinfo('stillhere'));
	const afterClose = await deadline(nextMessage(b), 'the second client to still work');
	check(text(afterClose).includes('\\challenge\\stillhere'),
		'two clients: the remaining client keeps working after the other disconnects');

	b.close();
}

/**
 * Datagrams from anywhere but the requested game server are dropped. Without
 * this, anyone who can guess the relay's ephemeral UDP port could inject
 * packets into a player's session.
 */
async function testForeignSourceIsDropped(relay, server) {
	const ws = open(relay.port, '/127.0.0.1:' + server.port);
	await deadline(opened(ws), 'the WebSocket to open');

	const before = server.received.length;
	ws.send(getinfo('spoof'));
	await deadline(nextMessage(ws), 'the infoResponse');

	const mine = server.received.slice(before).find(p => text(p.msg).includes('spoof'));
	check(Boolean(mine), 'spoof filter: the relay port used for this client is known');

	const intruder = dgram.createSocket('udp4');
	cleanups.push(() => { try { intruder.close(); } catch (err) { /* already closed */ } });
	await new Promise((resolve) => intruder.bind(0, '127.0.0.1', resolve));
	intruder.send(oob('disconnect'), mine.port, '127.0.0.1');

	check(await noMessageWithin(ws, 300), 'spoof filter: a datagram from another source is not forwarded');

	ws.send(getinfo('afterspoof'));
	const reply = await deadline(nextMessage(ws), 'the connection to still work after the spoofed packet');
	check(text(reply).includes('\\challenge\\afterspoof'), 'spoof filter: the connection survives the spoofed packet');

	intruder.close();
	ws.close();
}

/** An idle connection is reaped, and the relay survives it. */
async function testIdleTimeout(server) {
	const relay = await startRelay(['--timeout', '5']);
	const ws = open(relay.port, '/127.0.0.1:' + server.port);
	await deadline(opened(ws), 'the WebSocket to open');

	// The reaper runs every 5s, so an idle connection goes away after 5-10s.
	const closed = await deadline(closedWith(ws), 'the idle connection to be closed', 20000);
	check(closed.code === 1000, 'an idle connection is closed by the relay (got ' + closed.code + ')');
	check(relay.proc.exitCode === null, 'the relay survives reaping an idle connection');

	relay.proc.kill('SIGTERM');
}

/**
 * A relay that cannot bind must fail loudly. It used to print "Listening on
 * ..." and then keep running after EADDRINUSE, so a restarted-too-early relay
 * looked healthy to systemd/pm2 while relaying nothing at all.
 */
async function testBindFailureIsFatal(occupiedPort) {
	const proc = spawn(process.execPath, [RELAY, '--host', '127.0.0.1', '--port', String(occupiedPort)], {
		stdio: ['ignore', 'pipe', 'pipe']
	});
	cleanups.push(() => proc.kill('SIGKILL'));

	let output = '';
	proc.stdout.setEncoding('utf8');
	proc.stderr.setEncoding('utf8');
	proc.stdout.on('data', d => output += d);
	proc.stderr.on('data', d => output += d);

	const code = await deadline(new Promise((resolve) => proc.on('exit', resolve)),
		'the relay to exit after a failed bind');

	check(code === 1, 'a relay whose port is taken exits non-zero (got ' + code + ')');
	check(!output.includes('Listening on'), 'a relay whose port is taken never claims to be listening');
	check(/EADDRINUSE/.test(output), 'the bind failure is reported');
}

async function main() {
	const server = await startFakeServer();
	const relay = await startRelay();

	await roundTrip(relay.port, '/127.0.0.1:' + server.port, 'aaa111', 'path form');
	await roundTrip(relay.port, '/?target=127.0.0.1:' + server.port, 'bbb222', 'query form');
	// The browser build cannot resolve names itself and passes them here.
	await roundTrip(relay.port, '/localhost:' + server.port, 'ccc333', 'hostname form');
	// Behind a reverse proxy that does not strip its own location prefix
	// (nginx "proxy_pass http://127.0.0.1:8080;" without a trailing slash),
	// the target arrives as the last segment of a longer path.
	await roundTrip(relay.port, '/ws-relay/127.0.0.1:' + server.port, 'ddd444', 'proxied path form');

	// Several sequential packets on one connection keep working (the UDP
	// socket and its queue are reused).
	{
		const ws = open(relay.port, '/127.0.0.1:' + server.port);
		await deadline(opened(ws), 'the WebSocket to open');
		let ok = true;
		for (let i = 0; i < 5; i++) {
			ws.send(getinfo('seq' + i));
			const reply = await deadline(nextMessage(ws), 'infoResponse ' + i);
			ok = ok && text(reply).includes('\\challenge\\seq' + i);
		}
		check(ok, 'five packets in a row are all relayed in order');
		ws.close();
	}

	await testTwoClientsAtOnce(relay, server);
	await testForeignSourceIsDropped(relay, server);

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

	await testBindFailureIsFatal(relay.port);
	await testIdleTimeout(server);

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
