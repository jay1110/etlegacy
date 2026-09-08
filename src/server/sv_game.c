/*
 * Wolfenstein: Enemy Territory GPL Source Code
 * Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.
 *
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
 * In addition, Wolfenstein: Enemy Territory GPL Source Code is also
 * subject to certain additional terms. You should have received a copy
 * of these additional terms immediately following the terms and conditions
 * of the GNU General Public License which accompanied the source code.
 * If not, please request a copy in writing from id Software at the address below.
 *
 * id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.
 */
/**
 * @file sv_game.c
 * @brief Interface to the game dll
 */

#include "server.h"
#include "../botlib/botlib.h"
#ifdef __EMSCRIPTEN__
#include "../qcommon/net_nxac_web.h"
#include <emscripten/heap.h>
#endif

#ifdef FEATURE_TRACKER
#include "sv_tracker.h"
#endif

botlib_export_t *botlib_export;

#define TRAP_EXTENSIONS_LIST g_extensionTraps
#include "../qcommon/vm_ext.h"

static ext_trap_keys_t g_extensionTraps[] =
{
	{ "trap_DemoSupport_Legacy",           G_DEMOSUPPORT,            qfalse },
	{ "trap_SnapshotCallbackExt_Legacy",   G_SNAPSHOT_CALLBACK_EXT,  qfalse },
	{ "trap_SnapshotSetClientMask_Legacy", G_SNAPSHOT_SETCLIENTMASK, qfalse },
	{ "trap_CvarSetDescription_Legacy",    G_CVAR_SET_DESCRIPTION,   qfalse },
#ifdef __EMSCRIPTEN__
	{ "trap_NitmodNxACTransport1", G_NITMOD_NXAC_TRANSPORT, qfalse },
	{ "trap_NitmodDatabaseStorage1", G_NITMOD_DATABASE_STORAGE, qfalse },
#endif
	{ NULL,                                -1,                       qfalse }
};

/**
* @todo TODO: These functions must be used instead of pointer arithmetic, because
* the game allocates gentities with private information after the server shared part
*/

/**
 * @brief SV_NumForGentity
 * @param[in] ent
 * @return
 *
 * @note Unused
 */
int SV_NumForGentity(sharedEntity_t *ent)
{
	int num = ((byte *)ent - (byte *)sv.gentities) / sv.gentitySize;

	return num;
}

/**
 * @brief SV_GentityNum
 * @param[in] num
 * @return
 */
sharedEntity_t *SV_GentityNum(int num)
{
	sharedEntity_t *ent = ( sharedEntity_t * )((byte *)sv.gentities + sv.gentitySize * (num));

	return ent;
}

/**
 * @brief SV_GameClientNum
 * @param[in] num
 * @return
 */
playerState_t *SV_GameClientNum(int num)
{
	playerState_t *ps = ( playerState_t * )((byte *)sv.gameClients + sv.gameClientSize * (num));

	return ps;
}

/**
 * @brief SV_SvEntityForGentity
 * @param[in] gEnt
 * @return
 */
svEntity_t *SV_SvEntityForGentity(sharedEntity_t *gEnt)
{
	if (!gEnt || gEnt->s.number < 0 || gEnt->s.number >= MAX_GENTITIES)
	{
		Com_Error(ERR_DROP, "SV_SvEntityForGentity: bad gEnt");
	}
	return &sv.svEntities[gEnt->s.number];
}

/**
 * @brief SV_GEntityForSvEntity
 * @param[in] svEnt
 * @return
 */
sharedEntity_t *SV_GEntityForSvEntity(svEntity_t *svEnt)
{
	int num = svEnt - sv.svEntities;

	return SV_GentityNum(num);
}

/**
 * @brief Sends a command string to a client
 * @param[in] clientNum
 * @param[in] text
 */
void SV_GameSendServerCommand(int clientNum, const char *text)
{
	// record the game server commands in demos
	if (sv.demoState == DS_RECORDING)
	{
		SV_DemoWriteGameCommand(clientNum, text);
	}

	if (clientNum == -2)
	{
		SV_CL_AddReliableCommand(text);
		return;
	}

	if (clientNum == -1)
	{
		SV_SendServerCommand(NULL, "%s", text);
	}
	else
	{
		if (clientNum < 0 || clientNum >= sv_maxclients->integer)
		{
			return;
		}

		SV_SendServerCommand(svs.clients + clientNum, "%s", text);
	}
}

