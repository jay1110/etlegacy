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

#include <stdio.h>
#include <stdlib.h>

#include "wiseoverlay.h"
#include "reader.h"
#include "print.h"


REWiseStatus readWiseOverlayHeader(FILE *const fp,
                                   WiseOverlayHeader *const dest)
{
  REWiseStatus status;
  int ch;

  // Init structure
  //dest->dllName     = NULL;  // We skip this
  dest->initTextLen = 0;
  dest->initTexts   = NULL;

  // Read dllNameLen
  ch = fgetc(fp);
  if (ch == EOF) {
    printError("Failed to read dllNameLen (EOF).\n");
    return REWISE_ERROR_FILE_EOF;
  }
  dest->dllNameLen = (unsigned char)ch;
  printDebug("dllNameLen: %d\n", dest->dllNameLen);

  // Skip dllName (string) and dllSize (int)
  if (dest->dllNameLen > 0) {
    if (fseek(fp, (long)(dest->dllNameLen + 4), SEEK_CUR) != 0) {
      printError("Failed to skip dllName.\n");
      return REWISE_ERROR_FILE_SEEK;
    }
  }

  // Read flags (uint32)
  if ((status = readUInt32(fp, &dest->flags)) != REWISE_OK) {
    printError("Failed to read flags. %d\n", status);
    return status;
  }

  // Read 20 unknown bytes
  if ((status = optReadBytesInto(fp, dest->unknown_20, 20)) != REWISE_OK) {
    printError("Failed to read 20 unknown bytes\n");
    return status;
  }

  // Read inflated WiseScript.bin size
  if ((status = readUInt32(fp, &dest->inflatedSizeWiseScript)) != REWISE_OK) {
    printError("Failed to read inflatedSizeWiseScript. %d\n", status);
    return status;
  }

  // Read deflated WiseScript.bin size
  if ((status = readUInt32(fp, &dest->deflatedSizeWiseScript)) != REWISE_OK) {
    printError("Failed to read deflatedSizeWiseScript. %d\n", status);
    return status;
  }

  // Read deflated WISE0001.DLL
  if ((status = readUInt32(fp, &dest->deflatedSizeWiseDll)) != REWISE_OK) {
    printError("Failed to read deflatedSizeWiseDll. %d\n", status);
    return status;
  }

  // Read 12 unknown bytes
  if ((status = readUInt32(fp, &dest->unknownU32_1)) != REWISE_OK) {
    printError("Failed to read unknownU32_1. %d\n", status);
    return status;
  }
  if ((status = readUInt32(fp, &dest->unknownU32_2)) != REWISE_OK) {
    printError("Failed to read unknownU32_2. %d\n", status);
    return status;
  }
  if ((status = readUInt32(fp, &dest->unknownU32_3)) != REWISE_OK) {
    printError("Failed to read unknownU32_3. %d\n", status);
    return status;
  }

  // Read deflated PROGRESS.DLL size
  if ((status = readUInt32(fp, &dest->deflatedSizeProgressDll)) != REWISE_OK) {
    printError("Failed to read deflatedSizeProgressDll. %d\n", status);
    return status;
  }

  // Read deflated deflatedSizeSomeData6 size
  if ((status = readUInt32(fp, &dest->deflatedSizeSomeData6)) != REWISE_OK) {
    printError("Failed to read deflatedSizeProgressDll. %d\n", status);
    return status;
  }

  // Read deflated deflatedSizeSomeData7 size
  if ((status = readUInt32(fp, &dest->deflatedSizeSomeData7)) != REWISE_OK) {
    printError("Failed to read deflatedSizeProgressDll. %d\n", status);
    return status;
  }

  // Read 8 unknown bytes
  if ((status = optReadBytesInto(fp, dest->unknown_8, 8)) != REWISE_OK) {
    printError("Failed to read 16 unknown bytes\n");
    return status;
  }

  // Read 5th deflated size (probably some Wise script stuff)
  if ((status = readUInt32(fp, &dest->deflatedSizeSomeData5)) != REWISE_OK) {
    printError("Failed to read deflatedSizeSomeData5. %d\n", status);
    return status;
  }

  // Read 5th inflated size
  if ((status = readUInt32(fp, &dest->inflatedSizeSomeData5)) != REWISE_OK) {
    printError("Failed to read inflatedSizeSomeData5. %d\n", status);
    return status;
  }

  // Read eof (filesize, on multi-disc installers set to 0)
  if ((status = readUInt32(fp, &dest->eof)) != REWISE_OK) {
    printError("Failed to read eof. %d\n", status);
    return status;
  }

  // Read deflated .dib size (first file)
  if ((status = readUInt32(fp, &dest->deflatedDib)) != REWISE_OK) {
    printError("Failed to read deflatedDib. %d\n", status);
    return status;
  }

  // Read inflated .dib size
  if ((status = readUInt32(fp, &dest->inflatedDib)) != REWISE_OK) {
    printError("Failed to read inflatedDib. %d\n", status);
    return status;
  }

  // Read 2 unknown bytes
  if ((status = readBytesInto(fp, dest->unknown_2, 2)) != REWISE_OK) {
    printError("Failed to read 2 unknown bytes\n");
    return status;
  }

  // Read initTextLen
  ch = fgetc(fp);
  if (ch == EOF) {
    printError("Failed to read initTextLen (EOF).\n");
    return REWISE_ERROR_FILE_EOF;
  }
  dest->initTextLen = (unsigned char)ch;

  // Init texts
  if (dest->initTextLen) {
#ifdef REWISE_DEBUG
    printDebug("Read init texts, len: %d\n", dest->initTextLen);
    unsigned char * initTexts = (unsigned char*)malloc(sizeof(unsigned char) * dest->initTextLen);
    if (initTexts == NULL) {
      printError("Failed allocate memory for initTexts. Out of memory!\n");
      return REWISE_ERROR_SYSTEM_IO;
    }

    if ((status = readBytesInto(fp, initTexts, dest->initTextLen)) != REWISE_OK) {
      printError("Failed to read initTexts. %d\n", status);
      free(initTexts);
      return status;
    }
    dest->initTexts = initTexts;
#else
    // skip the texts on normal build
    if (fseek(fp, dest->initTextLen, SEEK_CUR) != 0) {
      return REWISE_ERROR_FILE_SEEK;
    }
#endif
  }

  printDebug("Offset after header read: %d (0x%X)\n", ftell(fp), ftell(fp));

  return REWISE_OK;
}


