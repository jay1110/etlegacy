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
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <time.h>
#include <libgen.h> // dirname
#include <errno.h>
#include <sys/stat.h> // mkdir
#include <utime.h>
#include <sys/statvfs.h>

// inflation with zlib
#include <assert.h>
#include <zlib.h>

// PATH_MAX, NAME_MAX
#ifdef __linux__
#include <linux/limits.h>
#else
#include <limits.h> // *BSD and MSYS2
#endif

#include "print.h"
#include "reader.h"
#include "exefile.h"
#include "pkzip.h"
#include "errors.h"
#include "wildcard.h"
#include "wiseoverlay.h"
#include "wisescript.h"
#include "version.h"


#ifndef REWISE_DEFAULT_TMP_PATH
#define REWISE_DEFAULT_TMP_PATH "/tmp/"
#endif

#define SIZE_KiB 1024
#define SIZE_MiB 1048576    // 1024^2
#define SIZE_GiB 1073741824 // 1024^3

// Inflate chunk size
#define CHUNK_SIZE 0x4000   // 16 KiB

// These are here to perform some sanity checks when inflating the
// .dib and WiseScript.bin files and when doing RAW extract, for those
// files the inflated size is unknown pre inflation.
// NOTE: 'Gothic2-Setup.exe' has large .dib size (1.4 MiB), others only
//       seen ~16 KiB.
#define MAX_FILESIZE_DIB    (2 * SIZE_MiB)
#define MAX_FILESIZE_SCRIPT (1 * SIZE_MiB)
#define MAX_FILESIZE_RAW    (uint32_t)-1

// Maximum user defined output path size
#define MAX_OUTPUT_PATH (PATH_MAX - WIN_PATH_CONVERT_MAX) - 2


enum Operation {
  OP_NONE         = 0,
  OP_EXTRACT      = 1,
  OP_EXTRACT_RAW  = 2,
  OP_LIST         = 3,
  OP_VERIFY       = 4,
  OP_RAW_VERIFY   = 5,
  OP_HELP         = 6
#ifdef REWISE_DEBUG
  ,OP_SCRIPT_DEBUG = 7
#endif
};


void printPrettySize(const size_t size) {
  if (size > SIZE_GiB) {
    printf("%.2f GiB", (float)size / SIZE_GiB);
  }
  else
  if (size > SIZE_MiB) {
    printf("%.2f MiB", (float)size / SIZE_MiB);
  }
  else
  if (size > SIZE_KiB) {
    printf("%.2f KiB", (float)size / SIZE_KiB);
  }
  else {
    printf("%zu bytes", size);
  }
}


unsigned long getFreeDiskSpace(const char *const path)
{
  struct statvfs fsStats;
  if (statvfs((const char *)path, &fsStats) != 0) {
    printError("Failed to determine free disk space for '%s'. Errno: %s\n",
               strerror(errno));
    return 0;
  }
  return fsStats.f_bsize * fsStats.f_bavail;
}


void convertMsDosTime(struct tm *const destTime, const uint16_t date,
                      const uint16_t time)
{
  destTime->tm_year = (int)((date >> 9) + 80);
  destTime->tm_mon  = (int)((date >> 5) & 0b0000000000001111);
  destTime->tm_mday = (int)(date & 0b0000000000011111);
  destTime->tm_hour = (int)(time >> 11);
  destTime->tm_min  = (int)((time >> 5) & 0b0000000000111111);
  destTime->tm_sec  = (int)(time & 0b0000000000011111) * 2;
}


/* Globals, parseWiseScript() callbacks will need them */
static FILE *InputFp;
static long ScriptDeflateOffset;
static char OutputPath[MAX_OUTPUT_PATH]; // should be absolute and end with a '/'
static bool IsPkFormat = false;
static SafeCStrings *Languages = NULL;
// when language or components filter is enabled
static uint8_t FilterLang = 0; // user input
static char FilterComponents[WISESCRIPT_COMP_MAX] = {0};  // user input
// wildcard file filters
WildcardFilters FileFilters;
// flags, 1 for skip by lang, 2 for skip by comp
#define SKIP_BY_LANG 1
#define SKIP_BY_COMP 2
static uint8_t SkipFile = 0;
// for the use with --list
static int LargestLangStrSize = 0;
static int LargestCompStrSize = 0;


void printHelp(void) {
  printf("==============================================================\n");
  printf("              Welcome to REWise version %s\n", REWISE_VERSION_STR);
  printf("==============================================================\n\n");
  printf(" Usage: rewise OPERATION [OPTIONS] INPUT_FILE\n\n");
  printf("  OPERATIONS\n");
  printf("   -x --extract      OUTPUT_PATH  Extract files.\n");
  printf("   -r --raw          OUTPUT_PATH  Extract all files in the overlay "
                                           "data. Filename format will be "
                                           "'EXTRACTED_%%09u'.\n");
  printf("   -l --list                      List files.\n");
  printf("   -V --verify                    Run extract without actually "
                                           "outputting files, crc32s will be "
                                           "checked.\n");
  printf("   -0 --raw-verify                Run raw extract without actually "
                                           "outputting files, crc32s will be "
                                           "checked.\n");
#ifdef REWISE_DEBUG
  printf("   -z --script-debug              Print parsed WiseScript.bin\n");
#endif
  printf("   -v --version                   Print version and exit.\n");
  printf("   -h --help                      Display this HELP.\n");
  printf("\n");
  printf("  OPTIONS (--list)\n");
  printf("   -e --extended                  Also print languages and "
                                           "components.\n");
  printf("\n");
  printf("  OPTIONS (--extract, --list, --verify)\n");
  printf("   -f --filter       FILTER       Filter files based on their "
                                           "filepath. Multiple of these "
                                           "filters may be set. '*' will "
                                           "match anything, use '\\*' to "
                                           "escape.\n");
  printf("   -F --file-filter  FILTER_FILE  Path to a file that contains "
                                           "one filter string per line.\n");
  printf("   -c --components   NAME         Filter files by components "
                                           "name. Note that default files "
                                           "are always included.\n");
  printf("   -g --language     INDEX        Filter files by language index. "
                                           "Note that default files are always "
                                           "included.\n");
  printf("\n");
  printf("  OPTIONS (any)\n");
  printf("   -p --preserve                  Don't delete WiseScript.bin "
                                           "from TMP_PATH.\n");
  printf("   -t --tmp-path     TMP_PATH     Set temporary path, default: %s\n",
         REWISE_DEFAULT_TMP_PATH);
#ifdef REWISE_DEBUG
  printf("   -d --debug                     Print debug info.\n");
#endif
  printf("   -s --silent                    Be silent, don't print anything.\n");
  printf("   -n --no-extract                Don't extract anything. Use "
                                           "WiseScript.bin at the given "
                                           "TMP_PATH, it won't delete it.\n");
  printf("\n");
  printf("  NOTES\n");
  printf("    - Path to directory OUTPUT_PATH and TMP_PATH should exist and "
         "be writable.\n");
  printf("    - All files REWise does output will be overwritten when they "
         "exist!\n");
}


/** @brief Joins the two given paths to `dest` and tries to create the
 *         directories that don't exist yet.
 *
 *  @param subPath  Rest of the filepath (including file) from
 *                  WiseScript.bin. Should not be larger then
 *                  (WIN_PATH_CONVERT_MAX + 1). The +1 for the \0
 *                  terminator.
 *  @param dest     A pre-allocated char buffer with size PATH_MAX
 */
