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

#include <stddef.h>
#include <string.h>
#include <errno.h>

#ifdef __linux__
#include <linux/limits.h>
#else
#include <limits.h> // *BSD and MSYS2
#endif


#include "wisescript.h"
#include "print.h"
#include "CP1252.h"


static int WiseScriptSTOP = 0;
static REWiseStatus WiseScriptStopStatus = REWISE_OK;
static WiseScriptParseState _WiseScriptState;


void initWiseScriptState(void)
{
  _WiseScriptState.hasLangSet = false;
  _WiseScriptState.hasCompSet = false;

  _WiseScriptState.currentLangUTF8Extra = 0;
  _WiseScriptState.currentCompUTF8Extra = 0;

  memset(_WiseScriptState.currentLang, 0, WISESCRIPT_LANG_MAX);
  memset(_WiseScriptState.currentComp, 0, WISESCRIPT_COMP_MAX);

  _WiseScriptState._branchCount = 0;
  memset(_WiseScriptState._branches, WISESCRIPT_BRANCH_CMP_NONE, WISESCRIPT_MAX_BRANCHES);
}


const WiseScriptParseState *getWiseScriptState(void)
{
  return &_WiseScriptState;
}




REWiseStatus readWiseScriptHeader(FILE *const fp,
                                  WiseScriptHeader *const header)
{
  REWiseStatus status = REWISE_OK;

  // init struct
  initSafeCString(&header->url);
  initSafeCString(&header->logPath);
  initSafeCString(&header->font);

  // Read 5 unknown bytes
  if ((status = optReadBytesInto(fp, header->unknown_5, 5)) != REWISE_OK) {
    printError("Failed to read 5 unknown bytes\n");
    return status;
  }

  // Read someoffset_1
  if ((status = readUInt32(fp, &header->someoffset_1)) != REWISE_OK) {
    printError("Failed to read someoffset_1\n");
    return status;
  }

  // Read someoffset_2
  if ((status = readUInt32(fp, &header->someoffset_2)) != REWISE_OK) {
    printError("Failed to read someoffset_2\n");
    return status;
  }

  // Read 4 unknown bytes
  if ((status = optReadBytesInto(fp, header->unknown_4, 4)) != REWISE_OK) {
    printError("Failed to read 3 unknown bytes\n");
    return status;
  }

  // Read datetime
  if ((status = readUInt32(fp, &header->datetime)) != REWISE_OK) {
    printError("Failed to read datetime\n");
    return status;
  }

  // Read 22 unknown bytes
  if ((status = optReadBytesInto(fp, header->unknown_22, 22)) != REWISE_OK) {
    printError("Failed to read 22 unknown bytes\n");
    return status;
  }

  if ((status = optReadCString(fp, &header->url, MAX_STRING_READ_FIXME)) != REWISE_OK) {
    printError("Failed to read WiseScriptHeader url.\n");
    return status;
  }

  if ((status = optReadCString(fp, &header->logPath, MAX_STRING_READ_FIXME)) != REWISE_OK) {
    printError("Failed to read WiseScriptHeader logpath.\n");
    return status;
  }

  status = optReadCString(fp, &header->font, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("Failed to read WiseScriptHeader font.\n");
    freeWiseScriptHeader(header);
    return status;
  }

  if ((status = optReadBytesInto(fp, header->unknown_6, 6)) != REWISE_OK) {
    freeWiseScriptHeader(header);
    return status;
  }

  if ((status = readBytesInto(fp, &header->languageCount, 1)) != REWISE_OK) {
    printError("Failed to read language count\n");
    return status;
  }

// For debugging unknown extra bytes (because the initial parsing will
// fail we do it extra here..)
#ifdef REWISE_DEBUG
  if ((getPrintFlags() & PRINT_DEBUG)) {
    fprintf(stdout, "WiseScriptHeader.unknown_22  : ");
    printHex(header->unknown_22, 22, "\n", stdout);
  }
#endif

  return status;
}


REWiseStatus readWiseScriptFileHeader(FILE *const fp,
                                      WiseScriptFileHeader *const data,
                                      const uint32_t langCount)
{
  REWiseStatus status = REWISE_OK;

  // init struct
  initSafeCString(&data->destFile);
  initSafeCString(&data->unknownString);
  initSafeCStrings(&data->fileTexts);

  status = optReadBytesInto(fp, data->unknown_2, 2);
  if (status != REWISE_OK) {
    return status;
  }

  status = readUInt32(fp, &data->deflateStart);
  if (status != REWISE_OK) {
    return status;
  }

  status = readUInt32(fp, &data->deflateEnd);
  if (status != REWISE_OK) {
    return status;
  }

  status = readUInt16(fp, &data->date);
  if (status != REWISE_OK) {
    return status;
  }

  status = readUInt16(fp, &data->time);
  if (status != REWISE_OK) {
    return status;
  }

  status = readUInt32(fp, &data->inflatedSize);
  if (status != REWISE_OK) {
    return status;
  }

  status = optReadBytesInto(fp, data->unknown_20, 20);
  if (status != REWISE_OK) {
    return status;
  }

  status = readUInt32(fp, &data->crc32);
  if (status != REWISE_OK) {
    return status;
  }

  status = readSafeCString(fp, &data->destFile, MAX_STRING_READ_DESTFILE);
  if (status != REWISE_OK) {
    printError("readWiseScriptFileHeader failed to read destFile.\n");
    return status;
  }

  // data->destFile is just 0x00
  if (data->destFile.string == NULL) {
    printError("readWiseScriptFileHeader destFile is a empty string @ 0x%08X\n", ftell(fp));
    return REWISE_ERROR_VALUE;
  }

  // parse filepath
  char *filePath = wiseScriptParsePath(data->destFile.string);
  if (filePath == NULL) {
    printError("readWiseScriptFileHeader invalid destFile at 0x%08X\n", ftell(fp));
    freeWiseScriptFileHeader(data);
    return REWISE_ERROR_VALUE;
  }
  freeSafeCString(&data->destFile);
  // FIXME this invalidates data->destFile.printlen and
  //      data->destFile.strlen, not that we need them for this,
  //      but yeah..
  data->destFile.string = filePath;

  status = optReadCStrings(fp, langCount, &data->fileTexts, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptFileHeader failed to read fileTexts.\n");
    freeWiseScriptFileHeader(data);
    return status;
  }

  status = optReadCString(fp, &data->unknownString, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptFileHeader failed to read unknownString.\n");
    return status;
  }

  return status;
}


REWiseStatus readWiseScriptUnknown0x03(FILE *const fp,
                                       WiseScriptUnknown0x03 *const data,
                                       const uint32_t langCount)
{
  REWiseStatus status = REWISE_OK;
  initSafeCStrings(&data->langStrings);

  status = optReadBytesInto(fp, &data->unknown_1, 1);
  if (status != REWISE_OK) {
    return status;
  }

  status = optReadCStrings(fp, langCount * 2, &data->langStrings, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x03 failed to read strings.\n");
    return status;
  }

  return status;
}


REWiseStatus readWiseScriptUnknown0x04(FILE *const fp,
                                       WiseScriptUnknown0x04 *const data,
                                       const uint32_t langCount)
{
  REWiseStatus status = REWISE_OK;
  initSafeCStrings(&data->dataStrings);

  status = optReadBytesInto(fp, &data->no, 1);
  if (status != REWISE_OK) {
    return status;
  }

  status = optReadCStrings(fp, langCount, &data->dataStrings, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x04 failed to read dataStrings.\n");
    return status;
  }

  return status;
}


REWiseStatus readWiseScriptUnknown0x05(FILE *const fp,
                                       WiseScriptUnknown0x05 *const data)
{
  REWiseStatus status = REWISE_OK;

  // init struct
  initSafeCString(&data->file);
  initSafeCString(&data->section);
  initSafeCString(&data->values);

  status = optReadCString(fp, &data->file, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x05 failed to read file.\n");
    return status;
  }

  status = optReadCString(fp, &data->section, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x05 failed to read section.\n");
    return status;
  }

  status = optReadCString(fp, &data->values, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x05 failed to read values.\n");
    return status;
  }

  return status;
}


REWiseStatus readWiseScriptUnknown0x06(FILE *const fp,
                                       WiseScriptUnknown0x06 *const data,
                                       const uint32_t langCount)
{
  REWiseStatus status = REWISE_OK;

  status = optReadBytesInto(fp, data->unknown_2, 2);
  if (status != REWISE_OK) {
    return status;
  }

  status = optReadUInt32(fp, &data->unknown);
    if (status != REWISE_OK) {
      return status;
    }

  // TODO FIXME make this optional
  data->deflateInfo.info = malloc(sizeof(WiseScriptDeflateInfo) * langCount);
  if (data->deflateInfo.info == NULL) {
    return REWISE_ERROR_SYSTEM_IO; // out of mem
  }
  data->deflateInfo.size = langCount;
  data->deflateInfo.count = 0;

  for (uint32_t i=0; i < langCount; ++i) {
    status = readUInt32(fp, &data->deflateInfo.info[i].deflateStart);
    if (status != REWISE_OK) {
      return status;
    }

    status = readUInt32(fp, &data->deflateInfo.info[i].deflateEnd);
    if (status != REWISE_OK) {
      return status;
    }

    status = readUInt32(fp, &data->deflateInfo.info[i].inflatedSize);
    if (status != REWISE_OK) {
      return status;
    }
    data->deflateInfo.count++;
  }

  status = optReadBytesInto(fp, &data->terminator, 1);
  if (status != REWISE_OK) {
    return status;
  }

  return status;
}


