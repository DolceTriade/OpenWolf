/*
===========================================================================

OpenWolf GPL Source Code
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company. 

This file is part of the OpenWolf GPL Source Code (OpenWolf Source Code).  

OpenWolf Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

OpenWolf Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenWolf Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the OpenWolf Source Code is also subject to certain additional terms. 
You should have received a copy of these additional terms immediately following the 
terms and conditions of the GNU General Public License which accompanied the OpenWolf 
Source Code.  If not, please request a copy in writing from id Software at the address 
below.

If you have questions concerning this license or the applicable additional terms, you 
may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, 
Maryland 20850 USA.

===========================================================================
*/

#include "../idLib/precompiled.h"
#include "client.h"

#include "../botlib/botlib.h"

extern botlib_export_t *botlib_export;

vm_t           *uivm;


// ydnar: can we put this in a header, pls?
void            Key_GetBindingByString(const char *binding, int *key1, int *key2);


/*
====================
GetClientState
====================
*/
static void GetClientState(uiClientState_t * state)
{
	state->connectPacketCount = clc.connectPacketCount;
	state->connState = cls.state;
	Q_strncpyz(state->servername, cls.servername, sizeof(state->servername));
	Q_strncpyz(state->updateInfoString, cls.updateInfoString, sizeof(state->updateInfoString));
	Q_strncpyz(state->messageString, clc.serverMessage, sizeof(state->messageString));
	state->clientNum = cl.snap.ps.clientNum;
}

/*
====================
LAN_LoadCachedServers
====================
*/
void LAN_LoadCachedServers()
{
	int             size;
	fileHandle_t    fileIn;
	char            filename[MAX_QPATH];

	cls.numglobalservers = cls.numfavoriteservers = 0;
	cls.numGlobalServerAddresses = 0;

	if(com_gameInfo.usesProfiles && cl_profile->string[0])
	{
		Com_sprintf(filename, sizeof(filename), "profiles/%s/servercache.dat", cl_profile->string);
	}
	else
	{
		Q_strncpyz(filename, "servercache.dat", sizeof(filename));
	}

	// Arnout: moved to mod/profiles dir
	//if (FS_SV_FOpenFileRead(filename, &fileIn)) {
	if(FS_FOpenFileRead(filename, &fileIn, qtrue))
	{
		FS_Read(&cls.numglobalservers, sizeof(int), fileIn);
		FS_Read(&cls.numfavoriteservers, sizeof(int), fileIn);
		FS_Read(&size, sizeof(int), fileIn);
		if(size == sizeof(cls.globalServers) + sizeof(cls.favoriteServers))
		{
			FS_Read(&cls.globalServers, sizeof(cls.globalServers), fileIn);
			FS_Read(&cls.favoriteServers, sizeof(cls.favoriteServers), fileIn);
		}
		else
		{
			cls.numglobalservers = cls.numfavoriteservers = 0;
			cls.numGlobalServerAddresses = 0;
		}
		FS_FCloseFile(fileIn);
	}
}

/*
====================
LAN_SaveServersToCache
====================
*/
void LAN_SaveServersToCache()
{
	int             size;
	fileHandle_t    fileOut;
	char            filename[MAX_QPATH];

	if(com_gameInfo.usesProfiles && cl_profile->string[0])
	{
		Com_sprintf(filename, sizeof(filename), "profiles/%s/servercache.dat", cl_profile->string);
	}
	else
	{
		Q_strncpyz(filename, "servercache.dat", sizeof(filename));
	}

	// Arnout: moved to mod/profiles dir
	//fileOut = FS_SV_FOpenFileWrite(filename);
	fileOut = FS_FOpenFileWrite(filename);
	FS_Write(&cls.numglobalservers, sizeof(int), fileOut);
	FS_Write(&cls.numfavoriteservers, sizeof(int), fileOut);
	size = sizeof(cls.globalServers) + sizeof(cls.favoriteServers);
	FS_Write(&size, sizeof(int), fileOut);
	FS_Write(&cls.globalServers, sizeof(cls.globalServers), fileOut);
	FS_Write(&cls.favoriteServers, sizeof(cls.favoriteServers), fileOut);
	FS_FCloseFile(fileOut);
}


/*
====================
GetNews
====================
*/
qboolean GetNews( qboolean begin )
{
	if ( begin ) // if not already using curl, start the download
	{
		CL_RequestMotd();
		Cvar_Set( "cl_newsString", "Retrieving..." );
	}

	if ( Cvar_VariableString( "cl_newsString" ) [ 0 ] == 'R' )
	{
		return qfalse;
	}
	else
	{
		return qtrue;
	}
}

/*
====================
LAN_ResetPings
====================
*/
static void LAN_ResetPings(int source)
{
	int             count, i;
	serverInfo_t   *servers = NULL;

	count = 0;

	switch (source)
	{
		case AS_LOCAL:
			servers = &cls.localServers[0];
			count = MAX_OTHER_SERVERS;
			break;
		case AS_GLOBAL:
			servers = &cls.globalServers[0];
			count = MAX_GLOBAL_SERVERS;
			break;
		case AS_FAVORITES:
			servers = &cls.favoriteServers[0];
			count = MAX_OTHER_SERVERS;
			break;
	}
	if(servers)
	{
		for(i = 0; i < count; i++)
		{
			servers[i].ping = -1;
		}
	}
}