/**
 * @brief Disconnects the client with a message
 * @param[in] clientNum
 * @param[in] reason
 * @param[in] length
 */
void SV_GameDropClient(int clientNum, const char *reason, int length)
{
	if (clientNum < 0 || clientNum >= sv_maxclients->integer)
	{
		return;
	}

	if (length)
	{
		SV_TempBan(svs.clients + clientNum, length);
	}

	SV_DropClient(svs.clients + clientNum, reason);
}

/**
 * @brief Sets mins and maxs for inline bmodels
 * @param[in,out] ent
 * @param[in] name
 */
void SV_SetBrushModel(sharedEntity_t *ent, const char *name)
{
	clipHandle_t h;
	vec3_t       mins, maxs;

	if (!name)
	{
		Com_Error(ERR_DROP, "SV_SetBrushModel: NULL for #%i", ent->s.number);
	}

	if (name[0] != '*')
	{
		Com_Error(ERR_DROP, "SV_SetBrushModel: %s of #%i isn't a brush model", name, ent->s.number);
	}

	ent->s.modelindex = Q_atoi(name + 1);

	h = CM_InlineModel(ent->s.modelindex);
	CM_ModelBounds(h, mins, maxs);
	VectorCopy(mins, ent->r.mins);
	VectorCopy(maxs, ent->r.maxs);
	ent->r.bmodel = qtrue;

	ent->r.contents = -1;       // we don't know exactly what is in the brushes

	SV_LinkEntity(ent);         // FIXME: remove
}

/**
 * @brief Also checks portalareas so that doors block sight
 * @param[in] p1
 * @param[in] p2
 * @return
 */
qboolean SV_inPVS(const vec3_t p1, const vec3_t p2)
{
	int  leafnum;
	int  cluster;
	int  area1, area2;
	byte *mask;

	leafnum = CM_PointLeafnum(p1);
	cluster = CM_LeafCluster(leafnum);
	area1   = CM_LeafArea(leafnum);
	mask    = CM_ClusterPVS(cluster);

	leafnum = CM_PointLeafnum(p2);
	cluster = CM_LeafCluster(leafnum);
	area2   = CM_LeafArea(leafnum);
	if (mask && (!(mask[cluster >> 3] & (1 << (cluster & 7)))))
	{
		return qfalse;
	}
	if (!CM_AreasConnected(area1, area2))
	{
		return qfalse;      // a door blocks sight
	}

	return qtrue;
}

/**
 * @brief Does NOT check portalareas
 * @param[in] p1
 * @param[in] p2
 * @return
 */
qboolean SV_inPVSIgnorePortals(const vec3_t p1, const vec3_t p2)
{
	int  leafnum;
	int  cluster;
	byte *mask;

	leafnum = CM_PointLeafnum(p1);
	cluster = CM_LeafCluster(leafnum);
	mask    = CM_ClusterPVS(cluster);

	leafnum = CM_PointLeafnum(p2);
	cluster = CM_LeafCluster(leafnum);

	if (mask && (!(mask[cluster >> 3] & (1 << (cluster & 7)))))
	{
		return qfalse;
	}

	return qtrue;
}

/**
 * @brief SV_AdjustAreaPortalState
 * @param[in] ent
 * @param[in] open
 */
void SV_AdjustAreaPortalState(sharedEntity_t *ent, qboolean open)
{
	svEntity_t *svEnt = SV_SvEntityForGentity(ent);

	if (svEnt->areanum2 == -1)
	{
		return;
	}

	CM_AdjustAreaPortalState(svEnt->areanum, svEnt->areanum2, open);
}

/**
 * @brief SV_EntityContact
 * @param[in] mins
 * @param[in] maxs
 * @param[in] gEnt
 * @param[in] capsule
 * @return
 */
qboolean SV_EntityContact(const vec3_t mins, const vec3_t maxs, const sharedEntity_t *gEnt, const qboolean capsule)
{
	const float  *origin = gEnt->r.currentOrigin;
	const float  *angles = gEnt->r.currentAngles;
	clipHandle_t ch;
	trace_t      trace;

	// check for exact collision
	ch = SV_ClipHandleForEntity(gEnt);
	CM_TransformedBoxTrace(&trace, vec3_origin, vec3_origin, mins, maxs,
	                       ch, -1, origin, angles, capsule);

	return trace.startsolid;
}

/**
 * @brief SV_GetServerinfo
 * @param[out] buffer
 * @param[in] bufferSize
 */