REWiseStatus readWiseScriptUnknown0x07(FILE *const fp,
                                       WiseScriptUnknown0x07 *const data)
{
  REWiseStatus status = REWISE_OK;

  // init struct
  initSafeCString(&data->unknownString_1);
  initSafeCString(&data->unknownString_2);
  initSafeCString(&data->unknownString_3);

  status = optReadBytesInto(fp, &data->unknown_1, 1);
  if (status != REWISE_OK) {
    return status;
  }

  status = optReadCString(fp, &data->unknownString_1, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x07 failed to read unknownString_1.\n");
    return status;
  }

  status = optReadCString(fp, &data->unknownString_2, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x07 failed to read unknownString_2.\n");
    return status;
  }

  status = optReadCString(fp, &data->unknownString_3, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x07 failed to read unknownString_3.\n");
    return status;
  }

  return status;
}


REWiseStatus readWiseScriptUnknown0x08(FILE *const fp,
                                       WiseScriptUnknown0x08 *const data)
{
  REWiseStatus status = REWISE_OK;
  status = readBytesInto(fp, &data->unknown_1, 1);
  if (status != REWISE_OK) {
    return status;
  }
  return status;
}


REWiseStatus readWiseScriptUnknown0x09(FILE *const fp,
                                       WiseScriptUnknown0x09 *const data,
                                       const uint32_t langCount)
{
  REWiseStatus status = REWISE_OK;

  // init struct
  initSafeCString(&data->unknownString_1);
  initSafeCString(&data->unknownString_2);
  initSafeCString(&data->unknownString_3);
  initSafeCString(&data->unknownString_4);
  initSafeCStrings(&data->unknownStrings);

  status = optReadBytesInto(fp, &data->unknown_1, 1);
  if (status != REWISE_OK) {
    return status;
  }

  status = optReadCString(fp, &data->unknownString_1, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x09 failed to read unknownString_1.\n");
    return status;
  }

  status = optReadCString(fp, &data->unknownString_2, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x09 failed to read unknownString_2.\n");
    freeWiseScriptUnknown0x09(data);
    return status;
  }

  status = optReadCString(fp, &data->unknownString_3, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x09 failed to read unknownString_3.\n");
    freeWiseScriptUnknown0x09(data);
    return status;
  }

  status = optReadCString(fp, &data->unknownString_4, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x09 failed to read unknownString_4.\n");
    freeWiseScriptUnknown0x09(data);
    return status;
  }

  status = optReadCStrings(fp, langCount, &data->unknownStrings, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x09 failed to read unknownStrings.\n");
    freeWiseScriptUnknown0x09(data);
    return status;
  }

  return status;
}


REWiseStatus readWiseScriptUnknown0x0A(FILE *const fp,
                                       WiseScriptUnknown0x0A *const data)
{
  REWiseStatus status = REWISE_OK;

  // init struct
  initSafeCString(&data->unknownString_1);
  initSafeCString(&data->unknownString_2);
  initSafeCString(&data->unknownString_3);

  status = readBytesInto(fp, data->unknown_2, 2);
  if (status != REWISE_OK) {
    return status;
  }

  status = optReadCString(fp, &data->unknownString_1, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x0A failed to read unknownString_1.\n");
    return status;
  }

  status = optReadCString(fp, &data->unknownString_2, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x0A failed to read unknownString_2.\n");
    freeWiseScriptUnknown0x0A(data);
    return status;
  }

  status = optReadCString(fp, &data->unknownString_3, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x0A failed to read unknownString_3.\n");
    freeWiseScriptUnknown0x0A(data);
    return status;
  }

  return status;
}


REWiseStatus readWiseScriptUnknown0x0B(FILE *const fp,
                                       WiseScriptUnknown0x0B *const data)
{
  REWiseStatus status = REWISE_OK;

  // init struct
  initSafeCString(&data->unknownString_1);

  status = optReadBytesInto(fp, &data->unknown_1, 1);
  if (status != REWISE_OK) {
    return status;
  }

  status = optReadCString(fp, &data->unknownString_1, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x0B failed to read unknownString_1.\n");
    return status;
  }

  return status;
}


REWiseStatus readWiseScriptUnknown0x0C(FILE *const fp,
                                       WiseScriptUnknown0x0C *const data)
{
  REWiseStatus status = REWISE_OK;

  // init struct
  initSafeCString(&data->varName);
  initSafeCString(&data->varValue);

  status = readBytesInto(fp, &data->unknown_1, 1);
  if (status != REWISE_OK) {
    return status;
  }

  status = readSafeCString(fp, &data->varName,
                           MAX_STRING_READ_VARNAME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x0C failed to read varName.\n");
    return status;
  }

  status = readSafeCString(fp, &data->varValue,
                           MAX_STRING_READ_VARVALUE);
  if (status != REWISE_OK) {
    freeWiseScriptUnknown0x0C(data);
    printError("readWiseScriptUnknown0x0C failed to read varValue.\n");
    return status;
  }

  return status;
}


REWiseStatus readWiseScriptUnknown0x11(FILE *const fp,
                                       WiseScriptUnknown0x11 *const data)
{
  REWiseStatus status = REWISE_OK;

  // init struct
  initSafeCString(&data->unknownString_1);

  status = optReadCString(fp, &data->unknownString_1, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x11 failed to read unknownString_1.\n");
    freeWiseScriptUnknown0x11(data);
    return status;
  }
  return REWISE_OK;
}


REWiseStatus readWiseScriptUnknown0x12(FILE *const fp,
                                       WiseScriptUnknown0x12 *const data,
                                       uint32_t langCount)
{
  REWiseStatus status = REWISE_OK;

  // init struct
  initSafeCString(&data->sourceFile);
  initSafeCString(&data->unknownString_1);
  initSafeCString(&data->destFile);
  initSafeCStrings(&data->unknownStrings);

  status = optReadBytesInto(fp, &data->unknown_1, 1);
  if (status != REWISE_OK) {
    return status;
  }

  status = optReadBytesInto(fp, data->unknown_41, 41);
  if (status != REWISE_OK) {
    return status;
  }

  status = optReadCString(fp, &data->sourceFile, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x12 failed to read sourceFile.\n");
    return status;
  }

  status = optReadCString(fp, &data->unknownString_1, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    freeWiseScriptUnknown0x12(data);
    printError("readWiseScriptUnknown0x12 failed to read unknownString_1.\n");
    return status;
  }

  status = optReadCStrings(fp, langCount, &data->unknownStrings, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    freeWiseScriptUnknown0x12(data);
    printError("readWiseScriptUnknown0x12 failed to read unknownStrings.\n");
    return status;
  }

  status = optReadCString(fp, &data->destFile, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    freeWiseScriptUnknown0x12(data);
    printError("readWiseScriptUnknown0x12 failed to read destFile.\n");
    return status;
  }

  return status;
}


REWiseStatus readWiseScriptUnknown0x14(FILE *const fp,
                                       WiseScriptUnknown0x14 *const data)
{
  REWiseStatus status = REWISE_OK;

  // init struct
  initSafeCString(&data->name);
  initSafeCString(&data->message);

  status = optReadUInt32(fp, &data->deflateStart);
  if (status != REWISE_OK) {
    return status;
  }

  status = readUInt32(fp, &data->deflateEnd);
  if (status != REWISE_OK) {
    return status;
  }

  status = readUInt32(fp, &data->inflatedSize);
  if (status != REWISE_OK) {
    return status;
  }

  status = optReadCString(fp, &data->name, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x14 failed to read name.\n");
    return status;
  }

  status = optReadCString(fp, &data->message, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    freeWiseScriptUnknown0x14(data);
    printError("readWiseScriptUnknown0x14 failed to read message.\n");
    return status;
  }

  return status;
}


REWiseStatus readWiseScriptUnknown0x15(FILE *const fp,
                                       WiseScriptUnknown0x15 *const data)
{
  REWiseStatus status = REWISE_OK;

  // init struct
  initSafeCString(&data->unknownString_1);
  initSafeCString(&data->unknownString_2);

  status = optReadBytesInto(fp, &data->unknown_1, 1);
  if (status != REWISE_OK) {
    return status;
  }

  status = optReadCString(fp, &data->unknownString_1, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x15 failed to read unknownString_1.\n");
    return status;
  }

  status = optReadCString(fp, &data->unknownString_2, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    freeWiseScriptUnknown0x15(data);
    printError("readWiseScriptUnknown0x15 failed to read unknownString_2.\n");
    return status;
  }

  return status;
}


REWiseStatus readWiseScriptUnknown0x16(FILE *const fp,
                                       WiseScriptUnknown0x16 *const data)
{
  REWiseStatus status = REWISE_OK;

  // init struct
  initSafeCString(&data->name);

  status = optReadCString(fp, &data->name, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x16 failed to read name.\n");
    return status;
  }
  return status;
}


REWiseStatus readWiseScriptUnknown0x17(FILE *const fp,
                                       WiseScriptUnknown0x17 *const data)
{
  REWiseStatus status = REWISE_OK;

  // init struct
  initSafeCString(&data->unknownString_1);

  status = optReadBytesInto(fp, &data->unknown_1, 1);
  if (status != REWISE_OK) {
    return status;
  }

  status = optReadBytesInto(fp, data->unknown_4, 4);
  if (status != REWISE_OK) {
    return status;
  }

  status = optReadCString(fp, &data->unknownString_1, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x17 failed to read unknownString_1.\n");
    return status;
  }

  return status;
}


