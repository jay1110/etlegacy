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

#include <errno.h>

#include "reader.h"
#include "CP1252.h"


void initSafeCString(SafeCString *const string)
{
  string->string = NULL;
  string->strlen = 0;
  string->printlen = 0;
}


void freeSafeCString(SafeCString *const string)
{
  if (string->string != NULL) {
    free(string->string);
    string->string = NULL;
  }
  string->strlen = 0;
  string->printlen = 0;
}


void initSafeCStrings(SafeCStrings *const strings)
{
  strings->count = 0;
  strings->strings = NULL;
}


void freeSafeCStrings(SafeCStrings *const strings)
{
  if (strings->strings != NULL) {

    for (uint32_t i = 0; i < strings->count; i++) {
      freeSafeCString(&strings->strings[i]);
    }

    free(strings->strings);
    strings->count = 0;
    strings->strings = NULL;
  }
}


REWiseStatus readBytesInto(FILE *const fp, unsigned char *const dest,
                           const uint32_t size)
{
  int ch;
  uint32_t chNo = 0;

  do {
    ch = fgetc(fp);
    dest[chNo] = (unsigned char)ch;
    chNo++;
  } while (ch != EOF && size != chNo);

  if (ch == EOF) {
    return REWISE_ERROR_FILE_EOF;
  }

  return REWISE_OK;
}


REWiseStatus readUInt32(FILE *const fp, unsigned int *const dest)
{
  unsigned char buff[4];
  REWiseStatus status;

  // read 4 bytes into the buffer
  status = readBytesInto(fp, buff, 4);

  // failed to read 4 bytes into the buffer
  if (status != REWISE_OK) {
    return status;
  }

  *dest = *(unsigned int*)buff;
  return REWISE_OK;
}


REWiseStatus readUInt16(FILE *const fp, uint16_t *const dest)
{
  REWiseStatus status;
  unsigned char buff[2];

  // read 2 bytes into the buffer
  status = readBytesInto(fp, buff, 2);

  // failed to read 2 bytes into the buffer
  if (status != REWISE_OK) {
    return status;
  }

  *dest = buff[0] + (buff[1] << 8);
  return REWISE_OK;
}


/** @brief Get the size of a C string in file.
 *
 *  This will not seek back and can be used to skip the zero terminated
 *  string.
 *
 *  @param fp    Input file pointer.
 *  @param size  The size including \0 of the string will be set to this.
 *  @param max   Maximum string length.
 */
REWiseStatus readCStringSize(FILE *const fp, int *const size,
                             const int max)
{
  int ch;
  int stringLength = 0;

  // determine string length
  do {
    if (stringLength == max) {
      return REWISE_ERROR_VALUE;
    }
    ch = fgetc(fp);
    if (ch == EOF) {
      return REWISE_ERROR_FILE_EOF;
    }
    stringLength++;
  } while (ch != 0x00);

  *size = stringLength;
  return 0;
}


/** @brief Get the size of a C string in file, same as
 *         `readCStringSize()` but also sets the print size and
 *         size after escaping non printable bytes / converting to UTF8
 *         (see `readSafeCString()` for more comments on this).
 *
 *  @param fp         Input file pointer.
 *  @param size       The size (as in file) including \0 of the string
 *                    will be set to this.
 *  @param safeSize   The new size (after escaping/converting) will be
 *                    set to this.
 *  @param printSize  The amount of characters when printed will be set
 *                    to this.
 *  @param max        Maximum amount of bytes that it may read.
 */
REWiseStatus readSafeCStringSize(FILE *const fp, int *const size,
                                 int *const safeSize,
                                 int *const printSize,
                                 const int max)
{
  int ich;
  unsigned char ch;
  int stringLength = 0; // amount of bytes read
  int safeLength = 0; // amount of bytes after escaping/converting
  int printLength = 0;

  ch = 0xFF;

  while (ch != 0x00) {
    if (stringLength == max) {
      printf("reader: max exceeded\n");
      return REWISE_ERROR_VALUE;
    }

    ich = fgetc(fp);
    if (ich == EOF) {
      return REWISE_ERROR_FILE_EOF;
    }

    ch = (unsigned char)ich;
    if (ch == 0x00) {
      stringLength++;
      safeLength++;
      break;
    }
    else
    if (ch < 0x20) {
      // print this char as hex \x19 for example
      safeLength += 3;
      printLength += 3;
    }
    else
    if (ch > 0x7E) {
      if (isPrintableCP1252Char(ch)) {
        // convert CP1252 char to UTF-8, this will occupy 2 bytes.
        safeLength++;
      }
      else {
        // print this char as hex \x19 for example, this will occupy 4
        // bytes.
        safeLength += 3;
        printLength += 3;
      }
    }

    stringLength++;
    safeLength++;
    printLength++;
  }

  *size = stringLength;
  *safeSize = safeLength;
  *printSize = printLength;
  return 0;
}