/*
====================
LAN_AddServer
====================
*/
static int LAN_AddServer( int source, const char *name, const char *address ) {
	int max, *count, i;
	netadr_t adr;
	serverInfo_t *servers = NULL;
	max = MAX_OTHER_SERVERS;
	count = 0;

	switch ( source ) {
	case AS_LOCAL:
		count = &cls.numlocalservers;
		servers = &cls.localServers[0];
		break;
	case AS_GLOBAL:
		max = MAX_GLOBAL_SERVERS;
		count = &cls.numglobalservers;
		servers = &cls.globalServers[0];
		break;
	case AS_FAVORITES:
		count = &cls.numfavoriteservers;
		servers = &cls.favoriteServers[0];
		break;
	}
	if ( servers && *count < max ) {
		NET_StringToAdr( address, &adr, NA_IP );
		for ( i = 0; i < *count; i++ ) {
			if ( NET_CompareAdr( servers[i].adr, adr ) ) {
				break;
			}
		}
		if ( i >= *count ) {
			servers[*count].adr = adr;
			Q_strncpyz( servers[*count].hostName, name, sizeof( servers[*count].hostName ) );
			servers[*count].visible = qtrue;
			( *count )++;
			return 1;
		}
		return 0;
	}
	return -1;
}

/*
====================
LAN_RemoveServer
====================
*/
static void LAN_RemoveServer( int source, const char *addr ) {
	int *count, i;
	serverInfo_t *servers = NULL;
	count = 0;
	switch ( source ) {
	case AS_LOCAL:
		count = &cls.numlocalservers;
		servers = &cls.localServers[0];
		break;
	case AS_GLOBAL:
		count = &cls.numglobalservers;
		servers = &cls.globalServers[0];
		break;
	case AS_FAVORITES:
		count = &cls.numfavoriteservers;
		servers = &cls.favoriteServers[0];
		break;
	}
	if ( servers ) {
		netadr_t comp;
		NET_StringToAdr( addr, &comp, NA_IP );
		for ( i = 0; i < *count; i++ ) {
			if ( NET_CompareAdr( comp, servers[i].adr ) ) {
				int j = i;
				while ( j < *count - 1 ) {
					Com_Memcpy( &servers[j], &servers[j + 1], sizeof( servers[j] ) );
					j++;
				}
				( *count )--;
				break;
			}
		}
	}
}

/*
====================
LAN_GetServerCount
====================
*/
static int LAN_GetServerCount(int source)
{
	switch (source)
	{
		case AS_LOCAL:
			return cls.numlocalservers;
			break;
		case AS_GLOBAL:
			return cls.numglobalservers;
			break;
		case AS_FAVORITES:
			return cls.numfavoriteservers;
			break;
	}
	return 0;
}

/*
====================
LAN_GetLocalServerAddressString
====================
*/
static void LAN_GetServerAddressString( int source, int n, char *buf, int buflen ) {
	switch ( source ) {
	case AS_LOCAL:
		if ( n >= 0 && n < MAX_OTHER_SERVERS ) {
			Q_strncpyz( buf, NET_AdrToStringwPort( cls.localServers[n].adr ), buflen );
			return;
		}
		break;
	case AS_GLOBAL:
		if ( n >= 0 && n < MAX_GLOBAL_SERVERS ) {
			Q_strncpyz( buf, NET_AdrToStringwPort( cls.globalServers[n].adr ), buflen );
			return;
		}
		break;
	case AS_FAVORITES:
		if ( n >= 0 && n < MAX_OTHER_SERVERS ) {
			Q_strncpyz( buf, NET_AdrToStringwPort( cls.favoriteServers[n].adr ), buflen );
			return;
		}
		break;
	}
	buf[0] = '\0';
}

/*
====================
LAN_GetServerInfo
====================
*/
static void LAN_GetServerInfo(int source, int n, char *buf, int buflen)
{
	char            info[MAX_STRING_CHARS];
	serverInfo_t   *server = NULL;

	info[0] = '\0';
	switch (source)
	{
		case AS_LOCAL:
			if(n >= 0 && n < MAX_OTHER_SERVERS)
			{
				server = &cls.localServers[n];
			}
			break;
		case AS_GLOBAL:
			if(n >= 0 && n < MAX_GLOBAL_SERVERS)
			{
				server = &cls.globalServers[n];
			}
			break;
		case AS_FAVORITES:
			if(n >= 0 && n < MAX_OTHER_SERVERS)
			{
				server = &cls.favoriteServers[n];
			}
			break;
	}
	if(server && buf)
	{
		buf[0] = '\0';
		Info_SetValueForKey(info, "hostname", server->hostName);
		Info_SetValueForKey(info, "serverload", va("%i", server->load));
		Info_SetValueForKey(info, "mapname", server->mapName);
		Info_SetValueForKey(info, "clients", va("%i", server->clients));
		Info_SetValueForKey(info, "sv_maxclients", va("%i", server->maxClients));
		Info_SetValueForKey(info, "ping", va("%i", server->ping));
		Info_SetValueForKey(info, "minping", va("%i", server->minPing));
		Info_SetValueForKey(info, "maxping", va("%i", server->maxPing));
		Info_SetValueForKey(info, "game", server->game);
		Info_SetValueForKey(info, "gametype", va("%i", server->gameType));
		Info_SetValueForKey(info, "nettype", va("%i", server->netType));
		Info_SetValueForKey(info, "addr", NET_AdrToStringwPort(server->adr));
		Info_SetValueForKey(info, "sv_allowAnonymous", va("%i", server->allowAnonymous));
		Info_SetValueForKey(info, "friendlyFire", va("%i", server->friendlyFire));	// NERVE - SMF
		Info_SetValueForKey(info, "maxlives", va("%i", server->maxlives));	// NERVE - SMF
		Info_SetValueForKey(info, "needpass", va("%i", server->needpass));	// NERVE - SMF
		Info_SetValueForKey(info, "punkbuster", va("%i", server->punkbuster));	// DHM - Nerve
		Info_SetValueForKey(info, "gamename", server->gameName);	// Arnout
		Info_SetValueForKey(info, "g_antilag", va("%i", server->antilag));	// TTimo
		Info_SetValueForKey(info, "weaprestrict", va("%i", server->weaprestrict));
		Info_SetValueForKey(info, "balancedteams", va("%i", server->balancedteams));
		Q_strncpyz(buf, info, buflen);
	}
	else
	{
		if(buf)
		{
			buf[0] = '\0';
		}
	}
}

