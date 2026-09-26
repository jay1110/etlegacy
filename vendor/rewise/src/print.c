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
#include <stdarg.h>

#include "print.h"


static int PrintFlags = (PRINT_INFO | PRINT_WARNING | PRINT_ERROR);


void setPrintFlags(const int flags)
{
  PrintFlags = flags;
}

void setPrintFlag(const int flag)
{
  PrintFlags |= flag;
}

void unsetPrintFlag(const int flag)
{
  PrintFlags &= ~flag;
}


void printInfo(const char *const fmt, ...)
{
  if (PrintFlags & PRINT_INFO) {
    va_list args;
    va_start(args, fmt);
    fprintf(stdout, "INFO: ");
    vfprintf(stdout, fmt, args);
    va_end(args);
  }
}


void printWarning(const char *const fmt, ...)
{
  if (PrintFlags & PRINT_WARNING) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "WARNING: ");
    vfprintf(stderr, fmt, args);
    va_end(args);
  }
}


void printError(const char *const fmt, ...)
{
  if (PrintFlags & PRINT_ERROR) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "ERROR: ");
    vfprintf(stderr, fmt, args);
    va_end(args);
  }
}


#ifdef REWISE_DEBUG

int getPrintFlags(void)
{
  return PrintFlags;
}


void printDebug(const char *const fmt, ...)
{
  if (PrintFlags & PRINT_DEBUG) {
    fprintf(stderr, "DEBUG: ");
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
  }
}


void printHex(const unsigned char *const value, const size_t size,
              const char *const end, FILE *const out)
{
  for (size_t i=0; i < size; i++) {
    fprintf(out, "%02X", value[i]);
  }
  if (end != NULL) {
    fprintf(out, end);
  }
}


void printBin(void *value, size_t size, const char *const end,
              FILE *const out)
{
  char *values =  (char *)value;
  for (size_t i=0; i < size; ++i) {
    char value = values[i];
    for (int x=7; x >= 0; --x) {
      if (value & (1 << (x))) {
        fprintf(out, "1");
      }
      else {
        fprintf(out, "0");
      }
    }
    fprintf(out, " ");
  }
  if (end != NULL) {
    fprintf(out, end);
  }
}
#endif // REWISE_DEBUG
