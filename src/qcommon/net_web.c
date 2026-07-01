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
 */
/**
 * @file net_web.c
 * @brief WebSocket-based networking layer for Emscripten/WASM builds.
 *
 * Replaces net_ip.c when building for the browser. Uses Emscripten's
 * WebSocket API to communicate with a WebSocket-to-UDP relay server.
 *
 * Browser limitations:
 * - No raw UDP/TCP socket access
 * - All network traffic must go through WebSocket or WebRTC
 * - A relay server is required to bridge WebSocket ↔ UDP for game servers
 */

#ifdef __EMSCRIPTEN__

#include "q_shared.h"
#include "qcommon.h"

#include <emscripten/websocket.h>
#include <string.h>

// Maximum number of simultaneous WebSocket connections
#define MAX_WS_CONNECTIONS 4

// Packet buffer for received data
#define WS_RECV_BUFFER_SIZE (MAX_MSGLEN * 4)

// Default WebSocket relay server URL (must match tools/ws-relay default port)
#define WS_DEFAULT_RELAY_URL "ws://localhost:8080"

typedef struct
{
	EMSCRIPTEN_WEBSOCKET_T socket;
	qboolean active;
	netadr_t remoteAddr;
	char url[MAX_STRING_CHARS];
} wsConnection_t;

typedef struct
{
	byte data[MAX_MSGLEN];
	int length;
	netadr_t from;
} wsPacket_t;

#define WS_PACKET_QUEUE_SIZE 64

static wsConnection_t wsConnections[MAX_WS_CONNECTIONS];
static wsPacket_t     packetQueue[WS_PACKET_QUEUE_SIZE];
static int            packetQueueHead   = 0;
static int            packetQueueTail   = 0;
static qboolean       networkingEnabled = qfalse;

static cvar_t *net_wsRelayServer;

/**
 * @brief Queue a received packet for later retrieval by NET_GetPacket
 */
static void WS_QueuePacket(const byte *data, int length, const netadr_t *from)
{
	int nextHead = (packetQueueHead + 1) % WS_PACKET_QUEUE_SIZE;

	if (nextHead == packetQueueTail)
	{
		Com_DPrintf("WS_QueuePacket: packet queue overflow\n");
		return;
	}

	if (length > MAX_MSGLEN)
	{
		Com_DPrintf("WS_QueuePacket: packet too large (%d bytes)\n", length);
		return;
	}

	Com_Memcpy(packetQueue[packetQueueHead].data, data, length);
	packetQueue[packetQueueHead].length = length;
	Com_Memcpy(&packetQueue[packetQueueHead].from, from, sizeof(netadr_t));

	packetQueueHead = nextHead;
}

/**
 * @brief WebSocket message callback
 */
static EM_BOOL WS_OnMessage(int eventType, const EmscriptenWebSocketMessageEvent *wsEvent, void *userData)
{
	wsConnection_t *conn = (wsConnection_t *)userData;

	if (!conn || !conn->active)
	{
		return EM_FALSE;
	}

	if (!wsEvent->isText && wsEvent->numBytes > 0)
	{
		WS_QueuePacket(wsEvent->data, wsEvent->numBytes, &conn->remoteAddr);
	}

	return EM_TRUE;
}

/**
 * @brief WebSocket open callback
 */
static EM_BOOL WS_OnOpen(int eventType, const EmscriptenWebSocketOpenEvent *wsEvent, void *userData)
{
	wsConnection_t *conn = (wsConnection_t *)userData;

	if (conn)
	{
		Com_Printf("WebSocket connected to %s\n", conn->url);
	}

	return EM_TRUE;
}

/**
 * @brief WebSocket error callback
 */
static EM_BOOL WS_OnError(int eventType, const EmscriptenWebSocketErrorEvent *wsEvent, void *userData)
{
	wsConnection_t *conn = (wsConnection_t *)userData;

	if (conn)
	{
		Com_Printf("WebSocket error on connection to %s\n", conn->url);
	}

	return EM_TRUE;
}

/**
 * @brief WebSocket close callback
 */
