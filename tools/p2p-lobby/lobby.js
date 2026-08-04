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
 * ET: Legacy peer-to-peer lobby, WebRTC signalling and fallback-relay server.
 *
 * A player can host a match inside the browser (a listen server) and other
 * players join that browser host over WebRTC data channels. Browsers cannot
 * find each other or exchange the SDP/ICE needed to open a WebRTC connection
 * on their own, so this server does three jobs on one port:
 *
 *   1. Lobby         - keeps the list of open (public) rooms and hands it out.
 *   2. Signalling     - forwards offer/answer/ICE-candidate messages between a
 *                       host and each joining peer so they can build a direct
 *                       WebRTC data channel.
 *   3. Fallback relay - when a direct WebRTC channel cannot be established (a
 *                       symmetric NAT with no TURN server, or simply a runtime
 *                       like Node with no RTCPeerConnection at all), game
 *                       packets are relayed as binary WebSocket frames instead,
 *                       so a match is never completely unreachable.
 *
 * It is deliberately a sibling of tools/ws-relay/relay.js and shares its
 * operational contract: the banner is printed only after a successful bind, a
 * failed bind exits non-zero (so `Restart=always` retries instead of a dead
 * process looking healthy), a single misbehaving connection can never take the
 * server down, and it leans on nothing but Node core plus `ws`.
 *
 * Usage:
 *   node lobby.js [--port 8081] [--host 0.0.0.0]
 *                 [--tls-cert cert.pem --tls-key key.pem]
 *                 [--max-connections 512] [--max-rooms 128]
 *                 [--ice stun:host:port,turn:host:port]
 *                 [--turn-user user] [--turn-pass pass] [--verbose]
 *
 * License: GPL-3.0-or-later (same as ET: Legacy)
 */

'use strict';

const fs = require('fs');
const http = require('http');
const https = require('https');
const crypto = require('crypto');
const { WebSocketServer } = require('ws');

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

const DEFAULT_PORT = 8081;
const DEFAULT_HOST = '0.0.0.0';
const DEFAULT_MAX_CONNECTIONS = 512;
const DEFAULT_MAX_ROOMS = 128;
const DEFAULT_ICE = 'stun:stun.l.google.com:19302';

// Must match MAX_MSGLEN in the engine: a game packet never exceeds this, so a
// binary frame carrying one is dest-id (4 bytes) + at most MAX_MSGLEN payload.
const MAX_MSGLEN = 16384;
const BINARY_HEADER = 4;
const MAX_BINARY = MAX_MSGLEN + BINARY_HEADER;

// A control message is small JSON; anything larger is either a bug or an abuse
// attempt and is dropped without parsing so a client cannot make us allocate.
const MAX_JSON = 8192;

// The ws layer accepts frames up to this size; the finer per-kind limits above
// are enforced in code so an oversized frame is *ignored* rather than closing
// the whole connection (which would also kill in-flight game traffic).
const WS_MAX_PAYLOAD = 65536;

// Room field bounds. Never trust the client - every field is clamped/stripped.
const NAME_MAX = 64;
const MAP_MAX = 64;
// A mod is an fs_game directory name (e.g. "legacy", "xmod").
const MOD_MAX = 32;
const MIN_PLAYERS = 2;
const MAX_PLAYERS = 32;
const MAX_BOTS = 31;
const MAX_TIMELIMIT = 1440;

// Control-message rate limit: a burst of 60 within a 10 s window is plenty for
// a real client (hello, host, a few updates, signalling); more is abuse. The
// binary data path is NOT limited this way - it carries the game itself.
const RATE_WINDOW_MS = 10000;
const RATE_LIMIT = 60;

const HEARTBEAT_INTERVAL_MS = 30000;
const MAX_MISSED_PONGS = 2;

// Coalesce room-list pushes to subscribers: a burst of joins/updates must not
// turn into a burst of full-list broadcasts.
const LIST_PUSH_COALESCE_MS = 250;

const ROOM_ID_MIN = 6;
const ROOM_ID_MAX = 8;

// --- Reclaiming a room after the host reloaded its page --------------------
// A hosted game lives in a browser tab, and reloading that tab (F5, or the
// page restarting itself to change the map) drops the host's WebSocket. The
// room is therefore not destroyed right away: it is kept "paused" for a short
// grace period during which only the original host - proving itself with the
// token it got when it created the room - can take it over again, so the
// invite link, the room id and the players stay valid across the reload.
// Joiners are told to wait (`hostaway`) and to reconnect (`hostback`) instead
// of being thrown out. When the grace period expires the room is closed for
// good, exactly as if the host had left.
const RECLAIM_GRACE_MS = 60000;
const HOST_TOKEN_BYTES = 16;

// ---------------------------------------------------------------------------
// Command line / environment configuration
// ---------------------------------------------------------------------------

const args = process.argv.slice(2);

function envInt(name, fallback) {
	const v = process.env[name];
	if (v === undefined) {
		return fallback;
	}
	const n = parseInt(v, 10);
	return isNaN(n) ? fallback : n;
}