/*
====================
LAN_GetServerPing
====================
*/
static int LAN_GetServerPing(int source, int n)
{
	serverInfo_t   *server = NULL;

	switch (source)
	{
		case AS_LOCAL:
			if(n >= 0 && n < MAX_OTHER_SERVERS)
			{
				server = &cls.localServers[n];
			}
			break;
		case AS_GLOBAL:
			if(n >= 0 && n < MAX_GLOBAL_SERVERS)
			{
				server = &cls.globalServers[n];
			}
			break;
		case AS_FAVORITES:
			if(n >= 0 && n < MAX_OTHER_SERVERS)
			{
				server = &cls.favoriteServers[n];
			}
			break;
	}
	if(server)
	{
		return server->ping;
	}
	return -1;
}

/*
====================
LAN_GetServerPtr
====================
*/
static serverInfo_t *LAN_GetServerPtr(int source, int n)
{
	switch (source)
	{
		case AS_LOCAL:
			if(n >= 0 && n < MAX_OTHER_SERVERS)
			{
				return &cls.localServers[n];
			}
			break;
		case AS_GLOBAL:
			if(n >= 0 && n < MAX_GLOBAL_SERVERS)
			{
				return &cls.globalServers[n];
			}
			break;
		case AS_FAVORITES:
			if(n >= 0 && n < MAX_OTHER_SERVERS)
			{
				return &cls.favoriteServers[n];
			}
			break;
	}
	return NULL;
}

/*
====================
LAN_CompareServers
====================
*/
static int LAN_CompareServers(int source, int sortKey, int sortDir, int s1, int s2)
{
	int             res;
	serverInfo_t   *server1, *server2;
	char            name1[MAX_NAME_LENGTH], name2[MAX_NAME_LENGTH];

	server1 = LAN_GetServerPtr(source, s1);
	server2 = LAN_GetServerPtr(source, s2);
	if(!server1 || !server2)
	{
		return 0;
	}

	res = 0;
	switch (sortKey)
	{
		case SORT_HOST:
			//% res = Q_stricmp( server1->hostName, server2->hostName );
			Q_strncpyz(name1, server1->hostName, sizeof(name1));
			Q_CleanStr(name1);
			Q_strncpyz(name2, server2->hostName, sizeof(name2));
			Q_CleanStr(name2);
			res = Q_stricmp(name1, name2);
			break;

		case SORT_MAP:
			res = Q_stricmp(server1->mapName, server2->mapName);
			break;
		case SORT_CLIENTS:
			if(server1->clients < server2->clients)
			{
				res = -1;
			}
			else if(server1->clients > server2->clients)
			{
				res = 1;
			}
			else
			{
				res = 0;
			}
			break;
		case SORT_GAME:
			if(server1->gameType < server2->gameType)
			{
				res = -1;
			}
			else if(server1->gameType > server2->gameType)
			{
				res = 1;
			}
			else
			{
				res = 0;
			}
			break;
		case SORT_PING:
			if(server1->ping < server2->ping)
			{
				res = -1;
			}
			else if(server1->ping > server2->ping)
			{
				res = 1;
			}
			else
			{
				res = 0;
			}
			break;
	}

	if(sortDir)
	{
		if(res < 0)
		{
			return 1;
		}
		if(res > 0)
		{
			return -1;
		}
		return 0;
	}
	return res;
}

/*
====================
LAN_GetPingQueueCount
====================
*/
static int LAN_GetPingQueueCount(void)
{
	return (CL_GetPingQueueCount());
}

/*
====================
LAN_ClearPing
====================
*/
static void LAN_ClearPing(int n)
{
	CL_ClearPing(n);
}

/*
====================
LAN_GetPing
====================
*/
static void LAN_GetPing(int n, char *buf, int buflen, int *pingtime)
{
	CL_GetPing(n, buf, buflen, pingtime);
}

/*
====================
LAN_GetPingInfo
====================
*/
static void LAN_GetPingInfo(int n, char *buf, int buflen)
{
	CL_GetPingInfo(n, buf, buflen);
}