REWiseStatus preparePath(const char *const basePath,
                         const char *const subPath, char *const dest)
{
  // Join paths
  if ((strlen(basePath) + strlen(subPath) + 1) > PATH_MAX) {
    printError("Overflow of final path > PATH_MAX\n");
    // we probably got invalid filepath (subPath) from WiseScript.bin
    return REWISE_ERROR_VALUE;
  }
  strcpy(dest, basePath);
  strcat(dest, subPath);

  // Try to create directories as needed
  char *outputFilePath;
  char *currentSubPath;
  char *separator;

  // make a copy which strchr may manipulate.
  outputFilePath = strdup(dest);

  if (outputFilePath == NULL) {
    printError("Errno: %s\n", strerror(errno));
    return REWISE_ERROR_SYSTEM_IO;
  }

  // get the path without filename
  currentSubPath = dirname(outputFilePath);

  // get the path its root (string until first '/')
  separator = strchr(currentSubPath, '/');

  // This should not happen because the given path by the user should exist.
  if (separator == NULL) {
    printError("This should not happen, please report if it does! (1)\n");
    return REWISE_ERROR_INTERNAL;
  }

  // iterate through all sub-directories from root
  while (separator != NULL) {
    // terminate the dirName string on next occurance of '/'
    separator[0] = 0x00;

    // do not create root
    if (currentSubPath[0] != 0x00) {
      // stat currentSubPath
      if (access(currentSubPath, F_OK) != 0) {
        // currentSubPath exists but is not a directory
        if (errno == ENOTDIR) {
          printError("Extract subpath '%s' exists but is not a directory!\n",
                     currentSubPath);
          free(outputFilePath);
          return REWISE_ERROR_SYSTEM_IO;
        }

        // currentSubPath does not exist, try to create a new directory
        if (errno == ENOENT) {
          errno = 0;
          if (mkdir(currentSubPath, 0777) != 0) {
            printError("Failed to create subpath (1): '%s'\n", currentSubPath);
            printError("Errno: %s\n", strerror(errno));
            free(outputFilePath);
            return REWISE_ERROR_SYSTEM_IO;
          }
        }
      }
    }

    // reset the previous set terminator
    separator[0] = '/';

    // set separator to next occurrence of '/' (will be set to NULL when
    // there are no more occurrences of '/'.
    separator = strchr(separator + 1, '/');
  }

  // last subdir
  if (access(currentSubPath, F_OK) != 0) {
    // currentSubPath exists but is not a directory
    if (errno == ENOTDIR) {
      printError("Extract path '%s' exists but is not a directory!\n",
                 currentSubPath);
      free(outputFilePath);
      return REWISE_ERROR_SYSTEM_IO;
    }

    // currentSubPath does not exist, try to create a new directory
    if (errno == ENOENT) {
      if (mkdir(currentSubPath, 0777) != 0) {
        printError("Failed to create subpath (2): '%s'\n", currentSubPath);
        printError("Errno: %s\n", strerror(errno));
        free(outputFilePath);
        return REWISE_ERROR_SYSTEM_IO;
      }
    }
  }

  // cleanup
  free(outputFilePath);

  return REWISE_OK;
}


/** @brief Set path from cli argument.
 *
 *  A relative path is allowed and will be resloved. The path should
 *  exist and access-able by current user.
 *
 *  @param optarg  The path given by the user.
 *  @param dest    Where to set the resolved/validated path to.
 *
 *  @return `true` on success, `false` on error.
 */
REWiseStatus setPath(const char *const optarg, char *dest) {
  // Resolve absolute path
  const char *const outputPath = realpath(optarg, dest);
  if (outputPath == NULL) {
    printError("Invalid PATH given, could not resolve absolute path for "
               "'%s'. Errno: %s\n", optarg, strerror(errno));
    return REWISE_ERROR_ARGS;
  }

  size_t outputPathLen = strlen(outputPath);
  // -2 for the potential '/' we may add
  if (outputPathLen >= (MAX_OUTPUT_PATH - 1)) {
    printError("Absolute path of PATH is to large.\n");
    return REWISE_ERROR_ARGS;
  }

  // Make sure the path ends with a '/'
  if (dest[outputPathLen - 1] != '/') {
    strcat(dest, "/");
  }

  // Make sure the path exists
  if (access(dest, F_OK) != 0) {
    // dest exists but is not a directory
    if (errno == ENOTDIR) {
      printError("'%s' is not a directory.\n", dest);
      return REWISE_ERROR_ARGS;
    }
    // NOTE: realpath would have failed when the directory does not exist.
    // dest does not exist
    /*if (errno == ENOENT) {
      printError("'%s' does not exist.\n", dest);
      return false;
    }*/
  }

  return REWISE_OK;
}


/** @brief Read and verify CRC32 after the inflate process.
 *  @note  Call only when not IsPkFormat.
 *
 *  @param expectedCRC  The expected CRC32.
 */
REWiseStatus readAndVerifyCRC32(const uint32_t expectedCRC)
{
  REWiseStatus status = REWISE_OK;
  uint32_t readcrc;
  if ((status = readUInt32(InputFp, &readcrc)) != REWISE_OK) {
    printError("Failed to read CRC32 (1)\n");
    return status;
  }
  printDebug("Read CRC32 after inflate: %08X\n", readcrc);

  // CRC32 doesn't match, but it may be one byte off for whatever
  // reason, so check again with 1 byte off.
  // TODO test if seeking to deflated start + size - 4 helps solving this
  if (readcrc != expectedCRC) {
    if (fseek(InputFp, -3, SEEK_CUR) != 0) {
      return REWISE_ERROR_FILE_SEEK;
    }
    if ((status = readUInt32(InputFp, &readcrc)) != REWISE_OK) {
      printError("Failed to read CRC32 (2)\n");
      return status;
    }
    // It still doesn't match ..
    if (readcrc != expectedCRC) {
      printError("CRC32 mismatch!\n");
      return REWISE_ERROR_CRC32_MISMATCH;
    }
  }

  return status;
}


/** @brief Inflate file from current position in the input file. It will
 *         read the PK header when IsPkFormat.
 *
 *  @param fp              Valid/open file pointer to input file.
 *  @param outputFilePath  File path where to write inflated data to.
 *                         It may be NULL, it will not write to file
 *                         then.
 *  @param maxSize         Maximum size of the inflated output.
 *  @param outcrc          The final CRC will be set as value to this
 *                         so it optionally can be checked against for
 *                         example the CRC32 from the file header
 *                         (WiseScript.bin) later.
 *
 *  @return `0` on success, `<0` on error or `1` when `IsPkFormat` is
 *          `true` and the first PK_SIG_CENTRAL_DIR is read.
 */