void SV_GetServerinfo(char *buffer, unsigned int bufferSize)
{
	if (bufferSize < 1)
	{
		Com_Error(ERR_DROP, "SV_GetServerinfo: bufferSize == %u", bufferSize);
	}
	Q_strncpyz(buffer, Cvar_InfoString(CVAR_SERVERINFO | CVAR_SERVERINFO_NOUPDATE), bufferSize);
}

/**
 * @brief SV_LocateGameData
 * @param[in] gEnts
 * @param[in] numGEntities
 * @param[in] sizeofGEntity_t
 * @param[in] clients
 * @param[in] sizeofGameClient
 */
void SV_LocateGameData(sharedEntity_t *gEnts, int numGEntities, int sizeofGEntity_t,
                       playerState_t *clients, int sizeofGameClient)
{
	sv.gentities    = gEnts;
	sv.gentitySize  = sizeofGEntity_t;
	sv.num_entities = numGEntities;

	sv.gameClients    = clients;
	sv.gameClientSize = sizeofGameClient;
}

/**
 * @brief SV_GetUsercmd
 * @param[in] clientNum
 * @param[out] cmd
 */
void SV_GetUsercmd(int clientNum, usercmd_t *cmd)
{
	if (clientNum < 0 || clientNum >= sv_maxclients->integer)
	{
		Com_Error(ERR_DROP, "SV_GetUsercmd: bad clientNum:%i", clientNum);
	}
	*cmd = svs.clients[clientNum].lastUsercmd;
}

/**
 * @brief SV_SendBinaryMessage
 * @param[in] cno
 * @param[out] buf
 * @param[in] buflen
 * @return 1 if message is in queue - 0 not sent (since 2.76)
 */
static int SV_SendBinaryMessage(int cno, char *buf, int buflen)
{
	if (cno < 0 || cno >= sv_maxclients->integer)
	{
		Com_Printf("SV_SendBinaryMessage: bad client %i - message not sent\n", cno);
		svs.clients[cno].binaryMessageLength = 0;
		return 0;
	}

	if (buflen < 0 || buflen > MAX_BINARY_MESSAGE)
	{
		Com_Printf("SV_SendBinaryMessage: bad buffer length %i - message not sent to client #%i\n", buflen, cno);
		svs.clients[cno].binaryMessageLength = 0;
		return 0;
	}

	svs.clients[cno].binaryMessageLength = buflen;
	Com_Memcpy(svs.clients[cno].binaryMessage, buf, buflen);
	return 1;
}

/**
 * @brief SV_BinaryMessageStatus
 * @param[in] cno
 * @return
 */
static int SV_BinaryMessageStatus(int cno)
{
	if (cno < 0 || cno >= sv_maxclients->integer)
	{
		return qfalse;
	}

	if (svs.clients[cno].binaryMessageLength == 0)
	{
		return MESSAGE_EMPTY;
	}

	if (svs.clients[cno].binaryMessageOverflowed)
	{
		return MESSAGE_WAITING_OVERFLOW;
	}

	return MESSAGE_WAITING;
}

/**
 * @brief SV_GameBinaryMessageReceived
 * @param[in] cno
 * @param[in] buf
 * @param[in] buflen
 * @param[in] commandTime
 */
void SV_GameBinaryMessageReceived(int cno, const char *buf, int buflen, int commandTime)
{
	VM_Call(gvm, GAME_MESSAGERECEIVED, cno, buf, buflen, commandTime);
}

//==============================================

extern int S_RegisterSound(const char *name, qboolean compressed);
extern int S_GetSoundLength(sfxHandle_t sfxHandle);

/**
 * @brief The module is making a system call
 * @param[in] args
 * @return
 */
