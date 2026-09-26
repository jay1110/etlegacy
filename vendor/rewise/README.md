# Reverse Engineering Wise

Extract files from Wise installers without executing them.

The aim of this project is to extract assets from old game installers
made with Wise installer without executing the PE/NE file (.exe), so
they can be used with free software implementations of the game engine.

```
==============================================================
              Welcome to REWise version 0.3.0-debug
==============================================================

 Usage: rewise OPERATION [OPTIONS] INPUT_FILE

  OPERATIONS
   -x --extract      OUTPUT_PATH  Extract files.
   -r --raw          OUTPUT_PATH  Extract all files in the overlay data. Filename format will be 'EXTRACTED_%09u'.
   -l --list                      List files.
   -V --verify                    Run extract without actually outputting files, crc32s will be checked.
   -0 --raw-verify                Run raw extract without actually outputting files, crc32s will be checked.
   -z --script-debug              Print parsed WiseScript.bin
   -v --version                   Print version and exit.
   -h --help                      Display this HELP.

  OPTIONS (--list)
   -e --extended                  Also print languages and components.

  OPTIONS (--extract, --list, --verify)
   -f --filter       FILTER       Filter files based on their filepath. Multiple of these filters may be set. '*' will match anything, use '\*' to escape.
   -F --file-filter  FILTER_FILE  Path to a file that contains one filter string per line.
   -c --components   NAME         Filter files by components name. Note that default files are always included.
   -g --language     INDEX        Filter files by language index. Note that default files are always included.

  OPTIONS (any)
   -p --preserve                  Don't delete WiseScript.bin from TMP_PATH.
   -t --tmp-path     TMP_PATH     Set temporary path, default: /tmp/
   -d --debug                     Print debug info.
   -s --silent                    Be silent, don't print anything.
   -n --no-extract                Don't extract anything. Use WiseScript.bin at the given TMP_PATH, it won't delete it.

  NOTES
    - Path to directory OUTPUT_PATH and TMP_PATH should exist and be writable.
    - All files REWise does output will be overwritten when they exist!
```


