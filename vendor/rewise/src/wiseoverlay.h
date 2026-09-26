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

#ifndef H_WISEOVERLAY
#define H_WISEOVERLAY

#include <stdbool.h>
#include <stdint.h>

#include "errors.h"

enum WiseOverlayHeaderFlags {
  WISE_FLAG_UNKNOWN_1  = 1,
  WISE_FLAG_UNKNOWN_2  = 2,
  WISE_FLAG_UNKNOWN_3  = 4,
  WISE_FLAG_UNKNOWN_4  = 8,
  WISE_FLAG_UNKNOWN_5  = 16, // seen in hluplink.exe, Swat 3 and glsetup.exe
  WISE_FLAG_UNKNOWN_6  = 32,
  WISE_FLAG_UNKNOWN_7  = 64,
  WISE_FLAG_UNKNOWN_8  = 128,
  WISE_FLAG_PK_ZIP     = 256,
  WISE_FLAG_UNKNOWN_10 = 512,
  WISE_FLAG_UNKNOWN_11 = 1024,
  WISE_FLAG_UNKNOWN_12 = 2048,
  WISE_FLAG_UNKNOWN_13 = 4096,
  WISE_FLAG_UNKNOWN_14 = 8192,
  WISE_FLAG_UNKNOWN_15 = 16384,
  WISE_FLAG_UNKNOWN_16 = 32768,
  WISE_FLAG_UNKNOWN_17 = 65536,
  WISE_FLAG_UNKNOWN_18 = 131072,  // only seen in Swat 3
  WISE_FLAG_UNKNOWN_19 = 262144,
  WISE_FLAG_UNKNOWN_20 = 524288, // only seen set in Wild Wheels
  WISE_FLAG_UNKNOWN_21 = 1048576,
  WISE_FLAG_UNKNOWN_22 = 2097152,
  WISE_FLAG_UNKNOWN_23 = 4194304, // only seen in glsetup.exe
  WISE_FLAG_UNKNOWN_24 = 8388608,
  WISE_FLAG_UNKNOWN_25 = 16777216,
  WISE_FLAG_UNKNOWN_26 = 33554432,
  WISE_FLAG_UNKNOWN_27 = 67108864,
  WISE_FLAG_UNKNOWN_28 = 134217728,
  WISE_FLAG_UNKNOWN_29 = 268435456,
  WISE_FLAG_UNKNOWN_30 = 536870912,
  WISE_FLAG_UNKNOWN_31 = 1073741824,
  WISE_FLAG_UNKNOWN_32 = 2147483648
};


typedef struct {
  unsigned char dllNameLen;
  //unsigned char * dllName; // We SKIP this (only when dllNameLen > 0)
  //uint32_t dllSize;        // We SKIP this (only when dllNameLen > 0)

  uint32_t flags;

  unsigned char unknown_20[20];
  uint32_t inflatedSizeWiseScript;
  uint32_t deflatedSizeWiseScript;  // second file
  uint32_t deflatedSizeWiseDll;     // third file (WISE0001.DLL)

  uint32_t unknownU32_1;
  uint32_t unknownU32_2;
  uint32_t unknownU32_3;

  uint32_t deflatedSizeProgressDll; // fourth file (PROGRESS.DLL)
  uint32_t deflatedSizeSomeData6;
  uint32_t deflatedSizeSomeData7;
  unsigned char unknown_8[8];
  uint32_t deflatedSizeSomeData5;   // fifth file (FILE000{n}.DAT)
  uint32_t inflatedSizeSomeData5;

  // On multi-disc installers this is set to 0x00000000, so it may
  // represent EOF instead of filesize? At least for now. Only compared
  // the two multi-disc installers listed in the README.md, need more
  // multi-disc installers to properly compare. On single file
  // installers this is this installer it's filesize.
  uint32_t eof;

  uint32_t deflatedDib;             // first file
  uint32_t inflatedDib;
  unsigned char unknown_2[2];       // always 0x08000 (LE) / 0x0008
                                    // (BE, as in file) ?

  unsigned char initTextLen;
  unsigned char * initTexts;
} WiseOverlayHeader;

REWiseStatus readWiseOverlayHeader(FILE *const fp,
                                   WiseOverlayHeader *const dest);
void freeWiseOverlayHeader(WiseOverlayHeader *const data);

#ifdef REWISE_DEBUG
void printOverlayHeader(const WiseOverlayHeader *const header);
#endif

#endif