static EM_BOOL WS_OnClose(int eventType, const EmscriptenWebSocketCloseEvent *wsEvent, void *userData)
{
	wsConnection_t *conn = (wsConnection_t *)userData;

	if (conn)
	{
		Com_Printf("WebSocket closed: %s (code: %d)\n", conn->url, wsEvent->code);
		conn->active = qfalse;
	}

	return EM_TRUE;
}

/**
 * @brief Find or create a WebSocket connection for a given address
 */
static wsConnection_t *WS_GetConnection(const netadr_t *to)
{
	int  i;
	char url[MAX_STRING_CHARS];

	// Look for existing connection
	for (i = 0; i < MAX_WS_CONNECTIONS; i++)
	{
		if (wsConnections[i].active && NET_CompareAdr(&wsConnections[i].remoteAddr, to))
		{
			return &wsConnections[i];
		}
	}

	// Find a free slot
	for (i = 0; i < MAX_WS_CONNECTIONS; i++)
	{
		if (!wsConnections[i].active)
		{
			break;
		}
	}

	if (i == MAX_WS_CONNECTIONS)
	{
		Com_Printf("WS_GetConnection: no free WebSocket slots\n");
		return NULL;
	}

	// Build WebSocket URL using the relay server
	if (net_wsRelayServer && net_wsRelayServer->string[0])
	{
		Com_sprintf(url, sizeof(url), "%s/%d.%d.%d.%d:%d",
		            net_wsRelayServer->string,
		            to->ip[0], to->ip[1], to->ip[2], to->ip[3],
		            BigShort(to->port));
	}
	else
	{
		Com_sprintf(url, sizeof(url), WS_DEFAULT_RELAY_URL "/%d.%d.%d.%d:%d",
		            to->ip[0], to->ip[1], to->ip[2], to->ip[3],
		            BigShort(to->port));
	}

	// Create WebSocket
	EmscriptenWebSocketCreateAttributes wsAttrs =
	{
		url,
		"binary",
		EM_TRUE
	};

	EMSCRIPTEN_WEBSOCKET_T ws = emscripten_websocket_new(&wsAttrs);

	if (ws <= 0)
	{
		Com_Printf("WS_GetConnection: failed to create WebSocket to %s\n", url);
		return NULL;
	}

	wsConnections[i].socket = ws;
	wsConnections[i].active = qtrue;
	Com_Memcpy(&wsConnections[i].remoteAddr, to, sizeof(netadr_t));
	Q_strncpyz(wsConnections[i].url, url, sizeof(wsConnections[i].url));

	// Set callbacks
	emscripten_websocket_set_onopen_callback(ws, &wsConnections[i], WS_OnOpen);
	emscripten_websocket_set_onerror_callback(ws, &wsConnections[i], WS_OnError);
	emscripten_websocket_set_onclose_callback(ws, &wsConnections[i], WS_OnClose);
	emscripten_websocket_set_onmessage_callback(ws, &wsConnections[i], WS_OnMessage);

	Com_Printf("WS_GetConnection: connecting to %s\n", url);

	return &wsConnections[i];
}

/*
====================
NET_CompareBaseAdrMask
====================
*/
qboolean NET_CompareBaseAdrMask(const netadr_t *a, const netadr_t *b, int netmask)
{
	if (a->type != b->type)
	{
		return qfalse;
	}

	if (a->type == NA_LOOPBACK)
	{
		return qtrue;
	}

	if (a->type == NA_IP)
	{
		byte cmpmask;
		int  i;

		if (netmask < 0 || netmask > 32)
		{
			netmask = 32;
		}

		for (i = 0; netmask >= 8; i++, netmask -= 8)
		{
			if (a->ip[i] != b->ip[i])
			{
				return qfalse;
			}
		}

		if (netmask)
		{
			cmpmask = (byte)((1 << netmask) - 1) << (8 - netmask);
			return (a->ip[i] & cmpmask) == (b->ip[i] & cmpmask);
		}
		else
		{
			return qtrue;
		}
	}

	return qfalse;
}

