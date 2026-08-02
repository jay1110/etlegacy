# Omni-bot

Everything Omni-bot related lives in this one directory: the bot sources, the
GameMonkey script engine they need, and the bot runtime data (scripts and
navigation meshes).

Omni-bot is the bot system ET: Legacy loads **server side**. The game module
(`qagame`) `dlopen()`s the bot library through
`vendor/Omnibot/Common/BotLoadLibrary.cpp` when the `FEATURE_OMNIBOT` build
option is on. Note that `vendor/Omnibot/` (one level up) only holds the *engine
side* of that interface - the headers and the loader. The bot library itself is
what lives here.

## Layout

| Path | Origin | What it is |
| --- | --- | --- |
| `source/` | `omni-bot-0.93.zip` (repository root) | The buildable 0.8x/0.93 bot tree. `source/Omnibot` is the bot library, `source/GameInterfaces/ET` the ET-specific glue and map scripts. |
| `source/Omnibot/dependencies/gmscriptex/` | <https://github.com/jswigart/gmscriptex> | The GameMonkey Script "Ex" engine. In the original tree this is an **empty git submodule**, which is why the zip alone cannot be built; it is filled in here. |
| `upstream/` | <https://github.com/jswigart/omni-bot> | The current upstream tree (a much newer rewrite). Kept for reference - it does **not** implement the 0.8x bot ABI ET: Legacy's loader expects. |
| `data/` | `omnibot.zip` (repository root) | Bot runtime data: `global_scripts/`, `et/scripts/`, `et/user/` and `et/nav/` (navigation meshes). |

Both original zips are kept in the repository root as the untouched archives
they were delivered as.

### What was left out

To keep the tree to a workable size, dependencies and subprojects that no build
target uses were not copied:

* `source/Omnibot/dependencies/`: `sfml`, `guichan-0.7.1`, `VisualLeakDetector`,
  `Detours Express 2.1` (GUI/debug-only, Windows-centric).
* `source/Omnibot/dependencies/gmscriptex/gmsrc_ex/src/`: `gmdebugger`,
  `gmconsole`, `examples`, `bin` and the standalone interpreter front-ends -
  only `gm/`, `binds/`, `platform/` and `3rdParty/` are globbed by the build.
* `upstream/Omnibot/dependencies/`: `WildMagic5`, `protobuf`, `sqlite`,
  `Opcode`, `Remotery`, `glext`, `tinyobjloader`, `VisualLeakDetector`.
* `data/et/nav/`: only the navigation meshes for the stock ET maps (battery,
  fueldump, goldrush, oasis, radar, railgun and their variants) are vendored.
  The complete set for ~2000 community maps is in `omnibot.zip`.

## Building

The native build is CMake based and is driven from `source/Omnibot`:

```sh
cmake -S vendor/omni-bot/source/Omnibot -B build-omnibot -DOMNIBOT_RTCW=OFF
cmake --build build-omnibot
```

It produces `omnibot_et.<arch>.so`, which `qagame` then loads.

### Options

| Option | Default | What it does |
| --- | --- | --- |
| `OMNIBOT_ET` / `OMNIBOT_RTCW` | `ON` | Which game modules to build. |
| `OMNIBOT_STATIC_BOOST` | `ON` | Link the compiled Boost libraries statically. Ignored when `OMNIBOT_STD_FILESYSTEM` is on, because then none are used. |
| `OMNIBOT_STD_FILESYSTEM` | `ON` for Emscripten, `OFF` otherwise | Use `<filesystem>`/`<regex>` instead of Boost.Filesystem/Boost.Regex, so no compiled Boost library is needed at all. Requires C++17. |
| `OMNIBOT_BOOST_INCLUDEDIR` | empty | Use these Boost headers instead of `find_package(Boost)`. For cross builds whose toolchain restricts `find_package` to its own sysroot. |

## WebAssembly status

The tree now *configures and compiles* for `emcc`, but **no `emcc` build has been
run yet** - it was prepared on a machine where the emsdk downloads are
unreachable, so everything below was verified with clang/GCC natively (which is
the same frontend emcc uses) and by inspection. Treat the wasm build as
"expected to work, unverified", and keep `FEATURE_OMNIBOT` off for the engine's
wasm build until someone has actually run it.

```sh
emcmake cmake -S vendor/omni-bot/source/Omnibot -B build-omnibot-wasm \
    -DOMNIBOT_RTCW=OFF -DCMAKE_BUILD_TYPE=Release \
    -DOMNIBOT_BOOST_INCLUDEDIR=/usr/include
cmake --build build-omnibot-wasm
```