let port = envInt('ETL_LOBBY_PORT', DEFAULT_PORT);
let host = process.env.ETL_LOBBY_HOST || DEFAULT_HOST;
let tlsCert = process.env.ETL_LOBBY_TLS_CERT || null;
let tlsKey = process.env.ETL_LOBBY_TLS_KEY || null;
let maxConnections = envInt('ETL_LOBBY_MAX_CONNECTIONS', DEFAULT_MAX_CONNECTIONS);
let maxRooms = envInt('ETL_LOBBY_MAX_ROOMS', DEFAULT_MAX_ROOMS);
let reclaimGraceMs = envInt('ETL_LOBBY_RECLAIM_MS', RECLAIM_GRACE_MS);
let iceSpec = process.env.ETL_LOBBY_ICE || DEFAULT_ICE;
let turnUser = process.env.ETL_LOBBY_TURN_USER || null;
let turnPass = process.env.ETL_LOBBY_TURN_PASS || null;
let verbose = false;

function usage() {
	console.log('ET: Legacy peer-to-peer lobby / signalling / fallback-relay server');
	console.log('');
	console.log('Usage: node lobby.js [options]');
	console.log('');
	console.log('Options:');
	console.log('  --port <port>            Listen port (default: 8081)');
	console.log('  --host <host>            Listen host (default: 0.0.0.0)');
	console.log('  --tls-cert <file>        TLS certificate (PEM) to serve wss://');
	console.log('  --tls-key <file>         TLS private key (PEM) to serve wss://');
	console.log('  --max-connections <n>    Connection limit (default: 512)');
	console.log('  --max-rooms <n>          Hosted-room limit (default: 128)');
	console.log('  --reclaim-ms <ms>        How long a room survives its host reloading its');
	console.log('                           page, so the same host can take it over again');
	console.log('                           (default: ' + RECLAIM_GRACE_MS + ', 0 disables it)');
	console.log('  --ice <list>             Comma separated stun:/turn: URLs advertised to clients');
	console.log('                           (default: ' + DEFAULT_ICE + ')');
	console.log('  --turn-user <user>       Username applied to turn: URLs');
	console.log('  --turn-pass <pass>       Credential applied to turn: URLs');
	console.log('  --verbose                Log every control message');
	console.log('  --help                   Show this help');
	console.log('');
	console.log('Provide both --tls-cert and --tls-key to accept secure wss:// (required');
	console.log('from HTTPS pages). Otherwise the server serves plain ws:// and http://.');
	process.exit(0);
}

for (let i = 0; i < args.length; i++) {
	const a = args[i];
	if (a === '--port' && args[i + 1]) {
		port = parseInt(args[++i], 10);
	} else if (a === '--host' && args[i + 1]) {
		host = args[++i];
	} else if (a === '--tls-cert' && args[i + 1]) {
		tlsCert = args[++i];
	} else if (a === '--tls-key' && args[i + 1]) {
		tlsKey = args[++i];
	} else if (a === '--max-connections' && args[i + 1]) {
		maxConnections = parseInt(args[++i], 10);
	} else if (a === '--max-rooms' && args[i + 1]) {
		maxRooms = parseInt(args[++i], 10);
	} else if (a === '--reclaim-ms' && args[i + 1]) {
		reclaimGraceMs = parseInt(args[++i], 10);
	} else if (a === '--ice' && args[i + 1]) {
		iceSpec = args[++i];
	} else if (a === '--turn-user' && args[i + 1]) {
		turnUser = args[++i];
	} else if (a === '--turn-pass' && args[i + 1]) {
		turnPass = args[++i];
	} else if (a === '--verbose') {
		verbose = true;
	} else if (a === '--help' || a === '-h') {
		usage();
	} else {
		console.error('Error: unknown or incomplete option: ' + a);
		process.exit(1);
	}
}

if (isNaN(port) || port < 0 || port > 65535) {
	console.error('Error: --port must be a number in 0..65535.');
	process.exit(1);
}
if (isNaN(maxConnections) || maxConnections < 1) {
	console.error('Error: --max-connections must be a number >= 1.');
	process.exit(1);
}
if (isNaN(maxRooms) || maxRooms < 1) {
	console.error('Error: --max-rooms must be a number >= 1.');
	process.exit(1);
}
if (isNaN(reclaimGraceMs) || reclaimGraceMs < 0) {
	console.error('Error: --reclaim-ms must be a number >= 0.');
	process.exit(1);
}
if ((tlsCert && !tlsKey) || (!tlsCert && tlsKey)) {
	console.error('Error: --tls-cert and --tls-key must be provided together.');
	process.exit(1);
}
const useTls = Boolean(tlsCert && tlsKey);

/**
 * Build the array of RTCIceServer objects handed to every client in `welcome`.
 * TURN URLs get the shared credential (browsers cannot be told a TURN
 * username/password any other way) - see the README for why TURN is what makes
 * peer-to-peer work behind carrier-grade/symmetric NAT.
 */
function buildIceServers(spec, user, pass) {
	const servers = [];
	String(spec).split(',').forEach(function (raw) {
		const url = raw.trim();
		if (!url) {
			return;
		}
		if (/^turns?:/i.test(url) && user) {
			servers.push({ urls: url, username: user, credential: pass || '' });
		} else {
			servers.push({ urls: url });
		}
	});
	return servers;
}