/*
====================
NET_CompareBaseAdr
====================
*/
qboolean NET_CompareBaseAdr(const netadr_t *a, const netadr_t *b)
{
	return NET_CompareBaseAdrMask(a, b, -1);
}

/*
====================
NET_CompareAdr
====================
*/
qboolean NET_CompareAdr(const netadr_t *a, const netadr_t *b)
{
	if (!NET_CompareBaseAdr(a, b))
	{
		return qfalse;
	}

	if (a->type == NA_IP || a->type == NA_IP6)
	{
		if (a->port != b->port)
		{
			return qfalse;
		}
	}

	return qtrue;
}

/*
====================
NET_AdrToStringNoPort
====================
*/
const char *NET_AdrToStringNoPort(const netadr_t *a)
{
	static char s[NET_ADDRSTRMAXLEN];

	if (a->type == NA_LOOPBACK)
	{
		Com_sprintf(s, sizeof(s), "loopback");
	}
	else if (a->type == NA_IP)
	{
		Com_sprintf(s, sizeof(s), "%i.%i.%i.%i",
		            a->ip[0], a->ip[1], a->ip[2], a->ip[3]);
	}
	else
	{
		Com_sprintf(s, sizeof(s), "unknown");
	}

	return s;
}

/*
====================
NET_AdrToString
====================
*/
const char *NET_AdrToString(const netadr_t *a)
{
	static char s[NET_ADDRSTRMAXLEN];

	if (a->type == NA_LOOPBACK)
	{
		Com_sprintf(s, sizeof(s), "loopback");
	}
	else if (a->type == NA_IP)
	{
		Com_sprintf(s, sizeof(s), "%i.%i.%i.%i:%hu",
		            a->ip[0], a->ip[1], a->ip[2], a->ip[3],
		            BigShort(a->port));
	}
	else
	{
		Com_sprintf(s, sizeof(s), "unknown");
	}

	return s;
}

/*
====================
NET_StringToAdr
====================
*/
int NET_StringToAdr(const char *s, netadr_t *a, netadrtype_t family)
{
	return Sys_StringToAdr(s, a, family);
}

/*
====================
Sys_StringToAdr
====================
*/
qboolean Sys_StringToAdr(const char *s, netadr_t *a, netadrtype_t family)
{
	char base[MAX_STRING_CHARS];
	char *port;

	if (!strcmp(s, "localhost"))
	{
		Com_Memset(a, 0, sizeof(*a));
		a->type = NA_LOOPBACK;
		return qtrue;
	}

	Q_strncpyz(base, s, sizeof(base));

	port = strstr(base, ":");
	if (port)
	{
		*port = '\0';
		port++;
	}

	a->type = NA_IP;

	if (sscanf(base, "%hhu.%hhu.%hhu.%hhu", &a->ip[0], &a->ip[1], &a->ip[2], &a->ip[3]) != 4)
	{
		a->type = NA_BAD;
		return qfalse;
	}

	if (port)
	{
		a->port = BigShort((short)atoi(port));
	}
	else
	{
		a->port = BigShort(PORT_SERVER);
	}

	return qtrue;
}

/*
====================
NET_IsLocalAddress
====================
*/
qboolean NET_IsLocalAddress(const netadr_t *adr)
{
	return (qboolean)(adr->type == NA_LOOPBACK);
}

/*
====================
NET_IsLocalAddressString
====================
*/
qboolean NET_IsLocalAddressString(const char *address)
{
	if (!strcmp(address, "localhost"))
	{
		return qtrue;
	}
	if (!strcmp(address, "127.0.0.1"))
	{
		return qtrue;
	}
	return qfalse;
}

/*
====================
NET_IsIPXAddress
====================
*/
qboolean NET_IsIPXAddress(const char *buf)
{
	return qfalse;
}

