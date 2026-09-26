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

#ifndef H_REWISE_EXEFILE
#define H_REWISE_EXEFILE

#include <stdint.h>

// https://github.com/lumbytyci/PExplorer/blob/master/src/pefile.h
// https://chuongdong.com/reverse%20engineering/2020/08/15/PE-Parser/
// https://learn.microsoft.com/en-us/windows/win32/debug/pe-format
// https://wiki.osdev.org/MZ
// https://wiki.osdev.org/PE
// https://wiki.osdev.org/NE <-- FlagWord is wrong and should be u16?
// https://en.wikipedia.org/wiki/New_Executable
// http://justsolve.archiveteam.org/wiki/MS-DOS_EXE
// http://justsolve.archiveteam.org/wiki/New_Executable
// https://jeffpar.github.io/kbarchive/kb/065/Q65122/
// http://benoit.papillault.free.fr/c/disc2/exefmt.txt
// https://web.archive.org/web/20220521105653/www.csn.ul.ie/~caolan/pub/winresdump/winresdump/doc/winexe.txt#expand
// http://www.ctyme.com/intr/rb-2939.htm
// https://learn.microsoft.com/en-us/windows/win32/api/verrsrc/ns-verrsrc-vs_fixedfileinfo
// https://learn.microsoft.com/en-us/windows/win32/menurc/vs-versioninfo
// https://web.archive.org/web/20240607195608/https://devblogs.microsoft.com/oldnewthing/20061220-15/?p=28653

typedef struct {
  uint16_t signature; // Should be 'MZ'
  uint16_t extra;
  uint16_t pages;
  uint16_t relocationItems;
  uint16_t headerSize;
  uint16_t minimumAllocation;
  uint16_t maximumAllocation;
  uint16_t initialSs;
  uint16_t initialSp;
  uint16_t checksum;
  uint16_t initialIp;
  uint16_t initialCs;
  uint16_t relocationTable;
  uint16_t overlay;
  uint16_t overlayInformation;
} MsDosHeader;


typedef struct {
  uint32_t signature;
  uint16_t machine;
  uint16_t numberOfSections;
  uint32_t timeDateStamp;
  uint32_t pointerToSymbolTable;
  uint32_t numberOfSymbols;
  uint16_t optionalHeaderSize;
  uint16_t characteristics;
} PeFileHeader;


typedef struct {
  char name[8];
  uint32_t virtualSize;
  uint32_t virtualAddress;
  uint32_t rawDataSize;
  uint32_t rawDataLocation;
  uint32_t relocationsLocation;
  uint32_t lineNumbersLocation;
  uint16_t numberOfRelocations;
  uint16_t numberOfLineNumbers;
  uint32_t characteristics;
} PeImageSectionHeader;


typedef struct {
  /* 0x00 */ uint16_t signature; // Should be 'NE'
  /* 0x02 */ uint8_t majorLinkerVersion;
  /* 0x03 */ uint8_t minorLinkerVersion;
  /* 0x04 */ uint16_t entryTableOffset;
  /* 0x06 */ uint16_t entryTableSize;
  /* 0x08 */ uint32_t crc32; // 3.1 reverved ..
  /* 0x0C */ uint16_t flags;
  /* 0x0E */ uint16_t autoDataSegIndex;
  /* 0x10 */ uint16_t heapSize;
  /* 0x12 */ uint16_t stackSize;

  /* 0x14 */ uint32_t entryPoint;
  /* 0x18 */ uint32_t stack;

  /* 0x1C */ uint16_t segCount;
  /* 0x1E */ uint16_t moduleRefCount;
  /* 0x20 */ uint16_t nonResidentSize;

  /* 0x22 */ uint16_t segTableOffset;
  /* 0x24 */ uint16_t resourceTableOffset;
  /* 0x26 */ uint16_t residentNameTableOffset;
  /* 0x28 */ uint16_t moduleRefTableOffset;
  /* 0x2A */ uint16_t importNameTableOffset;
  /* 0x2C */ uint32_t nonResidentNameTableOffset;
  /* 0x30 */ uint16_t movableEntryCount; // in the entryTable
  /* 0x32 */ uint16_t fileAlignmentShiftCount;
  /* 0x34 */ uint16_t resourceSegmentCount;
  /* 0x36 */ uint8_t targetOs;
  /* 0x37 */ uint8_t otherFlags;
  /* 0x38 */ uint16_t gangloadOffset; // TODO or "offset to return thunks"
  /* 0x3A */ uint16_t gangloadSize; // TODO or "offset to segment reference thunks"
  /* 0x3C */ uint16_t minSwapSize;
  /* 0x3E */ uint16_t expectedWinVersion;
  /* 0x40 .. */
} NeFileHeader;


typedef struct {
  // shifted left with NeFileHeader.fileAlignmentShiftCount to get
  // to the in file offset from file start.
  uint16_t sectorOffset;
  uint16_t size;
  uint16_t flags;
  uint16_t minAlloc;
} NeSegmentEntry;


typedef struct {
  uint16_t shiftedOffset;
  uint16_t size;
  uint16_t flags;
  uint16_t id;
  uint16_t handle;
  uint16_t usage;
} NeResourceNameInfo;


typedef struct {
  uint16_t typeId;
  uint16_t count;
  uint32_t reserved;
  // read 'count' * ResourceNameInfo
} NeResourceTypeInfo;


#ifdef REWISE_DEBUG
typedef struct {
  uint8_t stringSize;
  char string[256];
  uint16_t entryTableIndex;
} NeResidentNameEntry;
#endif


typedef enum {
  EXE_ANALYZE_FLAG_MZ = 1, // MZ signature found
  EXE_ANALYZE_FLAG_NE = 2, // NE signature found
  EXE_ANALYZE_FLAG_PE = 4  // PE signature found
} ExeAnalyzeFlags;


typedef struct {
  ExeAnalyzeFlags signatures;
  long fileSize;
  long overlayOffset;
  long overlaySize;
} ExeAnalyzeResult;


void initExeAnalyzeResult(ExeAnalyzeResult *const result);
int analyzeExe(FILE *const fp, ExeAnalyzeResult *const result);

#endif