const iceServers = buildIceServers(iceSpec, turnUser, turnPass);

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

const peers = new Map();       // peerId -> connection
const rooms = new Map();       // roomId -> room record
const subscribers = new Set(); // connections that asked for room-list pushes
let peerIdCounter = 0;

const stats = {
	connections: 0,
	roomsCreated: 0,
	binaryForwarded: 0,
	binaryDropped: 0,
	signalsForwarded: 0
};

function log() {
	if (verbose) {
		console.log.apply(console, arguments);
	}
}

// ---------------------------------------------------------------------------
// Validation / normalisation - the client is never trusted
// ---------------------------------------------------------------------------

function clampInt(value, lo, hi, fallback) {
	let n = typeof value === 'number' ? value : parseInt(value, 10);
	if (typeof value === 'boolean' || isNaN(n) || !isFinite(n)) {
		n = fallback;
	}
	n = Math.floor(n);
	if (n < lo) {
		n = lo;
	}
	if (n > hi) {
		n = hi;
	}
	return n;
}

/** Strip control/non-printable characters and trim; cap to `max` code units. */
function cleanString(value, max, fallback) {
	if (typeof value !== 'string') {
		if (value === undefined || value === null) {
			return fallback;
		}
		value = String(value);
	}
	// Remove C0 controls, DEL and the C1 range so a room name can never carry
	// terminal escapes, NULs or line breaks into anyone else's UI or logs.
	let s = value.replace(/[\u0000-\u001f\u007f-\u009f]/g, '').trim();
	if (s.length > max) {
		s = s.slice(0, max);
	}
	return s.length ? s : fallback;
}

/**
 * Compare a client-supplied token with the stored one without leaking its
 * length or a byte-by-byte match through timing.
 */
function safeTokenEqual(a, b) {
	const ab = Buffer.from(String(a));
	const bb = Buffer.from(String(b));
	if (ab.length !== bb.length) {
		return false;
	}
	return crypto.timingSafeEqual(ab, bb);
}

/**
 * Produce a fully normalised room from arbitrary client input, merged onto an
 * optional existing room (for `update`, where only a subset of fields is sent).
 * Every field is clamped/stripped so nothing a client sends can be trusted or
 * leak through to other players.
 */
function normaliseRoom(input, base) {
	input = (input && typeof input === 'object') ? input : {};
	base = base || {};

	const name = cleanString(
		input.name !== undefined ? input.name : base.name,
		NAME_MAX, base.name !== undefined ? base.name : 'ETL Host');

	// The map name becomes part of a command line in the engine, so it is
	// restricted to a safe identifier charset rather than merely trimmed.
	let map = cleanString(
		input.map !== undefined ? input.map : base.map,
		MAP_MAX, base.map !== undefined ? base.map : 'unknown');
	map = map.replace(/[^A-Za-z0-9_-]/g, '');
	if (!map.length) {
		map = base.map || 'unknown';
	}

	// Which mod the game runs (its fs_game directory). Joiners need it to put
	// the same game logic in place before they connect, so it travels with the
	// room; like the map name it ends up in a command line and a file path, so
	// it is restricted to the same safe identifier charset.
	let mod = cleanString(
		input.mod !== undefined ? input.mod : base.mod,
		MOD_MAX, base.mod !== undefined ? base.mod : 'legacy');
	mod = mod.replace(/[^A-Za-z0-9_-]/g, '');
	if (!mod.length) {
		mod = base.mod || 'legacy';
	}

	const maxPlayers = clampInt(
		input.maxPlayers !== undefined ? input.maxPlayers : base.maxPlayers,
		MIN_PLAYERS, MAX_PLAYERS, base.maxPlayers !== undefined ? base.maxPlayers : MAX_PLAYERS);

	// Bots can never fill the last slot that keeps the host in its own game, so
	// they are additionally capped to maxPlayers-1 after the absolute clamp.
	let bots = clampInt(
		input.bots !== undefined ? input.bots : base.bots,
		0, MAX_BOTS, base.bots !== undefined ? base.bots : 0);
	if (bots > maxPlayers - 1) {
		bots = maxPlayers - 1;
	}

	const timeLimit = clampInt(
		input.timeLimit !== undefined ? input.timeLimit : base.timeLimit,
		0, MAX_TIMELIMIT, base.timeLimit !== undefined ? base.timeLimit : 0);

	let players;
	if (input.players !== undefined) {
		players = clampInt(input.players, 0, maxPlayers, 1);
	} else if (base.players !== undefined) {
		players = clampInt(base.players, 0, maxPlayers, 1);
	} else {
		players = clampInt(1 + bots, 0, maxPlayers, 1);
	}

	let isPrivate;
	if (input.private !== undefined) {
		isPrivate = Boolean(input.private);
	} else if (base.private !== undefined) {
		isPrivate = Boolean(base.private);
	} else {
		isPrivate = false;
	}

	return {
		name: name,
		map: map,
		mod: mod,
		maxPlayers: maxPlayers,
		bots: bots,
		timeLimit: timeLimit,
		players: players,
		private: isPrivate
	};
}

