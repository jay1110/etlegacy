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

#include "CP1252.h"


// NOTE: ch should be >= 128 else stuff will overflow
bool isPrintableCP1252Char(unsigned char ch)
{
  // NOTE: you have to check before calling this!
  /*if (ch < 0x20) {
    return false;
  }*/

  if (ch == 0x7F || ch == 0x81 || ch == 0x8D || ch == 0x8F ||
      ch == 0x90 || ch == 0x9D)
  {
    return false;
  }

  return true;
}


const unsigned char *CP1252_to_UTF8(unsigned char ch)
{
  return CP1252_UTF8_Map[ch - 128];
}
