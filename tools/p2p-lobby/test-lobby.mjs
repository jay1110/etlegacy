#!/usr/bin/env node
/*
 * ET: Legacy
 * Copyright (C) 2012-2024 ET:Legacy team <mail@etlegacy.com>
 *
 * This file is part of ET: Legacy - http://www.etlegacy.com
 *
 * ET: Legacy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ET: Legacy is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ET: Legacy. If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * Protocol-level test for the ET: Legacy p2p lobby server.
 *
 * Starts lobby.js on an ephemeral port and drives it with raw `ws` clients -
 * exactly the wire the browser transport and the shell speak. It asserts the
 * full control protocol, the binary relay path, every hardening rule and the
 * operational contract (banner only after a successful bind, non-zero exit on
 * a failed bind), then cleans up so the process exits on its own.
 *
 * Run with:  npm --prefix tools/p2p-lobby install && node tools/p2p-lobby/test-lobby.mjs
 *
 * License: GPL-3.0-or-later (same as ET: Legacy)
 */

import { spawn } from 'node:child_process';
import net from 'node:net';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
let WebSocket;
try {
	WebSocket = require('ws');
} catch (err) {
	console.error('Cannot load "ws". Run: npm --prefix tools/p2p-lobby install');
	process.exit(2);
}

const HERE = path.dirname(fileURLToPath(import.meta.url));
const LOBBY = path.join(HERE, 'lobby.js');
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