REWiseStatus readWiseScriptUnknown0x19(FILE *const fp,
                                       WiseScriptUnknown0x19 *const data)
{
  REWiseStatus status = REWISE_OK;

  // init struct
  initSafeCString(&data->unknownString_1);
  initSafeCString(&data->unknownString_2);
  initSafeCString(&data->unknownString_3);

  status = optReadBytesInto(fp, &data->unknown_1, 1);
  if (status != REWISE_OK) {
    return status;
  }

  status = optReadCString(fp, &data->unknownString_1, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x19 failed to read unknownString_1\n");
    return status;
  }

  status = optReadCString(fp, &data->unknownString_2, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x19 failed to read unknownString_2\n");
    return status;
  }

  status = optReadCString(fp, &data->unknownString_3, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x19 failed to read unknownString_3\n");
    return status;
  }

  return status;
}


REWiseStatus readWiseScriptUnknown0x1A(FILE *const fp,
                                       WiseScriptUnknown0x1A *const data)
{
  REWiseStatus status = REWISE_OK;

  // init struct
  initSafeCString(&data->unknownString_1);
  initSafeCString(&data->unknownString_2);

  status = optReadBytesInto(fp, &data->unknown_1, 1);
  if (status != REWISE_OK) {
    return status;
  }

  status = optReadCString(fp, &data->unknownString_1, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x19 failed to read unknownString_1.\n");
    return status;
  }

  status = optReadCString(fp, &data->unknownString_2, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x19 failed to read unknownString_2.\n");
    return status;
  }

  return status;
}


REWiseStatus readWiseScriptUnknown0x1C(FILE *const fp,
                                       WiseScriptUnknown0x1C *const data)
{
  REWiseStatus status = REWISE_OK;

  // init struct
  initSafeCString(&data->unknownString_1);

  status = optReadCString(fp, &data->unknownString_1, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x1C failed to read unknownString_1.\n");
    return status;
  }

  return status;
}


REWiseStatus readWiseScriptUnknown0x1D(FILE *const fp,
                                       WiseScriptUnknown0x1D *const data)
{
  REWiseStatus status = REWISE_OK;

  // init struct
  initSafeCString(&data->unknownString_1);
  initSafeCString(&data->unknownString_2);

  status = optReadCString(fp, &data->unknownString_1, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x1D failed to read unknownString_1.\n");
    return status;
  }

  status = optReadCString(fp, &data->unknownString_2, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x1D failed to read unknownString_2.\n");
    return status;
  }

  return status;
}


REWiseStatus readWiseScriptUnknown0x1E(FILE *const fp,
                                       WiseScriptUnknown0x1E *const data)
{
  REWiseStatus status = REWISE_OK;

  initSafeCString(&data->unknownString);

  status = optReadBytesInto(fp, &data->unknown, 1);
  if (status != REWISE_OK) {
    return status;
  }

  status = optReadCString(fp, &data->unknownString, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x1E failed to read unknownString.\n");
    return status;
  }

  return status;
}


REWiseStatus readWiseScriptUnknown0x23(FILE *const fp,
                                       WiseScriptUnknown0x23 *const data)
{
  REWiseStatus status = REWISE_OK;

  // init struct
  initSafeCString(&data->varName);
  initSafeCString(&data->varValue);

  status = readBytesInto(fp, &data->unknown_1, 1);
  if (status != REWISE_OK) {
    return status;
  }

  status = readSafeCString(fp, &data->varName,
                           MAX_STRING_READ_VARNAME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x23 failed to read varName.\n");
    return status;
  }

  status = readSafeCString(fp, &data->varValue,
                           MAX_STRING_READ_VARVALUE);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x23 failed to read varValue.\n");
    freeWiseScriptUnknown0x23(data);
    return status;
  }

  return status;
}


REWiseStatus readWiseScriptUnknown0x30(FILE *const fp,
                                       WiseScriptUnknown0x30 *const data)
{
  REWiseStatus status = REWISE_OK;

  // init struct
  initSafeCString(&data->unknownString_1);
  initSafeCString(&data->unknownString_2);

  status = optReadBytesInto(fp, &data->unknown_1, 1);
  if (status != REWISE_OK) {
    return status;
  }

  status = optReadCString(fp, &data->unknownString_1, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x30 failed to read unknownString_1.\n");
    return status;
  }

  status = optReadCString(fp, &data->unknownString_2, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("readWiseScriptUnknown0x30 failed to read unknownString_2.\n");
    freeWiseScriptUnknown0x30(data);
    return status;
  }

  return status;
}


void freeWiseScriptHeader(WiseScriptHeader *const header)
{
  freeSafeCString(&header->url);
  freeSafeCString(&header->logPath);
  freeSafeCString(&header->font);
}


void freeWiseScriptFileHeader(WiseScriptFileHeader *const data)
{
  freeSafeCString(&data->destFile);
  freeSafeCString(&data->unknownString);
  freeSafeCStrings(&data->fileTexts);
}


void freeWiseScriptUnknown0x03(WiseScriptUnknown0x03 *const data)
{
  freeSafeCStrings(&data->langStrings);
}


void freeWiseScriptUnknown0x04(WiseScriptUnknown0x04 *const data)
{
  freeSafeCStrings(&data->dataStrings);
}


void freeWiseScriptUnknown0x05(WiseScriptUnknown0x05 *const data)
{
  freeSafeCString(&data->file);
  freeSafeCString(&data->section);
  freeSafeCString(&data->values);
}


void freeWiseScriptDeflateInfoContainer(
                        WiseScriptDeflateInfoContainer *const container)
{
  if (!container->size) {
    return;
  }
  free(container->info);
  container->info = NULL;
  container->size = 0;
  container->count = 0;
}


void freeWiseScriptUnknown0x06(WiseScriptUnknown0x06 *const data)
{
  freeWiseScriptDeflateInfoContainer(&data->deflateInfo);
}


void freeWiseScriptUnknown0x07(WiseScriptUnknown0x07 *const data)
{
  freeSafeCString(&data->unknownString_1);
  freeSafeCString(&data->unknownString_2);
  freeSafeCString(&data->unknownString_3);
}


void freeWiseScriptUnknown0x09(WiseScriptUnknown0x09 *const data)
{
  freeSafeCString(&data->unknownString_1);
  freeSafeCString(&data->unknownString_2);
  freeSafeCString(&data->unknownString_3);
  freeSafeCString(&data->unknownString_4);
  freeSafeCStrings(&data->unknownStrings);
}


void freeWiseScriptUnknown0x0A(WiseScriptUnknown0x0A *const data)
{
  freeSafeCString(&data->unknownString_1);
  freeSafeCString(&data->unknownString_2);
  freeSafeCString(&data->unknownString_3);
}


void freeWiseScriptUnknown0x0B(WiseScriptUnknown0x0B *const data)
{
  freeSafeCString(&data->unknownString_1);
}


void freeWiseScriptUnknown0x0C(WiseScriptUnknown0x0C *const data)
{
  freeSafeCString(&data->varName);
  freeSafeCString(&data->varValue);
}


void freeWiseScriptUnknown0x11(WiseScriptUnknown0x11 *const data)
{
  freeSafeCString(&data->unknownString_1);
}


void freeWiseScriptUnknown0x12(WiseScriptUnknown0x12 *const data)
{
  freeSafeCString(&data->sourceFile);
  freeSafeCString(&data->unknownString_1);
  freeSafeCString(&data->destFile);
  freeSafeCStrings(&data->unknownStrings);
}


void freeWiseScriptUnknown0x14(WiseScriptUnknown0x14 *const data)
{
  freeSafeCString(&data->name);
  freeSafeCString(&data->message);
}


void freeWiseScriptUnknown0x15(WiseScriptUnknown0x15 *const data)
{
  freeSafeCString(&data->unknownString_1);
  freeSafeCString(&data->unknownString_2);
}


void freeWiseScriptUnknown0x16(WiseScriptUnknown0x16 *const data)
{
  freeSafeCString(&data->name);
}


void freeWiseScriptUnknown0x17(WiseScriptUnknown0x17 *const data)
{
  freeSafeCString(&data->unknownString_1);
}


void freeWiseScriptUnknown0x19(WiseScriptUnknown0x19 *const data)
{
  freeSafeCString(&data->unknownString_1);
  freeSafeCString(&data->unknownString_2);
  freeSafeCString(&data->unknownString_3);
}


void freeWiseScriptUnknown0x1A(WiseScriptUnknown0x1A *const data)
{
  freeSafeCString(&data->unknownString_1);
  freeSafeCString(&data->unknownString_2);
}


void freeWiseScriptUnknown0x1C(WiseScriptUnknown0x1C *const data)
{
  freeSafeCString(&data->unknownString_1);
}


void freeWiseScriptUnknown0x1D(WiseScriptUnknown0x1D *const data)
{
  freeSafeCString(&data->unknownString_1);
  freeSafeCString(&data->unknownString_2);
}


void freeWiseScriptUnknown0x1E(WiseScriptUnknown0x1E *const data)
{
  freeSafeCString(&data->unknownString);
}


void freeWiseScriptUnknown0x23(WiseScriptUnknown0x23 *const data)
{
  freeSafeCString(&data->varName);
  freeSafeCString(&data->varValue);
}


void freeWiseScriptUnknown0x30(WiseScriptUnknown0x30 *const data)
{
  freeSafeCString(&data->unknownString_1);
  freeSafeCString(&data->unknownString_2);
}


