/* This file is part of REWise.
 *
 * REWise is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * REWise is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

/*
DISCLAIMER
----------
The way the data is interpreted may be very wrong! It is the result of many
hours of puzzling with ImHex and this is what made the most sense to me with
the data (different installer executables) available at this time. Also what
most values represent inside a operation struct is still a mystery.

Another source of getting insight on how the Wise installer handles this would
be to disassemble wise0132.dll, I have done this with Cutter and its Ghidra
plugin but it was very hard since I couldn't rename about half of the variables,
which is a known issue of Cutter + the Ghidra plugin (at this time), but it
gave some hints.


INSEPCTED INSTALLERS
--------------------

NAME          MD5                               FILE
----          ---                               ----
HL:CS  (CD)   43cd9aff74e04fa1b59321b898d9a6bd  counter-strike.exe
HLGOTY (CD)   f5b8b35ca02beeeb146e62a63a0273a6  SETUP.EXE
CS15          eedcfcf6545cef92f26fb9a7fdd77c42  csv15full.exe
RTCW   (CD)   f2d9e3e1eaaed66047210881d130384f  Setup.exe
ET            5cc104767ecdf0feb3a36210adf46a8e  WolfET.exe

The way this program currently interprets the binary Wise script file is as
follows:

 1. Read 'struct WiseScriptHeader'
 2. Read 'struct CStrings'
 3. Read one byte as operation code, we use this value to determine what struct
    to read, after that struct is read, continue this step (read one operation
    byte again etc..)

Operation codes:

  0x00  // Custom deflate file header
  0x03  // ?
  0x04  // Form data?
  0x05  // .ini file, section-name and values for that section
  0x06  // Deflated file just used by the installer? (No filename)
  0x07
  0x08  // end branch
  0x09  // function call ?
  0x0A
  0x0B
  0x0C  // if statement (new branch)
  0x0D  // else/default statement (inside if statement branch)
  0x0F  // Start form data?
  0x10  // End form data?
  0x11
  0x12  // File on install medium (CD/DVD), to copy?
  0x14  // Deflated file just used by the installer? (No filename)
  0x15
  0x16  // Temp filename?
  0x17
  0x18  // Skip this byte? On some installers also skip next 6 bytes FIXME
  0x19
  0x23  // else if statement (inside if statement branch)
  0x24  // Skip this byte? Only seen in RTCW
  0x25  // Skip this byte? Only seen in RTCW
  0x1A
  0x1B  // Skip this byte
  0x1C
  0x1D
  0x1E
  0x30  // read 1 byte and 2 strings, only seen in cuteftp.exe, same
        // as 0x15? or maybe even 0x23?
*/

#ifndef H_WISESCRIPT
#define H_WISESCRIPT

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "reader.h"
#include "errors.h"

#define WIN_PATH_MAX 260
#define WIN_PATH_CONVERT_MAX WIN_PATH_MAX * 2 // should be plenty

// buffer sizes
#define WISESCRIPT_LANG_MAX 255 // language name string "English" for example
#define WISESCRIPT_COMP_MAX 255 // component name string

// branching
#define WISESCRIPT_MAX_BRANCHES 1024
#define WISESCRIPT_BRANCH_CMP_NONE    0
#define WISESCRIPT_BRANCH_CMP_LANG    1
#define WISESCRIPT_BRANCH_CMP_COMP    2
#define WISESCRIPT_BRANCH_CMP_IGNORED 3

// Maximum string sizes for sanity, these values are guesses but should
// be large enough. Adjust as needed (FIXME, make more strict).
#define MAX_STRING_READ_FIXME     MAX_READ_STRING_LENGTH // from reader.h
#define MAX_STRING_READ_DESTFILE  WIN_PATH_MAX
#define MAX_STRING_READ_VARNAME   255
#define MAX_STRING_READ_VARVALUE  255
// for multilingual there are two selection strings
#define MAX_STRING_READ_LANGUAGE_SELECTION  1024
// language name string "Español" for example
#define MAX_STRING_READ_LANGUAGE  WISESCRIPT_LANG_MAX

