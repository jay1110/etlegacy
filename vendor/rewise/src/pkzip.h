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

/* Minimal PKZIP header parsing */

#ifndef H_REWISE_ZIP
#define H_REWISE_ZIP

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "errors.h"

// Resources:
// https://en.wikipedia.org/wiki/ZIP_(file_format)
// https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT


typedef enum {
  PK_SIG_LOCAL_FILE      = 0x04034b50,
  PK_SIG_DATA_DESC       = 0x08074b50,
  PK_SIG_CENTRAL_DIR     = 0x02014b50,
  PK_SIG_END_CENTRAL_DIR = 0x06054b50
} PKSignature;


typedef struct {
  //uint32_t signature;
  uint16_t requiredVersion;
  uint16_t gpFlags;
  uint16_t compressionMethod;
  uint16_t fileModTime;
  uint16_t fileModDate;
  uint32_t CRC32;
  uint32_t deflatedSize;
  uint32_t inflatedSize;
  uint16_t fileNameSize;
  uint16_t extraFieldsSize;
  // commented-out 'cause we skip them since no use of this has been
  // observed for now..
  // char *filename;
  // char *extrafields
} PKLocalFileHeader;


typedef struct {
  //uint32_t signature;
  uint16_t diskNo;
  uint16_t centralDirDiskNo;
  uint16_t centralDirDiskEntryNo;
  uint16_t centralDirEntryNo;
  uint32_t centralDirSize;
  uint32_t centralDirDiskOffset;
  uint16_t commentLength;
  //char *comment; // don't read, don't care..
} PKCentralDirectoryEnd;


REWiseStatus readPKSignature(FILE *const fp, uint32_t *const signature);
REWiseStatus readPKLocalFileHeader(FILE *const fp,
                                   PKLocalFileHeader *const fileHeader);
REWiseStatus findCentralDirectorySize(FILE *const fp, long *const offset);

#endif
