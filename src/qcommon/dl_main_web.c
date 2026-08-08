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
	unsigned int id;
	qboolean active;
	qboolean isDownload;
	qboolean triedRelay;
	emscripten_fetch_t *fetch;
	webCallbackFunc_t completeCb;
	webProgressCallbackFunc_t progressCb;
	void *userData;
	webRequest_t request;
} webFetchRequest_t;

static webFetchRequest_t fetchRequests[MAX_WEB_REQUESTS];
static unsigned int      nextRequestId = FILE_DOWNLOAD_ID + 1;

static qboolean Web_RetryThroughRelay(webFetchRequest_t *req);

/**
 * @brief Find a fetch request by its Emscripten fetch handle
 */
static webFetchRequest_t *FindFetchByHandle(emscripten_fetch_t *fetch)
{
	int i;

	if (!fetch)
	{
		return NULL;
	}

	// The slot is carried by the fetch itself. A request may fail before
	// emscripten_fetch() returns - a URL the browser refuses to even try
	// (mixed content, a malformed address) fails right away - and its handle
	// is not stored yet at that point, so a lookup by handle would come up
	// empty and the download would hang forever holding its slot.
	for (i = 0; i < MAX_WEB_REQUESTS; i++)
	{
		if (fetchRequests[i].active && fetch->userData == (void *)&fetchRequests[i])
		{
			return &fetchRequests[i];
		}
	}

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

		// A web page may not fetch just any URL, so a failure here does not
		// have to mean the file is not there - try the relay before giving up.
		if (Web_RetryThroughRelay(req))
		{
			emscripten_fetch_close(fetch);
			return;
		}

		if (req->request.data.fileHandle)
		{
			fclose(req->request.data.fileHandle);
			req->request.data.fileHandle = NULL;
		}

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
 * @brief Fill in the fetch attributes shared by every request this file issues
 *
 * The request slot is passed as user data so a callback always finds it back,
 * even when it runs before emscripten_fetch() returned (see FindFetchByHandle).
 */
static void InitFetchAttr(emscripten_fetch_attr_t *attr, webFetchRequest_t *req)
{
	emscripten_fetch_attr_init(attr);
	Q_strncpyz(attr->requestMethod, "GET", sizeof(attr->requestMethod));
	attr->attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
	attr->onsuccess  = FetchOnSuccess;
	attr->onerror    = FetchOnError;
	attr->onprogress = FetchOnProgress;
	attr->userData   = req;
}

/**
 * @brief Percent encode a string so it survives being passed as a query parameter
 * @return qfalse when the result does not fit into the output buffer
 */
static qboolean Web_URLEncode(const char *in, char *out, size_t outSize)
{
	static const char hex[] = "0123456789ABCDEF";
	size_t            pos   = 0;

	while (*in)
	{
		unsigned char c = (unsigned char)*in++;

		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
		    c == '-' || c == '_' || c == '.' || c == '~')
		{
			if (pos + 1 >= outSize)
			{
				return qfalse;
			}

			out[pos++] = (char)c;
		}
		else
		{
			if (pos + 3 >= outSize)
			{
				return qfalse;
			}

			out[pos++] = '%';
			out[pos++] = hex[c >> 4];
			out[pos++] = hex[c & 0x0f];
		}
	}

	out[pos] = '\0';
	return qtrue;
}

/**
 * @brief Build the URL that asks the relay to pass a download through
 *
 * The relay is reached over the same host as the WebSocket connection, so the
 * WebSocket scheme is swapped for the matching HTTP one (a wss:// relay is
 * https://, which is what a page served over HTTPS needs).
 *
 * @return qfalse when there is nothing to ask (no relay, not an absolute URL)
 */
static qboolean Web_RelayDownloadURL(const char *remoteName, char *out, size_t outSize)
{
	const char *relay;
	const char *scheme;
	const char *rest;
	char       base[MAX_STRING_CHARS];
	char       encoded[MAX_STRING_CHARS];
	size_t     len;

	// A relative URL is fetched from the page the client is served by, which
	// is always allowed - there is no point in sending that through the relay.
	if (!remoteName || (Q_stricmpn(remoteName, "http://", 7) && Q_stricmpn(remoteName, "https://", 8)))
	{
		return qfalse;
	}

	relay = Cvar_VariableString("net_wsRelayServer");

	if (!relay[0])
	{
		return qfalse;
	}

	if (!Q_stricmpn(relay, "ws://", 5))
	{
		scheme = "http://";
		rest   = relay + 5;
	}
	else if (!Q_stricmpn(relay, "wss://", 6))
	{
		scheme = "https://";
		rest   = relay + 6;
	}
	else if (!Q_stricmpn(relay, "http://", 7) || !Q_stricmpn(relay, "https://", 8))
	{
		scheme = "";
		rest   = relay;
	}
	else
	{
		return qfalse;
	}

	Com_sprintf(base, sizeof(base), "%s%s", scheme, rest);

	len = strlen(base);
	while (len && base[len - 1] == '/')
	{
		base[--len] = '\0';
	}

	if (!len || !Web_URLEncode(remoteName, encoded, sizeof(encoded)))
	{
		return qfalse;
	}

	if (len + strlen("/download?url=") + strlen(encoded) >= outSize)
	{
		return qfalse;
	}

	Com_sprintf(out, outSize, "%s/download?url=%s", base, encoded);
	return qtrue;
}

