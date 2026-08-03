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
 * Integration test for the browser transport src/web/etl-p2p.js against a real
 * lobby.js instance.
 *
 * Node has no RTCPeerConnection, so the transport transparently uses the
 * lobby's binary WebSocket relay for every peer - which is exactly the path we
 * want to exercise without a browser or any game data: a host and a joining
 * client, real packets flowing both ways with the right peer indices, and the
 * "host left" teardown. The transport is loaded with createTransport() so the
 * host and client are fully independent instances, while the browser
 * `window.ETLP2P` singleton keeps working unchanged.
 *
 * Run with:  npm --prefix tools/p2p-lobby install && node tools/p2p-lobby/test-p2p-client.mjs
 *
 * License: GPL-3.0-or-later (same as ET: Legacy)
 */

import { spawn } from 'node:child_process';
import net from 'node:net';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
let ws;
try {
	ws = require('ws');
} catch (err) {
	console.error('Cannot load "ws". Run: npm --prefix tools/p2p-lobby install');
	process.exit(2);
}

// The transport resolves its WebSocket lazily from the global scope, which is
// how the browser provides it. Install the shim before loading the transport.
globalThis.WebSocket = ws;

const HERE = path.dirname(fileURLToPath(import.meta.url));
const LOBBY = path.join(HERE, 'lobby.js');
const CLIENT = path.join(HERE, '..', '..', 'src', 'web', 'etl-p2p.js');
const ETLP2P = require(CLIENT);

const TIMEOUT_MS = 10000;
let failures = 0;
const cleanups = [];

function check(ok, what) {
	console.log((ok ? 'PASS ' : 'FAIL ') + what);
	if (!ok) {
		failures++;
	}
}

function delay(ms) {
	return new Promise((resolve) => setTimeout(resolve, ms));
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
	return { port, proc };
}

/** Wait for a single named event on a transport instance. */
function onceEvent(api, event, ms = TIMEOUT_MS) {
	return deadline(new Promise((resolve) => {
		const handler = (payload) => {
			api.off(event, handler);
			resolve(payload);
		};
		api.on(event, handler);
	}), 'event ' + event, ms);
}

/** Poll receive() until a packet shows up (or time out). */
async function waitReceive(api, what, ms = TIMEOUT_MS) {
	const stop = Date.now() + ms;
	while (Date.now() < stop) {
		const pkt = api.receive();
		if (pkt) {
			return pkt;
		}
		await delay(10);
	}
	throw new Error('timed out waiting for ' + what);
}

function u8(arr) {
	return new Uint8Array(arr);
}

