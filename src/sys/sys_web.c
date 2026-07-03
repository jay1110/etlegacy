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
 * @file sys_web.c
 * @brief Emscripten/WebAssembly platform-specific implementations.
 *
 * This file provides platform abstraction functions for the browser
 * environment, replacing the Unix/Windows-specific implementations.
 */

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <libgen.h>

#ifndef DEDICATED
#include "../sdl/sdl_defs.h"
#endif

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "sys_local.h"

static char homePath[MAX_OSPATH] = { 0 };

/**
 * @brief Sys_DefaultHomePath
 * @return Emscripten virtual filesystem path for user data
 */
char *Sys_DefaultHomePath(void)
{
	if (!*homePath)
	{
		Q_strncpyz(homePath, "/home/web_user/.etlegacy", sizeof(homePath));

		// Ensure the directory exists
		mkdir("/home", 0750);
		mkdir("/home/web_user", 0750);
		mkdir(homePath, 0750);
	}

	return homePath;
}

/**
 * @brief Sys_Milliseconds
 * @return Current time in milliseconds using emscripten_get_now()
 */
int Sys_Milliseconds(void)
{
	static double baseTime = 0.0;
	double        now;

	now = emscripten_get_now();

	if (baseTime == 0.0)
	{
		baseTime = now;
		return 0;
	}

	return (int)(now - baseTime);
}

/**
 * @brief Sys_Microseconds
 * @return Current time in microseconds
 */
int64_t Sys_Microseconds(void)
{
	static double baseTime = 0.0;
	double        now;

	now = emscripten_get_now();

	if (baseTime == 0.0)
	{
		baseTime = now;
		return 0;
	}

	return (int64_t)((now - baseTime) * 1000.0);
}

/**
 * @brief Sys_SnapVector
 * @param[in,out] v Vector
 */
void Sys_SnapVector(float *v)
{
	v[0] = rint(v[0]);
	v[1] = rint(v[1]);
	v[2] = rint(v[2]);
}

/**
 * @brief Sys_Cwd
 * @return Current working directory in the virtual filesystem
 */
char *Sys_Cwd(void)
{
	static char cwd[MAX_OSPATH];

	Q_strncpyz(cwd, "/etlegacy", sizeof(cwd));

	return cwd;
}

/**
 * @brief Sys_Dialog - Show a dialog using browser alert()
 * @param[in] type Dialog type
 * @param[in] message Message to display
 * @param[in] title Dialog title
 * @return Dialog result
 */
dialogResult_t Sys_Dialog(dialogType_t type, const char *message, const char *title)
{
	// Use EM_ASM with parameters to avoid JavaScript injection issues
	EM_ASM({
		var title   = UTF8ToString($0);
		var message = UTF8ToString($1);
		alert(title + ': ' + message);
	}, title, message);

	return DR_OK;
}

/**
 * @brief Sys_PlatformInit - Initialize the Emscripten platform
 */
void Sys_PlatformInit(void)
{
	// Create necessary virtual filesystem directories
	mkdir("/etlegacy", 0750);
	mkdir("/etlegacy/etmain", 0750);
	mkdir("/etlegacy/legacy", 0750);

	// Initialize the home path directory
	Sys_DefaultHomePath();
}

/**
 * @brief Sys_SetEnv - Set or unset environment variables
 * @param[in] name  Environment variable name
 * @param[in] value New value (empty to remove)
 */
void Sys_SetEnv(const char *name, const char *value)
{
	if (value && *value)
	{
		setenv(name, value, 1);
	}
	else
	{
		unsetenv(name);
	}
}

/**
 * @brief Sys_PID - Return current process ID (stub for Emscripten)
 * @return Always returns 1
 */
int Sys_PID(void)
{
	return 1;
}

/**
 * @brief Sys_PIDIsRunning - Check if a PID is running (stub for Emscripten)
 * @param[in] pid Process ID to check
 * @return Always returns qfalse
 */
qboolean Sys_PIDIsRunning(unsigned int pid)
{
	return qfalse;
}

/**
 * @brief Sys_ErrorDialog - Display an error dialog
 * @param[in] error Error message
 */
void Sys_ErrorDialog(const char *error)
{
	Sys_Dialog(DT_ERROR, error, "Error");
}

/**
 * @brief Sys_Chmod - Change file permissions (stub for Emscripten)
 * @param[in] file Filename
 * @param[in] mode Access mode
 */
void Sys_Chmod(const char *file, int mode)
{
	// No-op: Emscripten virtual filesystem doesn't support chmod meaningfully
}