/**
 * The wire representation of a room. `listing` records (the public list, GET
 * /rooms) never carry `private:true` because private rooms are simply not
 * listed; either way no host address is ever included.
 */
function roomRecord(room, listing) {
	return {
		roomId: room.roomId,
		name: room.name,
		map: room.map,
		mod: room.mod,
		players: room.players,
		maxPlayers: room.maxPlayers,
		bots: room.bots,
		timeLimit: room.timeLimit,
		private: listing ? false : room.private,
		// True while the host is reloading its page: the room is still there
		// (and keeps its id) but cannot be joined for a moment.
		paused: Boolean(room.paused),
		created: room.created
	};
}

function publicRoomList() {
	const list = [];
	rooms.forEach(function (room) {
		if (!room.private) {
			list.push(roomRecord(room, true));
		}
	});
	return list;
}

function publicRoomCount() {
	let n = 0;
	rooms.forEach(function (room) {
		if (!room.private) {
			n++;
		}
	});
	return n;
}

function generateRoomId() {
	const chars = 'abcdefghijklmnopqrstuvwxyz0123456789';
	for (let attempt = 0; attempt < 1000; attempt++) {
		const len = ROOM_ID_MIN + (crypto.randomBytes(1)[0] % (ROOM_ID_MAX - ROOM_ID_MIN + 1));
		const bytes = crypto.randomBytes(len);
		let id = '';
		for (let i = 0; i < len; i++) {
			id += chars[bytes[i] % chars.length];
		}
		if (!rooms.has(id)) {
			return id;
		}
	}
	return null;
}

// ---------------------------------------------------------------------------
// Sending helpers
// ---------------------------------------------------------------------------

function sendJson(conn, obj) {
	if (!conn || conn.closed) {
		return;
	}
	const ws = conn.ws;
	if (ws.readyState !== ws.OPEN) {
		return;
	}
	try {
		ws.send(JSON.stringify(obj));
	} catch (err) {
		// A dead socket must never throw out of a message handler.
	}
}

function sendError(conn, code, message) {
	sendJson(conn, { t: 'error', code: code, message: message || code });
}

// Coalesced room-list push to all subscribers.
let listPushTimer = null;
function scheduleListPush() {
	if (listPushTimer) {
		return;
	}
	listPushTimer = setTimeout(function () {
		listPushTimer = null;
		const list = publicRoomList();
		const payload = { t: 'rooms', rooms: list, count: list.length };
		subscribers.forEach(function (conn) {
			sendJson(conn, payload);
		});
	}, LIST_PUSH_COALESCE_MS);
	if (listPushTimer.unref) {
		listPushTimer.unref();
	}
}

// ---------------------------------------------------------------------------
// Room / peer relationship management
// ---------------------------------------------------------------------------

/** Occupancy used only for the join "full" check (host + bots + joined peers). */
function isRoomFull(room) {
	return (1 + room.bots + room.joinedPeers.size) >= room.maxPlayers;
}

function linkPeers(a, b) {
	a.partners.add(b.id);
	b.partners.add(a.id);
}

function unlinkPeers(a, b) {
	if (a) {
		a.partners.delete(b.id);
	}
	if (b) {
		b.partners.delete(a.id);
	}
}

/** Remove the room a connection hosts, evicting and notifying every joiner. */
function removeHostedRoom(conn) {
	if (!conn.roomId) {
		return;
	}
	const room = rooms.get(conn.roomId);
	conn.roomId = null;
	if (!room) {
		return;
	}
	closeRoom(room, conn);
}

/** Close a room for good: evict its joiners and forget it. */
function closeRoom(room, hostConn) {
	rooms.delete(room.roomId);
	if (room.reclaimTimer) {
		clearTimeout(room.reclaimTimer);
		room.reclaimTimer = null;
	}

	room.joinedPeers.forEach(function (peerId) {
		const joiner = peers.get(peerId);
		if (joiner) {
			joiner.joinedRoomId = null;
			if (hostConn) {
				unlinkPeers(joiner, hostConn);
			}
			sendJson(joiner, { t: 'roomclosed', roomId: room.roomId });
		}
	});
	room.joinedPeers.clear();

	if (!room.private) {
		scheduleListPush();
	}
}

/**
 * The host's connection went away without unhosting - it is most likely
 * reloading its page. Keep the room alive (paused) for the grace period so the
 * same host can reclaim it with its token, and tell the joiners to hold on
 * instead of dropping them.
 */
