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
 * @file dl_main_web.c
 * @brief Emscripten Fetch API-based download implementation.
 *
 * Replaces dl_main_curl.c for WebAssembly builds. Uses the Emscripten
 * Fetch API (emscripten_fetch) instead of libcurl for HTTP downloads.
 */

#ifdef __EMSCRIPTEN__

#include <emscripten/fetch.h>
#include <string.h>

#include "dl_public.h"
#include "q_shared.h"
#include "qcommon.h"

#define MAX_WEB_REQUESTS 8

typedef struct
{
	unsigned int          id;
	qboolean              active;
	emscripten_fetch_t   *fetch;
	webCallbackFunc_t     completeCb;
	webProgressCallbackFunc_t progressCb;
	void                 *userData;
	webRequest_t          request;
} webFetchRequest_t;

static webFetchRequest_t fetchRequests[MAX_WEB_REQUESTS];
static unsigned int      nextRequestId = FILE_DOWNLOAD_ID + 1;

/**
 * @brief Find a fetch request by its Emscripten fetch handle
 */
static webFetchRequest_t *FindFetchByHandle(emscripten_fetch_t *fetch)
{
	int i;

	for (i = 0; i < MAX_WEB_REQUESTS; i++)
	{
		if (fetchRequests[i].active && fetchRequests[i].fetch == fetch)
		{
			return &fetchRequests[i];
		}
	}

	return NULL;
}

/**
 * @brief Find a fetch request by its ID
 */
static webFetchRequest_t *FindFetchById(unsigned int id)
{
	int i;

	for (i = 0; i < MAX_WEB_REQUESTS; i++)
	{
		if (fetchRequests[i].active && fetchRequests[i].id == id)
		{
			return &fetchRequests[i];
		}
	}

	return NULL;
}

/**
 * @brief Emscripten fetch success callback
 */
static void FetchOnSuccess(emscripten_fetch_t *fetch)
{
	webFetchRequest_t *req = FindFetchByHandle(fetch);

	if (req)
	{
		req->request.httpCode = fetch->status;

		// Write downloaded data to file if applicable
		if (req->request.data.fileHandle)
		{
			fwrite(fetch->data, 1, fetch->numBytes, req->request.data.fileHandle);
			fclose(req->request.data.fileHandle);
			req->request.data.fileHandle = NULL;
		}
		else if (req->request.data.buffer && fetch->numBytes <= req->request.data.bufferSize)
		{
			Com_Memcpy(req->request.data.buffer, fetch->data, fetch->numBytes);
			req->request.data.bufferPos = fetch->numBytes;
		}

		req->request.data.requestLength = fetch->numBytes;

		if (req->completeCb)
		{
			req->completeCb(&req->request, REQUEST_OK);
		}

		req->active = qfalse;
	}

	emscripten_fetch_close(fetch);
}

/**
 * @brief Emscripten fetch error callback
 */
static void FetchOnError(emscripten_fetch_t *fetch)
{
	webFetchRequest_t *req = FindFetchByHandle(fetch);

	if (req)
	{
		req->request.httpCode = fetch->status;

		Com_Printf("Download failed: %s (HTTP %d)\n", fetch->url, fetch->status);

		if (req->completeCb)
		{
			req->completeCb(&req->request, REQUEST_NOK);
		}

		req->active = qfalse;
	}

	emscripten_fetch_close(fetch);
}

/**
 * @brief Emscripten fetch progress callback
 */
static void FetchOnProgress(emscripten_fetch_t *fetch)
{
	webFetchRequest_t *req = FindFetchByHandle(fetch);

	if (req && req->progressCb)
	{
		req->progressCb(&req->request, (double)fetch->dataOffset, (double)fetch->totalBytes);
	}
}

/**
 * @brief Find a free request slot
 */
static webFetchRequest_t *AllocFetchRequest(void)
{
	int i;

	for (i = 0; i < MAX_WEB_REQUESTS; i++)
	{
		if (!fetchRequests[i].active)
		{
			Com_Memset(&fetchRequests[i], 0, sizeof(webFetchRequest_t));
			fetchRequests[i].active = qtrue;
			fetchRequests[i].id     = nextRequestId++;
			return &fetchRequests[i];
		}
	}

	Com_Printf("AllocFetchRequest: no free request slots\n");
	return NULL;
}

/**
 * @brief DL_BeginDownload - Start downloading a file using the Emscripten Fetch API
 * @param[in] localName  Local file path to save to
 * @param[in] remoteName Remote URL to download from
 * @param     userData   User data pointer
 * @param     complete   Completion callback
 * @param     progress   Progress callback
 * @return Request ID or 0 on failure
 */
