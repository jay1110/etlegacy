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
 * ET: Legacy browser peer-to-peer transport + lobby client.
 *
 * This is the browser side of the "host a game inside the browser" feature. It
 * talks to tools/p2p-lobby/lobby.js to browse/host rooms and to exchange the
 * WebRTC signalling needed to open a direct data channel to every peer, and it
 * presents the engine (src/qcommon/net_web.c, through EM_ASM) with a tiny
 * send/receive packet interface keyed by *peer index*, which the engine turns
 * into synthetic 241.0.x.y IPv4 addresses.
 *
 * Two transports are used per peer, transparently:
 *   - a direct WebRTC RTCDataChannel (unreliable/unordered, like UDP), and
 *   - the lobby's binary WebSocket relay as a fallback whenever WebRTC cannot
 *     be established (no TURN through a symmetric NAT, or a runtime with no
 *     RTCPeerConnection at all, e.g. Node during tests).
 * A join is considered usable as soon as *either* transport works, so a
 * player is never stuck waiting for ICE.
 *
 * The file is intentionally plain ES5 (no modules, no bundler) so it can be
 * dropped straight into src/web/shell.html. When loaded under Node (for the
 * tests) it also exports the same object plus a createTransport() factory that
 * yields independent instances; the browser `window.ETLP2P` singleton keeps
 * working exactly as documented either way.
 *
 * License: GPL-3.0-or-later (same as ET: Legacy)
 */