// https://www.doubleblak.com/m/blogPosts.php?id=7
// MS-Dos FileTime
// DATE
// ----
// bit  0 -  6   Year
// bit  7 - 10   Month
// bit 11 - 15   Day
//
// TIME
// ----
// bit  0 -  4   Hour
// bit  5 - 10   Minutes
// bit 11 - 15   * 2 Seconds
void printDatetime(const uint16_t date, const uint16_t time)
{
  printf("%04d-%02d-%02d %02d:%02d:%02d",
         (date >> 9) + 1980,
         (date >> 5) & 0b0000000000001111,
         date & 0b0000000000011111,
         (time >> 11),
         (time >> 5) & 0b0000000000111111,
         (time & 0b0000000000011111) * 2);
}


// Debug prints //

#ifdef REWISE_DEBUG

void printWiseScriptHeader(const WiseScriptHeader *const header)
{
  printf("WiseScript Header\n-----------------\n");

  printf("WiseScriptHeader.unknown_5   : ");
  printHex(header->unknown_5, 5, "\n", stdout);

  printf("WiseScriptHeader.someoffset_1: 0x%08X %u\n",
         header->someoffset_1, header->someoffset_1);
  printf("WiseScriptHeader.someoffset_2: 0x%08X %u\n",
         header->someoffset_2, header->someoffset_2);

  printf("WiseScriptHeader.unknown_4   : ");
  printHex(header->unknown_4, 4, "\n", stdout);

  printf("WiseScriptHeader.datetime    : %u\n", header->datetime);

  printf("WiseScriptHeader.unknown_22  : ");
  printHex(header->unknown_22, 22, "\n", stdout);

  printf("WiseScriptHeader.url    : '%s'\n", header->url.string);
  printf("WiseScriptHeader.font   : '%s'\n", header->font.string);
  printf("WiseScriptHeader.logPath: '%s'\n", header->logPath.string);
  printf("-----------------\n");
}

void printWiseScriptUnknownStrings(const SafeCStrings *const strings)
{
  printf("WiseScript 7 Unknown Strings\n");
  printf("----------------------------\n");
  for (int i = 0; i < strings->count; i++) {
    printf("Unknown: \"%s\"\n", strings->strings[i].string);
  }
  printf("----------------------------\n");
}

void printWiseScriptLangSelection(const SafeCStrings *const langSelection)
{
  for (int i = 0; i < langSelection->count; i++) {
    printf("LangSelText: \"%s\"\n", langSelection->strings[i].string);
  }
}

void printWiseScriptLanguages(const SafeCStrings *const languages)
{
  printf("WiseScript Languages\n");
  printf("--------------------\n");
  for (int i = 0; i < languages->count; i++) {
    printf("LangText: \"%s\"\n", languages->strings[i].string);
  }
  printf("--------------------\n");
}

void printWiseScriptTexts(const SafeCStrings *const texts)
{
  printf("WiseScript Texts\n");
  printf("----------------\n");
  for (int i = 0; i < texts->count; i++) {
    printf("Text: \"%s\"\n", texts->strings[i].string);
  }
  printf("----------------\n");
}

void printWiseScriptFileHeader(const WiseScriptFileHeader *const data)
{
  printf("0x00 ");
  printHex(data->unknown_2, 2, NULL, stdout);
  printf(" 0x%08X 0x%08X ", data->deflateStart, data->deflateEnd);
  printDatetime(data->date, data->time);
  printf(" %11u ", data->inflatedSize);
  printHex(data->unknown_20, 20, NULL, stdout);

  printf(" %08X '%s' ", data->crc32, data->destFile.string);
  for (int i = 0; i < data->fileTexts.count; i++) {
    printf("\"%s\" ", data->fileTexts.strings[i].string);
  }
  printf("\"%s\"\n", data->unknownString.string);
}

void printWiseScriptUnknown0x03(const WiseScriptUnknown0x03 *const data)
{
  printf("0x03 0x%02X", data->unknown_1);
  for (int i = 0; i < data->langStrings.count; i++) {
    printf(" \"%s\"", data->langStrings.strings[i].string);
  }
  printf("\n");
}

void printWiseScriptUnknown0x04(const WiseScriptUnknown0x04 *const data)
{
  printf("0x04 0x%02X", data->no);
  for (int i = 0; i < data->dataStrings.count; i++) {
    printf(" \"%s\"", data->dataStrings.strings[i].string);
  }
  printf("\n");
}

void printWiseScriptUnknown0x05(const WiseScriptUnknown0x05 *const data)
{
  printf("0x05 '%s' '%s' '%s'\n", data->file.string,
         data->section.string, data->values.string);
}

void printWiseScriptUnknown0x06(const WiseScriptUnknown0x06 *const data)
{
  printf("0x06 0x");
  printHex(data->unknown_2, 2, "\n", stdout);
  for (uint32_t i=0; i < data->deflateInfo.count; ++i) {
    printf(" - 0x%08X 0x%08X %11u\n",
           data->deflateInfo.info[i].deflateStart,
           data->deflateInfo.info[i].deflateEnd,
           data->deflateInfo.info[i].inflatedSize);
  }
}

void printWiseScriptUnknown0x07(const WiseScriptUnknown0x07 *const data)
{
  printf("0x07 %02X '%s' '%s' '%s'\n", data->unknown_1,
         data->unknownString_1.string,
         data->unknownString_2.string,
         data->unknownString_3.string);
}

void printWiseScriptUnknown0x08(const WiseScriptUnknown0x08 *const data)
{
  printf("0x08 %02X\n", data->unknown_1);
}

void printWiseScriptUnknown0x09(const WiseScriptUnknown0x09 *const data)
{
  printf("0x09 %02X ",  data->unknown_1);
  printf("'%s' '%s' '%s' '%s'\n", data->unknownString_1.string,
                           data->unknownString_2.string,
                           data->unknownString_3.string,
                           data->unknownString_4.string);

  for (int i = 0; i < data->unknownStrings.count; i++) {
    printf("- \"%s\"\n", data->unknownStrings.strings[i].string);
  }
}

void printWiseScriptUnknown0x0A(const WiseScriptUnknown0x0A *const data)
{
  printf("0x0A ");
  printHex(data->unknown_2, 2, NULL, stdout);
  printf(" '%s' '%s' '%s'\n", data->unknownString_1.string,
         data->unknownString_2.string,
         data->unknownString_3.string);
}

void printWiseScriptUnknown0x0B(const WiseScriptUnknown0x0B *const data)
{
  printf("0x0B %02X \"%s\"\n", data->unknown_1,
         data->unknownString_1.string);
}

void printWiseScriptUnknown0x0C(const WiseScriptUnknown0x0C *const data)
{
  printf("0x0C %02X '%s' '%s'\n", data->unknown_1,
         data->varName.string,
         data->varValue.string);
}

void printWiseScriptUnknown0x0D(void)
{
  printf("0x0D \n");
}

void printWiseScriptUnknown0x11(const WiseScriptUnknown0x11 *const data)
{
  printf("0x11 '%s'\n", data->unknownString_1.string);
}

void printWiseScriptUnknown0x12(const WiseScriptUnknown0x12 *const data)
{
  printf("0x12 %02X ", data->unknown_1);
  printHex(data->unknown_41, 41, NULL, stdout);
  printf(" '%s' '%s' '%s'", data->sourceFile.string,
         data->unknownString_1.string,
         data->destFile.string);
  for (int i = 0; i < data->unknownStrings.count; i++) {
    printf(" \"%s\"", data->unknownStrings.strings[i].string);
  }
  printf("\n");
}

void printWiseScriptUnknown0x14(const WiseScriptUnknown0x14 *const data)
{
  printf("0x14 0x%08X 0x%08X %11u '%s' '%s'\n", data->deflateStart,
         data->deflateEnd, data->inflatedSize, data->name.string,
         data->message.string);
}

void printWiseScriptUnknown0x15(const WiseScriptUnknown0x15 *const data)
{
  printf("0x15 %02X '%s' '%s'\n", data->unknown_1,
         data->unknownString_1.string,
         data->unknownString_2.string);
}

void printWiseScriptUnknown0x16(const WiseScriptUnknown0x16 *const data)
{
  printf("0x16 '%s'\n", data->name.string);
}

void printWiseScriptUnknown0x17(const WiseScriptUnknown0x17 *const data)
{
  printf("0x17 %02X ", data->unknown_1);
  printHex(data->unknown_4, 4, NULL, stdout);
  printf(" '%s'\n", data->unknownString_1.string);
}

void printWiseScriptUnknown0x18(const uint32_t skipCount) {
  printf("0x18 - Skipped %u * 0x00\n", skipCount);
}

void printWiseScriptUnknown0x19(const WiseScriptUnknown0x19 *const data)
{
  printf("0x19 %02X '%s' '%s' '%s'\n", data->unknown_1,
         data->unknownString_1.string,
         data->unknownString_2.string,
         data->unknownString_3.string);
}

void printWiseScriptUnknown0x1A(const WiseScriptUnknown0x1A *const data)
{
  printf("0x1A %02X '%s' '%s'\n", data->unknown_1,
         data->unknownString_1.string,
         data->unknownString_2.string);
}

void printWiseScriptUnknown0x1C(const WiseScriptUnknown0x1C *const data)
{
  printf("0x1C '%s'\n", data->unknownString_1.string);
}

void printWiseScriptUnknown0x1D(const WiseScriptUnknown0x1D *const data) {
  printf("0x1D '%s' '%s'\n",
         data->unknownString_1.string, data->unknownString_2.string);
}

void printWiseScriptUnknown0x1E(const WiseScriptUnknown0x1E *const data)
{
  printf("0x1E 0x%02X '%s'\n", data->unknown,
         data->unknownString.string);
}

