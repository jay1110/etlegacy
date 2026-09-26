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
#include <errno.h>

#include "exefile.h"
#include "print.h"


int getOverlayFromPe(FILE *const fp, ExeAnalyzeResult *const result)
{
  long overlayOffset;
  PeFileHeader peFileHeader;
  int err;

  if (fread(&peFileHeader, sizeof(PeFileHeader), 1, fp) != 1) {
    err = errno;
    printError("getOverlayFromPe failed to read PE File Header\n");
    return err;
  }

  printDebug("PE Build: %d\n", peFileHeader.timeDateStamp);

  // Skip optional header
  if (peFileHeader.optionalHeaderSize > 0) {
    if (fseek(fp, peFileHeader.optionalHeaderSize, SEEK_CUR) != 0) {
      err = errno;
      printError("getOverlayFromPe failed to skip over the optional "
                 "header.\n");
      printError("getOverlayFromPe errno: %s\n", strerror(errno));
      return err;
    }
  }

  // Read sections
  overlayOffset = 0l;
  for (uint32_t i=0; i< peFileHeader.numberOfSections; i++) {
    PeImageSectionHeader sectionHeader;
    if (fread(&sectionHeader, sizeof(PeImageSectionHeader), 1, fp) != 1) {
      err = errno;
      printError("getOverlayFromPe failed to read section header.\n");
      return err;
    }

    if ((sectionHeader.rawDataLocation +
         sectionHeader.rawDataSize) > overlayOffset)
    {
      overlayOffset = sectionHeader.rawDataLocation + sectionHeader.rawDataSize;
    }
  }

  result->overlayOffset = overlayOffset;

  return 0;
}