// For filtering files by language or component and tracking branches
typedef struct {
  bool hasLangSet;
  bool hasCompSet;

  int currentLangUTF8Extra;
  int currentCompUTF8Extra;

  char currentLang[WISESCRIPT_LANG_MAX];
  char currentComp[WISESCRIPT_COMP_MAX];

  uint8_t _branches[WISESCRIPT_MAX_BRANCHES];
  uint32_t _branchCount;
} WiseScriptParseState;


/* WiseScriptHeader */
typedef struct {
  unsigned char unknown_5[5];

  // total deflated size of OP 0x00 files?
  // this seems to match the offset we can do filesize - someoffset_1
  // to get to the script file deflate offset, but not on all
  // installers..
  uint32_t someoffset_1;

  // TODO check these (filesize if not 0 ?, greater then 0 seen on
  // multidisc installers).
  uint32_t someoffset_2;

  unsigned char unknown_4[4];

  // Creation of this WiseScript.bin since UNIX epoch.
  uint32_t datetime;

  unsigned char unknown_22[22];

  SafeCString url;     // only seen in glsetup.exe, others just \0
  SafeCString logPath; // \0 terminated string
  SafeCString font;    // \0 terminated string

  unsigned char unknown_6[6];

  // if languageCount > 1: the total string count is larger, there
  // will be the language selection strings at top and the normal
  // strings (56) minus 1 (55) will be times the languageCount, plus 2.
  //
  // Language selection strings example when there are 6 languages:
  //
  //   "Select Language"                  ;; selection string 1
  //   "Please Select a Language"         ;; selection string 2
  //   "U.S. English"                     ;; language name
  //   "ENU"                              ;; language short
  //   "Fran.ias"
  //   "FRA"
  //   "Deutsch"
  //   "DEU"
  //   "Portugu.s"
  //   "PTG"
  //   "Espa.ol"
  //   "ESN"
  //   "Italiano"
  //   "ITA"
  //
  // The total string count seen with 6 languages is 434 and the
  // total string count seen with 1 language has been always 56, for a
  // languageCount of 5 the string count should be 287. As seen for now.
  //
  // if (languageCount > 1) {
  //   stringCount = (55 * languageCount) + (languageCount * 2) + 2;
  // }
  // else {
  //   stringCount = 56 = (55 * languageCount) + languageCount
  // }
  // 
  //
  // The container size (uint8_t) is a guess, because the neightbour
  // bytes are almost all 0x00 (as seen for now). So did you find a
  // installer with more then 255 languages? then FIXME :')
  uint8_t languageCount;

  // These are skipped here, but for reference:
  // .. read 7 unknown strings (most set to 7 * 0x0,
  //                            only seen set in smartsd.exe)
  //
  // .. read 1 string when the languageCount is 1 else
  //    (languageCount * 2) + 2
  //
  // .. read 55 * languageCount strings
} WiseScriptHeader;

/* 0x00 WiseScriptFileHeader */
typedef struct {
  unsigned char unknown_2[2];   // seen: 0x8000, 0x8100, 0x0000, 0x9800 0xA100
  uint32_t deflateStart;
  uint32_t deflateEnd;
  uint16_t date;                // MS-DOS date
  uint16_t time;                // MS-DOS time
  uint32_t inflatedSize;
  unsigned char unknown_20[20]; // 20 * \0? No see hl15of16.exe and hl1316.exe
  uint32_t crc32;               // do not check when it is 0
  SafeCString destFile;         // \0 terminated string
  SafeCStrings fileTexts;           // One file text per language
  // Seen used on hl15of16.exe and hl1316.exe, on others its \0
  SafeCString unknownString;
} WiseScriptFileHeader;

/* WiseScriptUnknown0x03 */
typedef struct {
  unsigned char unknown_1; // unknown, maybe flags?
  // error strings, two per language (1 title and 1 message)
  SafeCStrings langStrings; // langCount * 2, two strings per language.
} WiseScriptUnknown0x03;