void printWiseScriptUnknown0x23(const WiseScriptUnknown0x23 *const data)
{
  printf("0x23 %02X '%s' '%s'\n", data->unknown_1, data->varName.string,
         data->varValue.string);
}

void printWiseScriptUnknown0x24(void)
{
  printf("0x24 \n");
}

void printWiseScriptUnknown0x25(void)
{
  printf("0x25 \n");
}

void printWiseScriptUnknown0x30(const WiseScriptUnknown0x30 *const data)
{
  printf("0x30 %02X '%s' '%s'\n", data->unknown_1,
         data->unknownString_1.string,
         data->unknownString_2.string);
}
#endif // REWISE_DEBUG




void initWiseScriptCallbacks(WiseScriptCallbacks *const callbacks)
{
  callbacks->cb_header = NULL;
  callbacks->cb_unkownHeaderStrings = NULL;
  callbacks->cb_texts  = NULL;
  callbacks->cb_langSelection = NULL;
  callbacks->cb_languages = NULL;
  callbacks->cb_languageChanged = NULL;
  callbacks->cb_componentsChanged = NULL;
  callbacks->cb_0x00 = NULL;
  callbacks->cb_0x03 = NULL;
  callbacks->cb_0x04 = NULL;
  callbacks->cb_0x05 = NULL;
  callbacks->cb_0x06 = NULL;
  callbacks->cb_0x07 = NULL;
  callbacks->cb_0x08 = NULL;
  callbacks->cb_0x09 = NULL;
  callbacks->cb_0x0A = NULL;
  callbacks->cb_0x0B = NULL;
  callbacks->cb_0x0C = NULL;
  callbacks->cb_0x0D = NULL;
  callbacks->cb_0x0F = NULL;
  callbacks->cb_0x10 = NULL;
  callbacks->cb_0x11 = NULL;
  callbacks->cb_0x12 = NULL;
  callbacks->cb_0x14 = NULL;
  callbacks->cb_0x15 = NULL;
  callbacks->cb_0x16 = NULL;
  callbacks->cb_0x17 = NULL;
  callbacks->cb_0x18 = NULL;
  callbacks->cb_0x19 = NULL;
  callbacks->cb_0x1A = NULL;
  callbacks->cb_0x1C = NULL;
  callbacks->cb_0x1D = NULL;
  callbacks->cb_0x1E = NULL;
  callbacks->cb_0x23 = NULL;
  callbacks->cb_0x24 = NULL;
  callbacks->cb_0x25 = NULL;
  callbacks->cb_0x30 = NULL;
}


bool addNewBranch(const uint8_t branchType)
{
  if (_WiseScriptState._branchCount == WISESCRIPT_MAX_BRANCHES) {
    printError("Either the WiseScript.bin is corrupt or the REWise "
               "dev should consider increasing "
               "WISESCRIPT_MAX_BRANCHES.\n");
    return false;
  }
  _WiseScriptState._branches[_WiseScriptState._branchCount] = branchType;
  _WiseScriptState._branchCount++;
  return true;
}


REWiseStatus parseWiseScriptHeader(FILE *const fp,
                                   WiseScriptCallbacks *const callbacks)
{
  REWiseStatus status = REWISE_OK;
  uint32_t langCount, langStringCount, otherStringCount;

  // Read the header
  WiseScriptHeader header;
  status = readWiseScriptHeader(fp,  &header);
  if (status != REWISE_OK) {
    printError("parseWiseScriptHeader failed to read header.\n");
    return status;
  }

  // callback
  if (callbacks->cb_header != NULL) {
    (*callbacks->cb_header)(&header);
  }

  // Read 7 unknown strings
  {
    SafeCStrings unknownStrings;
    initSafeCStrings(&unknownStrings);
    status = optReadCStrings(fp, 7, &unknownStrings, MAX_STRING_READ_FIXME);
    if (status != REWISE_OK) {
      printError("parseWiseScriptHeader failed to read 7 unknownStrings.\n");
      freeWiseScriptHeader(&header);
      return status;
    }

    if (callbacks->cb_unkownHeaderStrings != NULL) {
      (*callbacks->cb_unkownHeaderStrings)(&unknownStrings);
    }
    freeSafeCStrings(&unknownStrings);
  }

  langCount = header.languageCount;

  if (langCount > 1) {
    // Read the selection texts present when multiple languages are
    // defined. They may contain non printable characters..
    SafeCStrings selectLanguageTexts;
    initSafeCStrings(&selectLanguageTexts);
    status = optReadCStrings(fp, 2, &selectLanguageTexts,
                          MAX_STRING_READ_LANGUAGE_SELECTION);
    if (status != REWISE_OK) {
      printError("parseWiseScriptHeader failed to read "
                "selectLanguageTexts.\n");
      freeWiseScriptHeader(&header);
      return status;
    }

    if (callbacks->cb_langSelection != NULL) {
      (*callbacks->cb_langSelection)(&selectLanguageTexts);
    }

    freeSafeCStrings(&selectLanguageTexts);

    langStringCount = (langCount * 2);
    otherStringCount = 55 * langCount;
  }
  else {
    langStringCount = 1;
    otherStringCount = 55;
  }
  printDebug("LanguageCount: %u\n", langCount);
  printDebug("langStringCount: %u\n", langStringCount);
  printDebug("otherStringCount: %u\n", otherStringCount);

  // Read language texts
  SafeCStrings languageTexts;
  initSafeCStrings(&languageTexts);
  status = readSafeCStrings(fp, langStringCount, &languageTexts,
                            MAX_STRING_READ_LANGUAGE);
  if (status != REWISE_OK) {
    printError("parseWiseScriptHeader failed to read languageTexts.\n");
    freeWiseScriptHeader(&header);
    return status;
  }

  if (callbacks->cb_languages != NULL) {
    (*callbacks->cb_languages)(&languageTexts);
  }

  freeSafeCStrings(&languageTexts);

  // Read the texts
  SafeCStrings texts;
  initSafeCStrings(&texts);
  status = optReadCStrings(fp, otherStringCount, &texts, MAX_STRING_READ_FIXME);
  if (status != REWISE_OK) {
    printError("parseWiseScriptHeader failed to read texts.\n");
    freeWiseScriptHeader(&header);
    return status;
  }

  // callback
  if (callbacks->cb_texts != NULL) {
    (*callbacks->cb_texts)(&texts);
  }

  // cleanup
  freeSafeCStrings(&texts);
  freeWiseScriptHeader(&header);

  return status;
}