async function main() {
	const lobby = await startLobby();
	const url = 'ws://127.0.0.1:' + lobby.port + '/';

	// A standalone "browser" instance just to prove the room-list plumbing.
	const browser = ETLP2P.createTransport();
	browser.configure({ lobbyUrl: url, playerName: 'Browser' });
	cleanups.push(() => browser.disconnect());

	let subCount = -1;
	let subResolve;
	const sawHostedRoom = new Promise((resolve) => { subResolve = resolve; });
	const unsub = browser.subscribeRooms((rooms, count) => {
		subCount = count;
		if (count >= 1) {
			subResolve(rooms);
		}
	});
	await deadline(browser.connect(), 'the browser to connect');
	await delay(200); // let the initial (empty) snapshot arrive
	check(browser.getRoomCount() === 0, 'getRoomCount is 0 before any room exists');

	// ---- host ----
	const host = ETLP2P.createTransport();
	host.configure({ lobbyUrl: url, playerName: 'HostPlayer', inviteBase: 'https://play.example/etl' });
	cleanups.push(() => host.disconnect());

	check(host.isSupported() === false, 'isSupported() is false in Node (no RTCPeerConnection)');

	await deadline(host.connect(), 'the host to connect');
	const hosted = await deadline(host.host({
		name: 'Node Host', map: 'oasis', maxPlayers: 8, bots: 1, timeLimit: 20, private: false
	}), 'host() to resolve');

	check(/^[a-z0-9]{6,8}$/.test(hosted.roomId), 'host() returns a room id');
	check(hosted.inviteUrl === 'https://play.example/etl?join=' + hosted.roomId, 'host() builds an invite URL with the room id');
	check(hosted.room && hosted.room.map === 'oasis', 'host() returns the normalised room');
	check(host.getRole() === 'host', 'getRole() is host after hosting');
	check(host.getRoomId() === hosted.roomId, 'getRoomId() matches the hosted room');

	// The subscriber must have been pushed the new public room.
	await deadline(sawHostedRoom, 'the browser subscriber to see the room');
	check(subCount >= 1, 'subscribeRooms() delivered a count >= 1 after a room appeared');
	check(browser.getRoomCount() >= 1, 'getRoomCount() reflects the hosted room');
	const listed = await deadline(browser.listRooms(), 'listRooms() to resolve');
	check(listed.some(r => r.roomId === hosted.roomId), 'listRooms() contains the hosted room');
	unsub();

	// ---- client joins ----
	const client = ETLP2P.createTransport();
	client.configure({ lobbyUrl: url, playerName: 'JoinPlayer' });
	cleanups.push(() => client.disconnect());

	await deadline(client.connect(), 'the client to connect');
	const peerJoin = onceEvent(host, 'peerjoin');
	const joined = await deadline(client.join(hosted.roomId), 'join() to resolve');

	check(joined.address === '241.0.0.1:27960', 'join() resolves with the host synthetic address (host is index 1)');
	check(joined.room && joined.room.roomId === hosted.roomId, 'join() returns the joined room');
	check(client.getRole() === 'client', 'getRole() is client after joining');
	check(client.addressForPeer(1) === '241.0.0.1:27960', 'addressForPeer(1) is 241.0.0.1:27960');

	const pj = await peerJoin;
	check(pj.name === 'JoinPlayer', 'the host peerjoin event carries the joiner name');
	check(pj.idx >= 1 && pj.idx <= 250, 'the joiner is assigned a peer index');
	const clientIdxOnHost = pj.idx;

	// ---- packets both ways over the relay fallback ----
	const toHost = u8([1, 2, 3, 4, 5]);
	check(client.send(1, toHost) === true, 'client.send() to the host returns true');
	const gotOnHost = await waitReceive(host, 'the host to receive the client packet');
	check(gotOnHost.peer === clientIdxOnHost, 'the host receives the packet tagged with the joiner index');
	check(gotOnHost.data.length === 5 && gotOnHost.data[0] === 1 && gotOnHost.data[4] === 5, 'the host receives the exact payload');

	const toClient = u8([9, 8, 7]);
	check(host.send(clientIdxOnHost, toClient) === true, 'host.send() to the joiner returns true');
	const gotOnClient = await waitReceive(client, 'the client to receive the host packet');
	check(gotOnClient.peer === 1, 'the client receives the packet tagged with the host index (1)');
	check(gotOnClient.data.length === 3 && gotOnClient.data[0] === 9 && gotOnClient.data[2] === 7, 'the client receives the exact payload');

	// ---- introspection ----
	const hostPeers = host.getPeers();
	check(hostPeers.length === 1 && hostPeers[0].idx === clientIdxOnHost, 'host.getPeers() lists the single joiner');
	check(hostPeers[0].transport === 'relay' && hostPeers[0].open === true, 'the joiner peer is on the relay transport and open');
	check(hostPeers[0].name === 'JoinPlayer', 'host.getPeers() carries the joiner name');
	const clientPeers = client.getPeers();
	check(clientPeers.length === 1 && clientPeers[0].idx === 1, 'client.getPeers() lists the host as index 1');
	check(client.active() === true && host.active() === true, 'active() is true while a match is up');

	// ---- host leaves -> client is thrown out ----
	const clientClosed = onceEvent(client, 'closed');
	host.stopHosting();
	const closedEv = await deadline(clientClosed, 'the client closed event after the host stops hosting');
	check(!!closedEv, 'stopHosting() throws the joined client out (closed event)');
	check(host.getRole() === null, 'the host role clears after stopHosting()');
	await delay(100);
	check(client.getRole() === null && client.active() === false, 'the client role clears after being thrown out');

	// ---- receive-queue cap (drop oldest, count drops) ----
	const capTest = ETLP2P.createTransport();
	const t = capTest._transport;
	for (let i = 0; i < 600; i++) {
		t.enqueue(5, u8([i & 0xff]));
	}
	let drained = 0;
	let first = null;
	let pkt;
	while ((pkt = capTest.receive())) {
		if (drained === 0) {
			first = pkt;
		}
		drained++;
	}
	check(drained === 512, 'the receive queue is capped at 512 packets (got ' + drained + ')');
	check(t.recvDrops === 600 - 512, 'dropped packets are counted (got ' + t.recvDrops + ')');
	check(first && first.data[0] === (600 - 512) & 0xff, 'the oldest packets are the ones dropped');

	// Shut everything down so the process exits on its own.
	browser.disconnect();
	host.disconnect();
	client.disconnect();
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
	console.log('\nAll p2p client checks passed.');
	process.exit(0);
}).catch(err => {
	for (const fn of cleanups) {
		try { fn(); } catch (e) { /* best effort */ }
	}
	console.error('\nTest error: ' + (err && err.stack ? err.stack : err));
	process.exit(1);
});