/* WiseScriptUnknown0x04 Form data? */
typedef struct {
  unsigned char no;  // read this struct again until 'no' == 0 ?
  SafeCStrings dataStrings; // langCount amount of strings
} WiseScriptUnknown0x04;

/* WiseScriptUnknown0x05 Something with .ini file */
typedef struct {
  // write .ini file?
  SafeCString file;    // open for writing in append mode
  SafeCString section; // ini section text
  SafeCString values;  // multiline string containing values.
} WiseScriptUnknown0x05;

/* WiseScriptUnknown0x06 deflated Wise file? */
typedef struct {
  uint32_t deflateStart;
  uint32_t deflateEnd;
  uint32_t inflatedSize;
} WiseScriptDeflateInfo;

typedef struct {
  WiseScriptDeflateInfo *info; // one per language
  uint32_t count; // amount of used WiseScriptDeflateInfo structs
  uint32_t size; // amount of allocated WiseScriptDeflateInfo structs
} WiseScriptDeflateInfoContainer;

typedef struct {
  unsigned char unknown_2[2];
  uint32_t unknown;
  WiseScriptDeflateInfoContainer deflateInfo;
  unsigned char terminator; // terminator?
} WiseScriptUnknown0x06;

/* WiseScriptUnknown0x07 */
typedef struct {
  unsigned char unknown_1;
  SafeCString unknownString_1;
  SafeCString unknownString_2;
  SafeCString unknownString_3;
} WiseScriptUnknown0x07;

/* WiseScriptUnknown0x08 aka the 'end' op */
typedef struct {
  unsigned char unknown_1;
} WiseScriptUnknown0x08;

/* WiseScriptUnknown0x09 */
typedef struct {
  unsigned char unknown_1;
  SafeCString unknownString_1; // .dll path/name or NULL for Wise internal
  SafeCString unknownString_2; // func name
  SafeCString unknownString_3; // args?
  SafeCString unknownString_4; // args?
  SafeCStrings unknownStrings; // one string per language count
} WiseScriptUnknown0x09;

/* WiseScriptUnknown0x0A */
typedef struct {
  unsigned char unknown_2[2]; // 0x0200
  SafeCString unknownString_1;
  SafeCString unknownString_2;
  SafeCString unknownString_3;
} WiseScriptUnknown0x0A;

/* WiseScriptUnknown0x0B - section definition */
typedef struct {
  unsigned char unknown_1;

  // examples:
  //   0x0B 00 '%MAINDIR%\cgame_mp_x86.dll'
  //   0x0B 00 '%MAINDIR%\qagame_mp_x86.dll'
  //   0x0B 00 '%MAINDIR%\ui_mp_x86.dll'
  SafeCString unknownString_1;
} WiseScriptUnknown0x0B;

/* WiseScriptUnknown0x0C - aka the 'if' struct */
typedef struct {
  // examples:
  //   0x0C 18 'DELAY' '3000'
  //   0x0C 00 'LANG' 'Deutsch'
  //   0x0C 00 'BRANDING' '1'
  //   0x0C 00 'NAME' '(null)'
  //   0x0C 07 'WOLF_VERSION' '1.32'
  //   0x0C 00 'WOLF_VERSION' '(null)'
  //   0x0C 00 'PATCH_INSTALLED' '1'
  //   0x0C 0A 'COMPONENTS' 'B'
  //   0x0C 00 'DISPLAY' 'Start Installation'
  //   0x0C 00 'DIRECTION' 'N'
  //   0x0C 02 'MAINDIR' '('
  //   0x0C 02 'MAINDIR' '?'
  //   0x0C 02 'MAINDIR' '/'
  //   0x0C 02 'THE_PATH' ':'
  //   0x0C 02 'MAINDIR' '*'
  //   0x0C 02 'MAINDIR' '"'
  //   0x0C 02 'MAINDIR' '<'
  //   0x0C 02 'MAINDIR' '>'
  //   0x0C 02 'MAINDIR' '|'
  //   0x0C 02 'MAINDIR' ';'
  //   0x0C 02 'MAINDIR' ')'
  //   0x0C 04 'MAINDIR' '%WIN%'
  //   0x0C 0A 'COMPONENTS' 'A'
  //   0x0C 00 'LANG' 'English'
  //   0x0C 00 'LANG' 'Deutsch'
  //   0x0C 00 'LANG' 'Italiano'
  //   0x0C 0A 'COMPONENTS' 'B'
  //   0x0C 00 'PATCH_INSTALLED' '1'
  //   0x0C 00 'COMPONENTS' 'A'
  unsigned char unknown_1;
  SafeCString varName;
  SafeCString varValue;
} WiseScriptUnknown0x0C;

