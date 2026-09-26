# REWise v0.3.1

 NOTE to packagers: no need to update from v0.3.0 to v0.3.1 since the
 REWise source files are untouched.

 - Fix extended tests file sources thanks to @grepwood, see
   https://codeberg.org/CYBERDEV/REWise/issues/1
 - Add CMake support thanks to @mnhauke, see
   https://codeberg.org/CYBERDEV/REWise/pulls/2

    CMake usage for normal build:

    ```
    $ mkdir build && cd build
    $ cmake ../
    $ make
    ```

    CMake for debug build:

    ```
    $ mkdir build && cd build
    $ cmake -DCMAKE_C_FLAGS='-DREWISE_DEBUG' -DCMAKE_BUILD_TYPE=Debug ../
    $ make
    ```


# REWise v0.3.0

 > Codename SHAREWARE

 - Multi lingual installer support.
 - PK installer support (installers created with the zip support option).
 - NE support.
 - File filters by wildcard string(s) and experimental by language
   and/or components.
 - Improved WiseScript.bin parsing (new/adjusted OP's).
 - New operation `--raw-verify`, to verify raw extract.
 - REWise will now exit with a different exit code for different errors.
 - Strings will be interpreted as `CP1252` and converted to `UTF-8`, non
   convertable characters will be escaped, example `0x0D` will become
   `'_x0D'`.
 - Debug code removed from 'normal' build, build with 'make debug' for
   debug build.
 - Fix segfaults due to uninitialized stuff (ouch..)
 - The `.dib` file is fully skipped now, no extract to `/tmp` anymore.
 - More values have been found in the overlay header, many deflated/inflated
   file sizes. So the advertised inflate size for  `WiseScript.bin` will
   be honored now, the extra sanity check that the inflated size should
   not be larger then `1 MiB` is still there.

There are still some issues, to list some:

 - WiseScript `OP 0x18` is not understood.
 - The code used to get the language/comp per file still has unknowns,
   therefore not all installer will work, but most do.
 - Inflated file size is larger then advertised, these will not work
   because REWise is strict about the advertised inflate size.
 - Installers that have weird extra bytes in the WiseScriptHeader.


# REWise v0.2.0

 > Codename ZLIB

 - Replaced the custom inflate and CRC32 code with an `zlib` implementation.
   Tests showed that the `zlib` implementation is around 16 times faster!
 - Maximum file sizes for `WiseScript.bin` and `WiseColors.bin` while
   inflating.
 - Fixed some compiler warnings.


# REWise v0.1.0

 > Codename INIT

This is the initial version.

The inflation process is heavily based on the inflation implementation of
[WiseUnpacker](https://github.com/mnadareski/WiseUnpacker). So a lot of
thanks to [mnadareski](https://github.com/mnadareski)!

Most time was spend on reverse-engineering `WiseScript.bin` to extract
files with their meta-data without as much of guessing as possible at that
time.

It can list, verify and extract files from various Valve game installers
released from 1999 to 2003 like Half-Life GOTY and Counter-Strike 1.5.
Return to Castle Wolfenstein (2001) and Wolfenstein: Enemy Territory (2001)
are also supported. For a more complete list of working and not working
installers see `README.md`.