function pauseHostedRoom(conn) {
	if (!conn.roomId) {
		return;
	}
	const room = rooms.get(conn.roomId);
	conn.roomId = null;
	if (!room) {
		return;
	}
	if (reclaimGraceMs <= 0) {
		closeRoom(room, conn);
		return;
	}

	room.paused = true;
	room.hostPeer = 0;
	room.joinedPeers.forEach(function (peerId) {
		const joiner = peers.get(peerId);
		if (joiner) {
			// The signalling link dies with the connection; the joiner
			// re-joins (and re-negotiates) once the host is back.
			unlinkPeers(joiner, conn);
			sendJson(joiner, { t: 'hostaway', roomId: room.roomId, grace: reclaimGraceMs });
		}
	});

	room.reclaimTimer = setTimeout(function () {
		room.reclaimTimer = null;
		if (rooms.get(room.roomId) === room && room.paused) {
			log('room ' + room.roomId + ' not reclaimed, closing');
			closeRoom(room, null);
		}
	}, reclaimGraceMs);
	if (room.reclaimTimer.unref) {
		room.reclaimTimer.unref();
	}

	log('room ' + room.roomId + ' paused, reclaimable for ' + reclaimGraceMs + ' ms');
	if (!room.private) {
		scheduleListPush();
	}
}

/** Detach a joiner from the room it joined, telling the host it left. */
function leaveJoinedRoom(conn) {
	if (!conn.joinedRoomId) {
		return;
	}
	const room = rooms.get(conn.joinedRoomId);
	conn.joinedRoomId = null;
	if (!room) {
		return;
	}
	room.joinedPeers.delete(conn.id);
	const hostConn = peers.get(room.hostPeer);
	if (hostConn) {
		unlinkPeers(conn, hostConn);
		sendJson(hostConn, { t: 'peerleft', peer: conn.id });
	}
}

// ---------------------------------------------------------------------------
// Control message handling
// ---------------------------------------------------------------------------