function delay(ms) {
	return new Promise((resolve) => setTimeout(resolve, ms));
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

function connectOnce(port) {
	return new Promise((resolve, reject) => {
		const sock = net.connect(port, '127.0.0.1');
		sock.once('connect', () => { sock.destroy(); resolve(); });
		sock.once('error', (err) => { sock.destroy(); reject(err); });
	});
}

/** Start lobby.js and wait until it reports it is listening. */
async function startLobby(extraArgs = []) {
	const port = await freePort();
	const proc = spawn(process.execPath, [LOBBY, '--host', '127.0.0.1', '--port', String(port), ...extraArgs], {
		stdio: ['ignore', 'pipe', 'pipe']
	});
	cleanups.push(() => proc.kill('SIGKILL'));

	const logLines = [];
	proc.stdout.setEncoding('utf8');
	proc.stderr.setEncoding('utf8');
	proc.stderr.on('data', d => logLines.push(d));

	await deadline(new Promise((resolve, reject) => {
		proc.on('exit', code => reject(new Error('lobby exited early (code ' + code + '): ' + logLines.join(''))));
		proc.stdout.on('data', d => {
			logLines.push(d);
			if (d.includes('Listening on')) {
				resolve();
			}
		});
	}), 'the lobby to start');

	// The banner is printed from the "listening" event, so the port must accept
	// a connection right away - no polling.
	await deadline(connectOnce(port), 'the lobby port to accept a connection right after its banner');
	return { port, proc, log: logLines };
}

/**
 * A buffered ws client: it records every text (parsed JSON) and binary frame
 * and lets a test await the next frame that matches a predicate, so pushes and
 * replies can interleave without races.
 */
class Client {
	constructor(port, urlPath = '/') {
		this.ws = new WebSocket('ws://127.0.0.1:' + port + urlPath);
		this.ws.binaryType = 'arraybuffer';
		this.jsonBuf = [];
		this.binBuf = [];
		this.jsonWaiters = [];
		this.binWaiters = [];
		this.peer = 0;
		cleanups.push(() => { try { this.ws.terminate(); } catch (e) { /* ignore */ } });

		this.ws.on('message', (data, isBinary) => {
			if (isBinary) {
				const u8 = new Uint8Array(data);
				if (!this._match(this.binWaiters, u8)) {
					this.binBuf.push(u8);
				}
				return;
			}
			let obj;
			try { obj = JSON.parse(data.toString()); } catch (e) { return; }
			if (!this._match(this.jsonWaiters, obj)) {
				this.jsonBuf.push(obj);
			}
		});
	}

	_match(waiters, value) {
		for (let i = 0; i < waiters.length; i++) {
			if (waiters[i].pred(value)) {
				const w = waiters.splice(i, 1)[0];
				w.resolve(value);
				return true;
			}
		}
		return false;
	}

	ready() {
		return new Promise((resolve, reject) => {
			// The socket may already be open: this client can be constructed
			// well before ready() is awaited (e.g. while another client does its
			// handshake), so a late listener would miss the 'open' event.
			if (this.ws.readyState === this.ws.OPEN) {
				resolve();
				return;
			}
			this.ws.on('open', resolve);
			this.ws.on('error', reject);
		});
	}

	send(obj) { this.ws.send(JSON.stringify(obj)); }
	sendRaw(text) { this.ws.send(text); }
	sendBinary(u8) { this.ws.send(u8); }

	waitJson(pred, what = 'a json message', ms = TIMEOUT_MS) {
		for (let i = 0; i < this.jsonBuf.length; i++) {
			if (pred(this.jsonBuf[i])) {
				return Promise.resolve(this.jsonBuf.splice(i, 1)[0]);
			}
		}
		return deadline(new Promise((resolve) => {
			this.jsonWaiters.push({ pred, resolve });
		}), what, ms);
	}

	waitType(t, what = t, ms = TIMEOUT_MS) {
		return this.waitJson((m) => m && m.t === t, what || t, ms);
	}

	waitBinary(what = 'a binary frame', ms = TIMEOUT_MS) {
		if (this.binBuf.length) {
			return Promise.resolve(this.binBuf.shift());
		}
		return deadline(new Promise((resolve) => {
			this.binWaiters.push({ pred: () => true, resolve });
		}), what, ms);
	}

	/** Resolve true if nothing (json+bin) arrives within ms, false otherwise. */
	silentFor(ms) {
		const before = this.jsonBuf.length + this.binBuf.length;
		return delay(ms).then(() => (this.jsonBuf.length + this.binBuf.length) === before);
	}

	closed() {
		return new Promise((resolve) => {
			if (this.ws.readyState === this.ws.CLOSED) {
				resolve({ code: this.ws._closeCode || 1000 });
				return;
			}
			this.ws.on('close', (code, reason) => resolve({ code, reason: String(reason) }));
			this.ws.on('error', () => { /* rejected upgrade also surfaces here */ });
		});
	}

	async hello(name = 'Player') {
		await this.ready();
		this.send({ t: 'hello', name, version: 1 });
		const w = await this.waitType('welcome', 'welcome');
		this.peer = w.peer;
		this.ice = w.ice;
		return w;
	}

	close() { try { this.ws.close(); } catch (e) { /* ignore */ } }
}

function u32le(id, payload) {
	const buf = new Uint8Array(4 + payload.length);
	buf[0] = id & 0xff; buf[1] = (id >>> 8) & 0xff; buf[2] = (id >>> 16) & 0xff; buf[3] = (id >>> 24) & 0xff;
	buf.set(payload, 4);
	return buf;
}

function readU32le(u8) {
	return (u8[0] | (u8[1] << 8) | (u8[2] << 16) | (u8[3] << 24)) >>> 0;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

async function testHelloAndIce(port) {
	const a = new Client(port);
	const b = new Client(port);
	const wa = await a.hello('Alice');
	const wb = await b.hello('Bob');

	check(typeof wa.peer === 'number' && wa.peer > 0, 'welcome carries a numeric peer id');
	check(wa.peer !== wb.peer, 'two clients get unique peer ids');
	check(Array.isArray(wa.ice) && wa.ice.length > 0 && !!wa.ice[0].urls, 'welcome delivers ICE servers');
	check(typeof wa.rooms === 'number', 'welcome carries the public room count');

	a.close();
	b.close();
}

async function testHostListAndHttp(port) {
	const host = new Client(port);
	await host.hello('Host');
	host.send({ t: 'host', room: { name: 'My Game', map: 'oasis', maxPlayers: 8, bots: 2, timeLimit: 15 } });
	const hosted = await host.waitType('hosted', 'hosted');
	check(/^[a-z0-9]{6,8}$/.test(hosted.roomId), 'hosted room id is a 6-8 char token');
	check(hosted.room.map === 'oasis' && hosted.room.maxPlayers === 8, 'hosted echoes the normalised room');

	const lister = new Client(port);
	await lister.hello('Lister');
	lister.send({ t: 'list' });
	const listed = await lister.waitType('rooms', 'rooms list');
	check(listed.count === 1, 'list count reflects the single public room');
	const found = listed.rooms.find(r => r.roomId === hosted.roomId);
	check(!!found, 'the hosted room appears in the list');
	check(found && found.private === false && found.created > 0, 'listed room is public and carries a created timestamp');
	check(found && !('hostPeer' in found) && !('joinedPeers' in found), 'listed room leaks no internal fields');

	const res = await fetch('http://127.0.0.1:' + port + '/rooms');
	const json = await res.json();
	check(res.headers.get('content-type').includes('application/json'), 'GET /rooms is application/json');
	check(res.headers.get('access-control-allow-origin') === '*', 'GET /rooms is CORS-open');
	check(json.count === 1 && json.rooms.some(r => r.roomId === hosted.roomId), 'GET /rooms shows the hosted room');

	const health = await fetch('http://127.0.0.1:' + port + '/health');
	check(health.status === 200 && (await health.text()) === 'ok', 'GET /health returns ok');
	const nope = await fetch('http://127.0.0.1:' + port + '/nope');
	check(nope.status === 404, 'unknown HTTP path is 404');

	host.close();
	lister.close();
	// Let the disconnect propagate so later tests start from a clean list.
	await delay(150);
}

async function testPrivateRoomHiddenButJoinable(port) {
	const host = new Client(port);
	await host.hello('SecretHost');
	host.send({ t: 'host', room: { name: 'hush', map: 'radar', maxPlayers: 4, private: true } });
	const hosted = await host.waitType('hosted', 'private hosted');
	check(hosted.room.private === true, 'a private room reports private:true to its host');

	const res = await fetch('http://127.0.0.1:' + port + '/rooms');
	const json = await res.json();
	check(!json.rooms.some(r => r.roomId === hosted.roomId), 'a private room is not listed');

	const joiner = new Client(port);
	await joiner.hello('Guest');
	joiner.send({ t: 'join', roomId: hosted.roomId });
	const joined = await joiner.waitType('joined', 'joined private room');
	check(joined.roomId === hosted.roomId, 'a private room is joinable by id');
	const peerMsg = await host.waitType('peer', 'host sees the joiner');
	check(peerMsg.peer === joiner.peer, 'the host is told the joiner peer id');

	host.close();
	joiner.close();
	await delay(150);
}

async function testUpdateAndSubscribe(port) {
	const sub = new Client(port);
	await sub.hello('Subscriber');
	sub.send({ t: 'subscribe' });
	await sub.waitType('rooms', 'initial subscribe snapshot');

	const host = new Client(port);
	await host.hello('UpHost');
	host.send({ t: 'host', room: { name: 'before', map: 'oasis', maxPlayers: 6 } });
	const hosted = await host.waitType('hosted', 'update host');

	const pushAdd = await sub.waitJson(m => m.t === 'rooms' && m.rooms.some(r => r.roomId === hosted.roomId), 'subscriber add push');
	check(!!pushAdd, 'a subscriber is pushed the new room');

	host.send({ t: 'update', room: { map: 'radar', players: 3 } });
	const updated = await host.waitType('updated', 'updated reply');
	check(updated.room.map === 'radar' && updated.room.players === 3, 'update changes the room fields');
	const pushUpd = await sub.waitJson(m => m.t === 'rooms' && m.rooms.some(r => r.roomId === hosted.roomId && r.map === 'radar'), 'subscriber update push');
	check(!!pushUpd, 'a subscriber is pushed the updated room');

	host.send({ t: 'unhost' });
	await host.waitType('unhosted', 'unhosted reply');
	const pushDel = await sub.waitJson(m => m.t === 'rooms' && !m.rooms.some(r => r.roomId === hosted.roomId), 'subscriber remove push');
	check(!!pushDel, 'a subscriber is pushed the removal on unhost');

	host.close();
	sub.close();
	await delay(150);
}

async function testDisconnectClosesRoom(port) {
	const host = new Client(port);
	await host.hello('DropHost');
	host.send({ t: 'host', room: { name: 'droproom', map: 'oasis', maxPlayers: 4 } });
	const hosted = await host.waitType('hosted', 'drop host');

	const joiner = new Client(port);
	await joiner.hello('DropGuest');
	joiner.send({ t: 'join', roomId: hosted.roomId });
	await joiner.waitType('joined', 'drop joined');
	await host.waitType('peer', 'drop peer');

	// The host vanishes without an unhost - the joiner must be told.
	host.ws.terminate();
	const rc = await joiner.waitType('roomclosed', 'roomclosed on host disconnect');
	check(rc.roomId === hosted.roomId, 'a joiner gets roomclosed when the host disconnects');

	await delay(150);
	const res = await fetch('http://127.0.0.1:' + port + '/rooms');
	const json = await res.json();
	check(!json.rooms.some(r => r.roomId === hosted.roomId), 'the room disappears when the host disconnects');

	joiner.close();
	await delay(100);
}

async function testJoinErrors(port) {
	const c = new Client(port);
	await c.hello('Errorer');
	c.send({ t: 'join', roomId: 'doesnotexist' });
	const e1 = await c.waitType('error', 'noroom error');
	check(e1.code === 'noroom', 'joining an unknown room -> noroom');

	c.send({ t: 'host', room: { name: 'mine', map: 'oasis', maxPlayers: 4 } });
	const hosted = await c.waitType('hosted', 'self host');
	c.send({ t: 'join', roomId: hosted.roomId });
	const e2 = await c.waitType('error', 'self error');
	check(e2.code === 'self', 'joining your own room -> self');
	c.send({ t: 'unhost' });
	await c.waitType('unhosted', 'self unhost');

	// A room that is full from the start: maxPlayers 2 with a bot leaves no slot.
	const fullHost = new Client(port);
	await fullHost.hello('FullHost');
	fullHost.send({ t: 'host', room: { name: 'packed', map: 'oasis', maxPlayers: 2, bots: 1 } });
	const fh = await fullHost.waitType('hosted', 'full host');
	const late = new Client(port);
	await late.hello('Latecomer');
	late.send({ t: 'join', roomId: fh.roomId });
	const e3 = await late.waitType('error', 'full error');
	check(e3.code === 'full', 'joining a full room -> full');

	c.close();
	fullHost.close();
	late.close();
	await delay(150);
}

async function testSignalling(port) {
	const host = new Client(port);
	await host.hello('SigHost');
	host.send({ t: 'host', room: { name: 'sig', map: 'oasis', maxPlayers: 4 } });
	const hosted = await host.waitType('hosted', 'sig host');

	const joiner = new Client(port);
	await joiner.hello('SigGuest');
	joiner.send({ t: 'join', roomId: hosted.roomId });
	await joiner.waitType('joined', 'sig joined');
	await host.waitType('peer', 'sig peer');

	const payload = { kind: 'offer', sdp: 'v=0 test', nested: { a: [1, 2, 3] } };
	joiner.send({ t: 'signal', to: host.peer, data: payload });
	const sig = await host.waitType('signal', 'host receives signal');
	check(sig.from === joiner.peer, 'signal carries the correct sender id');
	check(JSON.stringify(sig.data) === JSON.stringify(payload), 'signal payload is forwarded verbatim');

	// The reverse direction works too.
	host.send({ t: 'signal', to: joiner.peer, data: { kind: 'answer', sdp: 'reply' } });
	const back = await joiner.waitType('signal', 'joiner receives answer');
	check(back.from === host.peer && back.data.kind === 'answer', 'signalling works host -> joiner');

	// An unrelated peer cannot be signalled.
	const stranger = new Client(port);
	await stranger.hello('Stranger');
	joiner.send({ t: 'signal', to: stranger.peer, data: { kind: 'offer' } });
	const err = await joiner.waitType('error', 'signal to stranger error');
	check(err.code === 'nopeer', 'signalling an unrelated peer -> nopeer');
	check(await stranger.silentFor(300), 'the unrelated peer receives nothing');

	host.close();
	joiner.close();
	stranger.close();
	await delay(150);
}

async function testBinaryRelay(port) {
	const host = new Client(port);
	await host.hello('BinHost');
	host.send({ t: 'host', room: { name: 'bin', map: 'oasis', maxPlayers: 4 } });
	const hosted = await host.waitType('hosted', 'bin host');

	const joiner = new Client(port);
	await joiner.hello('BinGuest');
	joiner.send({ t: 'join', roomId: hosted.roomId });
	await joiner.waitType('joined', 'bin joined');
	await host.waitType('peer', 'bin peer');

	// joiner -> host
	const p1 = new Uint8Array([10, 20, 30, 40]);
	joiner.sendBinary(u32le(host.peer, p1));
	const r1 = await host.waitBinary('host receives relayed packet');
	check(readU32le(r1) === joiner.peer, 'relayed packet carries the joiner source id');
	check(r1.length === 4 + p1.length && r1[4] === 10 && r1[7] === 40, 'relayed payload is intact (joiner -> host)');

	// host -> joiner
	const p2 = new Uint8Array([99, 98, 97]);
	host.sendBinary(u32le(joiner.peer, p2));
	const r2 = await joiner.waitBinary('joiner receives relayed packet');
	check(readU32le(r2) === host.peer, 'relayed packet carries the host source id');
	check(r2[4] === 99 && r2[6] === 97, 'relayed payload is intact (host -> joiner)');

	// A packet to an unrelated peer is dropped silently.
	const stranger = new Client(port);
	await stranger.hello('BinStranger');
	joiner.sendBinary(u32le(stranger.peer, new Uint8Array([1, 2, 3])));
	check(await stranger.silentFor(300), 'a relay packet to an unrelated peer is dropped');

	host.close();
	joiner.close();
	stranger.close();
	await delay(150);
}

async function testValidation(port) {
	const host = new Client(port);
	await host.hello('Validator');
	const longName = 'x'.repeat(200);
	host.send({ t: 'host', room: {
		name: longName + '\u0001\u0007',
		map: 'bad map name!!;rm -rf',
		maxPlayers: 999,
		bots: 999,
		timeLimit: -50,
		players: 999,
		private: 'yes'
	} });
	const hosted = await host.waitType('hosted', 'validation host');
	const r = hosted.room;
	check(r.name.length <= 64, 'name is clamped to <= 64 chars (got ' + r.name.length + ')');
	check(!/[\u0000-\u001f\u007f-\u009f]/.test(r.name), 'control characters are stripped from the name');
	check(/^[A-Za-z0-9_-]*$/.test(r.map) && r.map.length > 0, 'map is restricted to a safe charset (got "' + r.map + '")');
	check(r.maxPlayers === 32, 'maxPlayers is clamped to 32');
	check(r.bots === r.maxPlayers - 1, 'bots is clamped to maxPlayers-1 (got ' + r.bots + ')');
	check(r.timeLimit === 0, 'a negative timeLimit is clamped to 0');
	check(r.players >= 0 && r.players <= r.maxPlayers, 'players is clamped into 0..maxPlayers');
	check(r.private === true, 'private is coerced to a boolean');

	// bots >= maxPlayers on a smaller room.
	host.send({ t: 'update', room: { maxPlayers: 4, bots: 10 } });
	const up = await host.waitType('updated', 'validation update');
	check(up.room.bots === 3, 'bots never exceeds maxPlayers-1 after an update (got ' + up.room.bots + ')');

	host.close();
	await delay(150);
}

async function testOversizeAndRate(port) {
	// Oversized JSON is ignored, not fatal.
	const c = new Client(port);
	await c.hello('Big');
	c.sendRaw('{"t":"ping","pad":"' + 'A'.repeat(9000) + '"}');
	check(await c.silentFor(300), 'an oversized JSON control frame is ignored');
	c.send({ t: 'ping' });
	const pong = await c.waitType('pong', 'ping still works after oversized frame');
	check(!!pong, 'the connection survives an oversized JSON frame');

	// Oversized binary is dropped, not fatal.
	const big = new Uint8Array(4 + 16385);
	big[0] = c.peer & 0xff;
	c.sendBinary(big);
	c.send({ t: 'ping' });
	const pong2 = await c.waitType('pong', 'ping still works after oversized binary');
	check(!!pong2, 'the connection survives an oversized binary frame');
	c.close();

	// The control-message rate limit closes an abusive client with 1008.
	const flood = new Client(port);
	await flood.hello('Flood');
	for (let i = 0; i < 80; i++) {
		try { flood.send({ t: 'ping' }); } catch (e) { break; }
	}
	const closed = await deadline(flood.closed(), 'the flooder to be closed');
	check(closed.code === 1008, 'a client past the control-message rate limit is closed with 1008 (got ' + closed.code + ')');
}

async function testMaxConnections() {
	const lobby = await startLobby(['--max-connections', '1']);
	const first = new Client(lobby.port);
	await first.hello('First');
	const second = new Client(lobby.port);
	const closed = await deadline(second.closed(), 'the second connection to be refused');
	check(closed.code === 1013, 'a connection past --max-connections is refused with 1013 (got ' + closed.code + ')');
	first.close();
	lobby.proc.kill('SIGTERM');
}

async function testMaxRooms() {
	const lobby = await startLobby(['--max-rooms', '1']);
	const a = new Client(lobby.port);
	await a.hello('RoomA');
	a.send({ t: 'host', room: { name: 'one', map: 'oasis', maxPlayers: 4 } });
	await a.waitType('hosted', 'first room hosted');

	const b = new Client(lobby.port);
	await b.hello('RoomB');
	b.send({ t: 'host', room: { name: 'two', map: 'oasis', maxPlayers: 4 } });
	const err = await b.waitType('error', 'too many rooms error');
	check(err.code === 'toomanyrooms', 'hosting past --max-rooms -> toomanyrooms');

	a.close();
	b.close();
	lobby.proc.kill('SIGTERM');
}

async function testBindFailureIsFatal(occupiedPort) {
	const proc = spawn(process.execPath, [LOBBY, '--host', '127.0.0.1', '--port', String(occupiedPort)], {
		stdio: ['ignore', 'pipe', 'pipe']
	});
	cleanups.push(() => proc.kill('SIGKILL'));

	let output = '';
	proc.stdout.setEncoding('utf8');
	proc.stderr.setEncoding('utf8');
	proc.stdout.on('data', d => output += d);
	proc.stderr.on('data', d => output += d);

	const code = await deadline(new Promise((resolve) => proc.on('exit', resolve)),
		'the lobby to exit after a failed bind');
	check(code === 1, 'a lobby whose port is taken exits non-zero (got ' + code + ')');
	check(!output.includes('Listening on'), 'a lobby whose port is taken never claims to be listening');
	check(/EADDRINUSE/.test(output), 'the bind failure is reported');
}

async function main() {
	const lobby = await startLobby();

	await testHelloAndIce(lobby.port);
	await testHostListAndHttp(lobby.port);
	await testPrivateRoomHiddenButJoinable(lobby.port);
	await testUpdateAndSubscribe(lobby.port);
	await testDisconnectClosesRoom(lobby.port);
	await testJoinErrors(lobby.port);
	await testSignalling(lobby.port);
	await testBinaryRelay(lobby.port);
	await testValidation(lobby.port);
	await testOversizeAndRate(lobby.port);

	await testMaxConnections();
	await testMaxRooms();
	await testBindFailureIsFatal(lobby.port);

	check(lobby.proc.exitCode === null, 'the lobby is still running at the end');
	lobby.proc.kill('SIGTERM');
}

main().then(() => {
	for (const fn of cleanups) {
		try { fn(); } catch (e) { /* best effort */ }
	}
	if (failures) {
		console.error('\n' + failures + ' check(s) failed.');
		process.exit(1);
	}
	console.log('\nAll lobby checks passed.');
	process.exit(0);
}).catch(err => {
	for (const fn of cleanups) {
		try { fn(); } catch (e) { /* best effort */ }
	}
	console.error('\nTest error: ' + (err && err.stack ? err.stack : err));
	process.exit(1);
});
