"""Execute the production server DB continuation state machine with delayed VM replies."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[2]
out=root/'build-wasm/db-boot-continuation-test';out.mkdir(exist_ok=True)
source=(root/'src/server/sv_game.c').read_text()
start=source.index('#define GAME_NITMOD_DB_PRE_SHUTDOWN')
end=source.index('\n#endif',start)
console_start=source.index('qboolean SV_GameCommand(void)')
console_end=source.index('\n}',console_start)+2
prefix=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int qboolean;
#define qfalse 0
#define qtrue 1
#define MAX_STRING_CHARS 1024
#define G_NITMOD_DATABASE_STORAGE 1
#define __EMSCRIPTEN__ 1
#define GAME_CONSOLE_COMMAND 9
enum { SS_DEAD,SS_LOADING,SS_GAME };
static struct { int state; } sv;
static int consoleCalls;
#define VM_Ext_IsActive(x) enabled
enum { NITMOD_DB_MAP=1,NITMOD_DB_RESTART,NITMOD_DB_SPAWN,NITMOD_DB_SHUTDOWN };
enum { NITMOD_DB_FINISH_RESTART=1,NITMOD_DB_FINISH_SPAWN };
static int gvm=1,enabled=1,com_errorEntered,state=2,prepare=1;
static int restarts,spawns,shutdowns,prepares;
static unsigned savedGeneration;
static int VM_Call(int vm,int command) {
    if(command==GAME_CONSOLE_COMMAND) { ++consoleCalls;return 1; }
    if(command==0x4e444202) return state;
    assert(command==0x4e444201);++prepares;return prepare;
}
#define Com_Printf(...) ((void)0)
static void Q_strncpyz(char *d,const char *s,int n) { snprintf(d,n,"%s",s); }
static void SV_NitmodDatabaseFinishRestart(void){++restarts;}
static void SV_NitmodDatabaseFinishSpawn(unsigned int g){++spawns;savedGeneration=g;}
static void SV_NitmodDatabaseMap(const char *s,int n){assert(0);}
static void SV_NitmodDatabaseRestart(int n){assert(0);}
static void SV_SpawnServer(const char *s){assert(0);}
static void SV_Shutdown(const char *s){++shutdowns;}
'''
tests=r'''
int main(void) {
    int i;
    sv.state=SS_DEAD;assert(!SV_GameCommand());
    sv.state=SS_LOADING;assert(!SV_GameCommand() && !consoleCalls);
    assert(SV_NitmodDatabaseWaitBoot(NITMOD_DB_FINISH_RESTART,0));
    assert(SV_GameCommand() && consoleCalls==1);
    gvm=0;assert(!SV_GameCommand() && consoleCalls==1);gvm=1;
    for(i=0;i<100;i++) {
        assert(SV_NitmodDatabaseFrame());
        assert(SV_NitmodDatabaseBootPending());
        assert(SV_NitmodDatabasePendingTransition());
        assert(!restarts && !spawns && !prepares);
    }
    state=1;assert(SV_NitmodDatabaseFrame());
    assert(restarts==1 && !SV_NitmodDatabaseBootPending());
    assert(!SV_GameCommand());
    sv.state=SS_GAME;assert(SV_GameCommand() && consoleCalls==2);
    assert(!SV_NitmodDatabaseFrame() && restarts==1);
    state=2;assert(SV_NitmodDatabaseWaitBoot(NITMOD_DB_FINISH_SPAWN,0xf0000001u));
    assert(SV_NitmodDatabaseFrame() && spawns==0);
    state=1;assert(SV_NitmodDatabaseFrame());
    assert(spawns==1 && savedGeneration==0xf0000001u);
    state=2;assert(SV_NitmodDatabaseWaitBoot(NITMOD_DB_FINISH_RESTART,0));
    state=0;assert(SV_NitmodDatabaseFrame()); /* failed boot reaches normal rejection */
    assert(restarts==2 && !SV_NitmodDatabaseBootPending());
    state=-1;assert(!SV_NitmodDatabaseWaitBoot(NITMOD_DB_FINISH_RESTART,0));
    enabled=0;state=2;assert(!SV_NitmodDatabaseConnectPending());enabled=1;
    state=1;prepare=2;
    assert(SV_NitmodDatabaseDefer(NITMOD_DB_SHUTDOWN,"test",0));
    assert(SV_NitmodDatabaseFrame() && !shutdowns);
    prepare=1;assert(SV_NitmodDatabaseFrame() && shutdowns==1);
    assert(!SV_NitmodDatabasePendingTransition());
    state=2;assert(SV_NitmodDatabaseWaitBoot(NITMOD_DB_FINISH_RESTART,0));
    i=prepares;assert(SV_NitmodDatabaseDefer(NITMOD_DB_SHUTDOWN,"during boot",0));
    assert(prepares==i && SV_NitmodDatabaseFrame() && shutdowns==1);
    state=1;assert(SV_NitmodDatabaseFrame() && shutdowns==1);
    assert(SV_NitmodDatabaseFrame() && shutdowns==2);
    puts("production engine DB continuation: delayed restart/spawn, single resume, failed/old VM and shutdown drain PASS");
}
'''
c=out/'test.c';c.write_text(prefix+source[start:end]+source[console_start:console_end]+tests)
subprocess.run([str(Path.home()/'emsdk/upstream/emscripten/emcc.bat'),str(c),'-O1','-sEXIT_RUNTIME=1','-o',str(out/'test.js')],check=True)
subprocess.run(['node',str(out/'test.js')],check=True)