This produces `ET/omnibot_et.wasm32.so`, an Emscripten `SIDE_MODULE`.
`/usr/include` is whatever directory holds a `boost/` header tree (`apt-get
install libboost-dev`); the headers are architecture independent, and pointing
at them directly is needed because Emscripten's toolchain file limits
`find_package()` to its own sysroot.

### What was done

1. **Boost.** `OMNIBOT_STD_FILESYSTEM` maps the `fs` and `obre` aliases in
   `source/Omnibot/Common/common.h` onto `std::filesystem`/`std::regex`, which
   removes the only Boost libraries that have to be compiled (and hence
   cross-compiled): `filesystem`, `regex`, `system` and `date_time`. What is
   left - `dynamic_bitset`, `multi_array`, `pool`, `lexical_cast`,
   `algorithm/string`, `bind`, the smart pointers - is header only.
   Note that `REGEX_OPTIONS` needed a separate spelling for the two libraries:
   Boost's `grep` is a superset of `basic`, so `basic|icase|grep` is one valid
   grammar there, while the standard allows at most one grammar bit and both
   libstdc++ and libc++ throw `regex_error("conflicting grammar options")` for
   it. `grep|icase` is what the Boost expression evaluates to and matches
   identically. `Utils::RegexMatch` swallowed that exception, so this would have
   made every `FindAllFiles()` filter match nothing without a single message; it
   now logs the failure.
2. **Shared memory, threads and sockets.** These turned out to be compiled out
   already: `ENABLE_FILE_DOWNLOADER`, `ENABLE_REMOTE_DEBUGGER`,
   `ENABLE_DEBUG_WINDOW` and `ENABLE_REMOTE_DEBUGGING` are commented out even
   for Windows in `Common/common.h`, and `INTERPROCESS` is never defined, so
   `boost::thread`, `boost::asio` and `boost::interprocess::message_queue` are
   never reached. What did need fixing is the *non*-interprocess debug-draw
   path in `Common/Interprocess.cpp`, which located the loaded `cgame` module
   with `<link.h>`/`dl_iterate_phdr`; Emscripten has neither, so it now asks its
   dynamic loader for `cgame.mp.wasm32.so` by name instead.
3. **Compiler flags.** `Common/CMakeLists.txt` did not actually compile with
   `-ffriend-injection`: the flags were passed to `set_target_properties`,
   which takes property/value *pairs*, so only `-pthread` ever reached the
   compiler and the other two silently became bogus property names. That call
   is now a proper `target_compile_options`, without the GCC-only
   `-ffriend-injection` (removed in GCC 8, never supported by clang) and
   without `-pthread` for Emscripten.
4. **PhysicsFS.** `dependencies/physfs/physfs_platforms.h` refused to build for
   an unknown platform; Emscripten now selects the POSIX/UNIX backend, without
   CD-ROM support.
5. **Packaging.** `MODULE_SUFFIX` is `.wasm32` for Emscripten and `omnibot-et`
   links with `-sSIDE_MODULE=1`, so the output is `omnibot_et.wasm32.so` - the
   name `vendor/Omnibot/Common/BotLoadLibrary.cpp` now looks for, matching the
   engine's other side modules.

Verified natively for all four combinations of {GCC 13, clang 18} x {Boost,
`OMNIBOT_STD_FILESYSTEM`}; the `std` builds link no Boost shared library
(`ldd omnibot_et.x86_64.so`) and contain no `boost::filesystem` symbols.

### What is left

1. Actually run the `emcmake` build above and fix whatever it turns up.
2. Preload the module in `src/web/shell.html` with `FS.createPreloadedFile`
   like `cgame`/`ui`/`qagame`. The browser will not compile a wasm module
   synchronously on the main thread, so a module that is merely written to the
   virtual filesystem is inert data and `dlopen()` fails.
3. Ship `data/` (the `global_scripts/`, `et/scripts/` and `et/nav/` trees, ~5 MB)
   into the browser filesystem, and mount it where `Common/FileSystem.cpp`
   expects it.
4. Build `qagame` with `FEATURE_OMNIBOT` for wasm - it is force-disabled in
   `cmake/ETLEmscripten.cmake` - which also means `qagame`, itself a side
   module, has to be able to `dlopen()` a second one.
5. Drop the `+set omnibot_enable 0` the web launcher passes for "Host game" and
   "Quick single game" in `src/web/shell.html`.

Also note that the browser build is a *client* that joins a native dedicated
server, and that server already runs the native Omni-bot. A wasm Omni-bot only
becomes useful for the in-browser listen server ("Host game" / "Quick single
game" in the web launcher).

## Licensing

Omni-bot and GameMonkey Script keep their own licences; see the files in
`source/`, `upstream/` and `data/`.
