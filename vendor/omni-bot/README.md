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

## WebAssembly status

The wasm port is **not done yet**. `source/Omnibot` as delivered cannot be
compiled with `emcc`; these are the blockers, in the order they have to be
solved:

1. **Boost.** `source/Omnibot/CMakeLists.txt` requires the compiled Boost
   libraries `system`, `filesystem`, `regex` and `date_time`. Emscripten ships
   no Boost port, so they must either be cross-built for wasm or replaced
   (`filesystem` → `std::filesystem`, `regex` → `std::regex`, `date_time` →
   `std::chrono`).
2. **Shared memory and threads.** `source/Omnibot/Common/Interprocess.cpp` uses
   `boost::interprocess::message_queue` and `Common/common.h` pulls in
   `boost/thread.hpp`. The browser build deliberately runs without
   `SharedArrayBuffer`/pthreads, so the debug-draw queue has to be stubbed out
   and the bot has to run on the engine's own frame loop.
3. **Sockets.** The remote debugger and `Common/FileDownloader.cpp` use
   `boost::asio`; there are no raw sockets in the browser, so those paths have
   to be compiled out.
4. **Compiler flags.** `Common/CMakeLists.txt` compiles with
   `-ffriend-injection`, a GCC-only flag that was removed long ago and which
   clang/emcc reject.
5. **Packaging.** Build `omnibot-et` as an Emscripten `SIDE_MODULE` named
   `omnibot_et.wasm32.so` - `BotLoadLibrary.cpp` needs a `wasm32` case in its
   `SUFFIX`/`POSTFIX` logic - preload it in `src/web/shell.html` like the other
   side modules so `qagame`'s `dlopen()` resolves synchronously, and ship the
   `data/` scripts and navs into the browser filesystem. Only then can
   `FEATURE_OMNIBOT` be enabled for the wasm build (it is force-disabled in
   `cmake/ETLEmscripten.cmake`).

Also note that the browser build is a *client* that joins a native dedicated
server, and that server already runs the native Omni-bot. A wasm Omni-bot only
becomes useful once the in-browser listen server ("Host game" / "Quick single
game" in the web launcher) should have bots too.

## Licensing

Omni-bot and GameMonkey Script keep their own licences; see the files in
`source/`, `upstream/` and `data/`.
