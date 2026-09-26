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

#include "pkzip.h"
#include "print.h"
#include "reader.h"


REWiseStatus readPKSignature(FILE *const fp, uint32_t *const signature)
{
  REWiseStatus status;

  if ((status = readUInt32(fp, signature)) != REWISE_OK) {
    printError("Failed to read PK signature\n");
    return status;
  }
  printDebug("PK signature: %08X\n", *signature);

  switch (*signature) {
    case PK_SIG_LOCAL_FILE:
    case PK_SIG_CENTRAL_DIR:
    case PK_SIG_DATA_DESC:
    case PK_SIG_END_CENTRAL_DIR:
      return REWISE_OK;

    default:
      printError("PK signature unknown\n");
      return REWISE_ERROR_PK_HEADER;
  };
}


REWiseStatus readPKLocalFileHeader(FILE *const fp,
                                   PKLocalFileHeader *const fileHeader)
{
  REWiseStatus status;

  // Read required version
  if ((status = readUInt16(fp, &fileHeader->requiredVersion)) != REWISE_OK) {
    printError("Failed to read PK local file header requiredVersion\n");
    return status;
  }
  printDebug("PK fileHeader->requiredVersion: %04X\n", fileHeader->requiredVersion);

  // Read general purpose flags
  if ((status = readUInt16(fp, &fileHeader->gpFlags)) != REWISE_OK) {
    printError("Failed to read PK local file header gpFlags\n");
    return status;
  }
  printDebug("PK fileHeader->gpFlags: %04X\n", fileHeader->gpFlags);

  // Read compression method
  if ((status = readUInt16(fp, &fileHeader->compressionMethod)) != REWISE_OK) {
    printError("Failed to read PK local file header compressionMethod\n");
    return status;
  }
  printDebug("PK fileHeader->compressionMethod: %04X\n", fileHeader->compressionMethod);

  // Read last file modification time
  if ((status = readUInt16(fp, &fileHeader->fileModTime)) != REWISE_OK) {
    printError("Failed to read PK local file header fileModTime\n");
    return status;
  }
  printDebug("PK fileHeader->fileModTime: %04X\n", fileHeader->fileModTime);

  // Read last file modification date
  if ((status = readUInt16(fp, &fileHeader->fileModDate)) != REWISE_OK) {
    printError("Failed to read PK local file header fileModDate\n");
    return status;
  }
  printDebug("PK fileHeader->fileModDate: %04X\n", fileHeader->fileModDate);

  // Read CRC32
  if ((status = readUInt32(fp, &fileHeader->CRC32)) != REWISE_OK) {
    printError("Failed to read PK local file header CRC32\n");
    return status;
  }
  printDebug("PK fileHeader->CRC32: %08X\n", fileHeader->CRC32);

  // Read deflatedSize
  if ((status = readUInt32(fp, &fileHeader->deflatedSize)) != REWISE_OK) {
    printError("Failed to read PK local file header deflatedSize\n");
    return status;
  }
  printDebug("PK fileHeader->deflatedSize: %08X\n", fileHeader->deflatedSize);

  // Read inflatedSize
  if ((status = readUInt32(fp, &fileHeader->inflatedSize)) != REWISE_OK) {
    printError("Failed to read PK local file header inflatedSize\n");
    return status;
  }
  printDebug("PK fileHeader->inflatedSize: %08X\n", fileHeader->inflatedSize);

  // Read fileNameSize
  if ((status = readUInt16(fp, &fileHeader->fileNameSize)) != REWISE_OK) {
    printError("Failed to read PK local file header fileNameSize\n");
    return status;
  }
  printDebug("PK fileHeader->fileNameSize: %04X\n", fileHeader->fileNameSize);

  // Read extraFieldsSize
  if ((status = readUInt16(fp, &fileHeader->extraFieldsSize)) != REWISE_OK) {
    printError("Failed to read PK local file header extraFieldsSize\n");
    return status;
  }
  printDebug("PK fileHeader->extraFieldsSize: %04X\n", fileHeader->extraFieldsSize);

  // SKIP fileNameSize
  if (fseek(fp, fileHeader->fileNameSize, SEEK_CUR) != 0) {
    return REWISE_ERROR_FILE_SEEK;
  }
  // SKIP extraFieldsSize
  if (fseek(fp, fileHeader->extraFieldsSize, SEEK_CUR) != 0) {
    return REWISE_ERROR_FILE_SEEK;
  }

  return REWISE_OK;
}


// This should return the length from start of the central directory
// to the end of the central directory end.
REWiseStatus findCentralDirectorySize(FILE *const fp, long *const offset)
{
  REWiseStatus status;
  uint32_t signature;

  // Go to end of file minus the minimum size of PKCentralDirectoryEnd
  // plus the max comment size.
  if (fseek(fp, -(sizeof(PKCentralDirectoryEnd) + ((uint16_t) - 1)), SEEK_END) != 0) {
    return REWISE_ERROR_FILE_SEEK;
  }

  // Now we are going to read 4 bytes and compare it to the Central Dir
  // End signature until we found it.
  signature = 0;
  while (signature != PK_SIG_END_CENTRAL_DIR) {
    if (fseek(fp, -3, SEEK_CUR) != 0) {
      printError("Did not find PK Central Directory End signature.\n");
      return REWISE_ERROR_FILE_SEEK;
    }
    if ((status = readUInt32(fp, &signature)) != REWISE_OK) {
      return status;
    }
  }

  PKCentralDirectoryEnd cdirEnd;
  if (fread(&cdirEnd, sizeof(cdirEnd), 1, fp) != 1) {
    if (feof(fp)) {
      return REWISE_ERROR_FILE_EOF;
    }
    else {
      return REWISE_ERROR_SYSTEM_IO;
    }
  }

  *offset = (cdirEnd.centralDirSize +
             sizeof(PKCentralDirectoryEnd) +
             sizeof(uint32_t) + // PKCentralDirectoryEnd signature
             cdirEnd.commentLength) - 2; // FIXME -2 ????

  return REWISE_OK;
}
