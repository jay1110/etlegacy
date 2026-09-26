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
#include <stdio.h>
#include <string.h>
#include <stdlib.h> // free

#include "wildcard.h"
#include "print.h"
#include "wisescript.h" // WIN_PATH_CONVERT_MAX

// This is used to filter files by wildcard strings.
// The special match all character is '*', it may be escaped with '\*'.
//
// Examples:
//
//   MAINDIR/*
//   MAINDIR/*.pk3
//   MAINDIR/some*thing/*.pk3
//   MAINDIR/main/mp_pakmaps4.pk3   <-- to match specific files, no
//                                      wildcard needed.

void initWildcardFilter(WildcardFilter *const filter)
{
  filter->string = NULL;
  filter->count = 0;
  filter->index = 0;

  for (int i=0; i < MAX_WILDCARD_SEGS; ++i) {
    filter->offsets[i] = -1;
    filter->lengths[i] = 0;
  }
}


void initWildcardFilters(WildcardFilters *const filters)
{

  filters->filters = NULL;
  filters->count = 0;
}


void freeWildcardFilter(WildcardFilter *const filter)
{
  if (filter->string != NULL) {
    free(filter->string);
    filter->string = NULL;
  }
}


void freeWildcardFilters(WildcardFilters *const filters)
{
  if (filters->filters != NULL) {
    for (int i=0; i < filters->count; ++i) {
      freeWildcardFilter(&filters->filters[i]);
    }
    free(filters->filters);
    filters->filters = NULL;
    filters->count = 0;
  }
}


/** @brief Parse the filter string and set the results to the given
 *         filter object so it can be used later to match strings.
 *
 *  @param filter  The filter object to update (the filter string
 *                 should be already set and may not be NULL). */
REWiseStatus parseWildcardFilter(WildcardFilter *const filter)
{
  char ch;
  int escape = 0;
  int star = 0;
  int length = 0;
  const char *fmt = filter->string;

  if (fmt[0] == '*') {
    filter->offsets[filter->index] = -2; // -2 == STAR
    star = 1;
  }
  else {
    filter->offsets[filter->index] = 0;
    length++;
  }
  filter->count++;

  for (int i=1; i <= strlen(fmt); ++i) {
    if (filter->count == MAX_WILDCARD_SEGS) {
      printError("Error to many segments in the wildcard filter '%s'.\n",
                 fmt);
      return REWISE_ERROR_ARGS;
    }

    ch = fmt[i];

    if (ch == '\\' && !escape) {
      escape = 1;
      length++;
      continue;
    }

    if (ch == '*' && !escape) {
      // set string length before the * (previous one)
      if (length) {
        filter->lengths[filter->index] = length; 
        length = 0;
      }
      filter->index++;
      filter->offsets[filter->index] = -2;
      filter->count++;
      star = 1;
    }
    else
    if (ch != 0) {
      if (star) {
        filter->count++;
        filter->index++;
        filter->offsets[filter->index] = i;
        star = 0;
      }
      if (escape) {
        escape = 0;
      }
      length++;
    }
    else
    if (length) { // \0 reached (end of string)
      filter->lengths[filter->index] = length;
      break;
    }
  }
  return REWISE_OK;
}


/** @brief Add/parse filter string to the given filters object.
 *
 *  @param filters  Where to add the parsed filter to. This should be
 *                  initialized.
 *  @param fmt      The filter string. */
REWiseStatus addWildcardFilter(WildcardFilters *const filters,
                               const char *const fmt)
{
  WildcardFilter *filter;
  WildcardFilter *newFilters;

  if (strlen(fmt) >= WIN_PATH_CONVERT_MAX) {
    printError("Input filter is too long! Max %d characters\n",
               WIN_PATH_CONVERT_MAX - 1);
    return REWISE_ERROR_ARGS;
  }

  newFilters = realloc(filters->filters,
                       sizeof(WildcardFilter) * (filters->count + 1));
  if (newFilters == NULL) {
    return REWISE_ERROR_SYSTEM_IO;
  }

  filter = &newFilters[filters->count];
  initWildcardFilter(filter);
  filters->filters = newFilters;
  filters->count++;

  if ((filter->string = strdup(fmt)) == NULL) {
    return REWISE_ERROR_SYSTEM_IO;
  }

  return parseWildcardFilter(filter);
}