REWiseStatus parseWiseScript(FILE *const fp,
                             WiseScriptParsedInfo *const parsedInfo,
                             WiseScriptCallbacks *const callbacks)
{
  REWiseStatus status = REWISE_OK;
  unsigned char op;
  uint32_t langCount;

  // workaround for OP 0x18
  int op0x18skip = -1;

  initWiseScriptState();

  // Read operation and struct
  WiseScriptSTOP = 0;
  status = REWISE_OK;
  langCount = parsedInfo->languageCount;
  while (status == REWISE_OK && WiseScriptSTOP == 0) {
    int ch = fgetc(fp);
    op = (unsigned char)ch;

    if (ch == EOF) {
      break;
    }

    switch (op) {
      case 0x00:
      {
        WiseScriptFileHeader data;
        status = readWiseScriptFileHeader(fp, &data, langCount);
        if (status == REWISE_OK) {
          if (callbacks->cb_0x00 != NULL) {
            (*callbacks->cb_0x00)(&data);
          }
          freeWiseScriptFileHeader(&data);
        }
      }
        break;

      case 0x03:
      {
        WiseScriptUnknown0x03 data;
        status = readWiseScriptUnknown0x03(fp, &data, langCount);
        if (status == REWISE_OK) {
          if (callbacks->cb_0x03 != NULL) {
            (*callbacks->cb_0x03)(&data);
          }
          freeWiseScriptUnknown0x03(&data);
        }
      }
        break;

      case 0x04:
      {
        WiseScriptUnknown0x04 data;
        status = readWiseScriptUnknown0x04(fp, &data, langCount);
        if (status == REWISE_OK) {
          if (callbacks->cb_0x04 != NULL) {
            (*callbacks->cb_0x04)(&data);
          }
          freeWiseScriptUnknown0x04(&data);
        }
      }
        break;

      case 0x05:
      {
        WiseScriptUnknown0x05 data;
        status = readWiseScriptUnknown0x05(fp, &data);
        if (status == REWISE_OK) {
          if (callbacks->cb_0x05 != NULL) {
            (*callbacks->cb_0x05)(&data);
          }
          freeWiseScriptUnknown0x05(&data);
        }
      }
        break;

      case 0x06:
      {
        WiseScriptUnknown0x06 data;
        status = readWiseScriptUnknown0x06(fp, &data, langCount);
        if (status == REWISE_OK) {
          if (callbacks->cb_0x06 != NULL) {
            (*callbacks->cb_0x06)(&data);
          }
          freeWiseScriptUnknown0x06(&data);
        }
      }
        break;

      case 0x07:
      {
        WiseScriptUnknown0x07 data;
        status = readWiseScriptUnknown0x07(fp, &data);
        if (status == REWISE_OK) {
          if (callbacks->cb_0x07 != NULL) {
            (*callbacks->cb_0x07)(&data);
          }
          freeWiseScriptUnknown0x07(&data);
        }
      }
        break;

      case 0x08:
      {
        WiseScriptUnknown0x08 data;
        status = readWiseScriptUnknown0x08(fp, &data);
        if (status == REWISE_OK) {

          // -- Branch state tracking
          // end of current branch
          if (data.unknown_1 == 0x00) {
            if (_WiseScriptState._branchCount) {
              uint8_t branch = _WiseScriptState._branches[_WiseScriptState._branchCount - 1];
              if (branch == WISESCRIPT_BRANCH_CMP_LANG) {
                // unset language
                _WiseScriptState.hasLangSet = false;
                _WiseScriptState.currentLangUTF8Extra = 0;
                memset(_WiseScriptState.currentLang, 0, WISESCRIPT_LANG_MAX);

                if (callbacks->cb_languageChanged != NULL) {
                  (*callbacks->cb_languageChanged)();
                }
              }
              else
              if (branch == WISESCRIPT_BRANCH_CMP_COMP) {
                // unset component
                _WiseScriptState.hasCompSet = false;
                _WiseScriptState.currentCompUTF8Extra = 0;
                memset(_WiseScriptState.currentComp, 0, WISESCRIPT_COMP_MAX);

                if (callbacks->cb_componentsChanged != NULL) {
                  (*callbacks->cb_componentsChanged)();
                }
              }

              _WiseScriptState._branchCount--;
              _WiseScriptState._branches[_WiseScriptState._branchCount] = WISESCRIPT_BRANCH_CMP_NONE;
            }
            else {
              printDebug("WARN! OP is 0x08 but branch count is already 0\n");
            }
          }
          // -- End branch state tracking

          if (callbacks->cb_0x08 != NULL) {
            (*callbacks->cb_0x08)(&data);
          }
        }
      }
        break;

      case 0x09:
      {
        WiseScriptUnknown0x09 data;
        status = readWiseScriptUnknown0x09(fp, &data, langCount);
        if (status == REWISE_OK) {

          // -- Branch state tracking
          // Some new branch?
          if (data.unknown_1 == 0x4A || data.unknown_1 == 0x0A) {
            if (!addNewBranch(WISESCRIPT_BRANCH_CMP_IGNORED)) {
              stopWiseScriptParse(REWISE_ERROR_BRANCHING);
              freeWiseScriptUnknown0x09(&data);
              break;
            }
          }
          // -- End branch state tracking

          if (callbacks->cb_0x09 != NULL) {
            (*callbacks->cb_0x09)(&data);
          }
          freeWiseScriptUnknown0x09(&data);
        }
      }
        break;

      case 0x0A:
      {
        WiseScriptUnknown0x0A data;
        status = readWiseScriptUnknown0x0A(fp, &data);
        if (status == REWISE_OK) {
          if (callbacks->cb_0x0A != NULL) {
            (*callbacks->cb_0x0A)(&data);
          }
          freeWiseScriptUnknown0x0A(&data);
        }
      }
        break;

      case 0x0B:
      {
        WiseScriptUnknown0x0B data;
        status = readWiseScriptUnknown0x0B(fp, &data);
        if (status == REWISE_OK) {
          if (callbacks->cb_0x0B != NULL) {
            (*callbacks->cb_0x0B)(&data);
          }
          freeWiseScriptUnknown0x0B(&data);
        }
      }
        break;

      case 0x0C:
      {
        WiseScriptUnknown0x0C data;
        status = readWiseScriptUnknown0x0C(fp, &data);
        if (status == REWISE_OK) {

          // -- Branch state tracking
          if (data.varName.string == NULL) {
            if (!addNewBranch(WISESCRIPT_BRANCH_CMP_IGNORED)) {
              stopWiseScriptParse(REWISE_ERROR_BRANCHING);
              freeWiseScriptUnknown0x0C(&data);
              break;
            }
          }
          else
          if (strncmp(data.varName.string, "LANG", 4) == 0) {
#ifdef REWISE_DEBUG
            if (_WiseScriptState.hasLangSet) {
              printDebug("WARN! OP 0x0C LANG but there is already language set\n");
              // FIXME: relax this for now until it is beter understood.
              //stopWiseScriptParse(REWISE_ERROR_BRANCHING);
              //freeWiseScriptUnknown0x0C(&data);
              //break;
            }
#endif

            if (!addNewBranch(WISESCRIPT_BRANCH_CMP_LANG)) {
              stopWiseScriptParse(REWISE_ERROR_BRANCHING);
              freeWiseScriptUnknown0x0C(&data);
              break;
            }

            if (data.varValue.string != NULL) {
              // NOTE: FIXME maybe check that the size of
              //       varValue < WISESCRIPT_LANG_MAX ?
              strncpy(_WiseScriptState.currentLang, data.varValue.string, WISESCRIPT_LANG_MAX);
            }
            else {
              printDebug("WARN! OP 0x0C LANG but no varValue\n");
              memset(_WiseScriptState.currentLang, 0x00, WISESCRIPT_LANG_MAX);
            }
            _WiseScriptState.hasLangSet = true;
            _WiseScriptState.currentLangUTF8Extra = (
              data.varValue.strlen - data.varValue.printlen - 1);

            if (callbacks->cb_languageChanged != NULL) {
              (*callbacks->cb_languageChanged)();
            }
          }
          else
          if (strncmp(data.varName.string, "COMPONENTS", 10) == 0) {
#ifdef REWISE_DEBUG
            if (_WiseScriptState.hasCompSet) {
              printDebug("WARN! OP 0x0C COMP but there is already component set\n");
              // FIXME: relax this for now until it is beter understood.
              //stopWiseScriptParse(REWISE_ERROR_BRANCHING);
              //freeWiseScriptUnknown0x0C(&data);
              //break;
            }
#endif

            if (!addNewBranch(WISESCRIPT_BRANCH_CMP_COMP)) {
              stopWiseScriptParse(REWISE_ERROR_BRANCHING);
              freeWiseScriptUnknown0x0C(&data);
              break;
            }

            if (data.varValue.string != NULL) {
              // NOTE: FIXME maybe check that the size of
              //       varValue < WISESCRIPT_COMP_MAX ?
              strncpy(_WiseScriptState.currentComp,
                      data.varValue.string, WISESCRIPT_COMP_MAX);
            }
            else {
              printDebug("WARN! OP 0x0C COMP but no varValue\n");
              memset(_WiseScriptState.currentComp, 0x00, WISESCRIPT_COMP_MAX);
            }
            _WiseScriptState.hasCompSet = true;
            _WiseScriptState.currentCompUTF8Extra = (
              data.varValue.strlen - data.varValue.printlen -1);

            if (callbacks->cb_componentsChanged != NULL) {
              (*callbacks->cb_componentsChanged)();
            }
          }
          else {
            if (!addNewBranch(WISESCRIPT_BRANCH_CMP_IGNORED)) {
              stopWiseScriptParse(REWISE_ERROR_BRANCHING);
              freeWiseScriptUnknown0x0C(&data);
              break;
            }
          }
          // -- End branch state tracking

          if (callbacks->cb_0x0C != NULL) {
            (*callbacks->cb_0x0C)(&data);
          }

          freeWiseScriptUnknown0x0C(&data);
        }
      }
        break;

      case 0x0F:
        // Start form data?
        if (callbacks->cb_0x0F != NULL) {
          (*callbacks->cb_0x0F)();
        }
        break;

      case 0x10:
        // end form data?
        if (callbacks->cb_0x10 != NULL) {
          (*callbacks->cb_0x10)();
        }
        break;

      case 0x11:
      {
        WiseScriptUnknown0x11 data;
        status = readWiseScriptUnknown0x11(fp, &data);
        if (status == REWISE_OK) {
          if (callbacks->cb_0x11 != NULL) {
            (*callbacks->cb_0x11)(&data);
          }
          freeWiseScriptUnknown0x11(&data);
        }
      }
        break;

      case 0x12:
      {
        WiseScriptUnknown0x12 data;
        status = readWiseScriptUnknown0x12(fp, &data, langCount);
        if (status == REWISE_OK) {
          if (callbacks->cb_0x12 != NULL) {
            (*callbacks->cb_0x12)(&data);
          }
          freeWiseScriptUnknown0x12(&data);
        }
      }
        break;

      case 0x14:
      {
        WiseScriptUnknown0x14 data;
        status = readWiseScriptUnknown0x14(fp, &data);
        if (status == REWISE_OK) {
          if (callbacks->cb_0x14 != NULL) {
            (*callbacks->cb_0x14)(&data);
          }
          freeWiseScriptUnknown0x14(&data);
        }
      }
        break;

      case 0x15:
      {
        WiseScriptUnknown0x15 data;
        status = readWiseScriptUnknown0x15(fp, &data);
        if (status == REWISE_OK) {
          if (callbacks->cb_0x15 != NULL) {
            (*callbacks->cb_0x15)(&data);
          }
          freeWiseScriptUnknown0x15(&data);
        }
      }
        break;

      case 0x16:
      {
        WiseScriptUnknown0x16 data;
        status = readWiseScriptUnknown0x16(fp, &data);
        if (status == REWISE_OK) {
          if (callbacks->cb_0x16 != NULL) {
            (*callbacks->cb_0x16)(&data);
          }
          freeWiseScriptUnknown0x16(&data);
        }
      }
        break;

      case 0x17:
      {
        WiseScriptUnknown0x17 data;
        status = readWiseScriptUnknown0x17(fp, &data);
        if (status == REWISE_OK) {
          if (callbacks->cb_0x17 != NULL) {
            (*callbacks->cb_0x17)(&data);
          }
          freeWiseScriptUnknown0x17(&data);
        }
      }
        break;

      // skip tailing zeros
      case 0x18:
      {
        // FIXME this is a weird one and currently a WORKAROUND to
        // support many installers.
        //
        // On some installers we should just skip this OP and on others
        // we need to skip 6 bytes after the OP. Why and how are still
        // unknown.
        //
        // Most of the time when we need to skip 6 additional bytes
        // there is 0x00 right after the OP char (+5 other chars, most
        // also 0x00), but some installers have been observed to have
        // OP 0x00 right after the 0x18 OP char..
        //
        // Some examples:
        //
        //   0x18
        //   0x18 0x00 0x00 0x00 0x00 0x00 0x00
        //   0x18 0x00 0x01 0x00 0x00 0x00 0x00
        //   0x18 0x00 0x02 0x00 0x00 0x00 0x00
        //   ..
        //   0x18 0x00 0x08 0x00 0x00 0x00 0x00
        //
        // Notes:
        //
        // - The skipped count for each 0x18 appears to be the same per
        //   installer. Except for the last 0x18 near EOF, that one
        //   sometimes reads one more?
        // - PE build date is not a factor.
        // - Previous and next OP's appear to be random, but more
        //   research is needed.

        // first time 0x18 shows we determine the skip count
        if (op0x18skip == -1) {
          ch = fgetc(fp);
          if (ch == EOF) {
            break;
          }

          if (fseek(fp, -1, SEEK_CUR) != 0) {
            stopWiseScriptParse(REWISE_ERROR_FILE_SEEK);
            break;
          }

          if (ch != 0) {
            op0x18skip = 0;
          }
          // skip 6 bytes
          else {
            op0x18skip = 6;
          }
        }

        // skip additional bytes
        if (op0x18skip) {
          if (fseek(fp, 6, SEEK_CUR) != 0) {
            stopWiseScriptParse(REWISE_ERROR_FILE_SEEK);
            break;
          }
        }

        if (callbacks->cb_0x18 != NULL) {
          (*callbacks->cb_0x18)((uint32_t)op0x18skip);
        }
      }
        break;

      case 0x19:
      {
        WiseScriptUnknown0x19 data;
        status = readWiseScriptUnknown0x19(fp, &data);
        if (status == REWISE_OK) {
          if (callbacks->cb_0x19 != NULL) {
            (*callbacks->cb_0x19)(&data);
          }
          freeWiseScriptUnknown0x19(&data);
        }
      }
        break;

      case 0x1A:
      {
        WiseScriptUnknown0x1A data;
        status = readWiseScriptUnknown0x1A(fp, &data);
        if (status == REWISE_OK) {
          if (callbacks->cb_0x1A != NULL) {
            (*callbacks->cb_0x1A)(&data);
          }
          freeWiseScriptUnknown0x1A(&data);
        }
      }
        break;

      // skip these
      case 0x1B:
        break;

      case 0x0D:
        // -- Branch state tracking
        {
          if (!_WiseScriptState._branchCount) {
            printDebug("WARN! OP 0x0D but currently not in a branch\n");
            // FIXME: relax this for now until it is beter understood.
            //stopWiseScriptParse(REWISE_ERROR_BRANCHING);
            break;
          }

          uint8_t branch = _WiseScriptState._branches[_WiseScriptState._branchCount - 1];
          if (branch == WISESCRIPT_BRANCH_CMP_LANG) {
            // set default language
            if (!parsedInfo->languages.count) {
              printError("No default language set, but requested.\n");
              stopWiseScriptParse(REWISE_ERROR_BRANCHING);
              break;
            }

            if (parsedInfo->languages.strings[0].string != NULL) {
            strncpy(_WiseScriptState.currentLang,
                    parsedInfo->languages.strings[0].string,
                    WISESCRIPT_LANG_MAX);
            }
            else {
              printWarning("Default language is \0.\n");
              memset(_WiseScriptState.currentLang, 0, WISESCRIPT_LANG_MAX);
            }

            _WiseScriptState.currentLangUTF8Extra = (
              parsedInfo->languages.strings[0].strlen -
              parsedInfo->languages.strings[0].printlen - 1);

            if (callbacks->cb_languageChanged != NULL) {
              (*callbacks->cb_languageChanged)();
            }
          }
          else
          if (branch == WISESCRIPT_BRANCH_CMP_COMP) {
            // unset component
            _WiseScriptState.hasCompSet = false;
            _WiseScriptState.currentCompUTF8Extra = 0;
            memset(_WiseScriptState.currentComp, 0, WISESCRIPT_COMP_MAX);

            if (callbacks->cb_componentsChanged != NULL) {
              (*callbacks->cb_componentsChanged)();
            }
          }
        }
        // -- End branch tracking

        if (callbacks->cb_0x0D != NULL) {
          (*callbacks->cb_0x0D)();
        }
        break;

      case 0x24: // TODO Skip? Only seen in RTCW
        if (callbacks->cb_0x24 != NULL) {
          (*callbacks->cb_0x24)();
        }
        break;

      case 0x25: // TODO Skip? Only seen in RTCW
        if (callbacks->cb_0x25 != NULL) {
          (*callbacks->cb_0x25)();
        }
        break;

      case 0x1C:
      {
        WiseScriptUnknown0x1C data;
        status = readWiseScriptUnknown0x1C(fp, &data);
        if (status == REWISE_OK) {
          // -- Branch state tracking
          if (!addNewBranch(WISESCRIPT_BRANCH_CMP_IGNORED)) {
            stopWiseScriptParse(REWISE_ERROR_BRANCHING);
            freeWiseScriptUnknown0x1C(&data);
            break;
          }
          // -- End branch state tracking

          if (callbacks->cb_0x1C != NULL) {
            (*callbacks->cb_0x1C)(&data);
          }
          freeWiseScriptUnknown0x1C(&data);
        }
      }
        break;

      case 0x1D:
      {
        WiseScriptUnknown0x1D data;
        status = readWiseScriptUnknown0x1D(fp, &data);
        if (status == REWISE_OK) {
          if (callbacks->cb_0x1D != NULL) {
            (*callbacks->cb_0x1D)(&data);
          }
          freeWiseScriptUnknown0x1D(&data);
        }
      }
        break;

      case 0x1E:
      {
        WiseScriptUnknown0x1E data;
        status = readWiseScriptUnknown0x1E(fp, &data);
        if (status == REWISE_OK && callbacks->cb_0x1E != NULL) {
          (*callbacks->cb_0x1E)(&data);
        }
        freeWiseScriptUnknown0x1E(&data);
      }
        break;

      case 0x23:
      {
        WiseScriptUnknown0x23 data;
        status = readWiseScriptUnknown0x23(fp, &data);

        if (status == REWISE_OK) {

          // -- Branch state tracking
          if (data.varName.string == NULL) {
            // don't feed NULL into strncmp
            // FIXME: this is a error?
            freeWiseScriptUnknown0x23(&data);
            break;
          }

          if (strncmp(data.varName.string, "LANG", 4) == 0) {
            if (!_WiseScriptState._branchCount) {
              printError("OP 0x23 LANG but currently not in a branch\n");
              stopWiseScriptParse(REWISE_ERROR_BRANCHING);
              freeWiseScriptUnknown0x23(&data);
              break;
            }

            if (_WiseScriptState._branches[_WiseScriptState._branchCount - 1] != WISESCRIPT_BRANCH_CMP_LANG) {
              printError("OP 0x23 LANG but not in LANG branch\n");
              stopWiseScriptParse(REWISE_ERROR_BRANCHING);
              freeWiseScriptUnknown0x23(&data);
              break;
            }

            if (data.varValue.string != NULL) {
              // NOTE: FIXME maybe check that the size of
              //       varValue < WISESCRIPT_LANG_MAX ?
              strncpy(_WiseScriptState.currentLang,
                      data.varValue.string, WISESCRIPT_LANG_MAX);
            }
            else {
              printDebug("WARN! OP 0x23 LANG but no varValue\n");
              memset(_WiseScriptState.currentLang, 0x00, WISESCRIPT_LANG_MAX);
            }

            _WiseScriptState.currentLangUTF8Extra = (
              data.varValue.strlen - data.varValue.printlen - 1);

            if (callbacks->cb_languageChanged != NULL) {
              (*callbacks->cb_languageChanged)();
            }
          }
          else
          if (strncmp(data.varName.string, "COMPONENTS", 10) == 0) {
            if (!_WiseScriptState._branchCount) {
              printError("OP 0x23 COMP but currently not in a branch\n");
              stopWiseScriptParse(REWISE_ERROR_BRANCHING);
              freeWiseScriptUnknown0x23(&data);
              break;
            }

            if (_WiseScriptState._branches[_WiseScriptState._branchCount - 1] != WISESCRIPT_BRANCH_CMP_COMP) {
              printError("OP 0x23 COMP but not in COMP branch\n");
              stopWiseScriptParse(REWISE_ERROR_BRANCHING);
              freeWiseScriptUnknown0x23(&data);
              break;
            }

            if (data.varValue.string != NULL) {
              // NOTE: FIXME maybe check that the size of
              //       varValue < WISESCRIPT_LANG_MAX ?
              strncpy(_WiseScriptState.currentComp,
                      data.varValue.string, WISESCRIPT_COMP_MAX);
            }
            else {
              printWarning("OP 0x23 COMP but no varValue\n");
              memset(_WiseScriptState.currentComp, 0x00, WISESCRIPT_COMP_MAX);
            }

            _WiseScriptState.currentCompUTF8Extra = (
              data.varValue.strlen - data.varValue.printlen - 1);

            if (callbacks->cb_componentsChanged != NULL) {
              (*callbacks->cb_componentsChanged)();
            }
          }
          // -- End branch state tracking
  
          if (callbacks->cb_0x23 != NULL) {
            (*callbacks->cb_0x23)(&data);
          }
          freeWiseScriptUnknown0x23(&data);
        }
      }
        break;

      case 0x30:
      {
        WiseScriptUnknown0x30 data;
        status = readWiseScriptUnknown0x30(fp, &data);
        if (status == REWISE_OK && callbacks->cb_0x30 != NULL) {
          (*callbacks->cb_0x30)(&data);
        }
        freeWiseScriptUnknown0x30(&data);
      }
        break;

      default:
        printError("parseWiseScript unknown OP: %02X at 0x%08X\n", ch,
                   ftell(fp));
        status = REWISE_ERROR_VALUE;
        break;
    } // switch
  } // while

  if (status != REWISE_OK || WiseScriptSTOP != 0) {
    printError("parseWiseScript OP 0x%02X failed\n", op);
    if (WiseScriptStopStatus != REWISE_OK) {
      return WiseScriptStopStatus;
    }
    else {
      return status;
    }
  }

  return status;
}