/* WiseScriptUnknown0x11 */
typedef struct {
  SafeCString unknownString_1;
} WiseScriptUnknown0x11;

/* WiseScriptUnknown0x12 File on install medium (CD/DVD) */
typedef struct {
  unsigned char unknown_1; // 0C
  unsigned char unknown_41[41];
  SafeCString sourceFile;
  SafeCString unknownString_1;
  // Unknown string(s), one per language
  SafeCStrings unknownStrings;
  SafeCString destFile;
} WiseScriptUnknown0x12;

/* WiseScriptUnknown0x14 Wise script file? */
typedef struct {
  uint32_t deflateStart;
  uint32_t deflateEnd;
  uint32_t inflatedSize;
  SafeCString name;
  SafeCString message;
} WiseScriptUnknown0x14;

/* WiseScriptUnknown0x15 */
typedef struct {
  unsigned char unknown_1;
  SafeCString unknownString_1;
  SafeCString unknownString_2;
} WiseScriptUnknown0x15;

/* WiseScriptUnknown0x16 (TempFileName) */
typedef struct {
  SafeCString name;
} WiseScriptUnknown0x16;

/* WiseScriptUnknown0x17 */
typedef struct {
  unsigned char unknown_1;
  unsigned char unknown_4[4];
  SafeCString unknownString_1;
} WiseScriptUnknown0x17;

/* WiseScriptUnknown0x19 */
typedef struct {
  unsigned char unknown_1;
  SafeCString unknownString_1;
  SafeCString unknownString_2;
  SafeCString unknownString_3;
} WiseScriptUnknown0x19;

/* WiseScriptUnknown0x1A */
typedef struct {
  unsigned char unknown_1;
  SafeCString unknownString_1;
  SafeCString unknownString_2;
} WiseScriptUnknown0x1A;

/* WiseScriptUnknown0x1C */
typedef struct {
  // examples
  // 0x1C 'RegDB Tree: Software\Sierra OnLine\Setup\GUNMANDEMO
  // 0x1C 'RegDB Root: 2
  // 0x1C 'RegDB Tree: Software\Sierra On-Line\Gunman Demo
  // 0x1C 'RegDB Root: 2
  // 0x1C 'RegDB Tree: Software\Valve\Gunman Demo
  // 0x1C 'RegDB Root: 1

  SafeCString unknownString_1;
} WiseScriptUnknown0x1C;

/* WiseScriptUnknown0x1D - only seen in Blitzkriegdemo.exe */
typedef struct {
  SafeCString unknownString_1;
  SafeCString unknownString_2;
} WiseScriptUnknown0x1D;


/* WiseScriptUnknown0x1E */
typedef struct {
  unsigned char unknown;
  SafeCString unknownString;
} WiseScriptUnknown0x1E;

/* WiseScriptUnknown0x23 aka the 'else if' struct, same as the
 * WiseScriptUnknown0x0C struct, but handled different. */
typedef struct {
  unsigned char unknown_1;
  SafeCString varName;
  SafeCString varValue;
} WiseScriptUnknown0x23;

/* WiseScriptUnknown0x30 */
typedef struct {
  unsigned char unknown_1;
  SafeCString unknownString_1;
  SafeCString unknownString_2;
} WiseScriptUnknown0x30;


REWiseStatus readWiseScriptHeader(FILE *const fp,
                                  WiseScriptHeader *const header);
REWiseStatus readWiseScriptFileHeader(FILE *const fp,
                                      WiseScriptFileHeader *const data,
                                      const uint32_t langCount);