/** @brief Add file filters from given input file.
 *
 *  @param filters   Where to add the filters to, this should be
 *                   initialized.
 *  @param filepath  Path to the filter file, there should be one
 *                   filter set per line. */
REWiseStatus addWildcardFilterFromFile(WildcardFilters *const filters,
                                       const char *const filepath)
{
  REWiseStatus status;
  size_t noRead, lineLength;
  char buff[WIN_PATH_CONVERT_MAX];
  FILE *fp;

  fp = fopen(filepath, "r");
  if (fp == NULL) {
    printError("Failed to open filters file '%s': %s\n", filepath,
               strerror(errno));
    return REWISE_ERROR_ARGS;
  }

  memset(buff, 0, WIN_PATH_CONVERT_MAX);
  while (1) {
    noRead = fread(buff, sizeof(char), WIN_PATH_CONVERT_MAX, fp);
    if (noRead < 2) {
      break;
    }

    lineLength = 0;
    for (size_t i=0; i < noRead; ++i) {
      char ch = buff[i];

// MSYS uses '\r\n' for line endings
#if defined(__CYGWIN__) || defined(_WIN32) || defined(_WIN64) || defined(REWISE_DEBUG_WIN)
      if (ch == '\r') {
        continue;
      }
#endif

      if (ch == '\n') {
        if (lineLength) {
          buff[lineLength] = 0;
          status = addWildcardFilter(filters, buff);
          if (status != REWISE_OK) {
            printError("Failed to add wildcard filter..\n");
            fclose(fp);
            return status;
          }
        }
        fseek(fp, -(noRead - lineLength - 1), SEEK_CUR);
        break;
      }

      lineLength++;
      if (lineLength == (WIN_PATH_CONVERT_MAX - 1)) {
        printError("Wildcard filter line too long (%d)\n", filters->count);
        fclose(fp);
        return REWISE_ERROR_ARGS;
      }
    }
  }

  fclose(fp);
  return 0;
}


/** @brief Test given string against given filter.
 *
 *  @param filter  Filter object
 *  @param str     The string to match
 *
 *  @returns       0 on match, 1 on no-match. */
int wildcardMatch(const WildcardFilter *const filter,
                  const char *const str)
{
  size_t len = strlen(str);
  int segIndex = 0;

  for (size_t i=0; i < len; ++i) {
    // check next segment
    if (filter->offsets[segIndex] == -2) {
      // no next segment (only '*' given)
      if (segIndex == filter->index) {
        return 0; // match
      }

      int nextSegOffset = filter->offsets[segIndex + 1];
      if (strncmp(str + i,
        filter->string + nextSegOffset,
        filter->lengths[segIndex + 1]) != 0)
      {
        continue; // try at next char again ..
      }
      else {
        // next segment (after the *)
        segIndex += 1; // yes inc here before
        i += filter->lengths[segIndex] - 1;
        segIndex += 1;
      }
    }
    else {
      int nextSegOffset = filter->offsets[segIndex];
      if (strncmp(str + i,
        filter->string + nextSegOffset,
        filter->lengths[segIndex]) != 0)
      {
        return 1; // nope no match
      }
      i += filter->lengths[segIndex] - 1;
      segIndex += 1;
    }

    if (segIndex > filter->count) {
      return 1;
    }
  }

  if ((segIndex - 1) != filter->index) {
    return 1; // does not match
  }

  return 0; // match
}


/** @brief Test string against all set filters.
 *
 *  @param filters  The filters
 *  @param string   The string to check that it matches or not
 *
 *  @returns        0 when the string matches, 1 when no match found. */
int matchWildcardFilters(const WildcardFilters *const filters,
                         const char *const string)
{
  for (int i=0; i < filters->count; ++i) {
    const WildcardFilter *const filter = &filters->filters[i];
    if (wildcardMatch(filter, string) == 0) {
      return 0; // match
    }
  }
  return 1; // no match found
}