void stopWiseScriptParse(const REWiseStatus status)
{
  WiseScriptSTOP = 1;
  WiseScriptStopStatus = status;
}


void initWiseScriptParsedInfo(WiseScriptParsedInfo *const parsedInfo)
{
  parsedInfo->headerSize = 0;
  parsedInfo->inflatedSize0x00 = 0;
  parsedInfo->inflatedSize0x06 = 0;
  parsedInfo->inflatedSize0x14 = 0;
  parsedInfo->totalInflatedSize = 0;
  parsedInfo->largestFileDeflateEnd = 0;
  parsedInfo->languageCount = 0;
  initSafeCStrings(&parsedInfo->languages);
  initSafeCStrings(&parsedInfo->components);
}


void freeWiseScriptParsedInfo(WiseScriptParsedInfo *const parsedInfo)
{
  freeSafeCStrings(&parsedInfo->languages);
  freeSafeCStrings(&parsedInfo->components);
}


#ifdef REWISE_DEBUG
REWiseStatus wiseScriptDebugPrint(FILE *const fp,
                                  WiseScriptParsedInfo *const parsedInfo)
{
  WiseScriptCallbacks callbacks;
  REWiseStatus status = REWISE_OK;

  initWiseScriptCallbacks(&callbacks);
  callbacks.cb_header = printWiseScriptHeader;
  callbacks.cb_unkownHeaderStrings = printWiseScriptUnknownStrings;
  callbacks.cb_langSelection = printWiseScriptLangSelection;
  callbacks.cb_languages = printWiseScriptLanguages;
  callbacks.cb_texts = printWiseScriptTexts;

  if ((status = parseWiseScriptHeader(fp, &callbacks)) != REWISE_OK) {
    return status;
  }

  callbacks.cb_0x00 = printWiseScriptFileHeader;
  callbacks.cb_0x03 = printWiseScriptUnknown0x03;
  callbacks.cb_0x04 = printWiseScriptUnknown0x04;
  callbacks.cb_0x05 = printWiseScriptUnknown0x05;
  callbacks.cb_0x06 = printWiseScriptUnknown0x06;
  callbacks.cb_0x07 = printWiseScriptUnknown0x07;
  callbacks.cb_0x08 = printWiseScriptUnknown0x08;
  callbacks.cb_0x09 = printWiseScriptUnknown0x09;
  callbacks.cb_0x0A = printWiseScriptUnknown0x0A;
  callbacks.cb_0x0B = printWiseScriptUnknown0x0B;
  callbacks.cb_0x0C = printWiseScriptUnknown0x0C;
  callbacks.cb_0x0D = printWiseScriptUnknown0x0D;
  callbacks.cb_0x11 = printWiseScriptUnknown0x11;
  callbacks.cb_0x12 = printWiseScriptUnknown0x12;
  callbacks.cb_0x14 = printWiseScriptUnknown0x14;
  callbacks.cb_0x15 = printWiseScriptUnknown0x15;
  callbacks.cb_0x16 = printWiseScriptUnknown0x16;
  callbacks.cb_0x17 = printWiseScriptUnknown0x17;
  callbacks.cb_0x18 = printWiseScriptUnknown0x18;
  callbacks.cb_0x19 = printWiseScriptUnknown0x19;
  callbacks.cb_0x1A = printWiseScriptUnknown0x1A;
  callbacks.cb_0x1C = printWiseScriptUnknown0x1C;
  callbacks.cb_0x1D = printWiseScriptUnknown0x1D;
  callbacks.cb_0x1E = printWiseScriptUnknown0x1E;
  callbacks.cb_0x23 = printWiseScriptUnknown0x23;
  callbacks.cb_0x24 = printWiseScriptUnknown0x24;
  callbacks.cb_0x25 = printWiseScriptUnknown0x25;
  callbacks.cb_0x30 = printWiseScriptUnknown0x30;

  return parseWiseScript(fp, parsedInfo, &callbacks);
}
#endif