# Index

 - [Index](#index)
 - [Acknowledgement](#acknowledgement)
 - [Technical](#technical)
 - .. [PE build dates](#pe-build-dates)
 - [State](#state)
 - .. [Known working retail installers](#known-working-retail-installers)
 - .. [Shareware](#shareware)
 - .. [Known issues](#known-issues)
 - .. .. [Multi-disc installers](#multi-disc-installers)
 - .. [Other things that might be a problem](#other-things-that-might-be-a-problem)
 - [Dependencies](#dependencies)
 - [Many thanks to](#many-thanks-to)
 - [Other projects](#other-projects)
 - [Resources on Wise](#resources-on-wise)


# Acknowledgement

The [WiseUnpacker project](https://github.com/mnadareski/WiseUnpacker)
kick started this project by giving insight on how to inflate files from Wise
installers. So a lot of thanks to [mnadareski](https://github.com/mnadareski/)!

REWise is different in that it tries to understand and proper parse the
`WiseScript.bin`, not everything is understood yet but it looks
like most of that is relevant is.


# Technical

 > NOTE: The PE build date will not be the same as the installer creation date
 > (or release date) since the overlay-data is just appended to a existing
 > Wise PE32 stub on creation of the installer. The supported installer release
 > dates seen so far are between 1999 and 2003. While the PE build dates are
 > between 1998 and 2001. NE files do not contain a build date.

A Wise installer is a PE or NE executable with extra data appended to the end of
it (overlay-data at the overlay-offset). The overlay-data contains a Wise specific
header. After the Wise header there is raw `DEFLATE`d data without file-headers,
after each `DEFLATE`d data entry there is a CRC32 for the inflated data. The
`DEFLATE`d data + CRC32 continues until `EOF`. Unless the installer is created
with the `zip` support option, then before each `DEFLATE` data there is a `zip`
local file header, and there is no CRC32 after the `DEFLATE` data since it is
in the `zip` local file header.

The first inflated file is a `.dib` file we call `WiseColors.dib`, containing
colors used by the installer, it is no use for us so we skip it.

The second inflated file is a binary file that has all sorts of data relevant
for the installation (including custom file headers). Within REWise this file
is named `WiseScript.bin`. Most time was spend on reverse engineering
different `WiseScript.bin` files with a hex-editor (ImHex) so a
`WiseScript.bin` can be parsed without as much of guessing as possible. One
discovered struct that is the most relevant for extracting files that would be
installed by the installer is (it contains file names, metadata, CRC32 and
offset to deflatedata):

```c
/* 0x00 WiseScriptFileHeader */
typedef struct {
  unsigned char unknown_2[2];   // seen: 0x8000, 0x8100, 0x0000, 0x9800 0xA100
  uint32_t deflateStart;
  uint32_t deflateEnd;
  uint16_t date;
  uint16_t time;
  uint32_t inflatedSize;
  unsigned char unknown_20[20]; // 20 * \0?
  uint32_t crc32;               // do not check when it is 0
  char * destFile;              // \0 terminated string
  char * fileText[langCount];   // \0 terminated string(s)
  unsigned char terminator;     // always \0? terminator?
} WiseScriptFileHeader;
```

On how `REWise` handles a `WiseScript.bin` file: SEE `src/wisescript.h` and
`src/wisescript.c`.


### PE build dates

 > **NOTE**: Stubs are patched on creation of the installer so we cannot do
   a sum of `compiled` installers and compare below. `VsVersionInfo` and
   `StringFileInfo` are patched and a value in a section somewhere up is
   patched to contain the overlay offset value.

VERSION    | BUILD DATE-TIME     | OVERLAY OFFSET | SHA256
:--------- | :------------------ | :------------- | :----------------------
?          | 1998-10-02 22:36:51 | `0x3800`   | ?
?          | 1998-10-23 23:23:16 | `0x3800`   | ?
7.01       | 1998-11-09 21:17:09 | `0x3800`   | 6a6266fc674bc61852d3e5dc2b3608de9205f518baf718880b81aa1a6179573f
?          | 1998-12-03 23:11:32 | `0x3800` / `0x3E00` | ?
?          | 1999-02-05 23:07:52 | `0x3A00`   | ?
?          | 1999-04-05 18:07:26 | `0x3A00`   | ?
?          | 1999-04-08 22:24:47 | `.rsrc`?       | ?
?          | 1999-05-21 22:48:48 | `0x3A00` / `0x5E00` | ?
?          | 1999-08-17 17:25:48 | `0x3A00` / `0x5200` | ?
8.11.0.505 | 2000-04-25 16:37:12 | `0x3A00` / `0x3C00` / `0x5200` | 23da437bde09458cd512644a139c6ce8d180187a9cc6d32e7bfef077ed99f580
8.12.0.508 | 2000-04-25 16:37:12 | `0x3A00` / `0x3C00` / `0x5200` | 23da437bde09458cd512644a139c6ce8d180187a9cc6d32e7bfef077ed99f580
8.14.0.512 | 2000-04-25 16:37:12 | `0x3A00` / `0x3C00` / `0x5200` | 23da437bde09458cd512644a139c6ce8d180187a9cc6d32e7bfef077ed99f580
?          | 2001-08-13 19:13:38 | `0x3A00` / `0x3C00` | ?
9.01.202.0 | 2001-10-25 21:47:11 | `0x3A00` / `0x3C00` / `0x9C00` | 96a173c62f632140e9d14a7eee0d42f8ef62bee4b66ed317f2077bdfaa32376a
9.02.204.0 | 2001-10-25 21:47:11 | `0x3A00` / `0x3C00` / `0x9C00` | 96a173c62f632140e9d14a7eee0d42f8ef62bee4b66ed317f2077bdfaa32376a
???        | 2031-05-20 22:54:01 | `0x4C00` | ?


# State

 - [x] [PE](https://en.wikipedia.org/wiki/Portable_Executable) and
       [NE](https://en.wikipedia.org/wiki/New_Executable) support.
 - [x] Installers build with `zip` support.
 - [x] Multilingual installer support.
 - [ ] Multi-file/disc installers.
 - [ ] Patch support.

## Known working retail installers

PE BUILD            | FILENAME           | NAME
:------------------ | :----------------- | :------------------------
17-08-1999 17:25:48 | counter-strike.exe | Half-Life Counter-Strike (2000)
17-08-1999 17:25:48 | SETUP.EXE          | SWAT 3: Elite Edition (2000)
17-08-1999 17:25:48 | setup.EXE          | Wild Wheels (2002) (aka Buzzing Cars)
25-04-2000 16:37:12 | SETUP.EXE          | Half-Life Game Of The Year Edition (1999)
25-04-2000 16:37:12 | Setup.exe          | Return to Castle Wolfenstein (2001)

## Shareware

A large collection of Wise installers is used to test REWise, see
[tests/extended/results/README.md](tests/extended/results/README.md) for
the current stats. A list of all the installer sources can be found
[here](tests/extended/sources/).

## Known issues

 - The `ScriptDeflateOffset` isn't correct on some installers, there
   are still some unknowns on how to calculate this proper.
 - WiseScript `OP 0x18` is not understood.
 - The code used to get the language/comp per file still has unknowns,
   therefore not all installers will work, but most do.
 - Installers where the inflated file size is larger then advertised,
   these will not work because REWise is strict about the advertised
   inflate size.
 - Installers that have weird extra bytes in the WiseScriptHeader.
 - Multi file/disc installers are current not supported.
 - To determine what Wise package/version was used other then the PE build
   date. However it looks like there is none, but:
   On [Wikipedia](https://en.wikipedia.org/wiki/Wise_installer) is a list of
   different Wise installer releases, comparing that list to the tested
   installers (games) their PE build date it suggests that the
   targeted Wise installer versions are either one of these:

    - WISE Installation System version 7 (1998) (InstallMaker)
    - WISE Installation System version 7 (1998) (InstallMaster)
    - WISE Installation System version 8 (1999) (InstallBuilder)
    - WISE Installation System version 8 (1999) (InstallMaker)
    - WISE Installation System version 8 (1999) (InstallMaster)
    - WISE Installation System version 9 (2001)


### Multi-disc installers

All multi-disc installers are current NOT supported.

A workaround is to append the `.W02`, `.W03`, etc.. to the `setup.exe`
to make it work with REWise. Example:
`cat setup.exe setup.W02 setup.W03 > new_setup.exe`


PE BUILD            | FILENAME           | NAME
:------------------ | :----------------- | :------------------------
25-04-2000 16:37:12 | setup.exe          | Hitman Contracts (2004)
25-10-2001 21:47:11 | Gothic2-Setup.exe  | Gothic II (2003)


#### `GOTHIC II`

Would be nice to support for the use with
[REGoth-bs](https://github.com/REGoth-project/REGoth-bs)
or [OpenGothic](https://github.com/Try/OpenGothic).


## Other things that might be a problem

 - REWise is only tested on Little Endian systems.
 - Wise installers where the PE was build before 1998 or after 2001, or
   installers that where created/released before 1999 or after 2004.


# Dependencies

NAME | LICENSE | URL
:--- | :------ | :---------------------
zlib | Zlib    | https://www.zlib.net/


# Many thanks to

 - The [WiseUnpacker project](https://github.com/mnadareski/WiseUnpacker) for
   a great source of information.
 - The [FragNet community](https://www.frag-net.com) for testing and feedback.
 - [Archive.org](https://archive.org), 
   [cd.textfiles.com](http://cd.textfiles.com) and
   [discmaster.textfiles.com](http://discmaster.textfiles.com), without them
   all the shareware installers would not have found.


# Other projects

 - https://github.com/mnadareski/WiseUnpacker
 - https://github.com/lmop/exwise
 - https://www.angelfire.com/ego/jmeister/hwun/

# Resources on Wise

 - <http://fileformats.archiveteam.org/wiki/Wise_installer_package>
 - <https://web.archive.org/web/19981212034130/http://www.wisesolutions.com/>
 - <https://web.archive.org/web/19990218173654/http://www.wisesolutions.com/products/demo.asp>
 - <https://web.archive.org/web/19990428140855/http://wisesolutions.com/products/demo.asp>
 - <https://web.archive.org/web/19990428140858/http://www.wisesolutions.com/products/demo.asp>