function handleControl(conn, msg) {
	if (!msg || typeof msg !== 'object' || typeof msg.t !== 'string') {
		sendError(conn, 'badrequest', 'malformed control message');
		return;
	}

	switch (msg.t) {
	case 'hello': {
		conn.name = cleanString(msg.name, NAME_MAX, 'Player');
		sendJson(conn, {
			t: 'welcome',
			peer: conn.id,
			ice: iceServers,
			rooms: publicRoomCount()
		});
		break;
	}

	case 'host': {
		if (rooms.size >= maxRooms && !conn.roomId) {
			sendError(conn, 'toomanyrooms', 'server room limit reached');
			return;
		}
		// One room per connection: re-hosting replaces the previous room.
		if (conn.roomId) {
			removeHostedRoom(conn);
		}
		const norm = normaliseRoom(msg.room, null);
		const roomId = generateRoomId();
		if (!roomId) {
			sendError(conn, 'toomanyrooms', 'could not allocate a room id');
			return;
		}
		const room = {
			roomId: roomId,
			hostPeer: conn.id,
			name: norm.name,
			map: norm.map,
			mod: norm.mod,
			maxPlayers: norm.maxPlayers,
			bots: norm.bots,
			timeLimit: norm.timeLimit,
			players: norm.players,
			private: norm.private,
			created: Date.now(),
			joinedPeers: new Set(),
			// Secret handed out to the host only. It is what lets the same
			// host take this room over again after its page reloaded, and it
			// is never part of a room record sent to anybody else.
			hostToken: crypto.randomBytes(HOST_TOKEN_BYTES).toString('hex'),
			paused: false,
			reclaimTimer: null
		};
		rooms.set(roomId, room);
		conn.roomId = roomId;
		stats.roomsCreated++;
		sendJson(conn, {
			t: 'hosted',
			roomId: roomId,
			hostToken: room.hostToken,
			room: roomRecord(room, false)
		});
		if (!room.private) {
			scheduleListPush();
		}
		break;
	}

	case 'update': {
		if (!conn.roomId) {
			sendError(conn, 'noroom', 'not hosting a room');
			return;
		}
		const room = rooms.get(conn.roomId);
		if (!room) {
			conn.roomId = null;
			sendError(conn, 'noroom', 'not hosting a room');
			return;
		}
		const wasPrivate = room.private;
		const norm = normaliseRoom(msg.room, room);
		room.name = norm.name;
		room.map = norm.map;
		room.mod = norm.mod;
		room.maxPlayers = norm.maxPlayers;
		room.bots = norm.bots;
		room.timeLimit = norm.timeLimit;
		room.players = norm.players;
		room.private = norm.private;
		sendJson(conn, { t: 'updated', room: roomRecord(room, false) });
		// The public list changes if the room is (or just became/left being) public.
		if (!room.private || !wasPrivate) {
			scheduleListPush();
		}
		break;
	}

	case 'reclaim': {
		// A host whose page reloaded takes its own room over again. The room
		// survived the disconnect (see pauseHostedRoom) and the token proves
		// this really is the host that created it - everything else about the
		// room, above all its id and therefore its invite link, stays as it
		// was.
		const roomId = typeof msg.roomId === 'string' ? msg.roomId : '';
		const token = typeof msg.hostToken === 'string' ? msg.hostToken : '';
		const room = rooms.get(roomId);
		if (!room || !token) {
			sendError(conn, 'noroom', 'no such room');
			return;
		}
		if (!room.hostToken || !safeTokenEqual(token, room.hostToken)) {
			sendError(conn, 'badtoken', 'not the host of this room');
			return;
		}
		// One room per connection, and a room has one host: drop whatever
		// either side was still attached to.
		if (conn.roomId && conn.roomId !== roomId) {
			removeHostedRoom(conn);
		}
		const previous = peers.get(room.hostPeer);
		if (previous && previous !== conn) {
			previous.roomId = null;
		}
		if (room.reclaimTimer) {
			clearTimeout(room.reclaimTimer);
			room.reclaimTimer = null;
		}
		room.hostPeer = conn.id;
		room.paused = false;
		conn.roomId = room.roomId;
		// The host may come back with changed settings (a different map, for
		// instance); merge whatever it sent onto the room it left behind.
		if (msg.room) {
			const norm = normaliseRoom(msg.room, room);
			room.name = norm.name;
			room.map = norm.map;
			room.mod = norm.mod;
			room.maxPlayers = norm.maxPlayers;
			room.bots = norm.bots;
			room.timeLimit = norm.timeLimit;
			room.players = norm.players;
			room.private = norm.private;
		}
		log('room ' + room.roomId + ' reclaimed by [' + conn.id + ']');
		sendJson(conn, {
			t: 'hosted',
			roomId: room.roomId,
			hostToken: room.hostToken,
			reclaimed: true,
			room: roomRecord(room, false)
		});
		// Everybody who was in the game is told to reconnect: the host is a
		// new peer now, so the old links are gone and have to be set up again.
		room.joinedPeers.forEach(function (peerId) {
			const joiner = peers.get(peerId);
			if (joiner) {
				joiner.joinedRoomId = null;
				sendJson(joiner, { t: 'hostback', roomId: room.roomId });
			}
		});
		room.joinedPeers.clear();
		if (!room.private) {
			scheduleListPush();
		}
		break;
	}

	case 'unhost': {
		removeHostedRoom(conn);
		sendJson(conn, { t: 'unhosted' });
		break;
	}

	case 'list': {
		const list = publicRoomList();
		sendJson(conn, { t: 'rooms', rooms: list, count: list.length });
		break;
	}

	case 'subscribe': {
		if (!conn.subscribed) {
			conn.subscribed = true;
			subscribers.add(conn);
		}
		// Deliver the current state immediately so a fresh subscriber is not
		// blank until the next change.
		const list = publicRoomList();
		sendJson(conn, { t: 'rooms', rooms: list, count: list.length });
		break;
	}

	case 'unsubscribe': {
		conn.subscribed = false;
		subscribers.delete(conn);
		break;
	}

	case 'join': {
		const roomId = typeof msg.roomId === 'string' ? msg.roomId : '';
		const room = rooms.get(roomId);
		if (!room) {
			sendError(conn, 'noroom', 'no such room');
			return;
		}
		if (room.hostPeer === conn.id) {
			sendError(conn, 'self', 'cannot join your own room');
			return;
		}
		if (room.paused) {
			// The host is reloading its page; there is nothing to connect to
			// until it is back (see pauseHostedRoom).
			sendError(conn, 'hostaway', 'the host is reloading, try again in a moment');
			return;
		}
		if (room.joinedPeers.has(conn.id)) {
			// Idempotent re-join: just re-send the joined record.
			sendJson(conn, { t: 'joined', roomId: room.roomId, host: room.hostPeer, room: roomRecord(room, false) });
			return;
		}
		if (isRoomFull(room)) {
			sendError(conn, 'full', 'room is full');
			return;
		}
		const hostConn = peers.get(room.hostPeer);
		if (!hostConn) {
			// Host vanished but room lingered - clean it up defensively.
			rooms.delete(room.roomId);
			sendError(conn, 'noroom', 'host is gone');
			return;
		}
		// A joiner can only be in one room at a time.
		leaveJoinedRoom(conn);

		room.joinedPeers.add(conn.id);
		conn.joinedRoomId = room.roomId;
		linkPeers(conn, hostConn);

		sendJson(conn, { t: 'joined', roomId: room.roomId, host: room.hostPeer, room: roomRecord(room, false) });
		sendJson(hostConn, { t: 'peer', peer: conn.id, name: conn.name });
		break;
	}

	case 'signal': {
		const to = peers.get(msg.to);
		if (!to || !conn.partners.has(msg.to)) {
			sendError(conn, 'nopeer', 'no signalling relationship with that peer');
			return;
		}
		sendJson(to, { t: 'signal', from: conn.id, data: msg.data });
		stats.signalsForwarded++;
		break;
	}

	case 'bye': {
		const to = peers.get(msg.to);
		if (to) {
			sendJson(to, { t: 'peerleft', peer: conn.id });
			unlinkPeers(conn, to);
		}
		break;
	}

	case 'ping': {
		sendJson(conn, { t: 'pong' });
		break;
	}

	default:
		sendError(conn, 'badrequest', 'unknown message type: ' + msg.t);
		break;
	}
}

/**
 * Forward a binary game packet along an established host<->joiner link. The
 * data path is intentionally not rate limited (it carries the match), only
 * size limited, and packets between peers with no join relationship are
 * dropped silently so the relay cannot be used as a generic mailbox.
 */