/**
 * @brief Sys_ListFilteredFiles
 */
void Sys_ListFilteredFiles(const char *basedir, const char *subdirs, const char *filter, char **list, int *numfiles)
{
	char          search[MAX_OSPATH];
	char          newsubdirs[MAX_OSPATH];
	char          filename[MAX_OSPATH];
	DIR           *fdir;
	struct dirent *d;
	struct stat   st;

	if (*numfiles >= MAX_FOUND_FILES - 1)
	{
		return;
	}

	if (strlen(subdirs))
	{
		Com_sprintf(search, sizeof(search), "%s/%s", basedir, subdirs);
	}
	else
	{
		Com_sprintf(search, sizeof(search), "%s", basedir);
	}

	fdir = opendir(search);
	if (!fdir)
	{
		return;
	}

	while ((d = readdir(fdir)) != NULL)
	{
		Com_sprintf(filename, sizeof(filename), "%s/%s", search, d->d_name);
		if (stat(filename, &st) == -1)
		{
			continue;
		}

		if (st.st_mode & S_IFDIR)
		{
			if (!Q_stricmp(d->d_name, ".") || !Q_stricmp(d->d_name, ".."))
			{
				continue;
			}

			if (strlen(subdirs))
			{
				Com_sprintf(newsubdirs, sizeof(newsubdirs), "%s/%s", subdirs, d->d_name);
			}
			else
			{
				Com_sprintf(newsubdirs, sizeof(newsubdirs), "%s", d->d_name);
			}

			Sys_ListFilteredFiles(basedir, newsubdirs, filter, list, numfiles);
		}

		if (*numfiles >= MAX_FOUND_FILES - 1)
		{
			break;
		}

		Com_sprintf(filename, sizeof(filename), "%s/%s", subdirs, d->d_name);

		if (!Com_FilterPath(filter, filename, qfalse))
		{
			continue;
		}

		list[*numfiles] = CopyString(filename);
		(*numfiles)++;
	}

	closedir(fdir);
}

/**
 * @brief Sys_FreeFileList
 * @param[out] list
 */
void Sys_FreeFileList(char **list)
{
	int i;

	if (!list)
	{
		return;
	}

	for (i = 0; list[i]; i++)
	{
		Z_Free(list[i]);
	}

	Z_Free(list);
}

/**
 * @brief Sys_ListFiles
 */
char **Sys_ListFiles(const char *directory, const char *extension, const char *filter, int *numfiles, qboolean wantsubs)
{
	struct dirent *d;
	DIR           *fdir;
	qboolean      dironly = wantsubs;
	char          search[MAX_OSPATH];
	int           nfiles;
	char          **listCopy;
	char          *list[MAX_FOUND_FILES];
	int           i;
	struct stat   st;
	int           extLen;

	if (filter)
	{
		nfiles = 0;
		Sys_ListFilteredFiles(directory, "", filter, list, &nfiles);

		list[nfiles] = NULL;
		*numfiles    = nfiles;

		if (!nfiles)
		{
			return NULL;
		}

		listCopy = Z_Malloc((nfiles + 1) * sizeof(*listCopy));
		for (i = 0; i < nfiles; i++)
		{
			listCopy[i] = list[i];
		}
		listCopy[i] = NULL;

		return listCopy;
	}

	if (!extension)
	{
		extension = "";
	}

	if (extension[0] == '/' && extension[1] == 0)
	{
		extension = "";
		dironly   = qtrue;
	}

	extLen = strlen(extension);

	fdir = opendir(directory);
	if (!fdir)
	{
		*numfiles = 0;
		return NULL;
	}

	nfiles = 0;

	while ((d = readdir(fdir)) != NULL)
	{
		Com_sprintf(search, sizeof(search), "%s/%s", directory, d->d_name);
		if (stat(search, &st) == -1)
		{
			continue;
		}

		if ((st.st_mode & S_IFDIR) && !dironly)
		{
			continue;
		}

		if (!(st.st_mode & S_IFDIR) && dironly)
		{
			continue;
		}

		if (*extension)
		{
			if (strlen(d->d_name) < extLen ||
			    Q_stricmp(d->d_name + strlen(d->d_name) - extLen, extension))
			{
				continue;
			}
		}

		if (nfiles == MAX_FOUND_FILES - 1)
		{
			break;
		}

		list[nfiles] = CopyString(d->d_name);
		nfiles++;
	}

	list[nfiles] = NULL;

	closedir(fdir);

	*numfiles = nfiles;

	if (!nfiles)
	{
		return NULL;
	}

	listCopy = Z_Malloc((nfiles + 1) * sizeof(*listCopy));
	for (i = 0; i < nfiles; i++)
	{
		listCopy[i] = list[i];
	}
	listCopy[i] = NULL;

	return listCopy;
}