/*
====================
LAN_MarkServerVisible
====================
*/
static void LAN_MarkServerVisible(int source, int n, qboolean visible)
{
	if(n == -1)
	{
		int             count = MAX_OTHER_SERVERS;
		serverInfo_t   *server = NULL;

		switch (source)
		{
			case AS_LOCAL:
				server = &cls.localServers[0];
				break;
			case AS_GLOBAL:
				server = &cls.globalServers[0];
				count = MAX_GLOBAL_SERVERS;
				break;
			case AS_FAVORITES:
				server = &cls.favoriteServers[0];
				break;
		}
		if(server)
		{
			for(n = 0; n < count; n++)
			{
				server[n].visible = visible;
			}
		}

	}
	else
	{
		switch (source)
		{
			case AS_LOCAL:
				if(n >= 0 && n < MAX_OTHER_SERVERS)
				{
					cls.localServers[n].visible = visible;
				}
				break;
			case AS_GLOBAL:
				if(n >= 0 && n < MAX_GLOBAL_SERVERS)
				{
					cls.globalServers[n].visible = visible;
				}
				break;
			case AS_FAVORITES:
				if(n >= 0 && n < MAX_OTHER_SERVERS)
				{
					cls.favoriteServers[n].visible = visible;
				}
				break;
		}
	}
}


/*
=======================
LAN_ServerIsVisible
=======================
*/
static int LAN_ServerIsVisible(int source, int n)
{
	switch (source)
	{
		case AS_LOCAL:
			if(n >= 0 && n < MAX_OTHER_SERVERS)
			{
				return cls.localServers[n].visible;
			}
			break;
		case AS_GLOBAL:
			if(n >= 0 && n < MAX_GLOBAL_SERVERS)
			{
				return cls.globalServers[n].visible;
			}
			break;
		case AS_FAVORITES:
			if(n >= 0 && n < MAX_OTHER_SERVERS)
			{
				return cls.favoriteServers[n].visible;
			}
			break;
	}
	return qfalse;
}

/*
=======================
LAN_UpdateVisiblePings
=======================
*/
qboolean LAN_UpdateVisiblePings(int source)
{
	return CL_UpdateVisiblePings_f(source);
}

/*
====================
LAN_GetServerStatus
====================
*/
int LAN_GetServerStatus(char *serverAddress, char *serverStatus, int maxLen)
{
	return CL_ServerStatus(serverAddress, serverStatus, maxLen);
}

/*
=======================
LAN_ServerIsInFavoriteList
=======================
*/
qboolean LAN_ServerIsInFavoriteList(int source, int n)
{
	int             i;
	serverInfo_t   *server = NULL;

	switch (source)
	{
		case AS_LOCAL:
			if(n >= 0 && n < MAX_OTHER_SERVERS)
			{
				server = &cls.localServers[n];
			}
			break;
		case AS_GLOBAL:
			if(n >= 0 && n < MAX_GLOBAL_SERVERS)
			{
				server = &cls.globalServers[n];
			}
			break;
		case AS_FAVORITES:
			if(n >= 0 && n < MAX_OTHER_SERVERS)
			{
				return qtrue;
			}
			break;
	}

	if(!server)
	{
		return qfalse;
	}

	for(i = 0; i < cls.numfavoriteservers; i++)
	{
		if(NET_CompareAdr(cls.favoriteServers[i].adr, server->adr))
		{
			return qtrue;
		}
	}

	return qfalse;
}

/*
====================
CL_GetGlConfig
====================
*/
static void CL_GetGlconfig(glconfig_t * config)
{
	*config = cls.glconfig;
}

/*
====================
CL_GetGlConfig2
====================
*/
static void CL_GetGlconfig2(glconfig2_t * config)
{
	*config = cls.glconfig2;
}

/*
====================
GetClipboarzdData
====================
*/
static void GetClipboardData(char *buf, int buflen)
{
	char           *cbd;

	cbd = Sys_GetClipboardData();

	if(!cbd)
	{
		*buf = 0;
		return;
	}

	Q_strncpyz(buf, cbd, buflen);

	Z_Free(cbd);
}

/*
====================
CLUI_GetCDKey
====================
*/
static void CLUI_GetCDKey(char *buf, int buflen)
{
	convar_t         *fs;

	fs = Cvar_Get("fs_game", "", CVAR_INIT | CVAR_SYSTEMINFO, "test");
	if(UI_usesUniqueCDKey() && fs && fs->string[0] != 0)
	{
		memcpy(buf, &cl_cdkey[16], 16);
		buf[16] = 0;
	}
	else
	{
		memcpy(buf, cl_cdkey, 16);
		buf[16] = 0;
	}
}


/*
====================
CLUI_SetCDKey
====================
*/
static void CLUI_SetCDKey(char *buf)
{
	convar_t         *fs;

	fs = Cvar_Get("fs_game", "", CVAR_INIT | CVAR_SYSTEMINFO, "test");
	if(UI_usesUniqueCDKey() && fs && fs->string[0] != 0)
	{
		memcpy(&cl_cdkey[16], buf, 16);
		cl_cdkey[32] = 0;
		// set the flag so the fle will be written at the next opportunity
		cvar_modifiedFlags |= CVAR_ARCHIVE;
	}
	else
	{
		memcpy(cl_cdkey, buf, 16);
		// set the flag so the fle will be written at the next opportunity
		cvar_modifiedFlags |= CVAR_ARCHIVE;
	}
}