/** @brief Verify that this is a valid Windows path and convert to
 *         UNIX path.
 *
 *   1 '/' is not allowed and will be escaped with '\/'
 *   2 '\' will be replaced with '/'
 *   3 '../' is not allowed and will be escaped with '\.\./'
 *   - CP1252 characters will be converted to UTF-8 characters, per
 *     character it will take 2 bytes of UTF-8.
 *   - Remove '%' on section start and end
 *
 *  @param path  Must be a valid pointer to a \0 terminated string.
 */
char *wiseScriptParsePath(const char *path)
{
  char buff[WIN_PATH_CONVERT_MAX + 1];
  char *returnPath;
  size_t pathLen;
  size_t newSize; // buffIndex + 1
  bool sectionStart;
  uint32_t sectionStartDotCount;
  uint32_t sectionSize;
  unsigned char ch;

  // first sanity check
  newSize = 0;
  pathLen = strlen(path);
  if (pathLen >= WIN_PATH_MAX) {
    printError("wiseScriptParsePath path is larger then WIN_PATH_MAX\n");
    return NULL;
  }
  else
  if (pathLen < 3) {
    printError("wiseScriptParsePath this is unexpected, we expect "
               "atleast 2 * '%%'.\n");
    return NULL;
  }

  if (path[0] != '%') {
    printError("wiseScriptParsePath expected '%%' at path start\n");
    return NULL;
  }

  // skip the first %
  path++;
  pathLen--;

  // init vars
  memset(buff, 0, WIN_PATH_CONVERT_MAX);
  sectionStart = true;
  sectionStartDotCount = 0;
  sectionSize = 0;

  // go over each character
  for (size_t i=0; i < pathLen; i++) {
    ch = path[i];

    // control characters are not allowed
    if (ch < 0x20) {
      printError("wiseScriptParsePath path contains control character "
                 "0x%02X\n", ch);
      return NULL;
    }

    // new path section (convert from Win to UNIX)
    if (sectionStartDotCount) {
      // keep track of how many dots this section starts with
      if (ch == '.') {
        sectionStartDotCount++;
      }
    }
    else
    if (sectionStart) {
      sectionStart = false;
      // keep track of how many dots this section starts with
      if (ch == '.') {
        sectionStartDotCount = 1;
      }

      // skip section '%'
      if (ch == '%') {
        continue;
      }
    }

    // new section start / old section end
    // replace Windows path separator with UNIX one
    if (ch == '\\') {

      // skip '%' if on the section end
      if (newSize && buff[newSize - 1] == '%') {
        buff[newSize - 1] = 0;
        newSize--;
      }

      // escape '../'
      if (sectionStartDotCount == 2 && sectionSize == 2) {
        if (newSize + 5 >= WIN_PATH_CONVERT_MAX) {
          goto pathErrorExceed;
        }
        buff[newSize - 2] = '\\';
        buff[newSize - 1] = '.';
        buff[newSize] = '\\';
        buff[newSize + 1] = '.';
        newSize += 2;
      }

      // + 2 because it makes no sence to end with a '/'
      if (newSize + 2 >= WIN_PATH_CONVERT_MAX) {
        goto pathErrorExceed;
      }

      buff[newSize] = '/';
      sectionStart = true;
      sectionSize = 0;
      sectionStartDotCount = 0;
      newSize++;
    }

    // escape '/'
    else
    if (ch == '/') {
      if (newSize + 2 >= WIN_PATH_CONVERT_MAX) {
        goto pathErrorExceed;
      }
      buff[newSize] = '\\';
      buff[newSize + 1] = '/';
      newSize += 2;
    }

    // skip section start '%'
    else
    if (sectionStart && ch == '%') {
      continue;
    }

    // character is OK, just add it
    else {
      if (newSize + 1 >= WIN_PATH_CONVERT_MAX) {
pathErrorExceed:
        printError("wiseScriptParsePath path exceeds "
                   "WIN_PATH_CONVERT_MAX while parsing\n");
        return NULL;
      }
      buff[newSize] = ch;
      newSize++;
      sectionSize++;
    }
  }

  if (buff[0] == 0) {
    printError("wiseScriptParsePath no filepath after parsing\n");
    return NULL;
  }
  else
  if (buff[newSize - 1] == '/') {
    printError("wiseScriptParsePath did not expect directory (end with '/')\n");
    return NULL;
  }
  else
  if (buff[newSize - 1] == '%') {
    buff[newSize - 1] = 0;
  }

  returnPath = strdup(buff);
  if (returnPath == NULL) {
    printError("wiseScriptParsePath errno: %s\n", strerror(errno));
    return NULL;
  }

  return returnPath;
}