intptr_t SV_GameSystemCalls(intptr_t *args)
{
	switch (args[0])
	{
	case G_PRINT:
		Com_Printf("%s", (char *)VMA(1));
		return 0;
	case G_ERROR:
		Com_Error(ERR_DROP, "%s", (char *)VMA(1));
		return 0;
	case G_MILLISECONDS:
		return Sys_Milliseconds();
	case G_CVAR_REGISTER:
		Cvar_Register(VMA(1), VMA(2), VMA(3), args[4]);
		return 0;
	case G_CVAR_UPDATE:
		Cvar_Update(VMA(1));
		return 0;
	case G_CVAR_SET:
		Cvar_SetSafe((const char *)VMA(1), (const char *)VMA(2));
		return 0;
	case G_CVAR_VARIABLE_INTEGER_VALUE:
		return Cvar_VariableIntegerValue((const char *)VMA(1));
	case G_CVAR_VARIABLE_STRING_BUFFER:
		Cvar_VariableStringBuffer(VMA(1), VMA(2), args[3]);
		return 0;
	case G_CVAR_LATCHEDVARIABLESTRINGBUFFER:
		Cvar_LatchedVariableStringBuffer(VMA(1), VMA(2), args[3]);
		return 0;
	case G_ARGC:
		return Cmd_Argc();
	case G_ARGV:
		Cmd_ArgvBuffer(args[1], VMA(2), args[3]);
		return 0;
	case G_SEND_CONSOLE_COMMAND:
		if (sv.demoState == DS_RECORDING)
		{
			SV_DemoWriteServerConsoleCommand(args[1], VMA(2));
		}

		Cbuf_ExecuteText(args[1], VMA(2));
		return 0;
	case G_FS_FOPEN_FILE:
		return FS_FOpenFileByMode(VMA(1), VMA(2), args[3]);
	case G_FS_READ:
		FS_Read(VMA(1), args[2], args[3]);
		return 0;
	case G_FS_WRITE:
		return FS_Write(VMA(1), args[2], args[3]);
	case G_FS_RENAME:
		FS_Rename(VMA(1), VMA(2));
		return 0;
	case G_FS_FCLOSE_FILE:
		FS_FCloseFile(args[1]);
		return 0;
	case G_FS_GETFILELIST:
		return FS_GetFileList(VMA(1), VMA(2), VMA(3), args[4]);

	case G_LOCATE_GAME_DATA:
		SV_LocateGameData(VMA(1), args[2], args[3], VMA(4), args[5]);
		return 0;
	case G_DROP_CLIENT:
		SV_GameDropClient(args[1], VMA(2), args[3]);
		return 0;
	case G_SEND_SERVER_COMMAND:
#ifdef FEATURE_TRACKER
		if (!Tracker_catchServerCommand(args[1], VMA(2)))
#endif
		{
			SV_GameSendServerCommand(args[1], VMA(2));
		}
		return 0;
	case G_LINKENTITY:
		SV_LinkEntity(VMA(1));
		return 0;
	case G_UNLINKENTITY:
		SV_UnlinkEntity(VMA(1));
		return 0;
	case G_ENTITIES_IN_BOX:
		return SV_AreaEntities(VMA(1), VMA(2), VMA(3), args[4]);
	case G_ENTITY_CONTACT:
		return SV_EntityContact(VMA(1), VMA(2), VMA(3), /* int capsule */ qfalse);
	case G_ENTITY_CONTACTCAPSULE:
		return SV_EntityContact(VMA(1), VMA(2), VMA(3), /* int capsule */ qtrue);
	case G_TRACE:
		SV_Trace(VMA(1), VMA(2), VMA(3), VMA(4), VMA(5), args[6], args[7], /* int capsule */ qfalse);
		return 0;
	case G_TRACECAPSULE:
		SV_Trace(VMA(1), VMA(2), VMA(3), VMA(4), VMA(5), args[6], args[7], /* int capsule */ qtrue);
		return 0;
	case G_POINT_CONTENTS:
		return SV_PointContents(VMA(1), args[2]);
	case G_SET_BRUSH_MODEL:
		SV_SetBrushModel(VMA(1), VMA(2));
		return 0;
	case G_IN_PVS:
		return SV_inPVS(VMA(1), VMA(2));
	case G_IN_PVS_IGNORE_PORTALS:
		return SV_inPVSIgnorePortals(VMA(1), VMA(2));

	case G_SET_CONFIGSTRING:
		// Don't allow the game to overwrite demo configstrings (unless it modifies the normal spectator clients configstrings, this exception allows for player connecting during a demo playback to be correctly rendered, else they will get an empty configstring so no icon, no name, nothing...)
		// ATTENTION: sv.demoState check must be placed LAST! Else, it will short-circuit and prevent normal players configstrings from being set!
		if ((sv_democlients->integer > 0 && args[1] >= CS_PLAYERS + sv_democlients->integer && args[1] < CS_PLAYERS + sv_maxclients->integer) || sv.demoState != DS_PLAYBACK)
		{
			SV_SetConfigstring(args[1], VMA(2));
		}
		return 0;
	case G_GET_CONFIGSTRING:
		SV_GetConfigstring(args[1], VMA(2), args[3]);
		return 0;
	case G_SET_USERINFO:
		SV_SetUserinfo(args[1], VMA(2));
		return 0;
	case G_GET_USERINFO:
		SV_GetUserinfo(args[1], VMA(2), args[3]);
		return 0;
	case G_GET_SERVERINFO:
		SV_GetServerinfo(VMA(1), args[2]);
		return 0;
	case G_ADJUST_AREA_PORTAL_STATE:
		SV_AdjustAreaPortalState(VMA(1), args[2]);
		return 0;
	case G_AREAS_CONNECTED:
		return CM_AreasConnected(args[1], args[2]);

	case G_BOT_ALLOCATE_CLIENT:
		return SV_BotAllocateClient(args[1]);

	case G_GET_USERCMD:
		SV_GetUsercmd(args[1], VMA(2));
		return 0;
	case G_GET_ENTITY_TOKEN:
	{
		const char *s;

		s = COM_Parse(&sv.entityParsePoint);
		Q_strncpyz(VMA(1), s, args[2]);
		if (!sv.entityParsePoint && !s[0])
		{
			return qfalse;
		}
		else
		{
			return qtrue;
		}
	}

	case G_DEBUG_POLYGON_CREATE:
		return BotImport_DebugPolygonCreate(args[1], args[2], VMA(3));
	case G_DEBUG_POLYGON_DELETE:
		BotImport_DebugPolygonDelete(args[1]);
		return 0;
	case G_REAL_TIME:
		return Com_RealTime(VMA(1));
	case G_SNAPVECTOR:
		Sys_SnapVector(VMA(1));
		return 0;
	case G_GETTAG:
		return SV_GetTag(args[1], args[2], VMA(3), VMA(4));

	case G_REGISTERTAG:
		return SV_LoadTag(VMA(1));

	case G_REGISTERSOUND:
		return S_RegisterSound(VMA(1), args[2]);
	case G_GET_SOUND_LENGTH:
		return S_GetSoundLength(args[1]);

	//====================================

	case BOTLIB_SETUP:
		return 0;
	case BOTLIB_SHUTDOWN:
		return -1;
	case BOTLIB_LIBVAR_SET:
		return 0;
	case BOTLIB_LIBVAR_GET:
		return 0;

	case BOTLIB_PC_LOAD_SOURCE:
		return botlib_export->PC_LoadSourceHandle(VMA(1));
	case BOTLIB_PC_FREE_SOURCE:
		return botlib_export->PC_FreeSourceHandle(args[1]);
	case BOTLIB_PC_READ_TOKEN:
		return botlib_export->PC_ReadTokenHandle(args[1], VMA(2));
	case BOTLIB_PC_SOURCE_FILE_AND_LINE:
		return botlib_export->PC_SourceFileAndLine(args[1], VMA(2), VMA(3));
	case BOTLIB_PC_UNREAD_TOKEN:
		botlib_export->PC_UnreadLastTokenHandle(args[1]);
		return 0;

	case BOTLIB_GET_CONSOLE_MESSAGE:
		return SV_BotGetConsoleMessage(args[1], VMA(2), args[3]);
	case BOTLIB_USER_COMMAND:
	{
		unsigned clientNum = args[1];

		if (clientNum < sv_maxclients->integer)
		{
			SV_ClientThink(&svs.clients[clientNum], VMA(2));
		}
	}
		return 0;

	case BOTLIB_EA_COMMAND:
	{
		unsigned clientNum = args[1];

		if (clientNum < sv_maxclients->integer)
		{
			SV_ExecuteClientCommand(&svs.clients[clientNum], VMA(2), qtrue, qfalse);
		}
	}

		return 0;

	case TRAP_MEMSET:
		Com_Memset(VMA(1), args[2], args[3]);
		return 0;

	case TRAP_MEMCPY:
		Com_Memcpy(VMA(1), VMA(2), args[3]);
		return 0;

	case TRAP_STRNCPY:
		return (intptr_t)strncpy(VMA(1), VMA(2), args[3]);

	case TRAP_SIN:
		return Q_FloatAsInt(sin(VMF(1)));

	case TRAP_COS:
		return Q_FloatAsInt(cos(VMF(1)));

	case TRAP_ATAN2:
		return Q_FloatAsInt(atan2(VMF(1), VMF(2)));

	case TRAP_SQRT:
		return Q_FloatAsInt(sqrt(VMF(1)));

	case TRAP_MATRIXMULTIPLY: // never called for real
		_MatrixMultiply(VMA(1), VMA(2), VMA(3));
		return 0;

	case TRAP_ANGLEVECTORS:
		angles_vectors(VMA(1), VMA(2), VMA(3), VMA(4));
		return 0;

	case TRAP_PERPENDICULARVECTOR:
		PerpendicularVector(VMA(1), VMA(2));
		return 0;

	case TRAP_FLOOR:
		return Q_FloatAsInt(floor(VMF(1)));

	case TRAP_CEIL:
		return Q_FloatAsInt(ceil(VMF(1)));

	case PB_STAT_REPORT:
		return 0;

	case G_SENDMESSAGE:
		return SV_SendBinaryMessage(args[1], VMA(2), args[3]);
	case G_MESSAGESTATUS:
		return SV_BinaryMessageStatus(args[1]);

	case TVG_GET_PLAYERSTATE:
		return SV_CL_GetPlayerstate(args[1], VMA(2));

	case G_TRAP_GETVALUE:
		return VM_Ext_GetValue(VMA(1), args[2], VMA(3));

	case G_DEMOSUPPORT:
		SV_DemoSupport(VMA(1));
		return 0;

	case G_SNAPSHOT_CALLBACK_EXT:
		sv.snapshotCallbackExt = qtrue;
		return 0;

	case G_SNAPSHOT_SETCLIENTMASK:
		SV_SnapshotSetClientMask(args[1], VMU64(2));
		return 0;

	case G_CVAR_SET_DESCRIPTION:
		return Cvar_SetDescriptionByName(VMA(1), VMA(2));

#ifdef __EMSCRIPTEN__
	case G_NITMOD_DATABASE_STORAGE:
	{
#ifndef NITMOD_WEB_DB_LIFECYCLE
        /* Do not activate asynchronous persistence without a host lifecycle
         * that drains before every destructive map/module transition. */
        return args[1] == 0 ? 0 : -1;
#else
		void *buffer = NULL;
		const char *path = NULL;
		size_t heapSize = emscripten_get_heap_size();
		uintptr_t address;
		if (!VM_Ext_IsActive(G_NITMOD_DATABASE_STORAGE) || args[1] < 0 || args[1] > 6) return -1;
		if (args[1] == 0) return Sys_WebDatabaseSupported();
		if (args[1] == 1 || args[1] == 2)
		{
			path = VMA(3); address = (uintptr_t)path;
			if (!address || address >= heapSize || !memchr(path, 0, MIN((size_t)MAX_QPATH, heapSize - address))) return -1;
		}
		if (args[1] == 2 || args[1] == 3 || args[1] == 4)
		{
			if (args[6] < 1 || args[6] > 64 * 1024 * 1024 || (args[1] == 3 && args[6] != 2 * sizeof(int))) return -1;
			buffer = VMA(5); address = (uintptr_t)buffer;
			if (!address || address >= heapSize || (size_t)args[6] > heapSize - address ||
			    (args[1] == 3 && address % sizeof(int))) return -1;
		}
		if (args[1] == 1 || args[1] == 2) return Sys_WebDatabaseRequest(args[1], path, args[4], buffer, args[6]);
		if (args[1] == 3) return Sys_WebDatabasePoll(args[2], (int *)buffer, (int *)buffer + 1, NULL, 0);
		if (args[1] == 4) return Sys_WebDatabaseCopy(args[2], buffer, args[6]);
		if (args[1] == 5) return Sys_WebDatabaseRelease(args[2]);
		return Sys_WebDatabaseCancel(args[2]);
#endif
	}
	case G_NITMOD_NXAC_TRANSPORT:
	{
		void *buffer = NULL;
		if (!VM_Ext_IsActive(G_NITMOD_NXAC_TRANSPORT) || args[1] < 0 || args[1] > 8)
		{
			return -1;
		}
		if (args[1] == 3 || args[1] == 5 || args[1] == 6)
		{
			size_t heapSize = emscripten_get_heap_size();
			uintptr_t address;
			if (args[4] < 1 || args[4] > 16384 || (args[1] == 3 && args[4] != 24))
			{
				return -1;
			}
			buffer = VMA(3);
			address = (uintptr_t)buffer;
			if (!address || address >= heapSize || (size_t)args[4] > heapSize - address)
			{
				return -1;
			}
		}
		return NET_NxACWebCall(NXWEB_QAGAME, NULL, args[1], args[2], buffer, args[4], args[5]);
	}
#endif

	default:
		Com_Error(ERR_DROP, "Bad game system trap: %ld", (long int) args[0]);
		break;
	}

	return -1;
}