/**
 * @brief Retry a failed download through the relay
 *
 * A browser may not fetch just any URL a server redirects it to with
 * sv_wwwBaseURL: a page served over HTTPS may not touch a http:// mirror at
 * all (mixed content) and a mirror on another host has to allow the page with
 * an Access-Control-Allow-Origin header, which a plain file mirror does not
 * send. The download then fails and the server falls back to sending the pk3
 * through the game connection itself, which is far slower than any web server.
 *
 * The relay the browser client already talks to can pass the file through with
 * the header the browser wants, so ask it once before giving up.
 *
 * @return qtrue when a retry was started - the request must not be completed
 */
static qboolean Web_RetryThroughRelay(webFetchRequest_t *req)
{
	emscripten_fetch_attr_t attr;
	char                    url[MAX_STRING_CHARS];

	if (!req->isDownload || req->triedRelay)
	{
		return qfalse;
	}

	if (!Web_RelayDownloadURL(req->request.url, url, sizeof(url)))
	{
		return qfalse;
	}

	// only ever retried once, whatever happens below
	req->triedRelay = qtrue;

	InitFetchAttr(&attr, req);

	req->fetch = emscripten_fetch(&attr, url);

	if (!req->fetch)
	{
		return qfalse;
	}

	Com_Printf("The download was refused by the browser or the mirror, retrying it through the relay: %s\n", url);

	return qtrue;
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
	webFetchRequest_t       *req;
	emscripten_fetch_attr_t attr;
	emscripten_fetch_t      *fetch;
	unsigned int            id;

	req = AllocFetchRequest();
	if (!req)
	{
		return 0;
	}

	id = req->id;

	req->completeCb       = complete;
	req->progressCb       = progress;
	req->userData         = userData;
	req->isDownload       = qtrue;
	req->request.userData = userData;
	req->request.id       = req->id;

	Q_strncpyz(req->request.url, remoteName, sizeof(req->request.url));
	Q_strncpyz(req->request.data.name, localName, sizeof(req->request.data.name));

	// Open the local file for writing
	if (localName && *localName)
	{
		if (FS_CreatePath(localName))
		{
			Com_Printf("DL_BeginDownload: failed to create path for %s\n", localName);
			req->active = qfalse;
			return 0;
		}

		req->request.data.fileHandle = Sys_FOpen(localName, "wb");

		if (!req->request.data.fileHandle)
		{
			Com_Printf("DL_BeginDownload: failed to open %s for writing\n", localName);
			req->active = qfalse;
			return 0;
		}
	}

	InitFetchAttr(&attr, req);

	fetch = emscripten_fetch(&attr, remoteName);

	if (!fetch)
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

	// The request may have failed and been completed - or retried through the
	// relay, which owns req->fetch from then on - before we got here, so only
	// take the handle when it is still this request that is running.
	if (req->active && !req->triedRelay)
	{
		req->fetch = fetch;
	}

	Com_Printf("Starting download: %s -> %s\n", remoteName, localName);

	return id;
}

/**
 * @brief Web_CreateRequest - Create a web request using the Emscripten Fetch API
 */
unsigned int Web_CreateRequest(const char *url, const char *authToken,
                               webUploadData_t *upload, void *userData,
                               webCallbackFunc_t complete,
                               webProgressCallbackFunc_t progress)
{
	webFetchRequest_t       *req;
	emscripten_fetch_attr_t attr;
	emscripten_fetch_t      *fetch;
	unsigned int            id;

	req = AllocFetchRequest();
	if (!req)
	{
		return 0;
	}

	id = req->id;

	req->completeCb       = complete;
	req->progressCb       = progress;
	req->userData         = userData;
	req->request.userData = userData;
	req->request.id       = req->id;

	Q_strncpyz(req->request.url, url, sizeof(req->request.url));

	InitFetchAttr(&attr, req);

	if (upload)
	{
		Q_strncpyz(attr.requestMethod, "POST", sizeof(attr.requestMethod));
		req->request.upload = qtrue;
	}

	// Add auth header if provided
	if (authToken && *authToken)
	{
		static const char *headers[] = { "Authorization", NULL, NULL };
		static char       authHeader[MAX_STRING_CHARS];

		Q_strncpyz(authHeader, authToken, sizeof(authHeader));
		headers[1] = authHeader;

		attr.requestHeaders = headers;
	}

	fetch = emscripten_fetch(&attr, url);

	if (!fetch)
	{
		Com_Printf("Web_CreateRequest: emscripten_fetch failed for %s\n", url);
		req->active = qfalse;
		return 0;
	}

	// the request may already have been completed from a callback (see DL_BeginDownload)
	if (req->active)
	{
		req->fetch = fetch;
	}

	return id;
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