unsigned int DL_BeginDownload(const char *localName, const char *remoteName,
                              void *userData, webCallbackFunc_t complete,
                              webProgressCallbackFunc_t progress)
{
	webFetchRequest_t *req;
	emscripten_fetch_attr_t attr;

	req = AllocFetchRequest();
	if (!req)
	{
		return 0;
	}

	req->completeCb = complete;
	req->progressCb = progress;
	req->userData   = userData;
	req->request.userData = userData;
	req->request.id = req->id;

	Q_strncpyz(req->request.url, remoteName, sizeof(req->request.url));
	Q_strncpyz(req->request.data.name, localName, sizeof(req->request.data.name));

	// Open the local file for writing
	if (localName && *localName)
	{
		char ospath[MAX_OSPATH];
		char *homepath = Cvar_VariableString("fs_homepath");

		Com_sprintf(ospath, sizeof(ospath), "%s/%s", homepath, localName);
		req->request.data.fileHandle = fopen(ospath, "wb");

		if (!req->request.data.fileHandle)
		{
			Com_Printf("DL_BeginDownload: failed to open %s for writing\n", ospath);
			req->active = qfalse;
			return 0;
		}
	}

	emscripten_fetch_attr_init(&attr);
	Q_strncpyz(attr.requestMethod, "GET", sizeof(attr.requestMethod));
	attr.attributes   = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
	attr.onsuccess    = FetchOnSuccess;
	attr.onerror      = FetchOnError;
	attr.onprogress   = FetchOnProgress;

	req->fetch = emscripten_fetch(&attr, remoteName);

	if (!req->fetch)
	{
		Com_Printf("DL_BeginDownload: emscripten_fetch failed for %s\n", remoteName);
		if (req->request.data.fileHandle)
		{
			fclose(req->request.data.fileHandle);
			req->request.data.fileHandle = NULL;
		}
		req->active = qfalse;
		return 0;
	}

	Com_Printf("Starting download: %s -> %s\n", remoteName, localName);

	return req->id;
}

/**
 * @brief Web_CreateRequest - Create a web request using the Emscripten Fetch API
 */
unsigned int Web_CreateRequest(const char *url, const char *authToken,
                               webUploadData_t *upload, void *userData,
                               webCallbackFunc_t complete,
                               webProgressCallbackFunc_t progress)
{
	webFetchRequest_t *req;
	emscripten_fetch_attr_t attr;

	req = AllocFetchRequest();
	if (!req)
	{
		return 0;
	}

	req->completeCb       = complete;
	req->progressCb       = progress;
	req->userData         = userData;
	req->request.userData = userData;
	req->request.id       = req->id;

	Q_strncpyz(req->request.url, url, sizeof(req->request.url));

	emscripten_fetch_attr_init(&attr);

	if (upload)
	{
		Q_strncpyz(attr.requestMethod, "POST", sizeof(attr.requestMethod));
		req->request.upload = qtrue;
	}
	else
	{
		Q_strncpyz(attr.requestMethod, "GET", sizeof(attr.requestMethod));
	}

	attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
	attr.onsuccess  = FetchOnSuccess;
	attr.onerror    = FetchOnError;
	attr.onprogress = FetchOnProgress;

	// Add auth header if provided
	if (authToken && *authToken)
	{
		static const char *headers[] = { "Authorization", NULL, NULL };
		static char        authHeader[MAX_STRING_CHARS];

		Q_strncpyz(authHeader, authToken, sizeof(authHeader));
		headers[1] = authHeader;

		attr.requestHeaders = headers;
	}

	req->fetch = emscripten_fetch(&attr, url);

	if (!req->fetch)
	{
		Com_Printf("Web_CreateRequest: emscripten_fetch failed for %s\n", url);
		req->active = qfalse;
		return 0;
	}

	return req->id;
}

/**
 * @brief DL_DownloadLoop - Process download events
 *
 * In Emscripten, downloads are handled asynchronously via callbacks,
 * so this function is essentially a no-op.
 */
void DL_DownloadLoop(void)
{
	// Emscripten fetch operations are event-driven.
	// Callbacks handle completion/error/progress.
}

/**
 * @brief DL_AbortAll - Abort all active downloads
 */
void DL_AbortAll(qboolean block, qboolean allowContinue)
{
	int i;

	for (i = 0; i < MAX_WEB_REQUESTS; i++)
	{
		if (fetchRequests[i].active)
		{
			if (fetchRequests[i].fetch)
			{
				emscripten_fetch_close(fetchRequests[i].fetch);
				fetchRequests[i].fetch = NULL;
			}

			if (fetchRequests[i].request.data.fileHandle)
			{
				fclose(fetchRequests[i].request.data.fileHandle);
				fetchRequests[i].request.data.fileHandle = NULL;
			}

			if (fetchRequests[i].completeCb)
			{
				fetchRequests[i].completeCb(&fetchRequests[i].request, REQUEST_ABORT);
			}

			fetchRequests[i].active = qfalse;
		}
	}
}

/**
 * @brief DL_Shutdown - Shutdown the download subsystem
 */
void DL_Shutdown(void)
{
	DL_AbortAll(qtrue, qfalse);
	Com_Memset(fetchRequests, 0, sizeof(fetchRequests));
	nextRequestId = FILE_DOWNLOAD_ID + 1;
}

#endif /* __EMSCRIPTEN__ */