REWiseStatus inflateFile(FILE *const fp, const char *outputFilePath,
                         long unsigned int maxSize, uint32_t *outcrc)
{
  REWiseStatus status = REWISE_OK;
  FILE *outputFp;
  int zstatus;
  z_stream strm;
  unsigned have;
  unsigned char buf_in[CHUNK_SIZE];
  unsigned char buf_out[CHUNK_SIZE];
  unsigned long inflateCRC = crc32(0, NULL, 0);
  long startPos;
  uint32_t pkCRC = 0;
  uint32_t pkDeflatedSize = 0;

  if (IsPkFormat) {
    printDebug(" - pre PK read: %08X\n", ftell(InputFp));

    uint32_t pkSig;
    if ((status = readPKSignature(InputFp, &pkSig)) != REWISE_OK) {
      return status;
    }

    if (pkSig != PK_SIG_LOCAL_FILE) {
      if (pkSig == PK_SIG_CENTRAL_DIR) {
        // NOTE: This is needed for raw extract
        return REWISE_PK_END;
      }
      printError("Invalid PK signature 0x%08X, "
                 "expected Local File Header signature\n", pkSig);
      return REWISE_ERROR_PK_HEADER;
    }

    PKLocalFileHeader pkLocalFileHeader;
    status = readPKLocalFileHeader(InputFp, &pkLocalFileHeader);
    if (status != REWISE_OK) {
      printError("Failed to readPKLocalFileHeader\n");
      return status;
    }

    pkCRC = pkLocalFileHeader.CRC32;
    pkDeflatedSize = pkLocalFileHeader.deflatedSize;
  }

  startPos = ftell(fp); // backup current pos

  printDebug("Deflate start: %ld 0x%08X\n", startPos, startPos);

  // Allocate inflate state.
  strm.zalloc   = Z_NULL;
  strm.zfree    = Z_NULL;
  strm.opaque   = Z_NULL;
  strm.avail_in = 0;
  strm.next_in  = Z_NULL;
  zstatus = inflateInit2(&strm, -15);

  // Failed to init z_stream.
  if (zstatus != Z_OK) {
    printError("Failed to init z_stream\n");
    return REWISE_ERROR_SYSTEM_IO;
  }

  // Open new file when an output filepath is given.
  if (outputFilePath != NULL) {
    outputFp = fopen(outputFilePath, "wb");
    if (outputFp == NULL) {
      printError("Failed to open output file '%s'\n", outputFilePath);
      (void)inflateEnd(&strm);
      return REWISE_ERROR_SYSTEM_IO;
    }
  }
  // Dummy: no output filepath is given, so we do not write to file.
  else {
    outputFp = NULL;
  }

  // Start inflation process.
  do {
    // Read next chunk to input buffer from input file.
    strm.avail_in = fread(buf_in, 1, CHUNK_SIZE, fp);
    if (ferror(fp)) {
      printError("inflate read error: %s\n", strerror(errno));
      (void)inflateEnd(&strm);
      if (outputFp != NULL) {
        fclose(outputFp);
      }
      if (feof(fp)) {
        return REWISE_ERROR_FILE_EOF;
      }
      return REWISE_ERROR_SYSTEM_IO;
    }
    if (strm.avail_in == 0) {
      // EOF?
      break;
    }
    strm.next_in = buf_in;

    // Process input buffer.
    do {
      // Reset output buffer.
      strm.avail_out = CHUNK_SIZE;
      strm.next_out = buf_out;

      // Inflate
      zstatus = inflate(&strm, Z_NO_FLUSH);
      assert(zstatus != Z_STREAM_ERROR);  /* state not clobbered */
      switch (zstatus) {
        case Z_MEM_ERROR:
          if (outputFp != NULL) {
            fclose(outputFp);
          }
          return REWISE_ERROR_SYSTEM_IO;
        case Z_NEED_DICT:
          zstatus = Z_DATA_ERROR;     /* and fall through */
        case Z_DATA_ERROR:
          if (strm.msg != NULL) {
            printError("zlib: %s\n", strm.msg);
          }
          else {
            printError("zlib error: %d\n", zstatus);
          }
          (void)inflateEnd(&strm);
          if (outputFp != NULL) {
            fclose(outputFp);
          }
          return REWISE_ERROR_INFLATE;
      }

      // Sanity check that the inflated size doesn't exceed the
      // maximum size.
      if (strm.total_out > maxSize) {
        (void)inflateEnd(&strm);
        if (outputFp != NULL) {
          fclose(outputFp);
        }
        printError("Inflated size is larger then expected\n");
        return REWISE_ERROR_INFLATE;
      }

      // Write to file.
      have = CHUNK_SIZE - strm.avail_out;
      if (outputFp != NULL) {
        if (fwrite(buf_out, 1, have, outputFp) != have || ferror(outputFp)) {
          printError("inflate write error: %s\n", strerror(errno));
          (void)inflateEnd(&strm);
          fclose(outputFp);
          return REWISE_ERROR_SYSTEM_IO;
        }
      }

      // Update the CRC32.
      if (have) {
        inflateCRC = crc32(inflateCRC, buf_out, have);
      }
    } while (strm.avail_out == 0);
  } while (zstatus != Z_STREAM_END);

  // cleanup
  if (outputFp != NULL) {
    fclose(outputFp);
  }
  (void)inflateEnd(&strm);

  // Verify CRC32
  if (IsPkFormat) {
    if (pkCRC != inflateCRC) {
      printError("PK CRC32 mismatch for '%s'\n", outputFilePath);
      return REWISE_ERROR_CRC32_MISMATCH;
    }

    // Potential seek back when read to much (most likely).
    if (fseek(fp, startPos + pkDeflatedSize, SEEK_SET) != 0) {
      return REWISE_ERROR_FILE_SEEK;
    }

    // NOTE: CRC32 from WiseScript.bin mismatches, that why the check
    // is skipped here.
  }
  else {
    // Potential seek back when read to much (most likely).
    if (fseek(fp, startPos + strm.total_in, SEEK_SET) != 0) {
      return REWISE_ERROR_FILE_SEEK;
    }

    // Read and verify the CRC32 after the deflate data
    if ((status = readAndVerifyCRC32(inflateCRC)) != REWISE_OK) {
      return status;
    }
  }

  // Set CRC32 of the inflated data
  if (outcrc != NULL) {
    (*outcrc) = (uint32_t)inflateCRC;
    printDebug("CRC32 of inflate data: %08X\n", inflateCRC);
  }

  return status;
}


/** @brief Extract file described in WiseScript.bin
 *
 *  It will also check against the CRC32 from the WiseScript.bin file
 *  entry and set the file datetime.
 *
 *  @param data            File header entry from WiseScript.bin
 *  @param outputFilePath  File path of the file to write to, may be
 *                         NULL to skip writing to file.
 *
 *  @return None
 */
REWiseStatus extractFile(const WiseScriptFileHeader *const data,
                         const char *outputFilePath)
{
  REWiseStatus status = REWISE_OK;
  uint32_t inflateCRC;

  // Seek to deflated file start
  if (fseek(InputFp, ((size_t)data->deflateStart) + ScriptDeflateOffset, SEEK_SET) != 0) {
    printError("Failed seek to file offset 0x%08X\n", ((long)data->deflateStart) + ScriptDeflateOffset);
    printError("Errno: %s\n", strerror(errno));
    stopWiseScriptParse(REWISE_ERROR_FILE_SEEK);
    return REWISE_ERROR_FILE_SEEK;
  }

  // Inflate/extract the file
  status = inflateFile(InputFp, outputFilePath,
                       (long unsigned int)data->inflatedSize, &inflateCRC);

  if (status != REWISE_OK && status < REWISE_ERROR_END) {
    printError("Failed to extract '%s'\n", data->destFile);
    stopWiseScriptParse(status);
    return status;
  }

  // Check that the CRC32 from WiseScript.bin matches, only check
  // when the CRC32 from WiseScript.bin is not 0. Also don't check
  // for zip installers.
  if (!IsPkFormat && data->crc32 != 0 && data->crc32 != inflateCRC) {
    printError("CRC32 mismatch for '%s' from WiseScript.bin\n", data->destFile);
    stopWiseScriptParse(REWISE_ERROR_CRC32_MISMATCH);
    return REWISE_ERROR_CRC32_MISMATCH;
  }

  return status;
}


/** @brief Check whether we need to skip this file based on language,
 *         components and/or wildcard filters. */
int filterFile(const WiseScriptFileHeader *const data)
{
  if (SkipFile) {
    return 1;
  }

  if (FileFilters.count == 0) {
    return 0;
  }

  return matchWildcardFilters(&FileFilters, data->destFile.string);
}


/* --- Initial information gathering by parsing the
 *     WiseOverlayHeader and WiseScript.bin ------- */