#ifdef __EMSCRIPTEN__
/* Version-1 private export; call only after the matching storage trap was
 * negotiated. Unrelated game modules retain their original export ABI. */
#define GAME_NITMOD_DB_PRE_SHUTDOWN 0x4e444201
static struct { int action,value; char text[MAX_STRING_CHARS]; } nitmodDbDeferred;
/* Console admission must not mutate a transition already waiting on DB. */
qboolean SV_NitmodDatabasePendingTransition(void)
{
    return nitmodDbDeferred.action != 0;
}
static int SV_NitmodDatabasePrepare(void)
{
    if(!gvm || !VM_Ext_IsActive(G_NITMOD_DATABASE_STORAGE)) return 1;
    return VM_Call(gvm,GAME_NITMOD_DB_PRE_SHUTDOWN);
}
qboolean SV_NitmodDatabaseDefer(int action,const char *text,int value)
{
    if(com_errorEntered || !gvm || !VM_Ext_IsActive(G_NITMOD_DATABASE_STORAGE)) return qfalse;
    if(nitmodDbDeferred.action)
    {
        if(action!=nitmodDbDeferred.action || value!=nitmodDbDeferred.value ||
           strcmp(text?text:"",nitmodDbDeferred.text))
            Com_Printf("[SQLite] Server transition already waiting for database commit.\n");
        return qtrue;
    }
    if(SV_NitmodDatabasePrepare()!=2) return qfalse;
    nitmodDbDeferred.action=action; nitmodDbDeferred.value=value;
    Q_strncpyz(nitmodDbDeferred.text,text?text:"",sizeof(nitmodDbDeferred.text));
    Com_Printf("[SQLite] Waiting for pending commits before server transition.\n");
    return qtrue;
}
qboolean SV_NitmodDatabaseFrame(void)
{
    int action,value;
    char text[MAX_STRING_CHARS];
    if(!nitmodDbDeferred.action) return qfalse;
    if(SV_NitmodDatabasePrepare()==2) return qtrue;
    action=nitmodDbDeferred.action; value=nitmodDbDeferred.value;
    Q_strncpyz(text,nitmodDbDeferred.text,sizeof(text));
    memset(&nitmodDbDeferred,0,sizeof(nitmodDbDeferred));
    /* Resume directly. Map names and messages remain data, never commands. */
    switch(action)
    {
    case NITMOD_DB_MAP: SV_NitmodDatabaseMap(text,value); break;
    case NITMOD_DB_RESTART: SV_NitmodDatabaseRestart(value); break;
    case NITMOD_DB_SPAWN: SV_SpawnServer(text); break;
    case NITMOD_DB_SHUTDOWN: SV_Shutdown(text); break;
    default: break;
    }
    return qtrue;
}
#endif

