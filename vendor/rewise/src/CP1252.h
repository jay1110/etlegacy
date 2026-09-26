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
#ifndef H_REWISE_CP1252
#define H_REWISE_CP1252

#include <stdbool.h>

// The first 126 bytes are the same as ASCII and UTF-8, also don't
// include DEL so 126+1, (255 - 127 = 128).
// To get to the right index, subtract 128 from the value
//
// not allowed:
// 0x00 -> 0x1F
// 0x7F (DEL)
// 0x81 unused in CP1252
// 0x8D unused in CP1252
// 0x8F unused in CP1252
// 0x90 unused in CP1252
// 0x9D unused in CP1252
//
// Each value is 2 bytes long, but these are strings so the size is
// 3 because \0 terminator.
static const unsigned char CP1252_UTF8_Map[128][3] = {
  "\u20AC", ""      , "\u201A", "\u0192",
  "\u201E", "\u2026", "\u2020", "\u2021",
  "\u02C6", "\u2030", "\u0160", "\u2039",
  "\u0152", ""      , "\u017D", "",
  ""      , "\u2018", "\u2019", "\u201C",
  "\u201D", "\u2022", "\u2013", "\u2014",
  "\u02DC", "\u2122", "\u0161", "\u203A",
  "\u0153", ""      , "\u017E", "\u0178",

  "\u00A0", "\u00A1", "\u00A2", "\u00A3",
  "\u00A4", "\u00A5", "\u00A6", "\u00A7",
  "\u00A8", "\u00A9", "\u00AA", "\u00AB",
  "\u00AC", "\u00AD", "\u00AE", "\u00AF",

  "\u00B0", "\u00B1", "\u00B2", "\u00B3",
  "\u00B4", "\u00B5", "\u00B6", "\u00B7",
  "\u00B8", "\u00B9", "\u00BA", "\u00BB",
  "\u00BC", "\u00BD", "\u00BE", "\u00BF",

  "\u00C0", "\u00C1", "\u00C2", "\u00C3",
  "\u00C4", "\u00C5", "\u00C6", "\u00C7",
  "\u00C8", "\u00C9", "\u00CA", "\u00CB",
  "\u00CC", "\u00CD", "\u00CE", "\u00CF",

  "\u00D0", "\u00D1", "\u00D2", "\u00D3",
  "\u00D4", "\u00D5", "\u00D6", "\u00D7",
  "\u00D8", "\u00D9", "\u00DA", "\u00DB",
  "\u00DC", "\u00DD", "\u00DE", "\u00DF",

  "\u00E0", "\u00E1", "\u00E2", "\u00E3",
  "\u00E4", "\u00E5", "\u00E6", "\u00E7",
  "\u00E8", "\u00E9", "\u00EA", "\u00EB",
  "\u00EC", "\u00ED", "\u00EE", "\u00EF",

  "\u00F0", "\u00F1", "\u00F2", "\u00F3",
  "\u00F4", "\u00F5", "\u00F6", "\u00F7",
  "\u00F8", "\u00F9", "\u00FA", "\u00FB",
  "\u00FC", "\u00FD", "\u00FE", "\u00FF"
};

bool isPrintableCP1252Char(unsigned char);
const unsigned char *CP1252_to_UTF8(unsigned char);

#endif