static WiseScriptParsedInfo *WISESCRIPT_PARSED_INFO = NULL;

void updateParsedInfoHeader(const WiseScriptHeader *const header)
{
  WISESCRIPT_PARSED_INFO->languageCount = header->languageCount;
}

void updateParsedInfoLanguages(const SafeCStrings *const languageStrings)
{
  uint8_t langCount = WISESCRIPT_PARSED_INFO->languageCount;
  SafeCStrings *languages = &WISESCRIPT_PARSED_INFO->languages;

  languages->strings = malloc(sizeof(SafeCString) * langCount);
  if (languages->strings == NULL) {
    stopWiseScriptParse(REWISE_ERROR_SYSTEM_IO);
    return;
  }

  // init all the pointers to NULL
  for (uint8_t i = 0; i < langCount; ++i) {
    initSafeCString(&languages->strings[i]);
  }

  languages->count = langCount;

  if (langCount == 1) {
    if (languageStrings->strings[0].string != NULL) {
      languages->strings[0].string = strndup(
        languageStrings->strings[0].string,
        languageStrings->strings[0].strlen);

      if (languages->strings[0].string == NULL) {
        stopWiseScriptParse(REWISE_ERROR_SYSTEM_IO);
        return;
      }
    }
    // empty string
    else {
      languages->strings[0].string = malloc(sizeof(char) * 1);
      if (languages->strings[0].string == NULL) {
        stopWiseScriptParse(REWISE_ERROR_SYSTEM_IO);
        return;
      }
      languages->strings[0].strlen = languageStrings->strings[0].strlen;
      languages->strings[0].printlen = languageStrings->strings[0].printlen;
      languages->strings[0].string[0] = 0;
    }
  }
  else
  if (langCount > 1) {
    uint8_t nextIndex = 0;
    for (uint8_t i = 0; i < langCount; ++i) {

      if (languageStrings->strings[nextIndex].string == NULL) {
        printError("This is a multi language "
                   "installer but not all languages have their "
                   "name set\n");
        stopWiseScriptParse(REWISE_ERROR_VALUE);
        return;
      }

      languages->strings[i].string = strdup(
        languageStrings->strings[nextIndex].string);

      if (languages->strings[i].string == NULL) {
        stopWiseScriptParse(REWISE_ERROR_SYSTEM_IO);
        return;
      }

      languages->strings[i].strlen = languageStrings->strings[nextIndex].strlen;
      languages->strings[i].printlen = languageStrings->strings[nextIndex].printlen;
      nextIndex += 2;
    }
  }
  else {
    printError("language count of 0?\n");
    stopWiseScriptParse(REWISE_ERROR_VALUE);
    return;
  }
}

void updatedParsedInfoComponents(void)
{
  const WiseScriptParseState *state = getWiseScriptState();
  if (!state->hasCompSet) {
    return;
  }

  SafeCStrings *comps = &WISESCRIPT_PARSED_INFO->components;

  // see if the component in already there
  for (uint32_t i=0; i < comps->count; ++i)
  {
    if (state->currentComp[0] == 0x00) {
      continue;
    }

    if (strcmp(comps->strings[i].string, state->currentComp) == 0) {
      return;
    }
  }

  SafeCString *newStrings = realloc(comps->strings, sizeof(SafeCString) * (comps->count + 1));
  if (newStrings == NULL) {
    stopWiseScriptParse(REWISE_ERROR_SYSTEM_IO);
    return;
  }

  comps->strings = newStrings;
  initSafeCString(&comps->strings[comps->count]);
  comps->count++;

  comps->strings[comps->count - 1].string = strdup(state->currentComp);
  if (comps->strings[comps->count - 1].string == NULL) {
    stopWiseScriptParse(REWISE_ERROR_SYSTEM_IO);
    return;
  }
}

void updateParsedInfo0x00Filtered(const WiseScriptFileHeader *const data)
{
  if (filterFile(data)) {
    return;
  }
  WISESCRIPT_PARSED_INFO->totalInflatedSize += data->inflatedSize;
  WISESCRIPT_PARSED_INFO->inflatedSize0x00  += data->inflatedSize;
}

void updateParsedInfo0x00(const WiseScriptFileHeader *const data) {
  if (data->deflateEnd > WISESCRIPT_PARSED_INFO->largestFileDeflateEnd) {
    WISESCRIPT_PARSED_INFO->largestFileDeflateEnd = data->deflateEnd;
  }
  WISESCRIPT_PARSED_INFO->totalInflatedSize += data->inflatedSize;
  WISESCRIPT_PARSED_INFO->inflatedSize0x00  += data->inflatedSize;
}

void updateParsedInfo0x06(const WiseScriptUnknown0x06 *const data) {
  for (uint32_t i=0; i < data->deflateInfo.count; ++i) {
    uint32_t deflateEnd = data->deflateInfo.info[i].deflateEnd;
    if (deflateEnd > WISESCRIPT_PARSED_INFO->largestFileDeflateEnd) {
      WISESCRIPT_PARSED_INFO->largestFileDeflateEnd = deflateEnd;
    }
  }
  WISESCRIPT_PARSED_INFO->totalInflatedSize += data->deflateInfo.info[0].inflatedSize;
  WISESCRIPT_PARSED_INFO->inflatedSize0x06  += data->deflateInfo.info[0].inflatedSize;
}

void updateParsedInfo0x14(const WiseScriptUnknown0x14 *const data) {
  if (data->deflateEnd > WISESCRIPT_PARSED_INFO->largestFileDeflateEnd) {
    WISESCRIPT_PARSED_INFO->largestFileDeflateEnd = data->deflateEnd;
  }
  WISESCRIPT_PARSED_INFO->totalInflatedSize += data->inflatedSize;
  WISESCRIPT_PARSED_INFO->inflatedSize0x14  += data->inflatedSize;
}


/** @brief peek the WiseScript.bin for the first time to get some
 *  info for sanity.
 *
 *  @param filepath  Filepath to WiseScript.bin
 **/
REWiseStatus wiseScriptGetParsedInfo(FILE *fp,
                                     WiseScriptParsedInfo *const parsedInfo)
{
  WiseScriptCallbacks callbacks;
  REWiseStatus status = REWISE_OK;

  WISESCRIPT_PARSED_INFO = parsedInfo;

  initWiseScriptCallbacks(&callbacks);

  // read and process header
  callbacks.cb_header = updateParsedInfoHeader;
  callbacks.cb_languages = updateParsedInfoLanguages;

  status = parseWiseScriptHeader(fp, &callbacks);
  if (status != REWISE_OK) {
    return status;
  }

  parsedInfo->headerSize = ftell(fp);

  // read and process opcodes
  callbacks.cb_componentsChanged = updatedParsedInfoComponents;
  callbacks.cb_0x00 = updateParsedInfo0x00;
  callbacks.cb_0x06 = updateParsedInfo0x06;
  callbacks.cb_0x14 = updateParsedInfo0x14;

  status = parseWiseScript(fp, parsedInfo, &callbacks);
  if (status != REWISE_OK) {
    return status;
  }

  return status;
}


/** @brief this is called after the initial information gathering and
 *         file filters have been set, to get the filtered inflated
 *         size.
 */
REWiseStatus wiseScriptGetFilteredInflationSize(FILE *const fp,
                                WiseScriptParsedInfo *const parsedInfo,
                                WiseScriptCallbacks *const callbacks)
{
  REWiseStatus status = REWISE_OK;
  WISESCRIPT_PARSED_INFO = parsedInfo;

  parsedInfo->inflatedSize0x00 = 0;
  callbacks->cb_0x00 = updateParsedInfo0x00Filtered;

  if ((status = parseWiseScript(fp, parsedInfo, callbacks)) != REWISE_OK)
  {
    callbacks->cb_0x00 = NULL;
    printError("wiseScriptGetFilteredInflationSize failed\n");
    return status;
  }

  callbacks->cb_0x00 = NULL;
  return status;
}



