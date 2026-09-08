#!/usr/bin/env python3
"""Compile only the exact production storage glue and FS key functions.
No game engine, live files, or shared CMake build directory is used.
"""
import argparse, hashlib, json, pathlib, re, subprocess
parser=argparse.ArgumentParser()
parser.add_argument('--emcc',default='emcc')
parser.add_argument('--out',default='build/database-storage-c')
args=parser.parse_args()
root=pathlib.Path(__file__).resolve().parents[2]
out=(root/args.out).resolve();out.mkdir(parents=True,exist_ok=True)
files=(root/'src/qcommon/files.c').read_text()
sys=(root/'src/sys/sys_web.c').read_text()
def function(source,name):
    match=re.search(r'^(?:static )?(?:void|char \*|int|qboolean)\s*'+name+r'\([^\n]*\)\n\{.*?^\}',source,re.M|re.S)
    if not match: raise RuntimeError('Function not found: '+name)
    return match.group(0)+'\n'
pre=r"""
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define MAX_OSPATH 256
#define MAX_QPATH 64
#define PATH_SEP '/'
typedef int qboolean;
enum { qfalse=0,qtrue=1 };
typedef struct { char *string; } testCvar;
static testCvar home={"/home/web_user/.etlegacy"};
static testCvar *fs_homepath=&home;
static void *fs_searchpaths=&home;
static char fs_gamedir[64]="nitmod";
static void Q_strncpyz(char *out,const char *in,int size) { snprintf(out,size,"%s",in); }
static void Com_sprintf(char *out,int size,const char *fmt,...) { va_list ap;va_start(ap,fmt);vsnprintf(out,size,fmt,ap);va_end(ap); }
"""
code=pre+'\n'.join(function(files,name) for name in ['FS_ReplaceSeparators','FS_BuildOSPath','FS_WebDatabaseKey'])
code+='\n'.join(function(sys,name) for name in ['Sys_WebDatabaseRequest','Sys_WebDatabasePoll','Sys_WebDatabaseCopy','Sys_WebDatabaseRelease','Sys_WebDatabaseCancel'])
code+=r"""
int DBFixtureKeyChecks(void) {
 char key[256]; int checks=0;
 #define EXPECT(x) do { ++checks; if(!(x)) return -checks; } while(0)
 EXPECT(FS_WebDatabaseKey("NITMOD_DB.sqlite",key,sizeof(key)));
 EXPECT(!strcmp(key,"/home/web_user/.etlegacy/nitmod/NITMOD_DB.sqlite"));
 home.string="/home/web_user/.etlegacy///";
 EXPECT(FS_WebDatabaseKey("db/users.sqlite",key,sizeof(key)));
 EXPECT(!strcmp(key,"/home/web_user/.etlegacy/nitmod/db/users.sqlite"));
 home.string="/home/web_user/.etlegacy";
 EXPECT(!FS_WebDatabaseKey("../outside.sqlite",key,sizeof(key)));
 EXPECT(!FS_WebDatabaseKey("/absolute.sqlite",key,sizeof(key)));
 EXPECT(!FS_WebDatabaseKey("db\\users.sqlite",key,sizeof(key)));
 EXPECT(!FS_WebDatabaseKey("db//users.sqlite",key,sizeof(key)));
 EXPECT(!FS_WebDatabaseKey("./users.sqlite",key,sizeof(key)));
 EXPECT(!FS_WebDatabaseKey("db/./users.sqlite",key,sizeof(key)));
 EXPECT(!FS_WebDatabaseKey("NITMOD_DB.sqlite",key,5));
 fs_searchpaths=NULL;
 EXPECT(!FS_WebDatabaseKey("NITMOD_DB.sqlite",key,sizeof(key)));
 fs_searchpaths=&home;
 strcpy(fs_gamedir,"etmain");
 EXPECT(FS_WebDatabaseKey("NITMOD_DB.sqlite",key,sizeof(key)));
 EXPECT(!strcmp(key,"/home/web_user/.etlegacy/etmain/NITMOD_DB.sqlite"));
 strcpy(fs_gamedir,"nitmod");
 return checks;
}
void *DBFixtureImage(int marker) {
 unsigned char *image=calloc(1,128);memcpy(image,"SQLite format 3",16);image[100]=marker;return image;
}
"""
source=out/'database_storage_c.c';source.write_text(code)
exports=['_malloc','_free','_DBFixtureKeyChecks','_DBFixtureImage']+['_'+n for n in ['Sys_WebDatabaseRequest','Sys_WebDatabasePoll','Sys_WebDatabaseCopy','Sys_WebDatabaseRelease','Sys_WebDatabaseCancel']]
cmd=[args.emcc,str(source),'-O1','-sMODULARIZE=1','-sEXPORT_NAME=DBFixture','-sALLOW_MEMORY_GROWTH=1',
     '-sEXPORTED_FUNCTIONS='+json.dumps(exports),'-sEXPORTED_RUNTIME_METHODS='+json.dumps(['ccall','getValue']),
     '--pre-js',str(root/'src/web/database_storage.js'),'-o',str(out/'db-c.js')]
subprocess.run(cmd,check=True,cwd=root)
(out/'source-manifest.json').write_text(json.dumps({str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [root/'src/qcommon/files.c',root/'src/sys/sys_web.c',root/'src/web/database_storage.js']},indent=2))
print(out)