/**
 * @brief Called every time a map changes
 */
void SV_ShutdownGameProgs(void)
{
	if (!gvm)
	{
		return;
	}

#ifdef __EMSCRIPTEN__
    /* Normal transitions drained before their destructive boundary. Forced
     * Hunk_Clear/error teardown cannot await IndexedDB: detach requests
     * below, without claiming that they were committed or rolled back. */
    if(SV_NitmodDatabasePrepare()==2)
        Com_Printf("[SQLite] Forced VM shutdown with unfinished persistence; no completion is reported.\n");
#endif

	// stop any demos
	SV_DemoStopAll();

	// shutdown game
	VM_Call(gvm, GAME_SHUTDOWN, qfalse);
#ifdef __EMSCRIPTEN__
	Sys_WebDatabaseReset();
	memset(&nitmodDbDeferred,0,sizeof(nitmodDbDeferred));
	NET_NxACWebReset(NXWEB_QAGAME);
#endif
	VM_Free(gvm);
	gvm = NULL;

	svcls.TVServer = qfalse;
}

/**
 * @brief Called for both a full init and a restart
 * @param[in] restart
 */
static void SV_InitGameVM(qboolean restart)
{
	int i;

	// start the entity parsing at the beginning
	sv.entityParsePoint = CM_EntityString();

	// clear all gentity pointers that might still be set from
	// a previous level
	for (i = 0 ; i < sv_maxclients->integer ; i++)
	{
		svs.clients[i].gentity = NULL;
	}

	if (svcls.TVServer && !restart)
	{
		for (i = 0; i < MAX_CONFIGSTRINGS; i++)
		{
			if (!svcl.gameState.stringOffsets[i] || i == CS_SYSTEMINFO)
			{
				continue;
			}

			SV_SetConfigstring(i, svcl.gameState.stringData + svcl.gameState.stringOffsets[i]);
		}
	}

#ifdef __EMSCRIPTEN__
	memset(&nitmodDbDeferred,0,sizeof(nitmodDbDeferred));
#endif
	// mark all extensions as inactive
	VM_Ext_ResetActive();
#ifdef __EMSCRIPTEN__
	NET_NxACWebReset(NXWEB_QAGAME);
#endif

	// use the current msec count for a random seed
	// init for this gamestate
	VM_Call(gvm, GAME_INIT, sv.time, Com_Milliseconds(), restart, qtrue, ETLEGACY_VERSION_INT);
}