/* --- WiseScript callbacks --- */

/** @brief Extract file described in WiseScript.bin
 *
 *  It will also check against the CRC32 from the WiseScript.bin file
 *  entry and set the file datetime.
 *
 *  This might be set as a callback and be called from
 *  `parseWiseScript()`.
 *
 *  @param data            File header entry from WiseScript.bin
 *  @param outputFilePath  File path of the file to write to, may be
 *                         NULL to skip writing to file.
 *
 *  @return None
 */
void cb_extractFile(const WiseScriptFileHeader *const data)
{
  REWiseStatus status = REWISE_OK;
  char outputFilePath[PATH_MAX];

  if (filterFile(data)) {
    return;
  }

  // Create the final absolute filepath and make sure the path exists (will be
  // created when it doesn't exist).
  status = preparePath(OutputPath, data->destFile.string, outputFilePath);
  if (status != REWISE_OK) {
    printError("preparePath failed.\n");
    stopWiseScriptParse(status);
    return;
  }

  if ((status = extractFile(data, outputFilePath)) != REWISE_OK) {
    stopWiseScriptParse(status);
    return;
  }

  // Set file access/modification datetime
  struct tm fileCreation;
  time_t creationSeconds;
  convertMsDosTime(&fileCreation, data->date, data->time);
  creationSeconds = mktime(&fileCreation);
  const struct utimbuf times = {
    .actime  = creationSeconds,
    .modtime = creationSeconds
  };
  if (utime(outputFilePath, &times) != 0) {
    printWarning("Failed to set access and modification datetime for file "
                 "'%s'\n", outputFilePath);
  }

  printInfo("Extracted %s\n", data->destFile);
}


/** @brief Verify file described in WiseScript.bin
 *
 *  Basically the same as `cb_extractFile()` but it won't create/write
 *  file. It will inflate the data and check the CRC32. It also verifies
 *  that the inflated data doesn't exceed the advertised filesize from
 *  the file header.
 *
 *  This might be set as a callback and be called from
 *  `parseWiseScript()`.
 *
 *  @param data  File header entry from WiseScript.bin
 *
 *  @return None
 */
void cb_noExtractFile(const WiseScriptFileHeader *const data)
{
  REWiseStatus status = REWISE_OK;
  if (filterFile(data)) {
    return;
  }

  if ((status = extractFile(data, NULL)) != REWISE_OK) {
    stopWiseScriptParse(status);
    return;
  }

  printInfo("CRC32 success for '%s'\n", data->destFile);
}


/** @brief Print file header entry from WiseScript.bin
 *
 *  Used for for listing files.
 *
 *  This might be set as a callback and be called from
 *  `parseWiseScript()`.
 *
 *  @param data  File header entry from WiseScript.bin
 *
 *  @return None
 */
void printFile(const WiseScriptFileHeader *const data) {
  if (filterFile(data)) {
    return;
  }

  struct tm fileDatetime;
  convertMsDosTime(&fileDatetime, data->date, data->time);
  printf("%12u %02d-%02d-%04d %02d:%02d:%02d '%s'\n", data->inflatedSize,
         fileDatetime.tm_mday, fileDatetime.tm_mon, fileDatetime.tm_year + 1900,
         fileDatetime.tm_hour, fileDatetime.tm_min, fileDatetime.tm_sec,
         data->destFile.string);
}


void printFileExtended(const WiseScriptFileHeader *const data) {
  if (filterFile(data)) {
    return;
  }

  struct tm fileDatetime;
  convertMsDosTime(&fileDatetime, data->date, data->time);
  printf("%12u %02d-%02d-%04d %02d:%02d:%02d", data->inflatedSize,
         fileDatetime.tm_mday, fileDatetime.tm_mon, fileDatetime.tm_year + 1900,
         fileDatetime.tm_hour, fileDatetime.tm_min, fileDatetime.tm_sec);

  const WiseScriptParseState *state = getWiseScriptState();

  if (state->hasLangSet) {
    printf(" %-*s", LargestLangStrSize + state->currentLangUTF8Extra, state->currentLang);
  }
  else {
    printf(" %-*s", LargestLangStrSize, "All");
  }

  if (state->hasCompSet) {
    printf(" %-*s", LargestCompStrSize + state->currentCompUTF8Extra, state->currentComp);
  }
  else {
    printf(" %-*s", LargestCompStrSize, "-");
  }

  printf(" %s\n", data->destFile.string);
}


void updateSkipFileByLanguage(void)
{
  const WiseScriptParseState *state = getWiseScriptState();

  if (state->hasLangSet) {
    // FIXME special case
    if (state->currentLang[0] == 0) {
      SkipFile = SkipFile & ~SKIP_BY_LANG;
    }

    // find the index of state->currentLang
    int index = -1;
    for (uint8_t i=0; i < Languages->count; ++i) {
      if (strncmp(state->currentLang,
          Languages->strings[i].string, WISESCRIPT_LANG_MAX) == 0)
      {
        index = i;
        break;
      }
    }

    if (index > -1 && (uint8_t)index == FilterLang) {
      SkipFile = SkipFile & ~SKIP_BY_LANG;
    }
    else {
      SkipFile |= SKIP_BY_LANG;
    }
  }
  else {
    SkipFile = SkipFile & ~SKIP_BY_LANG;
  }
}

void updateSkipFileByComponents(void)
{
  const WiseScriptParseState *state = getWiseScriptState();

  if (state->hasCompSet) {
    // FIXME special case
    if (state->currentComp[0] == 0) {
      SkipFile = SkipFile & ~SKIP_BY_COMP;
    }

    if (strncmp(state->currentComp,
        FilterComponents, WISESCRIPT_COMP_MAX) == 0)
    {
      SkipFile = SkipFile & ~SKIP_BY_COMP;
    }
    else {
      SkipFile |= SKIP_BY_COMP;
    }
  }
  else {
    SkipFile = SkipFile & ~SKIP_BY_COMP;
  }
}


/* --- End WiseScript callbacks --- */