/** @brief Read a zero terminated string and replace all non printable
 *         characters with something printable. Printable CP1252 will
 *         be converted to UTF-8, others will be escaped (for example
 *         0x0D will be replaced with '_x0D').
 *
 *         NOTE: it has been tried to error on non printable CP1252
 *               characters but it broke some installers so apparent
 *               those characters are allowed, that's why we escape
 *               them.
 *
 *         You are responsible to call `freeSafeCString()` when this
 *         returns `REWISE_OK`.
 *
 *  @param fp         Input file pointer.
 *  @param size       The size (as in file) including \0 of the string
 *                    will be set to this.
 *  @param safeSize   The new size (after escaping/converting) will be
 *                    set to this.
 *  @param printSize  The amount of characters when printed will be set
 *                    to this.
 *  @param max        Maximum amount of bytes that it may read.
 */
REWiseStatus readSafeCString(FILE *const fp, SafeCString *const dest,
                             const int max)
{
  REWiseStatus status;
  int size, safeSize, printSize;
  char *string = NULL;
  unsigned char ch;
  int chNo;
  long startOffset = ftell(fp);

  // get the string size in file and the CP1252 string converted
  // to UTF-8 size.
  status = readSafeCStringSize(fp, &size, &safeSize, &printSize, max);
  if (status != REWISE_OK) {
    return status;
  }

  // empty string
  if (size == 1) {
    return REWISE_OK;
  }

  fseek(fp, startOffset, SEEK_SET);

  // allocate string
  string = (char*)malloc(sizeof(char) * safeSize);
  if (string == NULL) {
    return REWISE_ERROR_SYSTEM_IO; // failed to allocate mem
  }
  string[safeSize - 1] = 0x00;

  // read the string to the new allocated space
  chNo = 0;
  do {
    ch = fgetc(fp);

    // ascii char
    if (ch == 0x00 || (ch >= 0x20 && ch < 0x7F)) {
      string[chNo] = ch;
    }
    else
    // convert CP1252 to UTF-8
    if (isPrintableCP1252Char(ch)) {
      const unsigned char *utf8Ch = CP1252_to_UTF8(ch);
      string[chNo] = utf8Ch[0];
      chNo++;
      string[chNo] = utf8Ch[1];
    }
    // escape as hex value
    else {
      // 5 'cause snprintf likes to write the \0
      // there always is room for a \0 so NP.
      // NOTE that we cannot use '\' in/as the escape because of win
      // filepaths, so we cannot use '\x%02X'.
      if (snprintf(string + chNo, sizeof(char) * 5, "_x%02X", ch) != 4)
      {
        free(string);
        return REWISE_ERROR_SYSTEM_IO;
      }
      chNo += 3;
    }
    chNo++;
  } while (ch != 0x00);

  dest->string = string;
  dest->strlen = safeSize;
  dest->printlen = printSize;

  return REWISE_OK;
}


REWiseStatus readSafeCStrings(FILE *const fp, const uint32_t count,
                              SafeCStrings *const dest, const int max)
{
  REWiseStatus status;
  uint32_t i;

  // allocate string pointers
  dest->strings = malloc(sizeof(SafeCString) * count);
  if (dest->strings == NULL) {
    return REWISE_ERROR_SYSTEM_IO; // out of mem
  }

  // init the new SafeCString's
  for (i = 0; i < count; i++) {
    initSafeCString(&dest->strings[i]);
  }

  // Read the text strings
  for (i = 0; i < count; i++) {
    status = readSafeCString(fp, &dest->strings[i], max);
    if (status != REWISE_OK) {
      freeSafeCStrings(dest);
      return status;
    }
    dest->count++;
  }

  return REWISE_OK;
}


// Debug build reads everything into mem
#ifdef REWISE_DEBUG

REWiseStatus optReadBytesInto(FILE *const fp, unsigned char *const dest,
                              const uint32_t size)
{
  return readBytesInto(fp, dest, size);
}


REWiseStatus optReadUInt32(FILE *const fp, unsigned int *const dest)
{
  return readUInt32(fp, dest);
}


REWiseStatus optReadCString(FILE *const fp, SafeCString *const dest,
                            const int max)
{
  return readSafeCString(fp, dest, max);
}


REWiseStatus optReadCStrings(FILE *const fp, const uint32_t count,
                             SafeCStrings *const dest, const int max)
{
  return readSafeCStrings(fp, count, dest, max);
}


// Normal build, skip optional
#else


REWiseStatus skipBytes(FILE *const fp, const int amount)
{
  if (fseek(fp, amount, SEEK_CUR) != 0) {
    return REWISE_ERROR_FILE_SEEK;
  }
  return REWISE_OK;
}


REWiseStatus optReadBytesInto(FILE *const fp, unsigned char *const dest,
                              const uint32_t size)
{
  return skipBytes(fp, size);
}


REWiseStatus optReadUInt32(FILE *const fp, unsigned int *const dest)
{
  return skipBytes(fp, sizeof(uint32_t));
}


REWiseStatus optReadCString(FILE *const fp, SafeCString *const dest,
                            const int max)
{
  int stringLength;
  return readCStringSize(fp, &stringLength, max);
}


REWiseStatus optReadCStrings(FILE *const fp, const uint32_t count,
                             SafeCStrings *const dest, const int max)
{
  REWiseStatus status;
  int stringLength;

  // Skip the text strings
  for (uint32_t i = 0; i < count; i++) {
    status = readCStringSize(fp, &stringLength, max);
    if (status != REWISE_OK) {
      return status;
    }
  }
  return REWISE_OK;
}

#endif  // Normal build
