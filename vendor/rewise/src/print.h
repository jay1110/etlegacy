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

#ifndef H_REWISE_PRINT
#define H_REWISE_PRINT


enum __PrintFlags {
  PRINT_SILENT  = 0,
  PRINT_INFO    = 1,
  PRINT_WARNING = 2,
  PRINT_ERROR   = 4
#ifdef REWISE_DEBUG
  ,PRINT_DEBUG  = 8
#endif
};


void setPrintFlags(const int flags);
void setPrintFlag(const int flag);
void unsetPrintFlag(const int flag);

void printInfo(const char *const fmt, ...);
void printWarning(const char *const fmt, ...);
void printError(const char *const fmt, ...);

#ifdef REWISE_DEBUG
int getPrintFlags(void);
void printDebug(const char *const fmt, ...);
void printHex(const unsigned char *const value, const size_t size,
              const char *const end, FILE *const out);
void printBin(void *value, size_t size, const char *const end,
              FILE *const out);
#else
# define printDebug(...)
#endif

#endif