int main(const int argc, char *const argv[]) {
  REWiseStatus status = REWISE_OK;
  char inputFile[PATH_MAX];
  char tmpPath[MAX_OUTPUT_PATH] = REWISE_DEFAULT_TMP_PATH;
  enum Operation operation = OP_NONE;
  bool optExtended = false;
  bool optNoExtract = false;
  bool optPreserveTmp = false;
  bool optFilterLang = false;
  bool optFilterComp = false;
  inputFile[0] = 0x00;

  initWildcardFilters(&FileFilters);

  // https://www.gnu.org/software/libc/manual/html_node/Getopt-Long-Options.html
  // https://www.gnu.org/software/libc/manual/html_node/Getopt-Long-Option-Example.html
  const struct option long_options[] = {
    // OPERATIONS
    {"extract"     , required_argument, NULL, 'x'},
    {"raw"         , required_argument, NULL, 'r'},
    {"list"        , no_argument      , NULL, 'l'},
    {"verify"      , no_argument      , NULL, 'V'},
    {"raw-verify"  , no_argument      , NULL, '0'},
#ifdef REWISE_DEBUG
    {"script-debug", no_argument      , NULL, 'z'},
#endif
    {"version"     , no_argument      , NULL, 'v'},
    {"help"        , no_argument      , NULL, 'h'},
    // OPTIONS
    {"extended"    , no_argument      , NULL, 'e'},
    {"filter"      , required_argument, NULL, 'f'},
    {"file-filter" , required_argument, NULL, 'F'},
    {"language"    , required_argument, NULL, 'g'},
    {"component"   , required_argument, NULL, 'c'},
    {"tmp-path"    , required_argument, NULL, 't'},
#ifdef REWISE_DEBUG
    {"debug"       , no_argument      , NULL, 'd'},
#endif
    {"preserve"    , no_argument      , NULL, 'p'},
    {"silent"      , no_argument      , NULL, 's'},
    {"no-extract"  , no_argument      , NULL, 'n'},
    {NULL          , 0                , NULL, 0}
  };

  int option_index = 0;
  for (;;) {
    int opt = getopt_long(argc, argv, "x:r:t:f:F:g:c:lehdspznVv0",
                          long_options, &option_index);

    if (opt == -1) {
      break;
    }

    switch (opt) {
      // OPERATIONS
      case 'x':
      {
        if (operation != OP_NONE) {
          printError("More then one operation is set! Do set only one.\n");
          return REWISE_ERROR_ARGS;
        }
        operation = OP_EXTRACT;
        if (setPath(optarg, OutputPath) != REWISE_OK) {
          status = REWISE_ERROR_ARGS;
          goto argParseError;
        }
      }
        break;

      case 'r':
        if (operation != OP_NONE) {
          printError("More then one operation is set! Do set only one.\n");
          status = REWISE_ERROR_ARGS;
          goto argParseError;
        }
        operation = OP_EXTRACT_RAW;
        if (setPath(optarg, OutputPath) != REWISE_OK) {
          status = REWISE_ERROR_ARGS;
          goto argParseError;
        }
        break;

      case 'l':
        if (operation != OP_NONE) {
          printError("More then one operation is set! Do set only one.\n");
          status = REWISE_ERROR_ARGS;
          goto argParseError;
        }
        operation = OP_LIST;
        break;

      case 'V':
        if (operation != OP_NONE) {
          printError("More then one operation is set! Do set only one.\n");
          status = REWISE_ERROR_ARGS;
          goto argParseError;
        }
        operation = OP_VERIFY;
        break;

      case '0':
        if (operation != OP_NONE) {
          printError("More then one operation is set! Do set only one.\n");
          status = REWISE_ERROR_ARGS;
          goto argParseError;
        }
        operation = OP_RAW_VERIFY;
        break;

#ifdef REWISE_DEBUG
      case 'z':
        if (operation != OP_NONE) {
          printError("More then one operation is set! Do set only one.\n");
          status = REWISE_ERROR_ARGS;
          goto argParseError;
        }
        operation = OP_SCRIPT_DEBUG;
        break;
#endif

      case 'v':
        printf("REWise v%s\n", REWISE_VERSION_STR);
        return REWISE_OK;

      case 'h':
        printHelp();
        return REWISE_OK;

      // OPTIONS
      case 'f': // add file filter string
        if (addWildcardFilter(&FileFilters, optarg) != REWISE_OK) {
          status = REWISE_ERROR_ARGS;
          goto argParseError;
        }
        break;

      case 'F': // add file filter from file
        if (addWildcardFilterFromFile(&FileFilters, optarg) != REWISE_OK)
        {
          status = REWISE_ERROR_ARGS;
          goto argParseError;
        }
        break;

      case 'g': // set language
        {
          // NOTE: this does not verify that the lang index is valid
          // only that it fits the container.
          char *endptr = NULL;
          long langIndex = strtol(optarg, &endptr, 10);
          if (langIndex > ((uint8_t) - 1)) {
            printError("What language index are you trying to resolve? "
                       "It is to large..\n");
            return REWISE_ERROR_ARGS;
          }

          // non numeric character(s) or to large
          if (endptr && *endptr) {
            printError("Invalid language index given, it should only contain numbers.\n");
            status = REWISE_ERROR_ARGS;
            goto argParseError;
          }

          FilterLang = (uint8_t)langIndex;
          optFilterLang = true;
        }
        break;

      case 'c': // set components
        {
          strncpy(FilterComponents, optarg, WISESCRIPT_COMP_MAX);
          optFilterComp = true;
        }
        break;

      case 'e': // extended info with list
        optExtended = true;
        break;


#ifdef REWISE_DEBUG
      case 'd':
        setPrintFlag(PRINT_DEBUG);
        break;
#endif

      case 's':
        setPrintFlags(PRINT_SILENT);
        break;

      case 't':
        if (setPath(optarg, tmpPath) != REWISE_OK) {
          printError("Invalid TMP_PATH given.\n");
          status = REWISE_ERROR_ARGS;
          goto argParseError;
        }
        break;

      case 'p':
        optPreserveTmp = true;
        break;

      case 'n':
        optNoExtract = true;
        break;

      case '?':
        // invalid option
        printError("Invalid operation or option\n");
        status = REWISE_ERROR_ARGS;
        goto argParseError;

      default:
#ifndef REWISE_DEBUG_DUMMY
        printError("Invalid operation or option\n");
        status = REWISE_ERROR_ARGS;
        goto argParseError;
#else
        // don't care about extra args
        break;
#endif
    }
  }

  if ((argc - 1 ) < optind) {
    printError("Please supply a input file\n");
    status = REWISE_ERROR_ARGS;
    goto argParseError;
  }
  if ((argc - 1 ) > optind) {
    printError("Please supply only one input file\n");
    status = REWISE_ERROR_ARGS;
    goto argParseError;
  }

  if (strlen(argv[optind]) > (PATH_MAX - 1)) {
    printError("What are you trying to do? INPUT_FILE is larger then PATH_MAX\n");
    status = REWISE_ERROR_ARGS;
    goto argParseError;
  }
  strcpy(inputFile, argv[optind]);

  if (operation == OP_NONE) {
    printError("Please specify a operation.\n");
    status = REWISE_ERROR_ARGS;
    goto argParseError;
  }

  /* Check if input file exists */
  if (access(inputFile, F_OK) != 0) {
    printError("InputFile '%s' not found. Errno: %s\n", inputFile,
               strerror(errno));
    status = REWISE_ERROR_ARGS;
    goto argParseError;
  }

  if (status != REWISE_OK) {
argParseError:
    freeWildcardFilters(&FileFilters);
    return status;
  }

  /* Open inputFile */
  InputFp = fopen(inputFile, "rb");

  if (InputFp == NULL) {
    printError("Failed to open inputFile '%s'\n", inputFile);
    printError("Errno: %s\n", strerror(errno));
    freeWildcardFilters(&FileFilters);
    return REWISE_ERROR_SYSTEM_IO;
  }

  ExeAnalyzeResult exeResult;
  initExeAnalyzeResult(&exeResult);

  {
    int err = analyzeExe(InputFp, &exeResult);
    if (err > 0) { // errno
      status = REWISE_ERROR_SYSTEM_IO;
      goto mainEnd;
    }
    else
    if (err < 0) { // one of our sanity checks
      status = REWISE_ERROR_INVALID_PENE;
      goto mainEnd;
    }
  }

  printDebug("InputFile: %s\n", inputFile);
  printDebug("OverlayOffset: %ld\n", exeResult.overlayOffset);

  // Seek to overlayData
  if (fseek(InputFp, exeResult.overlayOffset, SEEK_SET) != 0) {
    printError("Failed to seek to overlayData. Offset: 0x%08X\n", exeResult.overlayOffset);
    printError("Errno: %s\n", strerror(errno));
    status = REWISE_ERROR_SYSTEM_IO;
    goto mainEnd;
  }

  // Read Wise overlay header
  WiseOverlayHeader overlayHeader;
  status = readWiseOverlayHeader(InputFp, &overlayHeader);
  if (status != REWISE_OK) {
    printError("Failed to read WiseOverlayHeader.\n");
    goto mainEnd;
  }

#ifdef REWISE_DEBUG
  if ((getPrintFlags() & PRINT_DEBUG)) {
    printOverlayHeader(&overlayHeader);
  }
#endif

  // @NOTE: overlayHeader may be used later, but not the strings!
  freeWiseOverlayHeader(&overlayHeader);

  // Check if PKZIP formatted
  IsPkFormat = ((overlayHeader.flags & WISE_FLAG_PK_ZIP) > 0);
  printDebug("IsPkFormat: %d\n", IsPkFormat);

  // Sanity
  if (overlayHeader.inflatedSizeWiseScript > MAX_FILESIZE_SCRIPT) {
    printError("Advertised WiseScript.bin size looks insane.\n");
    status = REWISE_ERROR_VALUE;
    goto mainEnd;
  }
  if (overlayHeader.inflatedDib > MAX_FILESIZE_DIB) {
    printError("Advertised .dib size looks insane.\n");
    status = REWISE_ERROR_VALUE;
    goto mainEnd;
  }

  // Initial check on free disk space (TMP_PATH)
  unsigned long tmpFreeDiskSpace = getFreeDiskSpace(tmpPath);
  if (tmpFreeDiskSpace == 0) { // failed to determine free disk space
    status = REWISE_ERROR_SYSTEM_IO;
    goto mainEnd;
  }
  // Make sure there is enough free space to extract the WiseScript.bin
  if (tmpFreeDiskSpace < MAX_FILESIZE_SCRIPT) {
    printError("At-least 1 MiB of free space is required in the TMP_PATH.\n");
    status = REWISE_ERROR_SYSTEM_IO;
    goto mainEnd;
  }

  // Raw extract
  if (operation == OP_EXTRACT_RAW) {
    uint32_t extractCount = 0;
    char extractFilePath[PATH_MAX];

    // Start inflating and outputting files
    while (ftell(InputFp) < exeResult.fileSize) {
      char fileName[21];
      if (snprintf(fileName, 20, "EXTRACTED_%09u", extractCount) > 20) {
        // truncated
        printError("Failed to format filename, it truncated.\n");
        goto mainEnd;
      }
      status = preparePath(OutputPath, fileName, extractFilePath);
      if (status != REWISE_OK) {
        printError("Failed to create directories for '%s'.\n", fileName);
        goto mainEnd;
      }

#ifdef REWISE_DEBUG
      long startoffset = ftell(InputFp);
#endif

      status = inflateFile(InputFp, (const char *)extractFilePath,
                           MAX_FILESIZE_RAW, NULL);

      if (status == REWISE_PK_END) {
        status = REWISE_OK;
        break; // end of LocalFileHeaders
      }
      else
      if (status != REWISE_OK && status < REWISE_ERROR_END) {
        printError("Failed to extract '%s'.\n", extractFilePath);
        goto mainEnd;
      }

#ifdef REWISE_DEBUG
      printDebug("Deflated size: %ld %08X\n", ftell(InputFp) - startoffset);
#endif

      printInfo("Extracted '%s'\n", extractFilePath);

      extractCount++;
    }

    printInfo("Extracted %d files.\n", extractCount);
  }

  // Verify Raw
  else
  if (operation == OP_RAW_VERIFY) {
    uint32_t verifyCount = 0;

    // Start inflating and verifying files
    while (ftell(InputFp) < exeResult.fileSize) {
#ifdef REWISE_DEBUG
      long startoffset = ftell(InputFp);
#endif
      status = inflateFile(InputFp, NULL, MAX_FILESIZE_RAW, NULL);
      if (status == REWISE_PK_END) {
        status = REWISE_OK;
        break; // end of LocalFileHeaders
      }
      else
      if (status != REWISE_OK && status < REWISE_ERROR_END) {
        printError("Failed to verify index '%d'.\n", verifyCount);
        goto mainEnd;
      }
#ifdef REWISE_DEBUG
      printDebug("Deflated size: %ld %08X\n", ftell(InputFp) - startoffset);
#endif
      printInfo("CRC32 success for index '%d'\n", verifyCount);
      verifyCount++;
    }
    printInfo("Verified %d files.\n", verifyCount);
  }

  // Operations that extract and parse the WiseScript.bin
  else {
    char tmpFileScript[PATH_MAX];

    // Skip WiseColors.dib
    if (fseek(InputFp, overlayHeader.deflatedDib, SEEK_CUR) != 0) {
      printError("Failed to skip 'WiseColors.dib'.\n");
      status = REWISE_ERROR_FILE_SEEK;
      goto mainEnd;
    }

    // Create filepath for WiseScript.bin
    status = preparePath(tmpPath, "WiseScript.bin", tmpFileScript);
    if (status != REWISE_OK) {
      printError("Failed to create filepath for WiseScript.bin.\n");
      goto mainEnd;
    }
    // Extract WiseScript.bin
    if (optNoExtract == false) {
      status = inflateFile(InputFp, tmpFileScript,
                           overlayHeader.inflatedSizeWiseScript, NULL);
      if (status != REWISE_OK && status < REWISE_ERROR_END) {
        printError("Failed to extract '%s'.\n", tmpFileScript);
        goto mainEnd;
      }
    }

    // Open WiseScript.bin
    // check if file exists
    if (access(tmpFileScript, F_OK) != 0) {
      printError("'%s' not found\n", tmpFileScript);
      printError("errno: %s\n", strerror(errno));
      if (optNoExtract == true) {
        status = REWISE_ERROR_ARGS;
      }
      else {
        status = REWISE_ERROR_SYSTEM_IO;
      }
      goto mainEnd;
    }

    // open the file
    FILE *wsfp = fopen(tmpFileScript, "rb");

    // failed to open the file
    if (wsfp == NULL) {
      printError("failed to open file '%s'\n", tmpFileScript);
      printError("errno: %s\n", strerror(errno));
      status = REWISE_ERROR_SYSTEM_IO;
      goto mainEnd;
    }

    // Gather initial info
    WiseScriptParsedInfo parsedInfo;
    initWiseScriptParsedInfo(&parsedInfo);

    WiseScriptCallbacks callbacks;
    initWiseScriptCallbacks(&callbacks);

#ifdef REWISE_DEBUG
    // SCRIPT_DEBUG
    if (operation == OP_SCRIPT_DEBUG) {
      // read and process header
      WISESCRIPT_PARSED_INFO = &parsedInfo;
      callbacks.cb_header = updateParsedInfoHeader;
      callbacks.cb_languages = updateParsedInfoLanguages;

      if ((status = parseWiseScriptHeader(wsfp, &callbacks)) != REWISE_OK)
      {
        printError("wiseScriptGetParsedInfo failed\n");
        goto wiseScriptEnd;
      }

      parsedInfo.headerSize = ftell(wsfp);
      if (fseek(wsfp, 0, SEEK_SET) != 0) {
        status = REWISE_ERROR_FILE_SEEK;
        goto wiseScriptEnd;
      }

      if ((status = wiseScriptDebugPrint(wsfp, &parsedInfo)) != REWISE_OK)
      {
        printError("Debug print WiseScript failed.\n");
        goto wiseScriptEnd;
      }
      goto wiseScriptEnd;
    }
#endif

    if ((status = wiseScriptGetParsedInfo(wsfp, &parsedInfo)) != REWISE_OK)
    {
      goto wiseScriptEnd;
    }

    // ---------------------------------------------------------------
    // Determine the deflated file data offset inside WiseScript.bin,
    // this needs to be added to the inflateStart we got for files
    // from WiseScript to get to the real deflate data start offset in
    // the PE file.
    if (!IsPkFormat) {
      // Subtracting 'parsedInfo.largestFileDeflateEnd' from
      // 'overlayHeader.eof' or 'filesize' does the same thing on all
      // installers tested (777 at time of writing).
      // NOTE: Did not check multi file/disc installers.

      //ScriptDeflateOffset = overlayHeader.eof - parsedInfo.fileDeflateOffset;
      ScriptDeflateOffset = exeResult.fileSize - parsedInfo.largestFileDeflateEnd;
    }
    // PK zip installers
    else {
      long cdirSize;
      if ((status = findCentralDirectorySize(InputFp, &cdirSize)) != REWISE_OK) {
        printError("Failed to find PK central directory size\n");
        goto wiseScriptEnd;
      }

      ScriptDeflateOffset = exeResult.fileSize - (parsedInfo.largestFileDeflateEnd + cdirSize);
    }
    printDebug("scriptDeflateOffset: %ld (0x%08X)\n", ScriptDeflateOffset, ScriptDeflateOffset);
    // ---------------------------------------------------------------

    // Seek back to after the header + text (start of opcodes + data)
    if (fseek(wsfp, parsedInfo.headerSize, SEEK_SET) != 0) {
      printDebug("Failed to seek past header\n");
      status = REWISE_ERROR_SYSTEM_IO;
      goto wiseScriptEnd;
    }

    // Set global to be used by callbacks
    Languages = &parsedInfo.languages;

    if (optFilterLang) {
      if (FilterLang > (Languages->count - 1)) {
        printError("Language index %d not found.\n", FilterLang);
        status = REWISE_ERROR_ARGS;
        goto wiseScriptEnd;
      }
      callbacks.cb_languageChanged = updateSkipFileByLanguage;
    }

    if (optFilterComp) {
      bool found = false;
      for (uint8_t i=0; i < parsedInfo.components.count; ++i) {
        if (strncmp(FilterComponents, parsedInfo.components.strings[i].string,
            WISESCRIPT_COMP_MAX) == 0)
        {
          found = true;
          break;
        }
      }

      if (!found) {
        printError("Given components name \"%s\" not found.\n", FilterComponents);
        status = REWISE_ERROR_ARGS;
        goto wiseScriptEnd;
      }
      callbacks.cb_componentsChanged = updateSkipFileByComponents;
    }

    // If any file filter is enabled we need to again parse the script
    // with wiseScriptGetParsedInfo to get the new total inflated size.
    if (callbacks.cb_languageChanged ||
        callbacks.cb_componentsChanged ||
        FileFilters.count)
    {
      status = wiseScriptGetFilteredInflationSize(wsfp, &parsedInfo,
                                                  &callbacks);
      if (status != REWISE_OK) {
        goto wiseScriptEnd;
      }

      // Seek back to after the header + text (start of opcodes + data)
      if (fseek(wsfp, parsedInfo.headerSize, SEEK_SET) != 0) {
        printDebug("Failed to seek past header\n");
        status = REWISE_ERROR_FILE_SEEK;
        goto wiseScriptEnd;
      }
    }

    // LIST
    if (operation == OP_LIST) {
      uint8_t langPad;
      uint8_t compPad;
      if (optExtended) {
        printf("Languages:\n");
        for (uint8_t i=0; i < Languages->count; ++i) {
          if (Languages->strings[i].printlen > LargestLangStrSize) {
            LargestLangStrSize = Languages->strings[i].printlen;
          }
          printf(" (%d) '%s'\n", i, Languages->strings[i].string);
        }
        printf("\n");

        // pad "LANG" to LargestLangStrSize
        if (LargestLangStrSize < 4) {
          LargestLangStrSize = 4;
          langPad = 4 - LargestLangStrSize;
        }
        else {
          langPad = LargestLangStrSize - 4;
        }

        printf("Components:\n");
        for (uint8_t i=0; i < parsedInfo.components.count; ++i) {
          if (parsedInfo.components.strings[i].printlen > LargestCompStrSize)
          {
            LargestCompStrSize = parsedInfo.components.strings[i].printlen;
          }
          printf(" '%s'\n", parsedInfo.components.strings[i].string);
        }
        printf("\n");

        // pad "COMP" to LargestCompStrSize
        if (LargestCompStrSize < 4) {
          LargestCompStrSize = 4;
          compPad = 4 - LargestCompStrSize;
        }
        else {
          compPad = LargestCompStrSize - 4;
        }

        callbacks.cb_0x00 = printFileExtended;

        printf("Files:\n\n");
        printf("    FILESIZE FILEDATE   FILETIME LANG%*s COMP%*s FILEPATH\n", langPad, "", compPad, "");
        printf("------------ ---------- -------- ----%*s ----%*s ----------------------------\n", langPad, "", compPad, "");
      }
      else {
        callbacks.cb_0x00 = printFile;
        printf("    FILESIZE FILEDATE   FILETIME FILEPATH\n");
        printf("------------ ---------- -------- ----------------------------\n");
      }

      status = parseWiseScript(wsfp, &parsedInfo, &callbacks);
      if (status != REWISE_OK) {
        goto wiseScriptEnd;
      }

      if (optExtended) {
        printf("------------ ---------- -------- ----%*s ----%*s ----------------------------\n", langPad, "", compPad, "");
      }
      else {
        printf("------------ ---------- -------- ----------------------------\n");
      }
      printf("Total size: ");
      printPrettySize(parsedInfo.inflatedSize0x00);
      printf(" (%zu bytes)\n", parsedInfo.inflatedSize0x00);
    }
    // EXTRACT
    else
    if (operation == OP_EXTRACT) {
      // Check if there is enough free disk space
      unsigned long outputFreeDiskSpace = getFreeDiskSpace(OutputPath);
      // Failed to determine free disk space.
      if (outputFreeDiskSpace == 0) {
        status = REWISE_ERROR_SYSTEM_IO;
        goto wiseScriptEnd;
      }
      if (outputFreeDiskSpace <= parsedInfo.inflatedSize0x00) {
        printError("Not enough free disk space at '%s'. Required: %ld Left: "
                   "%ld\n", OutputPath, parsedInfo.inflatedSize0x00,
                   outputFreeDiskSpace);
        status = REWISE_ERROR_SYSTEM_IO;
        goto wiseScriptEnd;
      }

      // Start inflating and outputting files
      callbacks.cb_0x00 = &cb_extractFile;
      status = parseWiseScript(wsfp, &parsedInfo, &callbacks);

      // Something went wrong
      if (status != REWISE_OK) {
        goto wiseScriptEnd;
      }
    }
    else
    if (operation == OP_VERIFY) {
      callbacks.cb_0x00 = &cb_noExtractFile;
      status = parseWiseScript(wsfp, &parsedInfo, &callbacks);
      if (status != REWISE_OK) {
        goto wiseScriptEnd;
      }
      printInfo("All looks good!\n");
    }

wiseScriptEnd:

    fclose(wsfp);

    // remove tmp files
    if (optPreserveTmp == false && optNoExtract == false) {
      if (remove(tmpFileScript) != 0) {
        printError("Failed to remove '%s'. Errno: %s\n", tmpFileScript,
                   strerror(errno));
        status = REWISE_ERROR_SYSTEM_IO;
      }
    }

    if (status != REWISE_OK) {
      // flag that the error occured with the WiseScript.bin
      status |= REWISE_ERROR_WISESCRIPT;
      printError("Parsing WiseScript failed.\n");
    }

    freeWiseScriptParsedInfo(&parsedInfo);
  }

mainEnd:

  // Cleanup
  fclose(InputFp);
  freeWildcardFilters(&FileFilters);

  return status;
}