int getOverlayFromNe(FILE *const fp, ExeAnalyzeResult *const result)
{
  long overlayOffset;
  long beforeHeaderRead;
  NeFileHeader neFileHeader;
  int err;

  beforeHeaderRead = ftell(fp);

  if (fread(&neFileHeader, sizeof(NeFileHeader), 1, fp) != 1) {
    err = errno;
    printError("getOverlayFromNe failed to read NE File Header\n");
    return err;
  }

  printDebug("NE majorLinkerVersion        : %d\n", neFileHeader.majorLinkerVersion);
  printDebug("NE minorLinkerVersion        : %d\n", neFileHeader.minorLinkerVersion);
  printDebug("NE entryTableOffset          : 0x%08X\n", neFileHeader.entryTableOffset);
  printDebug("NE entryTableSize            : %d\n", neFileHeader.entryTableSize);
  printDebug("NE crc32                     : 0x%08X\n", neFileHeader.majorLinkerVersion);
  printDebug("NE flags                     : 0x%08X\n", neFileHeader.majorLinkerVersion);
  printDebug("NE autoDataSegIndex          : %d\n", neFileHeader.autoDataSegIndex);
  printDebug("NE heapSize                  : %d\n", neFileHeader.heapSize);
  printDebug("NE stackSize                 : %d\n", neFileHeader.stackSize);
  printDebug("NE entryPoint                : 0x%08X\n", neFileHeader.entryPoint);
  printDebug("NE stack                     : 0x%08X\n", neFileHeader.stack);
  printDebug("NE segCount                  : %d\n", neFileHeader.segCount);
  printDebug("NE moduleRefCount            : %d\n", neFileHeader.moduleRefCount);
  printDebug("NE nonResidentSize           : %d\n", neFileHeader.nonResidentSize);
  printDebug("NE segTableOffset            : %08X\n", neFileHeader.segTableOffset);
  printDebug("NE resourceTableOffset       : %08X\n", neFileHeader.resourceTableOffset);
  printDebug("NE residentNameTableOffset   : %08X\n", neFileHeader.residentNameTableOffset);
  printDebug("NE moduleRefTableOffset      : %08X\n", neFileHeader.moduleRefTableOffset);
  printDebug("NE importNameTableOffset     : %08X\n", neFileHeader.importNameTableOffset);
  printDebug("NE nonResidentNameTableOffset: %08X\n", neFileHeader.nonResidentNameTableOffset);
  printDebug("NE movableEntryCount         : %d\n", neFileHeader.movableEntryCount);
  printDebug("NE fileAlignmentShiftCount   : %d\n", neFileHeader.fileAlignmentShiftCount);
  printDebug("NE resourceSegmentCount      : %d\n", neFileHeader.resourceSegmentCount);
  printDebug("NE targetOs                  : %d\n", neFileHeader.targetOs);
  printDebug("NE gangloadOffset            : 0x%08X\n", neFileHeader.gangloadOffset);
  printDebug("NE gangloadSize              : 0x%08X\n", neFileHeader.gangloadSize);
  printDebug("NE minSwapSize               : %d\n", neFileHeader.minSwapSize);
  printDebug("NE expectedWinVersion        : %d\n\n", neFileHeader.expectedWinVersion);

  if (fseek(fp, neFileHeader.segTableOffset + beforeHeaderRead, SEEK_SET) != 0) {
    err = errno;
    printError("getOverlayFromNe failed to seek to segTableOffset\n");
    printError("getOverlayFromNe errno: %s\n", strerror(errno));
    return err;
  }

  printDebug("Segments Table\n");
  printDebug("--------------\n\n");
  overlayOffset = 0l;
  for (uint16_t i=0; i < neFileHeader.segCount; ++i) {
    NeSegmentEntry segEntry;
    if (fread(&segEntry, sizeof(NeSegmentEntry), 1, fp) != 1) {
      err = errno;
      printError("getOverlayFromNe failed to read segEntry\n");
      return err;
    }

    long o = (segEntry.sectorOffset << neFileHeader.fileAlignmentShiftCount)
              + segEntry.size;
    if (o > overlayOffset) {
      overlayOffset = o;
    }

    printDebug("NE segEntry.sectorOffset: 0x%08X\n", segEntry.sectorOffset);
    printDebug("NE segEntry.size        : 0x%08X\n", segEntry.size);
    printDebug("NE segEntry.flags       : 0x%08X\n", segEntry.flags);
    printDebug("NE segEntry.minAlloc    : 0x%08X\n", segEntry.minAlloc);
    printDebug("NE offset-after         : 0x%08X\n", o);
    printDebug("\n");
  }

  printDebug("Resource Table\n");
  printDebug("--------------\n\n");
  if (fseek(fp, neFileHeader.resourceTableOffset + beforeHeaderRead, SEEK_SET) != 0) {
    err = errno;
    printError("getOverlayFromNe failed to seek to resourceTableOffset\n");
    printError("getOverlayFromNe errno: %s\n", strerror(errno));
    return err;
  }
  // read shift alignment
  uint16_t resourceShift;
  if (fread(&resourceShift, sizeof(resourceShift), 1, fp) != 1) {
    err = errno;
    printError("getOverlayFromNe failed to read resourceShift\n");
    return err;
  }

  NeResourceTypeInfo resourceTypeInfo;
  NeResourceNameInfo resourceNameInfo;

  resourceTypeInfo.typeId = 1;
  while (resourceTypeInfo.typeId) {
    if (fread(&resourceTypeInfo, sizeof(resourceTypeInfo), 1, fp) != 1) {
      err = errno;
      printError("getOverlayFromNe failed to read resourceTypeInfo\n");
      return err;
    }

    if (resourceTypeInfo.typeId == 0) {
      break;
    }

    printDebug("NE resourceTypeInfo.typeId  : 0x%04X\n", resourceTypeInfo.typeId);
    printDebug("NE resourceTypeInfo.count   : 0x%04X\n", resourceTypeInfo.count);
    printDebug("NE resourceTypeInfo.reserved: 0x%04X\n\n", resourceTypeInfo.reserved);

    for (uint16_t i=0; i < resourceTypeInfo.count; ++i) {
      if (fread(&resourceNameInfo, sizeof(resourceNameInfo), 1, fp) != 1) {
        err = errno;
        printError("getOverlayFromNe failed to read resourceNameInfo\n");
        return err;
      }

      long o = (resourceNameInfo.shiftedOffset << resourceShift) + resourceNameInfo.size;
      if (o > overlayOffset) {
        overlayOffset = o;
      }

      printDebug("NE resourceNameInfo.shiftedOffset: 0x%04X\n", resourceNameInfo.shiftedOffset);
      printDebug("NE resourceNameInfo.size         : 0x%04X\n", resourceNameInfo.size);
      printDebug("NE resourceNameInfo.flags        : 0x%04X\n", resourceNameInfo.flags);
      printDebug("NE resourceNameInfo.id           : 0x%04X\n", resourceNameInfo.id);
      printDebug("NE resourceNameInfo.handle       : 0x%04X\n", resourceNameInfo.handle);
      printDebug("NE resourceNameInfo.usage        : 0x%04X\n--\n", resourceNameInfo.usage);
    }
  }

#ifdef REWISE_DEBUG
  printDebug("Resident Table\n");
  printDebug("--------------\n\n");
  if (fseek(fp, neFileHeader.residentNameTableOffset + beforeHeaderRead, SEEK_SET) != 0) {
    err = errno;
    printError("getOverlayFromNe failed to seek to residentNameTableOffset\n");
    printError("getOverlayFromNe errno: %s\n", strerror(errno));
    return err;
  }
  NeResidentNameEntry nameEntry;
  nameEntry.stringSize = 1;
  while (nameEntry.stringSize) {
    if (fread(&nameEntry.stringSize, sizeof(nameEntry.stringSize), 1, fp) != 1) {
      err = errno;
      printError("getOverlayFromNe failed to read nameEntry.stringSize\n");
      return err;
    }

    memset(nameEntry.string, 0, 256);

    if (nameEntry.stringSize) {
      int e = fread(nameEntry.string, sizeof(char) * nameEntry.stringSize , 1, fp);
      if (e != 1) {
        err = errno;
        printError("getOverlayFromNe failed to read nameEntry.stringSize (%d, %s)\n", e, strerror(errno));
        return err;
      }
    }

    if (fread(&nameEntry.entryTableIndex, sizeof(nameEntry.entryTableIndex), 1, fp) != 1) {
      err = errno;
      printError("getOverlayFromNe failed to read nameEntry.entryTableIndex\n");
      return err;
    }

    printDebug("NE nameEntry.stringSize     : %d\n", nameEntry.stringSize);
    printDebug("NE nameEntry.string         : %s\n", nameEntry.string);
    printDebug("NE nameEntry.entryTableIndex: %d\n", nameEntry.entryTableIndex);
    printDebug("\n");
  }

  if (neFileHeader.nonResidentSize) {
    printDebug("Non-Resident Table\n");
    printDebug("------------------\n\n");
    if (fseek(fp, neFileHeader.nonResidentNameTableOffset, SEEK_SET) != 0) {
      err = errno;
      printError("getOverlayFromNe failed to seek to nonResidentNameTableOffset\n");
      printError("getOverlayFromNe errno: %s\n", strerror(errno));
      return err;
    }
    nameEntry.stringSize = 1;
    while (nameEntry.stringSize) {
      if (fread(&nameEntry.stringSize, sizeof(nameEntry.stringSize), 1, fp) != 1) {
        err = errno;
        printError("getOverlayFromNe failed to read nameEntry.stringSize\n");
        return err;
      }

      memset(nameEntry.string, 0, 256);

      if (nameEntry.stringSize) {
        int e = fread(nameEntry.string, sizeof(char) * nameEntry.stringSize , 1, fp);
        if (e != 1) {
          err = errno;
          printError("getOverlayFromNe failed to read nameEntry.stringSize (%d, %s)\n", e, strerror(errno));
          return err;
        }
      }

      if (fread(&nameEntry.entryTableIndex, sizeof(nameEntry.entryTableIndex), 1, fp) != 1) {
        err = errno;
        printError("getOverlayFromNe failed to read nameEntry.entryTableIndex\n");
        return err;
      }

      printDebug("NE nameEntry.stringSize     : %d\n", nameEntry.stringSize);
      printDebug("NE nameEntry.string         : %s\n", nameEntry.string);
      printDebug("NE nameEntry.entryTableIndex: %d\n", nameEntry.entryTableIndex);
      printDebug("\n");
    }
  }
#endif // REWISE_DEBUG

  // TODO/FIXME what are those 705 bytes?
  result->overlayOffset = overlayOffset + 705;
  printDebug("NE overlay           : 0x%08X\n", overlayOffset);
  return 0;
}