/*
====================
GetConfigString
====================
*/
static int GetConfigString(int index, char *buf, int size)
{
	int             offset;

	if(index < 0 || index >= MAX_CONFIGSTRINGS)
	{
		return qfalse;
	}

	offset = cl.gameState.stringOffsets[index];
	if(!offset)
	{
		if(size)
		{
			buf[0] = 0;
		}
		return qfalse;
	}

	Q_strncpyz(buf, cl.gameState.stringData + offset, size);

	return qtrue;
}

/*
====================
FloatAsInt
====================
*/
static int FloatAsInt(float f)
{
	floatint_t      fi;

	fi.f = f;
	return fi.i;
}

static void UI_Con_GetText( char *buf, int size, int c )
{
	size_t len, cp, ofs;
	const char *conText;
	
	conText = Con_GetText( c );
	len = strlen( conText );

	if( len > size ) {
		cp = size - 1;
		ofs = len - cp;
	} else {
		cp = len;
		ofs = 0;
	}

	memcpy( buf, conText + ofs, cp );
	buf[len] = 0;
}

/*
====================
CL_UISystemCalls

The ui module is making a system call
====================
*/
intptr_t CL_UISystemCalls(intptr_t * args) {
	switch (args[0]) {
		case UI_ERROR:
			Com_Error(ERR_DROP, "%s", (char *)VMA(1));
			return 0;
		case UI_PRINT:
			Com_Printf("%s", (char *)VMA(1));
			return 0;
		case UI_MILLISECONDS:
			return Sys_Milliseconds();
		case UI_CVAR_REGISTER:
			Cvar_Register((vmCvar_t*)VMA(1), (char*)VMA(2), (char*)VMA(3), args[4]);
			return 0;
		case UI_CVAR_UPDATE:
			Cvar_Update((vmCvar_t*)VMA(1));
			return 0;
		case UI_CVAR_SET:
			Cvar_Set((char*)VMA(1), (char*)VMA(2));
			return 0;
		case UI_CVAR_VARIABLEVALUE:
			return FloatAsInt(Cvar_VariableValue((char*)VMA(1)));
		case UI_CVAR_VARIABLESTRINGBUFFER:
			Cvar_VariableStringBuffer((char*)VMA(1), (char*)VMA(2), args[3]);
			return 0;
		case UI_CVAR_LATCHEDVARIABLESTRINGBUFFER:
			Cvar_LatchedVariableStringBuffer((char*)VMA(1), (char*)VMA(2), args[3]);
			return 0;
		case UI_CVAR_SETVALUE:
			Cvar_SetValue((char*)VMA(1), VMF(2));
			return 0;
		case UI_CVAR_RESET:
			Cvar_Reset((char*)VMA(1));
			return 0;
		case UI_CVAR_CREATE:
			Cvar_Get((char*)VMA(1), (char*)VMA(2), args[3], (char*)VMA(4));
			return 0;
		case UI_CVAR_INFOSTRINGBUFFER:
			Cvar_InfoStringBuffer(args[1], (char*)VMA(2), args[3]);
			return 0;
		case UI_ARGC:
			return Cmd_Argc();
		case UI_ARGV:
			Cmd_ArgvBuffer(args[1], (char*)VMA(2), args[3]);
			return 0;
		case UI_CMD_EXECUTETEXT:
			Cbuf_ExecuteText(args[1], (char*)VMA(2));
			return 0;
		case UI_ADDCOMMAND:
			Cmd_AddCommand((char*)VMA(1), NULL, (char*)VMA(3));
			return 0;
		case UI_FS_FOPENFILE:
			return FS_FOpenFileByMode((char*)VMA(1), (fileHandle_t*)VMA( 2 ), (fsMode_t)args[3] );
		case UI_FS_READ:
			FS_Read2(VMA(1), args[2], args[3]);
			return 0;
		case UI_FS_WRITE:
			FS_Write(VMA(1), args[2], args[3]);
			return 0;
		case UI_FS_FCLOSEFILE:
			FS_FCloseFile(args[1]);
			return 0;
		case UI_FS_DELETEFILE:
			return FS_Delete((char*)VMA(1));
		case UI_FS_GETFILELIST:
			return FS_GetFileList((char*)VMA(1), (char*)VMA(2), (char*)VMA(3), args[4]);
		case UI_FS_SEEK:
			return FS_Seek( args[1], args[2], args[3] );
		case UI_R_REGISTERMODEL:
			return re.RegisterModel((char*)VMA(1));
		case UI_R_REGISTERSKIN:
			return re.RegisterSkin((char*)VMA(1));
		case UI_R_REGISTERSHADERNOMIP:
			return re.RegisterShaderNoMip((char*)VMA(1));
		case UI_R_CLEARSCENE:
			re.ClearScene();
			return 0;
		case UI_R_ADDREFENTITYTOSCENE:
			re.AddRefEntityToScene((refEntity_t*)VMA(1));
			return 0;
		case UI_R_ADDPOLYTOSCENE:
			re.AddPolyToScene(args[1], args[2], (polyVert_t*)VMA(3));
			return 0;
		case UI_R_ADDPOLYSTOSCENE:
			re.AddPolysToScene(args[1], args[2], (polyVert_t*)VMA(3), args[4]);
			return 0;
		case UI_R_ADDLIGHTTOSCENE:
			re.AddLightToScene((vec_t*)VMA(1), VMF(2), VMF(3), VMF(4), VMF(5), VMF(6), args[7], args[8]);
			return 0;
		case UI_R_ADDCORONATOSCENE:
			re.AddCoronaToScene((vec_t*)VMA(1), VMF(2), VMF(3), VMF(4), VMF(5), args[6], (qboolean)args[7]);
			return 0;
		case UI_R_RENDERSCENE:
			re.RenderScene((refdef_t*)VMA(1));
			return 0;
		case UI_R_SETCOLOR:
			re.SetColor((float*)VMA(1));
			return 0;
		case UI_R_SETCLIPREGION:
			re.SetClipRegion( (float*)VMA(1) );
			return 0;
		case UI_R_DRAW2DPOLYS:
			re.Add2dPolys((polyVert_t*)VMA(1), args[2], args[3]);
			return 0;
		case UI_R_DRAWSTRETCHPIC:
			re.DrawStretchPic(VMF(1), VMF(2), VMF(3), VMF(4), VMF(5), VMF(6), VMF(7), VMF(8), args[9]);
			return 0;
		case UI_R_DRAWROTATEDPIC:
			re.DrawRotatedPic(VMF(1), VMF(2), VMF(3), VMF(4), VMF(5), VMF(6), VMF(7), VMF(8), args[9], VMF(10));
			return 0;
		case UI_R_MODELBOUNDS:
			re.ModelBounds(args[1], (float*)VMA(2), (float*)VMA(3));
			return 0;
		case UI_UPDATESCREEN:
			SCR_UpdateScreen();
			return 0;
		case UI_CM_LERPTAG:
			return re.LerpTag((orientation_t*)VMA(1), (refEntity_t*)VMA(2), (char*)VMA(3), args[4]);
		case UI_S_REGISTERSOUND:
			return S_RegisterSound((char*)VMA(1), (qboolean)args[2]);
		case UI_S_STARTLOCALSOUND:
			//S_StartLocalSound(args[1], args[2], args[3]);
			S_StartLocalSound( args[1], args[2] );
			return 0;
		case UI_S_FADESTREAMINGSOUND:
			// Dushan - FIX ME
			//S_FadeStreamingSound(VMF(1), args[2], args[3]);
			return 0;
		case UI_S_FADEALLSOUNDS:
			// Dushan - FIX ME
			//S_FadeAllSounds(VMF(1), args[2], args[3]);
			return 0;
		case UI_KEY_KEYNUMTOSTRINGBUF:
			idKeyInput::KeynumToStringBuf(args[1], (char*)VMA(2), args[3]);
			return 0;
		case UI_KEY_GETBINDINGBUF:
			idKeyInput::GetBindingBuf(args[1], (char*)VMA(2), args[3]);
			return 0;
		case UI_KEY_SETBINDING:
			idKeyInput::SetBinding(args[1], (char*)VMA(2));
			return 0;
		case UI_KEY_BINDINGTOKEYS:
			idKeyInput::GetBindingByString((char*)VMA(1), (int*)VMA(2), (int*)VMA(3));
			return 0;
		case UI_KEY_ISDOWN:
			return idKeyInput::IsDown(args[1]);
		case UI_KEY_GETOVERSTRIKEMODE:
			return idKeyInput::GetOverstrikeMode();
		case UI_KEY_SETOVERSTRIKEMODE:
			idKeyInput::SetOverstrikeMode((qboolean)args[1]);
			return 0;
		case UI_KEY_CLEARSTATES:
			idKeyInput::ClearStates();
			return 0;
		case UI_KEY_GETCATCHER:
			return idKeyInput::GetCatcher();
		case UI_KEY_SETCATCHER:
			idKeyInput::SetCatcher(args[1]);
			return 0;
		case UI_GETCLIPBOARDDATA:
			GetClipboardData((char*)VMA(1), args[2]);
			return 0;
		case UI_GETCLIENTSTATE:
			GetClientState((uiClientState_t*)VMA(1));
			return 0;
		case UI_GETGLCONFIG:
			CL_GetGlconfig((glconfig_t*)VMA(1));
			return 0;
		case UI_GETCONFIGSTRING:
			return GetConfigString(args[1], (char*)VMA(2), args[3]);
		case UI_LAN_LOADCACHEDSERVERS:
			LAN_LoadCachedServers();
			return 0;
		case UI_LAN_SAVECACHEDSERVERS:
			LAN_SaveServersToCache();
			return 0;
		case UI_LAN_ADDSERVER:
			return LAN_AddServer(args[1], (char*)VMA(2), (char*)VMA(3));
		case UI_LAN_REMOVESERVER:
			LAN_RemoveServer(args[1], (char*)VMA(2));
			return 0;
		case UI_LAN_GETPINGQUEUECOUNT:
			return LAN_GetPingQueueCount();
		case UI_LAN_CLEARPING:
			LAN_ClearPing(args[1]);
			return 0;
		case UI_LAN_GETPING:
			LAN_GetPing(args[1], (char*)VMA(2), args[3], (int*)VMA(4));
			return 0;
		case UI_LAN_GETPINGINFO:
			LAN_GetPingInfo(args[1], (char*)VMA(2), args[3]);
			return 0;
		case UI_LAN_GETSERVERCOUNT:
			return LAN_GetServerCount(args[1]);
		case UI_LAN_GETSERVERADDRESSSTRING:
			LAN_GetServerAddressString(args[1], args[2], (char*)VMA(3), args[4]);
			return 0;
		case UI_LAN_GETSERVERINFO:
			LAN_GetServerInfo(args[1], args[2], (char*)VMA(3), args[4]);
			return 0;
		case UI_LAN_GETSERVERPING:
			return LAN_GetServerPing(args[1], args[2]);
		case UI_LAN_MARKSERVERVISIBLE:
			LAN_MarkServerVisible(args[1], args[2], (qboolean)args[3]);
			return 0;
		case UI_LAN_SERVERISVISIBLE:
			return LAN_ServerIsVisible(args[1], args[2]);
		case UI_LAN_UPDATEVISIBLEPINGS:
			return LAN_UpdateVisiblePings(args[1]);
		case UI_LAN_RESETPINGS:
			LAN_ResetPings(args[1]);
			return 0;
		case UI_LAN_SERVERSTATUS:
			return LAN_GetServerStatus((char*)VMA(1), (char*)VMA(2), args[3]);
		case UI_LAN_SERVERISINFAVORITELIST:
			return LAN_ServerIsInFavoriteList(args[1], args[2]);
		case UI_GETNEWS:
			return GetNews((qboolean)args[1]);
		case UI_LAN_COMPARESERVERS:
			return LAN_CompareServers(args[1], args[2], args[3], args[4], args[5]);
		case UI_MEMORY_REMAINING:
			return Hunk_MemoryRemaining();
		case UI_GET_CDKEY:
			CLUI_GetCDKey((char*)VMA(1), args[2]);
			return 0;
		case UI_SET_CDKEY:
			CLUI_SetCDKey((char*)VMA(1));
			return 0;
		case UI_R_REGISTERFONT:
			re.RegisterFont((char*)VMA(1), args[2], (fontInfo_t*)VMA(3));
			return 0;
		case UI_MEMSET:
			return (intptr_t)memset( VMA( 1 ), args[2], args[3] );
		case UI_MEMCPY:
			return (intptr_t)memcpy( VMA( 1 ), VMA( 2 ), args[3] );
		case UI_STRNCPY:
			return (intptr_t)strncpy( (char*)VMA( 1 ), (char*)VMA( 2 ), args[3] );
		case UI_SIN:
			return FloatAsInt(sin(VMF(1)));
		case UI_COS:
			return FloatAsInt(cos(VMF(1)));
		case UI_ATAN2:
			return FloatAsInt(atan2(VMF(1), VMF(2)));
		case UI_SQRT:
			return FloatAsInt(sqrt(VMF(1)));
		case UI_FLOOR:
			return FloatAsInt(floor(VMF(1)));
		case UI_CEIL:
			return FloatAsInt(ceil(VMF(1)));
		case UI_PARSE_ADD_GLOBAL_DEFINE:
			return Parse_AddGlobalDefine( (char*)VMA(1) );
		case UI_PARSE_LOAD_SOURCE:
			return Parse_LoadSourceHandle( (char*)VMA(1) );
		case UI_PARSE_FREE_SOURCE:
			return Parse_FreeSourceHandle( args[1] );
		case UI_PARSE_READ_TOKEN:
			return Parse_ReadTokenHandle( args[1], (pc_token_t*)VMA(2) );
		case UI_PARSE_SOURCE_FILE_AND_LINE:
			return Parse_SourceFileAndLine( args[1], (char*)VMA(2), (int*)VMA(3) );
		case UI_PC_ADD_GLOBAL_DEFINE:
			return Parse_AddGlobalDefine((char*)VMA(1));
		case UI_PC_REMOVE_ALL_GLOBAL_DEFINES:
			Parse_RemoveAllGlobalDefines();
			return 0;
		case UI_PC_LOAD_SOURCE:
			return Parse_LoadSourceHandle((char*)VMA(1));
		case UI_PC_FREE_SOURCE:
			return Parse_FreeSourceHandle(args[1]);
		case UI_PC_READ_TOKEN:
			return Parse_ReadTokenHandle(args[1], (pc_token_t*)VMA(2));
		case UI_PC_SOURCE_FILE_AND_LINE:
			return Parse_SourceFileAndLine(args[1], (char*)VMA(2), (int*)VMA(3));
		case UI_PC_UNREAD_TOKEN:
			Parse_UnreadLastTokenHandle(args[1]);
			return 0;
		case UI_S_STOPBACKGROUNDTRACK:
			S_StopBackgroundTrack();
			return 0;
		case UI_S_STARTBACKGROUNDTRACK:
			//S_StartBackgroundTrack(VMA(1), VMA(2), args[3]);	//----(SA) added fadeup time
			S_StartBackgroundTrack( (char*)VMA(1), (char*)VMA(2));
			return 0;
		case UI_REAL_TIME:
			return Com_RealTime((qtime_t*)VMA(1));
		case UI_CIN_PLAYCINEMATIC:
			Com_DPrintf("UI_CIN_PlayCinematic\n");
			return CIN_PlayCinematic((char*)VMA(1), args[2], args[3], args[4], args[5], args[6]);
		case UI_CIN_STOPCINEMATIC:
			return CIN_StopCinematic(args[1]);
		case UI_CIN_RUNCINEMATIC:
			return CIN_RunCinematic(args[1]);
		case UI_CIN_DRAWCINEMATIC:
			CIN_DrawCinematic(args[1]);
			return 0;
		case UI_CIN_SETEXTENTS:
			CIN_SetExtents(args[1], args[2], args[3], args[4], args[5]);
			return 0;
		case UI_R_REMAP_SHADER:
			re.RemapShader((char*)VMA(1), (char*)VMA(2), (char*)VMA(3));
			return 0;
		case UI_CL_GETLIMBOSTRING:
			return CL_GetLimboString(args[1], (char*)VMA(2));
		case UI_CL_TRANSLATE_STRING:
			CL_TranslateString((char*)VMA(1), (char*)VMA(2));
			return 0;
		case UI_CHECKAUTOUPDATE:
#if !defined(UPDATE_SERVER)
			CL_CheckAutoUpdate();
#endif
			return 0;
		case UI_GET_AUTOUPDATE:
#if !defined(UPDATE_SERVER)
			CL_GetAutoUpdate();
#endif
			return 0;
		case UI_OPENURL:
			CL_OpenURL((char *)VMA(1));
			return 0;
		case UI_GETHUNKDATA:
			Com_GetHunkInfo((int*)VMA(1), (int*)VMA(2));
			return 0;
#if defined(USE_REFENTITY_ANIMATIONSYSTEM)
		case UI_R_REGISTERANIMATION:
			return re.RegisterAnimation((char *)VMA(1));
		case UI_R_BUILDSKELETON:
			return re.BuildSkeleton((refSkeleton_t*)VMA(1), args[2], args[3], args[4], VMF(5), (qboolean)args[6]);
		case UI_R_BLENDSKELETON:
			return re.BlendSkeleton((refSkeleton_t*)VMA(1), (refSkeleton_t*)VMA(2), VMF(3));
		case UI_R_BONEINDEX:
			return re.BoneIndex(args[1], (char *)VMA(2));
		case UI_R_ANIMNUMFRAMES:
			return re.AnimNumFrames(args[1]);
		case UI_R_ANIMFRAMERATE:
			return re.AnimFrameRate(args[1]);
#endif
		case UI_GETGLCONFIG2:
			CL_GetGlconfig2((glconfig2_t*)VMA(1));
			return 0;
		case UI_CON_GETTEXT:
			UI_Con_GetText( (char *)VMA( 1 ), args[2], args[3] );
			return 0;
		case UI_CVAR_VARIABLEINT:
			return Cvar_VariableIntegerValue( (char *)VMA(1) );
		case UI_R_DRAWSPRITE: {
				float * uv = (float*)VMA(5);
				re.DrawStretchPic(VMF(1), VMF(2), VMF(3), VMF(4), uv[0], uv[1], uv[2], uv[3], args[6]);
			} return 0;
		default:
			Com_Error( ERR_DROP, "Bad UI system trap: %ld", (long int) args[0] );

	}

	return 0;
}