REWiseStatus readWiseScriptUnknown0x03(FILE *const fp,
                                       WiseScriptUnknown0x03 *const data,
                                       const uint32_t langCount);
REWiseStatus readWiseScriptUnknown0x04(FILE *const fp,
                                       WiseScriptUnknown0x04 *const data,
                                       const uint32_t langCount);
REWiseStatus readWiseScriptUnknown0x05(FILE *const fp,
                                       WiseScriptUnknown0x05 *const data);
REWiseStatus readWiseScriptUnknown0x06(FILE *const fp,
                                       WiseScriptUnknown0x06 *const data,
                                       const uint32_t langCount);
REWiseStatus readWiseScriptUnknown0x07(FILE *const fp,
                                       WiseScriptUnknown0x07 *const data);
REWiseStatus readWiseScriptUnknown0x08(FILE *const fp,
                                       WiseScriptUnknown0x08 *const data); // no-free (no strings)
REWiseStatus readWiseScriptUnknown0x09(FILE *const fp,
                                       WiseScriptUnknown0x09 *const data, const uint32_t langCount);
REWiseStatus readWiseScriptUnknown0x0A(FILE *const fp,
                                       WiseScriptUnknown0x0A *const data);
REWiseStatus readWiseScriptUnknown0x0B(FILE *const fp,
                                       WiseScriptUnknown0x0B *const data);
REWiseStatus readWiseScriptUnknown0x0C(FILE *const fp,
                                       WiseScriptUnknown0x0C *const data);
REWiseStatus readWiseScriptUnknown0x11(FILE *const fp,
                                       WiseScriptUnknown0x11 *const data);
REWiseStatus readWiseScriptUnknown0x12(FILE *const fp,
                                       WiseScriptUnknown0x12 *const data,
                                       const uint32_t langCount);
REWiseStatus readWiseScriptUnknown0x14(FILE *const fp,
                                       WiseScriptUnknown0x14 *const data);
REWiseStatus readWiseScriptUnknown0x15(FILE *const fp,
                                       WiseScriptUnknown0x15 *const data);
REWiseStatus readWiseScriptUnknown0x16(FILE *const fp,
                                       WiseScriptUnknown0x16 *const data);
REWiseStatus readWiseScriptUnknown0x17(FILE *const fp,
                                       WiseScriptUnknown0x17 *const data);
REWiseStatus readWiseScriptUnknown0x19(FILE *const fp,
                                       WiseScriptUnknown0x19 *const data);
REWiseStatus readWiseScriptUnknown0x1A(FILE *const fp,
                                       WiseScriptUnknown0x1A *const data);
REWiseStatus readWiseScriptUnknown0x1C(FILE *const fp,
                                       WiseScriptUnknown0x1C *const data);
REWiseStatus readWiseScriptUnknown0x1D(FILE *const fp,
                                       WiseScriptUnknown0x1D *const data);
REWiseStatus readWiseScriptUnknown0x1E(FILE *const fp,
                                       WiseScriptUnknown0x1E *const data);
REWiseStatus readWiseScriptUnknown0x23(FILE *const fp,
                                       WiseScriptUnknown0x23 *const data);
REWiseStatus readWiseScriptUnknown0x30(FILE *const fp,
                                       WiseScriptUnknown0x30 *const data);