void freeWiseOverlayHeader(WiseOverlayHeader *const data)
{
  if (data->initTexts != NULL) {
    free(data->initTexts);
    data->initTexts = NULL;
  }
}


#ifdef REWISE_DEBUG

void printOverlayHeader(const WiseOverlayHeader *const header)
{
  fprintf(stderr, "OverlayHeader.dllNameLen             : %u\n", header->dllNameLen);
  fprintf(stderr, "OverlayHeader.flags                  : ");
  printBin((void*)&header->flags, sizeof(header->flags), "\n", stderr);
  fprintf(stderr, "OverlayHeader.unknown_20             : ");
  printHex(header->unknown_20, 20, "\n", stderr);
  fprintf(stderr, "OverlayHeader.inflatedSizeWiseScript : %u\n", header->inflatedSizeWiseScript);
  fprintf(stderr, "OverlayHeader.deflatedSizeWiseScript : %u\n", header->deflatedSizeWiseScript);
  fprintf(stderr, "OverlayHeader.deflatedSizeWiseDll    : %u\n", header->deflatedSizeWiseDll);
  fprintf(stderr, "OverlayHeader.unknownU32_1           : %u\n", header->unknownU32_1);
  fprintf(stderr, "OverlayHeader.unknownU32_2           : %u\n", header->unknownU32_2);
  fprintf(stderr, "OverlayHeader.unknownU32_3           : %u\n", header->unknownU32_3);
  fprintf(stderr, "OverlayHeader.deflatedSizeProgressDll: %u\n", header->deflatedSizeProgressDll);
  fprintf(stderr, "OverlayHeader.deflatedSizeSomeData6  : %u\n", header->deflatedSizeSomeData6);
  fprintf(stderr, "OverlayHeader.deflatedSizeSomeData7  : %u\n", header->deflatedSizeSomeData7);
  fprintf(stderr, "OverlayHeader.unknown_8              : ");
  printHex(header->unknown_8, 8, "\n", stderr);
  fprintf(stderr, "OverlayHeader.deflatedSizeSomeData5  : %u\n", header->deflatedSizeSomeData5);
  fprintf(stderr, "OverlayHeader.inflatedSizeSomeData5  : %u\n", header->inflatedSizeSomeData5);
  fprintf(stderr, "OverlayHeader.eof                    : %u\n", header->eof);
  fprintf(stderr, "OverlayHeader.deflatedDib            : %u\n", header->deflatedDib);
  fprintf(stderr, "OverlayHeader.inflatedDib            : %u\n", header->inflatedDib);
  fprintf(stderr, "OverlayHeader.unknown_2              : ");
  printHex(header->unknown_2, 2, "\n", stderr);
}
#endif