/*
====================
CL_ShutdownUI
====================
*/
void CL_ShutdownUI(void) {
	cls.keyCatchers &= ~KEYCATCH_UI;
	cls.uiStarted = qfalse;
	if(!uivm)
	{
		return;
	}
	VM_Call(uivm, UI_SHUTDOWN);
	VM_Free(uivm);
	uivm = NULL;
}

/*
====================
CL_InitUI
====================
*/
void CL_InitUI(void) {
	int v;

	uivm = VM_Create("ui", CL_UISystemCalls, VMI_NATIVE );
	if(!uivm)
	{
		Com_Error(ERR_FATAL, "VM_Create on UI failed");
	}

	// sanity check
	v = VM_Call(uivm, UI_GETAPIVERSION);
	if(v != UI_API_VERSION) {
		Com_Error(ERR_FATAL, "User Interface is version %d, expected %d", v, UI_API_VERSION);
		cls.uiStarted = qfalse;
	}

#if defined (USE_HTTP)
	//	if the session id has something in it, then assume that the browser sent it from the
	//	command line and tell ui we're already logged in.  
	if ( com_sessionid->string[0] ) {
		VM_Call( uivm, UI_AUTHORIZED, AUTHORIZE_OK );
	}
#endif

	// init for this gamestate
	VM_Call(uivm, UI_INIT, (cls.state >= CA_AUTHORIZING && cls.state < CA_ACTIVE));
}


qboolean UI_usesUniqueCDKey()
{
	if(uivm)
	{
		return (qboolean)(VM_Call(uivm, UI_HASUNIQUECDKEY) == qtrue);
	}
	else
	{
		return qfalse;
	}
}

/*
====================
UI_GameCommand

See if the current console command is claimed by the ui
====================
*/
qboolean UI_GameCommand(void)
{
	if(!uivm)
	{
		return qfalse;
	}

	return (qboolean)VM_Call(uivm, UI_CONSOLE_COMMAND, cls.realtime);
}