void freeWiseScriptHeader(WiseScriptHeader *const header);
void freeWiseScriptFileHeader(WiseScriptFileHeader *const data);
void freeWiseScriptUnknown0x03(WiseScriptUnknown0x03 *const data);
void freeWiseScriptUnknown0x04(WiseScriptUnknown0x04 *const data);
void freeWiseScriptUnknown0x05(WiseScriptUnknown0x05 *const data);
void freeWiseScriptUnknown0x06(WiseScriptUnknown0x06 *const data);
void freeWiseScriptUnknown0x07(WiseScriptUnknown0x07 *const data);
void freeWiseScriptUnknown0x09(WiseScriptUnknown0x09 *const data);
void freeWiseScriptUnknown0x0A(WiseScriptUnknown0x0A *const data);
void freeWiseScriptUnknown0x0B(WiseScriptUnknown0x0B *const data);
void freeWiseScriptUnknown0x0C(WiseScriptUnknown0x0C *const data);
void freeWiseScriptUnknown0x11(WiseScriptUnknown0x11 *const data);
void freeWiseScriptUnknown0x12(WiseScriptUnknown0x12 *const data);
void freeWiseScriptUnknown0x14(WiseScriptUnknown0x14 *const data);
void freeWiseScriptUnknown0x15(WiseScriptUnknown0x15 *const data);
void freeWiseScriptUnknown0x16(WiseScriptUnknown0x16 *const data);
void freeWiseScriptUnknown0x17(WiseScriptUnknown0x17 *const data);
void freeWiseScriptUnknown0x19(WiseScriptUnknown0x19 *const data);
void freeWiseScriptUnknown0x1A(WiseScriptUnknown0x1A *const data);
void freeWiseScriptUnknown0x1C(WiseScriptUnknown0x1C *const data);
void freeWiseScriptUnknown0x1D(WiseScriptUnknown0x1D *const data);
void freeWiseScriptUnknown0x1E(WiseScriptUnknown0x1E *const data);
void freeWiseScriptUnknown0x23(WiseScriptUnknown0x23 *const data);
void freeWiseScriptUnknown0x30(WiseScriptUnknown0x30 *const data);


// For debugging
#ifdef REWISE_DEBUG
void printWiseScriptHeader(const WiseScriptHeader *const header);
void printWiseScriptUnknownStrings(const SafeCStrings *const strings);
void printWiseScriptLanguages(const SafeCStrings *const languages);
void printWiseScriptTexts(const SafeCStrings *const texts);
void printWiseScriptFileHeader(const WiseScriptFileHeader *const data);
void printWiseScriptUnknown0x03(const WiseScriptUnknown0x03 *const data);
void printWiseScriptUnknown0x04(const WiseScriptUnknown0x04 *const data);
void printWiseScriptUnknown0x05(const WiseScriptUnknown0x05 *const data);
void printWiseScriptUnknown0x06(const WiseScriptUnknown0x06 *const data);
void printWiseScriptUnknown0x07(const WiseScriptUnknown0x07 *const data);
void printWiseScriptUnknown0x08(const WiseScriptUnknown0x08 *const data);
void printWiseScriptUnknown0x09(const WiseScriptUnknown0x09 *const data);
void printWiseScriptUnknown0x0A(const WiseScriptUnknown0x0A *const data);
void printWiseScriptUnknown0x0B(const WiseScriptUnknown0x0B *const data);
void printWiseScriptUnknown0x0C(const WiseScriptUnknown0x0C *const data);
void printWiseScriptUnknown0x0D(void);
void printWiseScriptUnknown0x11(const WiseScriptUnknown0x11 *const data);
void printWiseScriptUnknown0x12(const WiseScriptUnknown0x12 *const data);
void printWiseScriptUnknown0x14(const WiseScriptUnknown0x14 *const data);
void printWiseScriptUnknown0x15(const WiseScriptUnknown0x15 *const data);
void printWiseScriptUnknown0x16(const WiseScriptUnknown0x16 *const data);
void printWiseScriptUnknown0x17(const WiseScriptUnknown0x17 *const data);
void printWiseScriptUnknown0x18(const uint32_t skipCount);
void printWiseScriptUnknown0x19(const WiseScriptUnknown0x19 *const data);
void printWiseScriptUnknown0x1A(const WiseScriptUnknown0x1A *const data);
void printWiseScriptUnknown0x1C(const WiseScriptUnknown0x1C *const data);
void printWiseScriptUnknown0x1D(const WiseScriptUnknown0x1D *const data);
void printWiseScriptUnknown0x1E(const WiseScriptUnknown0x1E *const data);
void printWiseScriptUnknown0x23(const WiseScriptUnknown0x23 *const data);
void printWiseScriptUnknown0x24(void);
void printWiseScriptUnknown0x25(void);
void printWiseScriptUnknown0x30(const WiseScriptUnknown0x30 *const data);
#endif