/*
====================
Sys_IsLANAddress
====================
*/
qboolean Sys_IsLANAddress(const netadr_t *adr)
{
	if (adr->type == NA_LOOPBACK)
	{
		return qtrue;
	}

	if (adr->type != NA_IP)
	{
		return qfalse;
	}

	// Class A: 10.x.x.x
	if (adr->ip[0] == 10)
	{
		return qtrue;
	}

	// Class B: 172.16.x.x - 172.31.x.x
	if (adr->ip[0] == 172 && adr->ip[1] >= 16 && adr->ip[1] <= 31)
	{
		return qtrue;
	}

	// Class C: 192.168.x.x
	if (adr->ip[0] == 192 && adr->ip[1] == 168)
	{
		return qtrue;
	}

	// Loopback: 127.x.x.x
	if (adr->ip[0] == 127)
	{
		return qtrue;
	}

	return qfalse;
}

/*
====================
Sys_ShowIP
====================
*/
void Sys_ShowIP(void)
{
	Com_Printf("WebSocket networking - no local IP addresses\n");
}

/*
====================
NET_GetPacket
====================
*/
qboolean NET_GetPacket(netadr_t *net_from, msg_t *net_message, fd_set *fdr)
{
	// First check the loopback queue
	if (NET_GetLoopPacket(NS_CLIENT, net_from, net_message))
	{
		return qtrue;
	}

	// Then check the WebSocket packet queue
	if (packetQueueHead != packetQueueTail)
	{
		wsPacket_t *pkt = &packetQueue[packetQueueTail];

		Com_Memcpy(net_message->data, pkt->data, pkt->length);
		net_message->cursize = pkt->length;
		Com_Memcpy(net_from, &pkt->from, sizeof(netadr_t));

		packetQueueTail = (packetQueueTail + 1) % WS_PACKET_QUEUE_SIZE;
		return qtrue;
	}

	return qfalse;
}

/*
====================
Sys_SendPacket
====================
*/
void Sys_SendPacket(int length, const void *data, const netadr_t *to)
{
	wsConnection_t *conn;

	if (to->type == NA_LOOPBACK)
	{
		NET_SendLoopPacket(NS_SERVER, length, data, to);
		return;
	}

	if (to->type != NA_IP && to->type != NA_IP6)
	{
		Com_Error(ERR_FATAL, "Sys_SendPacket: bad address type %d", to->type);
		return;
	}

	conn = WS_GetConnection(to);
	if (!conn)
	{
		return;
	}

	EMSCRIPTEN_RESULT result = emscripten_websocket_send_binary(conn->socket, (void *)data, length);

	if (result != EMSCRIPTEN_RESULT_SUCCESS)
	{
		Com_DPrintf("Sys_SendPacket: WebSocket send failed (result: %d)\n", result);
	}
}

/*
====================
NET_Init
====================
*/
void NET_Init(void)
{
	Com_Memset(wsConnections, 0, sizeof(wsConnections));
	packetQueueHead = 0;
	packetQueueTail = 0;

	net_wsRelayServer = Cvar_Get("net_wsRelayServer", WS_DEFAULT_RELAY_URL, CVAR_ARCHIVE);

	Com_Printf("WebSocket networking initialized\n");
	Com_Printf("Relay server: %s\n", net_wsRelayServer->string);

	networkingEnabled = qtrue;
}

/*
====================
NET_Shutdown
====================
*/
void NET_Shutdown(void)
{
	int i;

	for (i = 0; i < MAX_WS_CONNECTIONS; i++)
	{
		if (wsConnections[i].active)
		{
			emscripten_websocket_close(wsConnections[i].socket, 1000, "shutdown");
			emscripten_websocket_delete(wsConnections[i].socket);
			wsConnections[i].active = qfalse;
		}
	}

	networkingEnabled = qfalse;
	Com_Printf("WebSocket networking shutdown\n");
}

/*
====================
NET_Event
====================
*/
void NET_Event(fd_set *fdr)
{
	// WebSocket events are handled asynchronously via callbacks
	// Nothing to do here
}

/*
====================
NET_Sleep
====================
*/
void NET_Sleep(int64_t usec)
{
	// In browser, we can't block. Just return.
}

/*
====================
NET_Restart_f
====================
*/
void NET_Restart_f(void)
{
	NET_Shutdown();
	NET_Init();
}

#endif /* __EMSCRIPTEN__ */
