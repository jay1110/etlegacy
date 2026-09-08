/* NxAC host bridge. Local stream endpoints are explicitly virtual loopback
 * endpoints. Remote streams are bound to the established game relay socket;
 * address/port authorization occurs in that relay, not in module text. */
#include "net_nxac_web.h"
#include <limits.h>
#include <string.h>
#include <stdlib.h>

#define NX_CHANNELS 16
#define NX_SESSIONS 64
#define NX_QUEUE 65536
#define NX_CHUNK 16384
#define NX_TEXT 21920
#define NX_LOCAL_FIRST_PORT 41000
/* ABI1 opcodes match Nitmod game/nitmod_nxac_host.h. */
enum { NX_CAPS,NX_LISTEN,NX_CONNECT,NX_INFO,NX_ACCEPT,NX_SEND,NX_RECV,NX_CLOSE,NX_RESET };
enum { NX_LISTENER=1,NX_LOCAL=2,NX_REMOTE=3 };
typedef struct { int localPort,peerPort; char peerIp[16]; } nxWebInfo_t;
typedef char nxWebInfoSize[sizeof(nxWebInfo_t)==24?1:-1];
typedef struct {
    int id,owner,kind,state,peer,listener,accepted,localPort,remotePort,head,size;
    unsigned int generation;
    qboolean eof,openSent;
    netadr_t address;
    byte queue[NX_QUEUE];
} nxWebChannel_t;
static nxWebChannel_t channels[NX_CHANNELS];
static struct { qboolean ready; netadr_t address; } sessions[NX_SESSIONS];
static unsigned int generations[2];
static int nextId=1,nextPort=NX_LOCAL_FIRST_PORT;
static const char base64Digits[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static qboolean Owner(int owner){return owner==NXWEB_CGAME || owner==NXWEB_QAGAME;}
static nxWebChannel_t *Find(int id){int i;for(i=0;i<NX_CHANNELS;++i)if(channels[i].id==id && id>0)return &channels[i];return NULL;}
static nxWebChannel_t *Owned(int owner,int id){nxWebChannel_t *c=Find(id);return c && c->owner==owner && c->generation==generations[owner]?c:NULL;}
static nxWebChannel_t *Allocate(int owner,int kind){
    int i;if(nextId<=0 || nextId==INT_MAX)return NULL; /* No handle reuse after wrap. */
    for(i=0;i<NX_CHANNELS;++i)if(!channels[i].id){nxWebChannel_t *c=&channels[i];memset(c,0,sizeof(*c));c->id=nextId++;c->owner=owner;c->kind=kind;c->generation=generations[owner];return c;}return NULL;
}
static qboolean Ready(const netadr_t *address){int i;if(!address)return qfalse;for(i=0;i<NX_SESSIONS;++i)if(sessions[i].ready && NET_CompareAdr(address,&sessions[i].address))return qtrue;return qfalse;}
static void Close(nxWebChannel_t *c,qboolean notify){
    int i,id;if(!c || !c->id)return;id=c->id;
    if(c->kind==NX_REMOTE && notify){char text[64];Com_sprintf(text,sizeof(text),"nxac1 close %d",c->id);NET_WebNxACSend(&c->address,text);}
    if(c->kind==NX_LOCAL){nxWebChannel_t *peer=Find(c->peer);if(peer){peer->peer=0;peer->eof=qtrue;}}
    memset(c,0,sizeof(*c));
    /* A listener owns its unaccepted and accepted local server endpoints. */
    for(i=0;i<NX_CHANNELS;++i)if(channels[i].id && channels[i].listener==id)Close(&channels[i],qfalse);
}
void NET_NxACWebReset(int owner){int i;if(!Owner(owner))return;for(i=0;i<NX_CHANNELS;++i)if(channels[i].id && channels[i].owner==owner)Close(&channels[i],qtrue);++generations[owner];}
static int Push(nxWebChannel_t *c,const byte *data,int size){int first;if(size<0 || size>NX_QUEUE-c->size)return 0;first=NX_QUEUE-((c->head+c->size)%NX_QUEUE);if(first>size)first=size;memcpy(c->queue+(c->head+c->size)%NX_QUEUE,data,first);memcpy(c->queue,data+first,size-first);c->size+=size;return size;}
static int Pop(nxWebChannel_t *c,byte *data,int size){int first;if(size>c->size)size=c->size;first=NX_QUEUE-c->head;if(first>size)first=size;memcpy(data,c->queue+c->head,first);memcpy(data+first,c->queue,size-first);c->head=(c->head+size)%NX_QUEUE;c->size-=size;return size;}
static int Encode(const byte *data,int size,char *out){int i=0,n=0;while(i<size){unsigned int a=data[i++],b=0,c=0;int haveB=i<size,haveC;if(haveB)b=data[i++];haveC=i<size;if(haveC)c=data[i++];out[n++]=base64Digits[a>>2];out[n++]=base64Digits[((a&3)<<4)|(b>>4)];out[n++]=haveB?base64Digits[((b&15)<<2)|(c>>6)]:'=';out[n++]=haveC?base64Digits[c&63]:'=';}out[n]=0;return n;}
static int Digit(char c){const char *p;if(!c)return -1;p=strchr(base64Digits,c);return p?(int)(p-base64Digits):-1;}
static int Decode(const char *text,byte *data){
    int len=(int)strlen(text),i,n=0;if(!len || len>21848 || len%4)return -1;
    for(i=0;i<len;i+=4){int a=Digit(text[i]),b=Digit(text[i+1]),c=text[i+2]=='='?-2:Digit(text[i+2]),d=text[i+3]=='='?-2:Digit(text[i+3]);
        if(a<0 || b<0 || c==-1 || d==-1 || (c==-2 && d!=-2) || ((c==-2 || d==-2) && i+4!=len))return -1;
        if((c==-2 && (b&15)) || (d==-2 && c>=0 && (c&3)))return -1;
        if(n>=NX_CHUNK)return -1;data[n++]=(byte)((a<<2)|(b>>4));
        if(c>=0){if(n>=NX_CHUNK)return -1;data[n++]=(byte)((b<<4)|(c>>2));}
        if(d>=0){if(n>=NX_CHUNK)return -1;data[n++]=(byte)((c<<6)|d);}
    }return n;
}
static int Number(const char **text){const char *p=*text;int n=0;if(*p<'0'||*p>'9')return -1;while(*p>='0'&&*p<='9'){int digit=*p++-'0';if(n>(INT_MAX-digit)/10)return -1;n=n*10+digit;}*text=p;return n;}
static void RemoteError(nxWebChannel_t *c){if(c){char text[64];Com_sprintf(text,sizeof(text),"nxac1 close %d",c->id);NET_WebNxACSend(&c->address,text);c->state=-1;c->eof=qtrue;}}
qboolean NET_NxACWebRelayMessage(const netadr_t *from,const char *text){
    const char *p;char verb[16];int i=0,id,n;nxWebChannel_t *c;
    if(!from || !text || strncmp(text,"nxac1 ",6))return qfalse;
    /* No strcpy/strlen on the payload until the net_web ingress bound, also
     * independently verified here, is satisfied. */
    for(i=0;i<NX_TEXT && text[i];++i){}if(i==NX_TEXT)return qtrue;
    p=text+6;if(!strcmp(p,"ready")){for(i=0;i<NX_SESSIONS;++i)if(sessions[i].ready && NET_CompareAdr(from,&sessions[i].address))return qtrue;for(i=0;i<NX_SESSIONS;++i)if(!sessions[i].ready){sessions[i].ready=qtrue;sessions[i].address=*from;break;}return qtrue;}
    i=0;while(*p && *p!=' ' && i<15)verb[i++]=*p++;verb[i]=0;if(*p++!=' ')return qtrue;
    id=Number(&p);c=Find(id);if(!c || c->kind!=NX_REMOTE || c->owner!=NXWEB_CGAME || c->generation!=generations[NXWEB_CGAME] || !NET_CompareAdr(from,&c->address))return qtrue;
    if(!strcmp(verb,"opened")){
        if(*p++!=' ' || c->state!=0 || !c->openSent){RemoteError(c);return qtrue;}n=Number(&p);if(*p || n<1 || n>65535){RemoteError(c);return qtrue;}c->localPort=n;c->state=1;return qtrue;
    }
    if(!strcmp(verb,"data")){
        byte bytes[NX_CHUNK];if(*p++!=' ' || c->state!=1){RemoteError(c);return qtrue;}n=Decode(p,bytes);
        if(n<0 || Push(c,bytes,n)!=n)RemoteError(c);return qtrue;
    }
    if(!strcmp(verb,"eof") && !*p){c->eof=qtrue;return qtrue;}
    if(!strcmp(verb,"error")){c->state=-1;c->eof=qtrue;return qtrue;}
    RemoteError(c);return qtrue;
}
void NET_NxACWebRelayClosed(const netadr_t *from){int i;if(!from)return;for(i=0;i<NX_SESSIONS;++i)if(sessions[i].ready && NET_CompareAdr(from,&sessions[i].address))sessions[i].ready=qfalse;for(i=0;i<NX_CHANNELS;++i)if(channels[i].id && channels[i].kind==NX_REMOTE && NET_CompareAdr(from,&channels[i].address)){channels[i].state=-1;channels[i].eof=qtrue;}}
intptr_t NET_NxACWebCall(int owner,const netadr_t *serverAddress,int op,int handle,void *buffer,int length,int value){
    nxWebChannel_t *c,*peer,*listener;int i,n;
    if(!Owner(owner))return -1;
    if(op==NX_CAPS){if(owner==NXWEB_QAGAME)return 2;if(serverAddress && serverAddress->type==NA_LOOPBACK)return 2;return Ready(serverAddress)?1:0;}
    if(op==NX_RESET){NET_NxACWebReset(owner);return 0;}
    if(op==NX_LISTEN){
        if(owner!=NXWEB_QAGAME || value<0 || value>65535)return -1;
        for(i=0;i<NX_CHANNELS;++i)if(channels[i].id && channels[i].kind==NX_LISTENER)return -1;
        c=Allocate(owner,NX_LISTENER);if(!c)return -1;c->state=1;c->localPort=value?value:27960;return c->id;
    }
    if(op==NX_CONNECT){
        if(owner!=NXWEB_CGAME || !serverAddress || value<1 || value>65535)return -1;
        if(serverAddress->type==NA_LOOPBACK){
            listener=NULL;for(i=0;i<NX_CHANNELS;++i)if(channels[i].id && channels[i].kind==NX_LISTENER && channels[i].generation==generations[NXWEB_QAGAME] && channels[i].localPort==value){listener=&channels[i];break;}
            if(!listener)return -1;
            c=Allocate(owner,NX_LOCAL);if(!c)return -1;peer=Allocate(NXWEB_QAGAME,NX_LOCAL);if(!peer){Close(c,qfalse);return -1;}
            if(nextPort>=65000)nextPort=NX_LOCAL_FIRST_PORT;
            c->state=peer->state=1;c->localPort=nextPort++;c->remotePort=value;c->peer=peer->id;c->accepted=1;
            peer->localPort=value;peer->remotePort=c->localPort;peer->peer=c->id;peer->listener=listener->id;return c->id;
        }
        if(!Ready(serverAddress))return -1;
        for(i=0;i<NX_CHANNELS;++i)if(channels[i].id && channels[i].kind==NX_REMOTE && NET_CompareAdr(serverAddress,&channels[i].address))return -1;
        c=Allocate(owner,NX_REMOTE);if(!c)return -1;c->address=*serverAddress;c->remotePort=value;
        {char text[64];Com_sprintf(text,sizeof(text),"nxac1 open %d %d",c->id,value);c->openSent=NET_WebNxACSend(serverAddress,text);}return c->id;
    }
    c=Owned(owner,handle);if(!c)return -1;
    /* Reject an old Cgame handle after the engine has changed game sessions. */
    if(owner==NXWEB_CGAME && c->kind==NX_REMOTE && (!serverAddress || !NET_CompareAdr(serverAddress,&c->address)))return -1;
    if(owner==NXWEB_CGAME && c->kind==NX_LOCAL && (!serverAddress || serverAddress->type!=NA_LOOPBACK))return -1;
    if(op==NX_CLOSE){Close(c,qtrue);return 0;}
    if(op==NX_INFO){
        nxWebInfo_t info;if(!buffer || length!=sizeof(info))return -1;
        if(c->kind==NX_REMOTE && c->state==0 && !c->openSent){char text[64];Com_sprintf(text,sizeof(text),"nxac1 open %d %d",c->id,c->remotePort);c->openSent=NET_WebNxACSend(&c->address,text);}
        if(c->state<=0)return c->state;
        memset(&info,0,sizeof(info));info.localPort=c->localPort;info.peerPort=c->remotePort;if(c->kind!=NX_REMOTE)Q_strncpyz(info.peerIp,"127.0.0.1",sizeof(info.peerIp));
        memcpy(buffer,&info,sizeof(info));return 1;
    }
    if(op==NX_ACCEPT){if(owner!=NXWEB_QAGAME || c->kind!=NX_LISTENER)return -1;for(i=0;i<NX_CHANNELS;++i)if(channels[i].id && channels[i].listener==c->id && !channels[i].accepted){channels[i].accepted=1;return channels[i].id;}return -1;}
    if((op!=NX_SEND && op!=NX_RECV) || !buffer || length<1 || length>NX_CHUNK || c->kind==NX_LISTENER || c->state!=1)return -1;
    if(op==NX_RECV){if(c->size)return Pop(c,(byte *)buffer,length);return c->eof?-2:0;}
    if(c->eof)return -1;
    if(c->kind==NX_LOCAL){peer=Find(c->peer);if(!peer)return -1;n=NX_QUEUE-peer->size;if(n>length)n=length;return n?Push(peer,(const byte *)buffer,n):0;}
    {char text[NX_TEXT];Com_sprintf(text,sizeof(text),"nxac1 data %d ",c->id);n=(int)strlen(text);Encode((const byte *)buffer,length,text+n);return NET_WebNxACSend(&c->address,text)?length:0;}
}