/**
 * @brief Called on a map_restart, but not on a normal map change
 */
void SV_RestartGameProgs(void)
{
	if (!gvm)
	{
		return;
	}
#ifdef __EMSCRIPTEN__
    if(SV_NitmodDatabasePrepare()==2)
        Com_Printf("[SQLite] Forced VM restart with unfinished persistence; no completion is reported.\n");
#endif
	VM_Call(gvm, GAME_SHUTDOWN, qtrue);
#ifdef __EMSCRIPTEN__
    Sys_WebDatabaseReset();
#endif

	// do a restart instead of a free
	gvm = VM_Restart(gvm);
	if (!gvm)
	{
		Com_Error(ERR_FATAL, "VM_Restart on game failed");
	}

	SV_InitGameVM(qtrue);

#ifdef FEATURE_TRACKER
	Tracker_MapRestart();
#endif
}

/**
 * @brief Called on a normal map change, not on a map_restart
 */
void SV_InitGameProgs(void)
{
	sv.num_tagheaders = 0;
	sv.num_tags       = 0;

	// load the dll
	if (svcls.state >= CA_AUTHORIZING)
	{
		gvm            = VM_Create("tvgame", qfalse, SV_GameSystemCalls, VMI_NATIVE);
		svcls.TVServer = gvm != NULL;
	}
	else
	{
		gvm = VM_Create("qagame", qfalse, SV_GameSystemCalls, VMI_NATIVE);
	}

	if (!gvm)
	{
		VM_Error(ERR_FATAL, "game", Sys_GetDLLName("qagame"));
	}

	SV_InitGameVM(qfalse);

#ifdef FEATURE_TRACKER
	Tracker_Map(sv_mapname->string);
#endif
}