/**
 * @brief Sys_FOpen
 */
FILE *Sys_FOpen(const char *ospath, const char *mode)
{
	return fopen(ospath, mode);
}

/**
 * @brief Sys_Mkdir
 */
qboolean Sys_Mkdir(const char *path)
{
	int result = mkdir(path, 0750);

	if (result != 0 && errno != EEXIST)
	{
		return qfalse;
	}

	return qtrue;
}

/**
 * @brief Sys_Mkfifo - Not supported on Emscripten
 */
FILE *Sys_Mkfifo(const char *ospath)
{
	return NULL;
}

/**
 * @brief Sys_GLimpSafeInit
 */
void Sys_GLimpSafeInit(void)
{
	// No-op for Emscripten
}

/**
 * @brief Sys_GLimpInit
 */
void Sys_GLimpInit(void)
{
	// No-op for Emscripten - SDL handles GL context creation
}

/**
 * @brief CON_Init - Console init (minimal for web)
 */
void CON_Init(void)
{
	// No-op for Emscripten
}

/**
 * @brief CON_Shutdown
 */
void CON_Shutdown(void)
{
	// No-op for Emscripten
}

/**
 * @brief CON_Input
 * @return NULL (no console input in browser)
 */
char *CON_Input(void)
{
	return NULL;
}

/**
 * @brief CON_Print
 * @param[in] msg Message to print
 */
void CON_Print(const char *msg)
{
	fputs(msg, stdout);
	fflush(stdout);
}

/**
 * @brief Sys_DllExtension - Check if a filename has a valid DLL extension
 * @param[in] name Filename to check
 * @return qtrue if the extension is valid
 */
qboolean Sys_DllExtension(const char *name)
{
	const char *p;

	if (!(p = strrchr(name, '.')))
	{
		return qfalse;
	}

	// Game logic modules are Emscripten SIDE_MODULEs named "<name>.mp.wasm32.so"
	// (see DLL_EXT in q_platform.h); the ".so" suffix lets Emscripten precompile
	// them at preload time so dlopen() succeeds.
	if (!Q_stricmp(p, ".so"))
	{
		return qtrue;
	}

	return qfalse;
}

/**
 * @brief Sys_RandomBytes - Fill a buffer with random bytes
 * @param[out] bytes Destination buffer
 * @param[in] len Number of bytes to write
 * @return qtrue on success
 *
 * Emscripten backs /dev/urandom with the browser's crypto.getRandomValues().
 */
qboolean Sys_RandomBytes(void *bytes, int len)
{
	FILE *fp;

	fp = fopen("/dev/urandom", "r");
	if (!fp)
	{
		return qfalse;
	}

	setvbuf(fp, NULL, _IONBF, 0); // don't buffer reads from /dev/urandom

	if (fread(bytes, sizeof(byte), len, fp) != len)
	{
		fclose(fp);
		return qfalse;
	}

	fclose(fp);
	return qtrue;
}

/**
 * @brief Sys_GetCurrentUser - Get current user username
 * @return username (there is no real user account in the browser)
 */
char *Sys_GetCurrentUser(void)
{
	return "player";
}

/**
 * @brief Sys_LowPhysicalMemory
 * @return qfalse (memory growth is handled by the browser)
 */
qboolean Sys_LowPhysicalMemory(void)
{
	return qfalse;
}

/**
 * @brief Sys_Dirname - Return the directory portion of a path
 * @param[in] path
 * @return pointer to the directory component
 */
const char *Sys_Dirname(char *path)
{
	return dirname(path);
}

/**
 * @brief Sys_OpenURL - Open a URL in a new browser tab
 * @param[in] url URL to open
 * @param[in] doexit Unused in the browser environment
 */
void Sys_OpenURL(const char *url, qboolean doexit)
{
	Com_Printf("Open URL: %s\n", url);

	// window.open() is the browser equivalent of launching an external browser.
	// The URL is passed as a UTF8 string parameter so it is not interpreted as code.
	EM_ASM({
		var target = UTF8ToString($0);
		window.open(target, '_blank');
	}, url);
}

#endif /* __EMSCRIPTEN__ */