(function (global) {
	'use strict';

	// Synthetic address scheme shared with the engine: a remote peer index
	// 1..250 maps to 241.0.<(idx>>8)&0xff>.<idx&0xff>:27960. 241/8 is reserved
	// space, so it can never collide with a real server the player might use.
	var ADDR_PREFIX = 241;
	var PORT = 27960;
	var MAX_PEER_INDEX = 250;

	var RECV_QUEUE_CAP = 512;      // packets buffered for the engine before drop-oldest
	var MAX_MSGLEN = 16384;        // engine MAX_MSGLEN - payload hard cap
	var RTC_OPEN_TIMEOUT_MS = 8000; // give ICE this long before we stop hoping
	var RECONNECT_BASE_MS = 500;
	var RECONNECT_MAX_MS = 8000;

	var DATA_CHANNEL_LABEL = 'etl';

	// -----------------------------------------------------------------------
	// Environment shims - resolved lazily so a test can install globals after
	// this script is loaded but before connect() is called.
	// -----------------------------------------------------------------------

	function getWebSocket() {
		if (typeof WebSocket !== 'undefined') {
			return WebSocket;
		}
		if (global && global.WebSocket) {
			return global.WebSocket;
		}
		return null;
	}

	function getRTC() {
		if (typeof RTCPeerConnection !== 'undefined') {
			return RTCPeerConnection;
		}
		if (typeof window !== 'undefined' && window.RTCPeerConnection) {
			return window.RTCPeerConnection;
		}
		if (typeof window !== 'undefined' && window.webkitRTCPeerConnection) {
			return window.webkitRTCPeerConnection;
		}
		if (global && global.RTCPeerConnection) {
			return global.RTCPeerConnection;
		}
		return null;
	}

	// -----------------------------------------------------------------------
	// Little-endian uint32 helpers for the binary relay framing.
	// -----------------------------------------------------------------------

	function writeU32LE(u8, off, value) {
		u8[off] = value & 0xff;
		u8[off + 1] = (value >>> 8) & 0xff;
		u8[off + 2] = (value >>> 16) & 0xff;
		u8[off + 3] = (value >>> 24) & 0xff;
	}

	function readU32LE(u8, off) {
		return (u8[off] | (u8[off + 1] << 8) | (u8[off + 2] << 16) | (u8[off + 3] << 24)) >>> 0;
	}

	function toU8(data) {
		if (data instanceof Uint8Array) {
			return data;
		}
		if (data instanceof ArrayBuffer) {
			return new Uint8Array(data);
		}
		if (data && data.buffer instanceof ArrayBuffer) {
			return new Uint8Array(data.buffer, data.byteOffset || 0, data.byteLength);
		}
		return new Uint8Array(0);
	}

	// -----------------------------------------------------------------------
	// Transport instance
	// -----------------------------------------------------------------------

	function Transport() {
		this.config = {
			lobbyUrl: defaultLobbyUrl(),
			iceServers: null,
			playerName: 'Player',
			inviteBase: defaultInviteBase()
		};

		this.ws = null;
		this.peerId = 0;          // our own lobby peer id (from welcome)
		this.iceServers = [];     // advertised by the lobby, or overridden by config
		this.connected = false;   // welcome received
		this.connecting = null;   // in-flight connect() promise

		this.role = null;         // 'host' | 'client' | null
		this.roomId = null;
		this.room = null;         // local copy of the room record
		this.hostToken = null;    // host: secret that lets us reclaim the room
		this.reclaiming = false;  // host: an unattended reclaim is in flight
		this.hostPeerId = 0;      // client: the host's lobby peer id

		this.peers = {};          // lobby peerId -> peer object
		this.idxToPeer = {};      // peer index -> lobby peerId
		this.recvQueue = [];      // FIFO of {peer: idx, data: Uint8Array}
		this.recvDrops = 0;

		this.listeners = {};      // event name -> [cb]
		this.roomSubs = [];       // subscribeRooms callbacks
		this.subscribed = false;  // have we told the lobby to push us the list
		this.lastRoomCount = 0;

		this.listWaiters = [];    // pending listRooms() resolvers
		this.pending = {};        // one-shot control-reply resolvers keyed by type

		this.wantReconnect = false;
		this.reconnectDelay = RECONNECT_BASE_MS;
		this.reconnectTimer = null;
		this.rtcOpenTimers = {};
		this.destroyed = false;
	}

	function defaultLobbyUrl() {
		if (typeof location !== 'undefined' && location.host) {
			var scheme = location.protocol === 'https:' ? 'wss:' : 'ws:';
			return scheme + '//' + location.host + '/';
		}
		return 'ws://127.0.0.1:8081/';
	}

	function defaultInviteBase() {
		if (typeof location !== 'undefined' && location.href) {
			// The invite URL simply carries ?join=<id> back to the same page.
			return location.origin + location.pathname;
		}
		return '';
	}

	var P = Transport.prototype;

	// ---- events ------------------------------------------------------------

	P.on = function (event, cb) {
		if (!this.listeners[event]) {
			this.listeners[event] = [];
		}
		this.listeners[event].push(cb);
		return this;
	};

	P.off = function (event, cb) {
		var list = this.listeners[event];
		if (!list) {
			return this;
		}
		for (var i = list.length - 1; i >= 0; i--) {
			if (list[i] === cb) {
				list.splice(i, 1);
			}
		}
		return this;
	};

	P.emit = function (event, payload) {
		var list = this.listeners[event];
		if (!list) {
			return;
		}
		// Copy first: a handler may add/remove listeners while we iterate.
		var copy = list.slice();
		for (var i = 0; i < copy.length; i++) {
			try {
				copy[i](payload);
			} catch (err) {
				// A buggy listener must never break the transport.
			}
		}
	};

	P.status = function (text) {
		this.emit('status', { text: text });
	};

	// ---- configuration -----------------------------------------------------

	P.configure = function (opts) {
		opts = opts || {};
		if (opts.lobbyUrl) {
			this.config.lobbyUrl = opts.lobbyUrl;
		}
		if (opts.iceServers) {
			this.config.iceServers = opts.iceServers;
			this.iceServers = opts.iceServers;
		}
		if (opts.playerName) {
			this.config.playerName = String(opts.playerName);
		}
		if (opts.inviteBase !== undefined && opts.inviteBase !== null) {
			this.config.inviteBase = String(opts.inviteBase);
		}
		return this;
	};

	P.isSupported = function () {
		return !!getRTC();
	};

	// ---- lobby connection --------------------------------------------------

	P.connect = function () {
		var self = this;
		if (self.connected && self.ws && self.ws.readyState === 1) {
			return Promise.resolve();
		}
		if (self.connecting) {
			return self.connecting;
		}

		var WS = getWebSocket();
		if (!WS) {
			return Promise.reject(new Error('WebSocket is not available'));
		}

		self.connecting = new Promise(function (resolve, reject) {
			var ws;
			try {
				ws = new WS(self.config.lobbyUrl);
			} catch (err) {
				self.connecting = null;
				reject(err);
				return;
			}
			ws.binaryType = 'arraybuffer';
			self.ws = ws;

			var settled = false;

			ws.onopen = function () {
				self.status('connected to lobby');
				self.reconnectDelay = RECONNECT_BASE_MS;
				self.sendControl({ t: 'hello', name: self.config.playerName, version: 1 });
			};

			ws.onmessage = function (event) {
				self.onLobbyMessage(event.data, function onWelcome() {
					if (!settled) {
						settled = true;
						self.connecting = null;
						resolve();
					}
				});
			};

			ws.onerror = function () {
				// The close handler does the actual teardown/reconnect.
				if (!settled) {
					settled = true;
					self.connecting = null;
					reject(new Error('lobby connection failed'));
				}
			};

			ws.onclose = function (ev) {
				self.connected = false;
				self.ws = null;
				var reason = ev && ev.reason ? String(ev.reason) : '';
				if (!settled) {
					settled = true;
					self.connecting = null;
					reject(new Error('lobby closed before welcome' + (reason ? ': ' + reason : '')));
					return;
				}
				self.onLobbyClosed(reason);
			};
		});

		return self.connecting;
	};

	P.disconnect = function () {
		this.wantReconnect = false;
		this.clearReconnect();
		this.teardownAllPeers();
		this.role = null;
		this.roomId = null;
		this.room = null;
		this.connected = false;
		if (this.ws) {
			try {
				this.ws.close(1000, 'client disconnect');
			} catch (err) { /* ignore */ }
			this.ws = null;
		}
	};

	P.clearReconnect = function () {
		if (this.reconnectTimer) {
			clearTimeout(this.reconnectTimer);
			this.reconnectTimer = null;
		}
	};

	/**
	 * The lobby socket dropped. While we are actively hosting or joined we
	 * reconnect with backoff and re-register the room, because losing the
	 * signalling/relay socket must not silently kill a match.
	 */
	P.onLobbyClosed = function (reason) {
		this.connected = false;
		if (!this.wantReconnect || this.destroyed) {
			this.emit('closed', { reason: reason || 'lobby closed' });
			return;
		}
		this.status('lobby lost, reconnecting');
		this.scheduleReconnect();
	};

	P.scheduleReconnect = function () {
		var self = this;
		if (self.reconnectTimer || self.destroyed) {
			return;
		}
		var delay = self.reconnectDelay;
		self.reconnectDelay = Math.min(RECONNECT_MAX_MS, self.reconnectDelay * 2);
		self.reconnectTimer = setTimeout(function () {
			self.reconnectTimer = null;
			self.connect().then(function () {
				self.reregister();
			}).catch(function () {
				self.scheduleReconnect();
			});
		}, delay);
	};

	/** After a reconnect, re-create the room (host) or re-join it (client). */
	P.reregister = function () {
		var self = this;
		if (self.role === 'host' && self.room) {
			// Take the *same* room over again if the lobby still has it: the
			// room id is in the invite link players already have, so losing it
			// to a dropped WebSocket would invalidate every link handed out.
			// The lobby keeps a room alive for a grace period and hands it back
			// to whoever proves ownership with the host token; falling back to
			// a fresh room is handled in handleError.
			if (self.roomId && self.hostToken) {
				self.reclaiming = true;
				self.sendControl({
					t: 'reclaim',
					roomId: self.roomId,
					hostToken: self.hostToken,
					room: self.room
				});
			} else {
				self.sendControl({ t: 'host', room: self.room });
			}
			if (self.subscribed) {
				self.sendControl({ t: 'subscribe' });
			}
		} else if (self.role === 'client' && self.roomId) {
			self.sendControl({ t: 'join', roomId: self.roomId });
		} else if (self.subscribed) {
			self.sendControl({ t: 'subscribe' });
		}
	};

	P.sendControl = function (obj) {
		if (!this.ws || this.ws.readyState !== 1) {
			return false;
		}
		try {
			this.ws.send(JSON.stringify(obj));
			return true;
		} catch (err) {
			return false;
		}
	};

	// ---- lobby message dispatch -------------------------------------------

	P.onLobbyMessage = function (data, onWelcome) {
		// Binary frames are relayed game packets; text frames are JSON control.
		if (typeof data !== 'string') {
			this.onRelayFrame(data);
			return;
		}
		var msg;
		try {
			msg = JSON.parse(data);
		} catch (err) {
			return; // never throw out of the socket handler
		}
		if (!msg || typeof msg.t !== 'string') {
			return;
		}

		try {
			this.dispatchControl(msg, onWelcome);
		} catch (err) {
			// Defensive: a malformed/unexpected message must not break us.
		}
	};

	P.dispatchControl = function (msg, onWelcome) {
		switch (msg.t) {
		case 'welcome':
			this.peerId = msg.peer >>> 0;
			if (!this.config.iceServers && msg.ice) {
				this.iceServers = msg.ice;
			}
			this.lastRoomCount = msg.rooms || 0;
			this.connected = true;
			if (onWelcome) {
				onWelcome();
			}
			break;

		case 'hosted':
			this.handleHosted(msg);
			break;

		case 'updated':
			this.room = msg.room || this.room;
			this.emit('roomupdate', { room: this.room });
			this.resolvePending('update', msg);
			break;

		case 'unhosted':
			this.resolvePending('unhost', msg);
			break;

		case 'rooms':
			this.handleRooms(msg);
			break;

		case 'joined':
			this.handleJoined(msg);
			break;

		case 'peer':
			this.handlePeerArrived(msg);
			break;

		case 'peerleft':
			this.handlePeerLeft(msg.peer >>> 0);
			break;

		case 'roomclosed':
			this.handleRoomClosed(msg);
			break;

		case 'hostaway':
			// The host's page went away (most likely a reload). The room and
			// its id live on for a moment - hold everything until it is back.
			this.handleHostAway(msg);
			break;

		case 'hostback':
			// The host is back with a new peer id: every link has to be set up
			// again, so the room has to be re-joined.
			this.handleHostBack(msg);
			break;

		case 'signal':
			this.handleSignal(msg);
			break;

		case 'pong':
			this.resolvePending('ping', msg);
			break;

		case 'error':
			this.handleError(msg);
			break;

		default:
			break;
		}
	};

	P.handleError = function (msg) {
		// A refused reclaim is not an error the caller has to see: the room is
		// simply gone (or this lobby is an older build that does not know the
		// message), so host a new one instead. Only a reclaim that ran
		// unattended - after a reconnect - is retried this way; an explicit
		// reclaim() call is answered with its rejection.
		if (this.reclaiming && (msg.code === 'noroom' || msg.code === 'badtoken' ||
			msg.code === 'badrequest')) {
			this.reclaiming = false;
			if (!this.pending['host'] && this.role === 'host' && this.room) {
				this.hostToken = null;
				this.sendControl({ t: 'host', room: this.room });
				return;
			}
		}
		this.emit('error', { message: (msg.code || 'error') + ': ' + (msg.message || '') });
		// Reject whatever one-shot request is most likely waiting.
		var err = new Error(msg.message || msg.code || 'lobby error');
		err.code = msg.code;
		if (msg.code === 'noroom' || msg.code === 'full' || msg.code === 'self' ||
			msg.code === 'badrequest' || msg.code === 'hostaway' ||
			msg.code === 'badtoken') {
			// 'hostaway': the room is paused because its host is reloading.
			// 'badtoken': a reclaim of a room this page does not own (any more).
			this.rejectPending('join', err);
			this.rejectPending('host', err);
		}
		if (msg.code === 'toomanyrooms') {
			this.rejectPending('host', err);
		}
	};

	// ---- one-shot request/response bookkeeping ----------------------------

	P.waitFor = function (key) {
		var self = this;
		return new Promise(function (resolve, reject) {
			self.pending[key] = { resolve: resolve, reject: reject };
		});
	};

	P.resolvePending = function (key, value) {
		var p = this.pending[key];
		if (p) {
			delete this.pending[key];
			p.resolve(value);
		}
	};

	P.rejectPending = function (key, err) {
		var p = this.pending[key];
		if (p) {
			delete this.pending[key];
			p.reject(err);
		}
	};

	// ---- room browsing -----------------------------------------------------

	P.listRooms = function () {
		var self = this;
		return self.connect().then(function () {
			return new Promise(function (resolve) {
				self.listWaiters.push(resolve);
				self.sendControl({ t: 'list' });
			});
		});
	};

	P.handleRooms = function (msg) {
		var list = msg.rooms || [];
		this.lastRoomCount = typeof msg.count === 'number' ? msg.count : list.length;

		// Any pending listRooms() resolves with the next room list we see.
		if (this.listWaiters.length) {
			var resolve = this.listWaiters.shift();
			resolve(list);
		}

		this.emit('rooms', { rooms: list, count: this.lastRoomCount });
		for (var i = 0; i < this.roomSubs.length; i++) {
			try {
				this.roomSubs[i](list, this.lastRoomCount);
			} catch (err) { /* ignore listener errors */ }
		}
	};

	P.subscribeRooms = function (cb) {
		var self = this;
		self.roomSubs.push(cb);
		if (!self.subscribed) {
			self.subscribed = true;
			self.connect().then(function () {
				self.sendControl({ t: 'subscribe' });
			}).catch(function () { /* reconnect logic will retry */ });
		}
		return function unsubscribe() {
			for (var i = self.roomSubs.length - 1; i >= 0; i--) {
				if (self.roomSubs[i] === cb) {
					self.roomSubs.splice(i, 1);
				}
			}
			if (!self.roomSubs.length && self.subscribed) {
				self.subscribed = false;
				self.sendControl({ t: 'unsubscribe' });
			}
		};
	};

	P.getRoomCount = function () {
		return this.lastRoomCount || 0;
	};

	// ---- hosting -----------------------------------------------------------

	P.host = function (settings) {
		var self = this;
		return self.connect().then(function () {
			self.wantReconnect = true;
			var promise = self.waitFor('host');
			self.sendControl({ t: 'host', room: settings || {} });
			return promise;
		}).then(function (msg) {
			return {
				roomId: self.roomId,
				inviteUrl: self.inviteUrlFor(self.roomId),
				room: self.room
			};
		});
	};

	P.handleHosted = function (msg) {
		this.role = 'host';
		this.roomId = msg.roomId;
		this.room = msg.room || null;
		// Secret that proves this page owns the room. Keeping it means the very
		// same room (and invite link) can be taken over again after a reload -
		// see reclaim() and P.reregister.
		this.hostToken = msg.hostToken || this.hostToken || null;
		this.reclaiming = false;
		this.status((msg.reclaimed ? 'hosting again ' : 'hosting ') + this.roomId);
		this.resolvePending('host', msg);
	};

	/**
	 * Take a room this page hosted before back over, keeping its id, after the
	 * page reloaded. Resolves like host(); rejects if the lobby does not have
	 * the room any more (grace period over) or does not know the message, so
	 * the caller can host a fresh one.
	 */
	P.reclaim = function (roomId, hostToken, settings) {
		var self = this;
		if (!roomId || !hostToken) {
			return Promise.reject(new Error('nothing to reclaim'));
		}
		return self.connect().then(function () {
			self.wantReconnect = true;
			self.roomId = roomId;
			self.hostToken = hostToken;
			var promise = self.waitFor('host');
			self.sendControl({
				t: 'reclaim',
				roomId: roomId,
				hostToken: hostToken,
				room: settings || undefined
			});
			return promise;
		}).then(function () {
			return {
				roomId: self.roomId,
				inviteUrl: self.inviteUrlFor(self.roomId),
				room: self.room
			};
		}).catch(function (err) {
			// Nothing was taken over - do not leave a half-set room behind.
			if (self.role !== 'host') {
				self.roomId = null;
				self.hostToken = null;
			}
			throw err;
		});
	};

	P.inviteUrlFor = function (roomId) {
		var base = this.config.inviteBase || '';
		if (!roomId) {
			return base;
		}
		var sep = base.indexOf('?') === -1 ? '?' : '&';
		return base + sep + 'join=' + encodeURIComponent(roomId);
	};

	P.updateRoom = function (partial) {
		var self = this;
		if (self.role !== 'host') {
			return Promise.reject(new Error('not hosting'));
		}
		// Keep a local, merged copy so a reconnect re-registers the latest room.
		if (self.room && partial) {
			for (var k in partial) {
				if (Object.prototype.hasOwnProperty.call(partial, k)) {
					self.room[k] = partial[k];
				}
			}
		}
		var promise = self.waitFor('update');
		self.sendControl({ t: 'update', room: partial || {} });
		return promise;
	};

	P.stopHosting = function () {
		// Closing every peer connection is what throws all players out - that is
		// the whole point of "the host left". The lobby additionally pushes
		// roomclosed to each joiner when we unhost.
		this.wantReconnect = false;
		this.teardownAllPeers();
		this.sendControl({ t: 'unhost' });
		this.role = null;
		this.roomId = null;
		this.room = null;
		this.hostToken = null;
	};

	P.handlePeerArrived = function (msg) {
		var peerId = msg.peer >>> 0;
		var peer = this.ensurePeer(peerId, msg.name || 'Player');
		// The host is the WebRTC answerer; it waits for the joiner's offer. The
		// relay path is already usable, so play can start immediately.
		this.emit('peerjoin', { idx: peer.idx, peer: peerId, name: peer.name });
		this.armRtcTimeout(peer);
	};

	// ---- joining -----------------------------------------------------------

	P.join = function (roomId) {
		var self = this;
		return self.connect().then(function () {
			self.wantReconnect = true;
			var promise = self.waitFor('join');
			self.sendControl({ t: 'join', roomId: roomId });
			return promise;
		});
	};

	P.handleJoined = function (msg) {
		var self = this;
		self.role = 'client';
		self.roomId = msg.roomId;
		self.room = msg.room || null;
		self.hostPeerId = msg.host >>> 0;

		// The host is always peer index 1 on a joining client.
		var peer = self.ensurePeer(self.hostPeerId, (self.room && self.room.name) || 'Host', 1);
		self.status('joined ' + self.roomId);

		// Kick off WebRTC as the offerer, but the relay path is already usable,
		// so resolve join() right away - a joining player is never stuck.
		self.startOffer(peer);
		self.armRtcTimeout(peer);

		self.resolvePending('join', {
			address: self.addressForPeer(1),
			room: self.room,
			peer: self.hostPeerId
		});
	};

	P.leave = function () {
		if (this.role === 'client' && this.hostPeerId) {
			this.sendControl({ t: 'bye', to: this.hostPeerId });
		}
		this.wantReconnect = false;
		this.teardownAllPeers();
		this.role = null;
		this.roomId = null;
		this.room = null;
		this.hostPeerId = 0;
	};

	P.handleHostAway = function (msg) {
		if (this.role !== 'client') { return; }
		// Drop the transport to the host - its page is gone - but keep the room
		// id, so the game can be picked up again when the host is back.
		this.teardownAllPeers();
		this.hostPeerId = 0;
		this.emit('hostaway', { roomId: msg.roomId, grace: msg.grace || 0 });
	};

	P.handleHostBack = function (msg) {
		var self = this;
		if (self.role !== 'client' || !self.roomId || self.roomId !== msg.roomId) {
			return;
		}
		// The host is a new peer now, so the room has to be joined again. The
		// address the engine talks to (peer index 1) stays the same.
		self.join(self.roomId).then(function (joined) {
			self.emit('hostback', { roomId: msg.roomId, address: joined.address });
		}).catch(function (err) {
			self.emit('error', { message: 'could not rejoin the game: ' +
				(err && err.message || err) });
		});
	};

	P.handleRoomClosed = function (msg) {
		// The host went away. Everything we had to that room is gone.
		this.teardownAllPeers();
		this.role = null;
		this.roomId = null;
		this.hostPeerId = 0;
		this.wantReconnect = false;
		this.emit('closed', { reason: 'room closed by host' });
	};

	P.handlePeerLeft = function (peerId) {
		var peer = this.peers[peerId];
		if (!peer) {
			return;
		}
		var idx = peer.idx;
		this.destroyPeer(peer);
		this.emit('peerleave', { idx: idx, peer: peerId });
	};

	// ---- peer bookkeeping --------------------------------------------------

	P.allocIndex = function (preferred) {
		if (preferred && !this.idxToPeer[preferred]) {
			return preferred;
		}
		for (var i = 1; i <= MAX_PEER_INDEX; i++) {
			if (!this.idxToPeer[i]) {
				return i;
			}
		}
		return 0; // no free index - refuse the peer rather than reuse one
	};

	P.ensurePeer = function (peerId, name, preferredIdx) {
		var existing = this.peers[peerId];
		if (existing) {
			if (name) {
				existing.name = name;
			}
			return existing;
		}
		var idx = this.allocIndex(preferredIdx);
		var peer = {
			peer: peerId,
			idx: idx,
			name: name || 'Player',
			pc: null,
			dc: null,
			rtcOpen: false,
			transport: 'relay',
			pendingCandidates: []
		};
		this.peers[peerId] = peer;
		if (idx) {
			this.idxToPeer[idx] = peerId;
		}
		return peer;
	};

	P.destroyPeer = function (peer) {
		if (!peer) {
			return;
		}
		if (this.rtcOpenTimers[peer.peer]) {
			clearTimeout(this.rtcOpenTimers[peer.peer]);
			delete this.rtcOpenTimers[peer.peer];
		}
		try { if (peer.dc) { peer.dc.close(); } } catch (err) { /* ignore */ }
		try { if (peer.pc) { peer.pc.close(); } } catch (err) { /* ignore */ }
		if (peer.idx && this.idxToPeer[peer.idx] === peer.peer) {
			delete this.idxToPeer[peer.idx];
		}
		delete this.peers[peer.peer];
	};

	P.teardownAllPeers = function () {
		var ids = Object.keys(this.peers);
		for (var i = 0; i < ids.length; i++) {
			this.destroyPeer(this.peers[ids[i]]);
		}
		this.peers = {};
		this.idxToPeer = {};
	};

	// ---- WebRTC ------------------------------------------------------------

	P.newPeerConnection = function (peer) {
		var RTC = getRTC();
		if (!RTC) {
			return null;
		}
		var self = this;
		var pc;
		try {
			pc = new RTC({ iceServers: self.iceServers || [] });
		} catch (err) {
			return null;
		}
		peer.pc = pc;

		pc.onicecandidate = function (ev) {
			if (ev && ev.candidate) {
				self.sendControl({ t: 'signal', to: peer.peer, data: { kind: 'candidate', candidate: ev.candidate } });
			}
		};
		pc.oniceconnectionstatechange = function () {
			var s = pc.iceConnectionState;
			if (s === 'failed' || s === 'disconnected' || s === 'closed') {
				self.markRelay(peer);
			}
		};
		return pc;
	};

	/** Joining client: create the data channel and send an offer. */
	P.startOffer = function (peer) {
		var self = this;
		var pc = self.newPeerConnection(peer);
		if (!pc) {
			return; // no WebRTC here (e.g. Node) - relay carries everything
		}
		var dc;
		try {
			dc = pc.createDataChannel(DATA_CHANNEL_LABEL, { ordered: false, maxRetransmits: 0 });
		} catch (err) {
			self.markRelay(peer);
			return;
		}
		self.attachDataChannel(peer, dc);

		pc.createOffer().then(function (offer) {
			return pc.setLocalDescription(offer).then(function () {
				self.sendControl({ t: 'signal', to: peer.peer, data: { kind: 'offer', sdp: offer.sdp, type: offer.type } });
			});
		}).catch(function () {
			self.markRelay(peer);
		});
	};

	P.attachDataChannel = function (peer, dc) {
		var self = this;
		peer.dc = dc;
		try {
			dc.binaryType = 'arraybuffer';
		} catch (err) { /* ignore */ }

		dc.onopen = function () {
			peer.rtcOpen = true;
			peer.transport = 'rtc';
			self.status('direct link to peer ' + peer.idx + ' open');
		};
		dc.onclose = function () {
			self.markRelay(peer);
		};
		dc.onerror = function () {
			self.markRelay(peer);
		};
		dc.onmessage = function (ev) {
			self.enqueue(peer.idx, toU8(ev.data));
		};
	};

	P.markRelay = function (peer) {
		if (!peer) {
			return;
		}
		peer.rtcOpen = false;
		peer.transport = 'relay';
	};

	/** Handle an incoming signalling message (offer/answer/candidate). */
	P.handleSignal = function (msg) {
		var self = this;
		var from = msg.from >>> 0;
		var data = msg.data || {};
		var peer = self.peers[from];

		if (data.kind === 'offer') {
			// We are the host answering a joiner's offer.
			if (!peer) {
				peer = self.ensurePeer(from, 'Player');
			}
			self.answerOffer(peer, data);
		} else if (data.kind === 'answer') {
			if (peer && peer.pc) {
				self.applyRemoteDescription(peer, { type: 'answer', sdp: data.sdp });
			}
		} else if (data.kind === 'candidate') {
			if (peer) {
				self.addCandidate(peer, data.candidate);
			}
		}
	};

	P.answerOffer = function (peer, data) {
		var self = this;
		var RTC = getRTC();
		if (!RTC) {
			return;
		}
		var pc = peer.pc || self.newPeerConnection(peer);
		if (!pc) {
			return;
		}
		pc.ondatachannel = function (ev) {
			self.attachDataChannel(peer, ev.channel);
		};
		self.applyRemoteDescription(peer, { type: 'offer', sdp: data.sdp }).then(function () {
			return pc.createAnswer();
		}).then(function (answer) {
			return pc.setLocalDescription(answer).then(function () {
				self.sendControl({ t: 'signal', to: peer.peer, data: { kind: 'answer', sdp: answer.sdp, type: answer.type } });
			});
		}).catch(function () {
			self.markRelay(peer);
		});
	};

	P.applyRemoteDescription = function (peer, desc) {
		var self = this;
		if (!peer.pc) {
			return Promise.resolve();
		}
		return peer.pc.setRemoteDescription(desc).then(function () {
			// Flush any ICE candidates that arrived before the description.
			var pending = peer.pendingCandidates;
			peer.pendingCandidates = [];
			for (var i = 0; i < pending.length; i++) {
				try {
					peer.pc.addIceCandidate(pending[i]);
				} catch (err) { /* ignore */ }
			}
		}).catch(function () {
			self.markRelay(peer);
		});
	};

	P.addCandidate = function (peer, candidate) {
		if (!candidate || !peer.pc) {
			return;
		}
		// Before setRemoteDescription addIceCandidate throws; queue until then.
		if (!peer.pc.remoteDescription || !peer.pc.remoteDescription.type) {
			peer.pendingCandidates.push(candidate);
			return;
		}
		try {
			peer.pc.addIceCandidate(candidate);
		} catch (err) { /* ignore bad candidate */ }
	};

	P.armRtcTimeout = function (peer) {
		var self = this;
		if (self.rtcOpenTimers[peer.peer]) {
			return;
		}
		self.rtcOpenTimers[peer.peer] = setTimeout(function () {
			delete self.rtcOpenTimers[peer.peer];
			if (!peer.rtcOpen) {
				// ICE did not converge in time - stay on the relay (it has been
				// carrying traffic all along) and stop hoping quietly.
				self.markRelay(peer);
			}
		}, RTC_OPEN_TIMEOUT_MS);
		if (self.rtcOpenTimers[peer.peer].unref) {
			self.rtcOpenTimers[peer.peer].unref();
		}
	};

	// ---- packet plumbing (engine bridge) ----------------------------------

	P.enqueue = function (idx, u8) {
		if (!idx || !u8 || u8.length > MAX_MSGLEN) {
			return;
		}
		if (this.recvQueue.length >= RECV_QUEUE_CAP) {
			this.recvQueue.shift();
			this.recvDrops++;
		}
		this.recvQueue.push({ peer: idx, data: u8 });
	};

	/** Relayed binary frame from the lobby: [srcPeerId u32le][payload]. */
	P.onRelayFrame = function (data) {
		var u8 = toU8(data);
		if (u8.length < 4) {
			return;
		}
		var src = readU32LE(u8, 0);
		var peer = this.peers[src];
		if (!peer) {
			return; // packet from a peer we do not know - drop
		}
		// Copy the payload out so it does not alias the socket's buffer.
		this.enqueue(peer.idx, u8.slice(4));
	};

	P.send = function (peerIdx, u8) {
		var peerId = this.idxToPeer[peerIdx];
		if (!peerId) {
			return false;
		}
		var peer = this.peers[peerId];
		if (!peer) {
			return false;
		}
		var payload = toU8(u8);
		if (payload.length > MAX_MSGLEN) {
			return false;
		}

		// Prefer the direct data channel; fall back to the lobby relay frame.
		if (peer.rtcOpen && peer.dc && peer.dc.readyState === 'open') {
			try {
				peer.dc.send(payload);
				return true;
			} catch (err) {
				this.markRelay(peer);
			}
		}
		return this.sendRelay(peer, payload);
	};

	P.sendRelay = function (peer, payload) {
		if (!this.ws || this.ws.readyState !== 1) {
			return false;
		}
		var frame = new Uint8Array(4 + payload.length);
		writeU32LE(frame, 0, peer.peer);
		frame.set(payload, 4);
		try {
			this.ws.send(frame);
			return true;
		} catch (err) {
			return false;
		}
	};

	P.receive = function () {
		if (!this.recvQueue.length) {
			return null;
		}
		return this.recvQueue.shift();
	};

	P.active = function () {
		return this.role === 'host' || this.role === 'client';
	};

	P.addressForPeer = function (idx) {
		var hi = (idx >> 8) & 0xff;
		var lo = idx & 0xff;
		return ADDR_PREFIX + '.0.' + hi + '.' + lo + ':' + PORT;
	};

	// ---- introspection -----------------------------------------------------

	P.getRole = function () {
		return this.role;
	};

	P.getRoomId = function () {
		return this.roomId;
	};

	P.getRoom = function () {
		return this.room;
	};

	/** Host only: the secret needed to reclaim this room after a reload. */
	P.getHostToken = function () {
		return this.hostToken;
	};

	P.getPeers = function () {
		var out = [];
		var ids = Object.keys(this.peers);
		for (var i = 0; i < ids.length; i++) {
			var p = this.peers[ids[i]];
			var open = p.rtcOpen ? (p.dc && p.dc.readyState === 'open')
				: (this.ws && this.ws.readyState === 1);
			out.push({
				idx: p.idx,
				peer: p.peer,
				name: p.name,
				transport: p.rtcOpen ? 'rtc' : 'relay',
				open: !!open
			});
		}
		out.sort(function (a, b) { return a.idx - b.idx; });
		return out;
	};

	// -----------------------------------------------------------------------
	// Public object assembly
	// -----------------------------------------------------------------------

	/**
	 * Wrap a Transport instance in the flat, bound API the shell and engine
	 * call. Constants live on the object too so callers never reach into a
	 * prototype.
	 */
	function buildApi(instance) {
		var api = {
			ADDR_PREFIX: ADDR_PREFIX,
			PORT: PORT,
			_transport: instance
		};
		var methods = [
			'configure', 'isSupported', 'connect', 'disconnect', 'listRooms',
			'subscribeRooms', 'getRoomCount', 'host', 'reclaim', 'updateRoom',
			'stopHosting', 'join', 'leave', 'getRole', 'getRoomId', 'getPeers',
			'getRoom', 'getHostToken',
			'on', 'off', 'send', 'receive', 'active', 'addressForPeer'
		];
		for (var i = 0; i < methods.length; i++) {
			(function (name) {
				api[name] = function () {
					return instance[name].apply(instance, arguments);
				};
			})(methods[i]);
		}
		return api;
	}

	function createTransport() {
		return buildApi(new Transport());
	}

	var singleton = buildApi(new Transport());
	singleton.createTransport = createTransport;
	singleton.Transport = Transport;

	if (typeof module !== 'undefined' && module.exports) {
		module.exports = singleton;
	}
	if (global) {
		global.ETLP2P = singleton;
	}

})(typeof globalThis !== 'undefined' ? globalThis : (typeof self !== 'undefined' ? self : this));