function handleBinary(conn, buffer) {
	if (buffer.length < BINARY_HEADER || buffer.length > MAX_BINARY) {
		stats.binaryDropped++;
		return;
	}
	const dest = buffer.readUInt32LE(0);
	if (!conn.partners.has(dest)) {
		stats.binaryDropped++;
		return;
	}
	const target = peers.get(dest);
	if (!target || target.closed || target.ws.readyState !== target.ws.OPEN) {
		stats.binaryDropped++;
		return;
	}
	// Rewrite the header from destination id to source id in place-ish: we
	// build a fresh frame so the payload is copied exactly once.
	const out = Buffer.allocUnsafe(buffer.length);
	out.writeUInt32LE(conn.id, 0);
	buffer.copy(out, BINARY_HEADER, BINARY_HEADER);
	try {
		target.ws.send(out, { binary: true });
		stats.binaryForwarded++;
	} catch (err) {
		stats.binaryDropped++;
	}
}

// ---------------------------------------------------------------------------
// Connection lifecycle
// ---------------------------------------------------------------------------

function cleanupConnection(conn) {
	if (conn.closed) {
		return;
	}
	conn.closed = true;

	peers.delete(conn.id);
	subscribers.delete(conn);

	// A peer disappearing does not destroy its room right away: a host whose
	// page is reloading gets a grace period to reclaim it (see
	// pauseHostedRoom). Joiners are detached as usual.
	pauseHostedRoom(conn);
	leaveJoinedRoom(conn);

	// Break any lingering signalling links (e.g. via bye without a join).
	conn.partners.forEach(function (peerId) {
		const other = peers.get(peerId);
		if (other) {
			other.partners.delete(conn.id);
		}
	});
	conn.partners.clear();
}

function onConnection(ws, req) {
	if (peers.size >= maxConnections) {
		try {
			ws.close(1013, 'Server is full');
		} catch (err) { /* already gone */ }
		return;
	}

	try {
		req.socket.setNoDelay(true);
	} catch (err) { /* socket already gone */ }

	const conn = {
		id: ++peerIdCounter,
		ws: ws,
		name: 'Player',
		closed: false,
		isAlive: true,
		missedPongs: 0,
		roomId: null,        // room this connection hosts
		joinedRoomId: null,  // room this connection joined
		partners: new Set(), // peer ids with an active signalling/data link
		subscribed: false,
		msgTimes: []
	};
	peers.set(conn.id, conn);
	stats.connections++;

	log('[' + conn.id + '] connected from ' + (req.socket.remoteAddress || '?'));

	ws.on('message', function (data, isBinary) {
		if (conn.closed) {
			return;
		}

		// `ws` reports frame type via isBinary. Text frames are control JSON,
		// binary frames are the game-packet fallback path.
		if (isBinary) {
			const buf = Buffer.isBuffer(data) ? data : Buffer.from(data);
			if (buf.length > MAX_BINARY) {
				stats.binaryDropped++;
				return;
			}
			try {
				handleBinary(conn, buf);
			} catch (err) {
				// A single malformed packet must never break the connection.
			}
			return;
		}

		// Text control message.
		const text = data.toString();
		if (text.length > MAX_JSON) {
			// Oversized control frame - ignore without parsing.
			return;
		}
		if (rateLimited(conn)) {
			try {
				ws.close(1008, 'rate limit exceeded');
			} catch (err) { /* ignore */ }
			return;
		}
		let obj;
		try {
			obj = JSON.parse(text);
		} catch (err) {
			sendError(conn, 'badrequest', 'invalid JSON');
			return;
		}
		try {
			if (verbose) {
				log('[' + conn.id + '] <- ' + (obj && obj.t));
			}
			handleControl(conn, obj);
		} catch (err) {
			// Defensive: never let a handler bug close the whole server.
			console.error('control handler error: ' + (err && err.stack ? err.stack : err));
			sendError(conn, 'badrequest', 'internal error');
		}
	});

	ws.on('pong', function () {
		conn.isAlive = true;
		conn.missedPongs = 0;
	});

	ws.on('close', function () {
		cleanupConnection(conn);
		log('[' + conn.id + '] disconnected');
	});

	ws.on('error', function () {
		cleanupConnection(conn);
	});
}

/** Sliding-window rate limiter; returns true when the burst limit is exceeded. */
function rateLimited(conn) {
	const now = Date.now();
	const cutoff = now - RATE_WINDOW_MS;
	// Drop timestamps that fell out of the window.
	while (conn.msgTimes.length && conn.msgTimes[0] < cutoff) {
		conn.msgTimes.shift();
	}
	conn.msgTimes.push(now);
	return conn.msgTimes.length > RATE_LIMIT;
}

// ---------------------------------------------------------------------------
// HTTP endpoints (same port): /rooms, /health
// ---------------------------------------------------------------------------