/**
 * @brief See if the current console command is claimed by the game
 * @return
 */
qboolean SV_GameCommand(void)
{
	if (sv.state != SS_GAME)
	{
		return qfalse;
	}

	return VM_Call(gvm, GAME_CONSOLE_COMMAND);
}

#ifndef DEDICATED
extern qboolean CL_GetTag(int clientNum, char *tagname, orientation_t *orientation);
#endif

/**
 * @brief SV_GetTag
 * @param[in] clientNum - unused
 * @param[in] tagFileNumber
 * @param[out] tagname
 * @param[out] orientation
 * @return qfalse if unable to retrieve tag information for this client
 */
qboolean SV_GetTag(int clientNum, int tagFileNumber, char *tagname, orientation_t *orientation)
{
	if (tagFileNumber > 0 && tagFileNumber <= sv.num_tagheaders)
	{
		int i;

		for (i = sv.tagHeadersExt[tagFileNumber - 1].start; i < sv.tagHeadersExt[tagFileNumber - 1].start + sv.tagHeadersExt[tagFileNumber - 1].count; i++)
		{
			if (!Q_stricmp(sv.tags[i].name, tagname))
			{
				VectorCopy(sv.tags[i].origin, orientation->origin);
				VectorCopy(sv.tags[i].axis[0], orientation->axis[0]);
				VectorCopy(sv.tags[i].axis[1], orientation->axis[1]);
				VectorCopy(sv.tags[i].axis[2], orientation->axis[2]);
				return qtrue;
			}
		}
	}

	// lets try and remove the inconsitancy between ded/non-ded servers...
	// - bleh, some code in clientthink_real really relies on this working on player models...
	// only only this code for the release builds so we can test out the hitbox code with the clients
#if !defined(DEDICATED) && !defined(LEGACY_DEBUG)
	if (com_dedicated->integer)
	{
		return qfalse;
	}

	if (clientNum < 0 || clientNum >= MAX_CLIENTS)
	{
		return qfalse;
	}

	return CL_GetTag(clientNum, tagname, orientation);
#else
	return qfalse;
#endif
}
