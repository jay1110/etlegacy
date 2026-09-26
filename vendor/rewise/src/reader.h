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

#ifndef H_REWISE_READER
#define H_REWISE_READER

#include <stdio.h>
#include <string.h>
#include <unistd.h>       // access, F_OK
#include <stdint.h>       // uint32_t
#include <stdlib.h>       // malloc

// PATH_MAX
#ifdef __linux__
#include <linux/limits.h>
#else
#include <limits.h> // *BSD and MSYS2
#endif

#include "errors.h"


// Sanity: default max amount of bytes for reading strings
#define MAX_READ_STRING_LENGTH 10240


typedef struct {
  char *string;
  int strlen; // total number of bytes including \0
  int printlen; // number of chars when printed
} SafeCString;


typedef struct {
  SafeCString *strings;
  uint32_t count; // how many SafeCString are allocated
} SafeCStrings;


void initSafeCString(SafeCString *const string);
void freeSafeCString(SafeCString *const string);
void initSafeCStrings(SafeCStrings *const strings);
void freeSafeCStrings(SafeCStrings *const strings);

REWiseStatus readBytesInto(FILE *const fp, unsigned char *const dest,
                           const uint32_t size);
REWiseStatus readUInt32(FILE *const fp, unsigned int *const dest);
REWiseStatus readUInt16(FILE *const fp, uint16_t *const dest);

REWiseStatus readSafeCString(FILE *const fp, SafeCString *const dest,
                             const int max);
REWiseStatus readSafeCStrings(FILE *const fp, const uint32_t count,
                              SafeCStrings *const dest, const int max);


// Optional
REWiseStatus optReadBytesInto(FILE *const fp, unsigned char *const dest,
                              const uint32_t size);
REWiseStatus optReadUInt32(FILE *const fp, unsigned int *const dest);
REWiseStatus optReadCString(FILE *const fp, SafeCString *const dest,
                            const int max);
REWiseStatus optReadCStrings(FILE *const fp, const uint32_t count,
                             SafeCStrings *const dest, const int max);

#endif