typedef struct {
  void (*cb_header)(const WiseScriptHeader *const);
  void (*cb_unkownHeaderStrings)(const SafeCStrings *const);
  void (*cb_texts)(const SafeCStrings *const);
  void (*cb_langSelection)(const SafeCStrings *const);
  void (*cb_languages)(const SafeCStrings *const);
  void (*cb_languageChanged)(void);
  void (*cb_componentsChanged)(void);
  void (*cb_0x00)(const WiseScriptFileHeader *const);
  void (*cb_0x03)(const WiseScriptUnknown0x03 *const);
  void (*cb_0x04)(const WiseScriptUnknown0x04 *const);
  void (*cb_0x05)(const WiseScriptUnknown0x05 *const);
  void (*cb_0x06)(const WiseScriptUnknown0x06 *const);
  void (*cb_0x07)(const WiseScriptUnknown0x07 *const);
  void (*cb_0x08)(const WiseScriptUnknown0x08 *const);
  void (*cb_0x09)(const WiseScriptUnknown0x09 *const);
  void (*cb_0x0A)(const WiseScriptUnknown0x0A *const);
  void (*cb_0x0B)(const WiseScriptUnknown0x0B *const);
  void (*cb_0x0C)(const WiseScriptUnknown0x0C *const);
  void (*cb_0x0D)(void);
  void (*cb_0x0F)(void);                   // start form data?
  void (*cb_0x10)(void);                   // end form data?
  void (*cb_0x11)(const WiseScriptUnknown0x11 *const);
  void (*cb_0x12)(const WiseScriptUnknown0x12 *const);
  void (*cb_0x14)(const WiseScriptUnknown0x14 *const);
  void (*cb_0x15)(const WiseScriptUnknown0x15 *const);
  void (*cb_0x16)(const WiseScriptUnknown0x16 *const);
  void (*cb_0x17)(const WiseScriptUnknown0x17 *const);
  void (*cb_0x18)(const uint32_t skipCount);
  void (*cb_0x19)(const WiseScriptUnknown0x19 *const);
  void (*cb_0x1A)(const WiseScriptUnknown0x1A *const);
  void (*cb_0x1C)(const WiseScriptUnknown0x1C *const);
  void (*cb_0x1D)(const WiseScriptUnknown0x1D *const);
  void (*cb_0x1E)(const WiseScriptUnknown0x1E *const);
  void (*cb_0x23)(const WiseScriptUnknown0x23 *const);
  void (*cb_0x24)(void);
  void (*cb_0x25)(void);
  void (*cb_0x30)(const WiseScriptUnknown0x30 *const);
} WiseScriptCallbacks;


typedef struct {
  size_t totalInflatedSize;
  size_t inflatedSize0x00;
  size_t inflatedSize0x06;
  size_t inflatedSize0x14;
  size_t headerSize; // so we can skip the header + texts next time
  uint8_t languageCount; // From the overlay header
  long largestFileDeflateEnd;
  SafeCStrings languages;
  SafeCStrings components;
} WiseScriptParsedInfo;


const WiseScriptParseState *getWiseScriptState(void);
void initWiseScriptCallbacks(WiseScriptCallbacks *const callbacks);

void initWiseScriptParsedInfo(WiseScriptParsedInfo *const parsedInfo);
void freeWiseScriptParsedInfo(WiseScriptParsedInfo *const parsedInfo);

REWiseStatus parseWiseScriptHeader(FILE *const fp,
                                   WiseScriptCallbacks *callbacks);
REWiseStatus parseWiseScript(FILE *const fp,
                             WiseScriptParsedInfo *parsedInfo,
                             WiseScriptCallbacks *callbacks);
void stopWiseScriptParse(REWiseStatus status);


#ifdef REWISE_DEBUG
// Debug print the parsed WiseScript structures
REWiseStatus wiseScriptDebugPrint(FILE *const fp,
                                  WiseScriptParsedInfo *const parsedInfo);
#endif

char *wiseScriptParsePath(const char *path);

#endif
