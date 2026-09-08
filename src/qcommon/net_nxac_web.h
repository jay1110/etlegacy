/* Nitmod NxAC host bridge, negotiated ABI1. No freely addressed TCP proxy. */
#ifndef NET_NXAC_WEB_H
#define NET_NXAC_WEB_H
#include "q_shared.h"
#include "qcommon.h"
#define NXWEB_CGAME 0
#define NXWEB_QAGAME 1
/* Caller memory is validated/mapped by the VM dispatch before this call. */
intptr_t NET_NxACWebCall(int owner,const netadr_t *serverAddress,int operation,int handle,void *buffer,int length,int value);
void NET_NxACWebReset(int owner);
qboolean NET_NxACWebRelayMessage(const netadr_t *from,const char *text);
void NET_NxACWebRelayClosed(const netadr_t *from);
/* Supplied by net_web: use only an existing active game WebSocket and refuse
 * unavailable/backpressured sends. It must never create a new connection. */
qboolean NET_WebNxACSend(const netadr_t *to,const char *text);
#endif