void initExeAnalyzeResult(ExeAnalyzeResult *const result)
{
  result->signatures = 0;
  result->overlayOffset = 0;
  result->overlaySize = 0;
}


//   0 - success
// > 0 errno
// < 0 other error
int analyzeExe(FILE *const fp, ExeAnalyzeResult *const result)
{
  long fileSize;
  size_t noRead;
  int err;

  // NOTE: this assumes the current offset is 0

  // Determine file size
  if (fseek(fp, 0l, SEEK_END) != 0) {
    err = errno;
    printError("analyzeExe failed to seek to end of file\n");
    return err;
  }
  if ((fileSize = ftell(fp)) < 1) {
    err = errno;
    printError("analyzeExe failed to determine file size of file\n");
    return err;
  }
  // Set cursor back to start of the file
  if (fseek(fp, 0l, SEEK_SET) != 0) {
    err = errno;
    printError("analyzeExe failed to seek to start of file\n");
    return err;
  }

  result->fileSize = fileSize;

  // Read MsDosHeader
  size_t readSize = sizeof(MsDosHeader);
  if ((long)readSize > fileSize) {
    printError("analyzeExe file is to small to contain a MS Dos header.\n");
    return -1;
  }
  MsDosHeader msDosHeader;
  noRead = fread(&msDosHeader, readSize, 1, fp);
  if (noRead != 1) {
    err = errno;
    printError("analyzeExe failed to read the MS Dos header. "
               "noRead: %ld readSize: %ld\n", noRead, readSize);
    return err;
  }

  if (msDosHeader.signature != 0x5A4D) {
    printError("analyzeExe this is not a PE file for sure. The "
               "MS-DOS header signature doesn't match (not MZ).\n");
    return -2;
  }

  result->signatures |= EXE_ANALYZE_FLAG_MZ;

  // Read e_lfanew (offset to PE/NE file header)
  uint32_t e_lfanew;
  if (fseek(fp, 0x3C, SEEK_SET) != 0) {
    err = errno;
    printError("analyzeExe failed to seek to 0x3C.\n");
    printError("analyzeExe errno: %s\n", strerror(errno));
    return err;
  }
  if (fread(&e_lfanew, 4, 1, fp) != 1) {
    err = errno;
    printError("analyzeExe failed to read e_lfanew.\n");
    return err;
  }
  if (e_lfanew >= fileSize) {
    printError("analyzeExe PE file offset is larger then file size.\n");
    return -3;
  }

  // Seek to PE or NE start
  if (fseek(fp, (long)e_lfanew, SEEK_SET) != 0) {
    err = errno;
    printError("analyzeExe failed to seek to e_lfanew.\n");
    printError("analyzeExe errno: %s\n", strerror(errno));
    return err;
  }

  // Read signature to determine if it's a PE or NE
  uint16_t signature = 0;
  if (fread(&signature, sizeof(signature), 1, fp) != 1) {
    err = errno;
    printError("analyzeExe failed to read signature\n");
    return err;
  }

  if (fseek(fp, -1 * (long)sizeof(signature), SEEK_CUR) != 0) {
    err = errno;
    return err;
  }

  // PE signature
  if (signature == 0x4550) {
    result->signatures |= EXE_ANALYZE_FLAG_PE;
    if ((err = getOverlayFromPe(fp, result)) != 0) {
      return err;
    }
  }
  // NE signature
  else
  if (signature == 0x454E) {
    result->signatures |= EXE_ANALYZE_FLAG_NE;
    if ((err = getOverlayFromNe(fp, result)) != 0) {
      return err;
    }
  }
  // Unknown signature
  else {
    printError("analyzeExe this is not a PE or NE file for sure (2).\n");
    return -4;
  }

  if (result->overlayOffset > fileSize) {
    printError("analyzeExe overlay offset larger then file size.\n");
    return -5;
  }
  else
  if (result->overlayOffset < 4096) { // TODO find smallest overlay offset seen
    printError("analyzeExe overlay offset to small.\n");
    return -6;
  }

  result->overlaySize = fileSize - result->overlayOffset;

  // TODO find minimum working overlay size and create a sane value from that
  if (result->overlaySize < 4096) {
    printError("analyzeExe overlay size to small.\n");
    return -7;
  }

  return 0;
}

