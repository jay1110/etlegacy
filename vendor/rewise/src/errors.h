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

#ifndef H_REWISE_ERRORS
#define H_REWISE_ERRORS


// Exit codes (indicators of what did go wrong, where it did go wrong
// should be printed to stderr).
typedef enum {
  // Everything went OK
  REWISE_OK                   = 0,

  /* General errors */
  // Reading/writing to file or memory failed (out of disk space/memory,
  // not the right permissions or a corrupt system)
  REWISE_ERROR_SYSTEM_IO      = 1,
  // User supplied faulty input arguments
  REWISE_ERROR_ARGS           = 2,
  // Something occurred internal that should never happen.. please
  // report when observed.
  REWISE_ERROR_INTERNAL       = 3,
  // File reading errors
  REWISE_ERROR_FILE_EOF       = 4,
  REWISE_ERROR_FILE_SEEK      = 5,
  REWISE_ERROR_VALUE          = 6, // unexpected value

  /* Invalid file data */
  // Failed to parse the exe (PE/NE).
  REWISE_ERROR_INVALID_PENE   = 8,
  REWISE_ERROR_NOT_PENE       = 9,
  // Not a (supported) Wise installer or the installer is corrupt.
  REWISE_ERROR_NOT_WISE       = 10,

  /* Inflate errors */
  // Inflate failed
  REWISE_ERROR_INFLATE        = 16,
  // Error with invalid PK header signature
  REWISE_ERROR_PK_HEADER      = 17,
  // A CRC32 mismatched
  REWISE_ERROR_CRC32_MISMATCH = 18,

  /* Specific errors for debugging */
  REWISE_ERROR_BRANCHING      = 24,

  /* Special flag */
  // This one is a flag, when set it indicates that the error occured
  // while parsing the WiseScript.bin So this flag is set after the
  // WiseScript.bin has been extracted and opened for reading.
  REWISE_ERROR_WISESCRIPT     = 64,

  /* Do not use 65 - 127 */

  /* 128 and above (for exit codes) are reserved to catch segfaults */
  REWISE_ERROR_END            = 127,

  /* Internal use only (not used as exit code) */
  REWISE_PK_END               = 128

} REWiseStatus;

#endif
