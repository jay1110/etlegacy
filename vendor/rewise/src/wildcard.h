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


#ifndef H_REWISE_WILDCARD
#define H_REWISE_WILDCARD

#include "errors.h"

// How many segments a wildcard string may contain. 'MAINDIR/*' has
// two segments, 'MAINDIR/main/*' also has two segments. 'MAINDIR/*.pk3'
// has three segments. So how many elements are there when we split the
// wildcard string on '*' (including the split character(s))?
#define MAX_WILDCARD_SEGS 6


// This will virtual split the wildcard string into segments so we can
// use it more easy to match. The split character is the special match
// all character '*', that char will also count as a segment. The
// segment offset will be set to -2 for the match all char, to -1 when
// unset/unused and to the offset inside the wildcard string of that
// segment otherwise.
typedef struct {
  char *string; // the input filter string
  int offsets[MAX_WILDCARD_SEGS]; // segment offset
  int lengths[MAX_WILDCARD_SEGS]; // segment length
  int count; // segment count
  int index; // last index
} WildcardFilter;


// Container so multiple wildcard filters can be set.
typedef struct {
  WildcardFilter *filters;
  int count;
} WildcardFilters;


void initWildcardFilters(WildcardFilters *const filters);
void freeWildcardFilters(WildcardFilters *const filters);

REWiseStatus addWildcardFilter(WildcardFilters *const filters,
                               const char *const fmt);
REWiseStatus addWildcardFilterFromFile(WildcardFilters *const filters,
                                       const char *const filepath);
int wildcardMatch(const WildcardFilter *const filter,
                  const char *const str);
int matchWildcardFilters(const WildcardFilters *const filters,
                         const char *const string);

#endif