function handleHttpRequest(req, res) {
	// Only the two documented read-only endpoints exist; everything else is a
	// 404 so the server never doubles as an open proxy or file server.
	//
	// The endpoint is the last path segment, so the lobby answers behind a
	// reverse proxy that keeps its own location prefix in the forwarded path
	// as well (nginx's `proxy_pass http://127.0.0.1:8081;` without a trailing
	// slash forwards "/p2p-lobby/rooms" unchanged). WebSocket upgrades are not
	// affected by this - they are accepted on any path.
	const url = req.url || '/';
	const pathOnly = url.split('?')[0];
	const endpoint = pathOnly.split('/').filter(function (part) {
		return part.length > 0;
	}).pop() || '';

	if (req.method === 'GET' && endpoint === 'rooms') {
		const list = publicRoomList();
		const body = JSON.stringify({ count: list.length, rooms: list });
		res.writeHead(200, {
			'Content-Type': 'application/json',
			'Access-Control-Allow-Origin': '*',
			'Cache-Control': 'no-store'
		});
		res.end(body);
		return;
	}

	if (req.method === 'GET' && endpoint === 'health') {
		res.writeHead(200, { 'Content-Type': 'text/plain', 'Access-Control-Allow-Origin': '*' });
		res.end('ok');
		return;
	}

	res.writeHead(404, { 'Content-Type': 'text/plain' });
	res.end('not found');
}

// ---------------------------------------------------------------------------
// Server setup
// ---------------------------------------------------------------------------

let httpServer;
if (useTls) {
	let creds;
	try {
		creds = { cert: fs.readFileSync(tlsCert), key: fs.readFileSync(tlsKey) };
	} catch (err) {
		console.error('Error: could not read TLS cert/key: ' + err.message);
		process.exit(1);
	}
	httpServer = https.createServer(creds, handleHttpRequest);
} else {
	httpServer = http.createServer(handleHttpRequest);
}

const wss = new WebSocketServer({
	server: httpServer,
	maxPayload: WS_MAX_PAYLOAD,
	perMessageDeflate: false
});

wss.on('connection', onConnection);
wss.on('error', function (err) {
	handleServerError('WebSocket server', err);
});

const scheme = useTls ? 'wss' : 'ws';
let listening = false;
let startupFailed = false;

httpServer.on('listening', function () {
	listening = true;
	const bound = httpServer.address();
	const boundPort = bound && bound.port ? bound.port : port;
	console.log('ET: Legacy peer-to-peer lobby server');
	console.log('Listening on ' + scheme + '://' + host + ':' + boundPort);
	console.log('HTTP status:  http' + (useTls ? 's' : '') + '://' + host + ':' + boundPort + '/rooms');
	console.log('Max connections: ' + maxConnections + ', max rooms: ' + maxRooms);
	console.log('ICE servers advertised: ' + iceServers.map(function (s) { return s.urls; }).join(', '));
	console.log('');
});

httpServer.on('error', function (err) {
	handleServerError('HTTP server', err);
});

if (useTls) {
	httpServer.on('tlsClientError', function () {
		// Probes and plain-HTTP hits on the TLS port must not affect the server.
	});
}

/**
 * A failure before the socket is listening (EADDRINUSE, EACCES, ...) means the
 * server never came up. Exiting non-zero is better than a dead process that
 * looks healthy to systemd - `Restart=always` then retries. relay.js once had
 * the opposite bug (it printed its banner and kept running after EADDRINUSE);
 * this must never do that.
 */
function handleServerError(what, err) {
	if (listening) {
		console.error(what + ' error: ' + err.message);
		return;
	}
	if (startupFailed) {
		return;
	}
	startupFailed = true;
	console.error('Error: ' + what + ' could not listen on ' + host + ':' + port + ': ' + err.message);
	process.exit(1);
}

httpServer.listen(port, host);

// ---------------------------------------------------------------------------
// Heartbeat - drop peers whose connection vanished without a close frame
// ---------------------------------------------------------------------------

const heartbeatTimer = setInterval(function () {
	peers.forEach(function (conn) {
		if (conn.missedPongs >= MAX_MISSED_PONGS) {
			try {
				conn.ws.terminate();
			} catch (err) { /* ignore */ }
			cleanupConnection(conn);
			return;
		}
		conn.missedPongs++;
		try {
			conn.ws.ping();
		} catch (err) {
			cleanupConnection(conn);
		}
	});
}, HEARTBEAT_INTERVAL_MS);
if (heartbeatTimer.unref) {
	heartbeatTimer.unref();
}

// ---------------------------------------------------------------------------
// Robustness + shutdown
// ---------------------------------------------------------------------------

process.on('uncaughtException', function (err) {
	console.error('Uncaught exception (server keeps running): ' + (err && err.stack ? err.stack : err));
});

process.on('unhandledRejection', function (reason) {
	console.error('Unhandled rejection (server keeps running): ' + reason);
});

let shuttingDown = false;
function shutdown() {
	if (shuttingDown) {
		return;
	}
	shuttingDown = true;
	console.log('\nShutting down lobby server...');
	clearInterval(heartbeatTimer);
	if (listPushTimer) {
		clearTimeout(listPushTimer);
	}

	peers.forEach(function (conn) {
		try {
			conn.ws.close(1001, 'server shutting down');
		} catch (err) { /* ignore */ }
	});

	const done = function () {
		console.log('Lobby server stopped.');
		process.exit(0);
	};
	wss.close(function () {
		httpServer.close(done);
	});
	setTimeout(done, 5000).unref();
}

process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);
