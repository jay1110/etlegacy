#-----------------------------------------------------------------
# Emscripten/WebAssembly Platform Configuration
#-----------------------------------------------------------------
# This file is included when building with the Emscripten toolchain.
# It configures compiler/linker flags and disables features that
# are incompatible with the browser/WASM environment.
#-----------------------------------------------------------------

if(NOT EMSCRIPTEN)
	return()
endif()

message(STATUS "Configuring for Emscripten/WebAssembly target")

#-----------------------------------------------------------------
# Force-disable features incompatible with WASM
#-----------------------------------------------------------------
set(RENDERER_DYNAMIC OFF CACHE BOOL "Disable dynamic renderer loading for Emscripten" FORCE)
set(FEATURE_OPENAL OFF CACHE BOOL "Disable OpenAL for Emscripten" FORCE)
set(FEATURE_RENDERER_VULKAN OFF CACHE BOOL "Disable Vulkan for Emscripten" FORCE)
set(FEATURE_RENDERER2 OFF CACHE BOOL "Disable renderer2 for Emscripten" FORCE)
# Use the OpenGL 1.x (fixed-function) renderer and let Emscripten emulate the
# legacy immediate-mode/matrix-stack GL calls on top of WebGL via
# -sLEGACY_GL_EMULATION=1 (see linker flags below). The rendererGLES target
# still pulls in shared fixed-function code (e.g. src/renderer/tr_flares.c uses
# glPushMatrix/glMatrixMode), so it cannot link against a pure GLES2 context.
set(FEATURE_RENDERER1 ON CACHE BOOL "Enable OpenGL1 renderer for Emscripten" FORCE)
set(FEATURE_RENDERER_GLES OFF CACHE BOOL "Disable GLES renderer for Emscripten" FORCE)
# Use Emscripten's own WebGL-backed GLEW emulation (linked via -lGLEW in
# cmake/ETLSetupFeatures.cmake) instead of the bundled desktop GLEW, whose
# object files reference GLX/WGL entry points (glXGetProcAddressARB, ...) that
# cannot be linked in a WebAssembly build.
set(BUNDLED_GLEW OFF CACHE BOOL "Use Emscripten's built-in GLEW for Emscripten" FORCE)
# Emscripten ships its own WebGL-backed SDL2 (linked via -s USE_SDL=2, set in the
# linker flags below). Building the bundled desktop SDL2 is both redundant and
# incompatible with the browser target, so disable it and rely on the port for
# the SDL2 headers and library.
set(BUNDLED_SDL OFF CACHE BOOL "Use Emscripten's built-in SDL2 for Emscripten" FORCE)
set(FEATURE_IRC_CLIENT OFF CACHE BOOL "Disable IRC for Emscripten" FORCE)
set(FEATURE_IRC_SERVER OFF CACHE BOOL "Disable IRC server for Emscripten" FORCE)
set(FEATURE_AUTOUPDATE OFF CACHE BOOL "Disable autoupdate for Emscripten" FORCE)
set(FEATURE_DBMS OFF CACHE BOOL "Disable DBMS for Emscripten" FORCE)
set(FEATURE_CURL OFF CACHE BOOL "Disable cURL for Emscripten" FORCE)
set(FEATURE_SSL OFF CACHE BOOL "Disable SSL for Emscripten" FORCE)
set(FEATURE_AUTH OFF CACHE BOOL "Disable auth for Emscripten" FORCE)
set(FEATURE_OMNIBOT OFF CACHE BOOL "Disable Omnibot for Emscripten" FORCE)
set(FEATURE_TRACKER OFF CACHE BOOL "Disable tracker for Emscripten" FORCE)
set(FEATURE_ANTICHEAT OFF CACHE BOOL "Disable anticheat for Emscripten" FORCE)
set(FEATURE_PAKISOLATION OFF CACHE BOOL "Disable pak isolation for Emscripten" FORCE)
set(FEATURE_OGG_VORBIS OFF CACHE BOOL "Disable OGG Vorbis for Emscripten" FORCE)
set(FEATURE_THEORA OFF CACHE BOOL "Disable Theora for Emscripten" FORCE)
set(FEATURE_IPV6 OFF CACHE BOOL "Disable IPv6 for Emscripten" FORCE)
set(FEATURE_FREETYPE OFF CACHE BOOL "Disable Freetype for Emscripten" FORCE)
set(BUILD_SERVER OFF CACHE BOOL "Disable dedicated server for Emscripten" FORCE)
# The browser build is a *client*. It cannot host a game server itself (no raw
# UDP sockets / no listen sockets), so it joins a native dedicated server
# through the WebSocket->UDP relay in tools/ws-relay (see src/qcommon/net_web.c).
# It does however need the client-side game logic modules (cgame + ui); these
# are built as Emscripten SIDE_MODULEs and loaded at runtime via dlopen (see
# cmake/ETLBuildMod.cmake and the MAIN_MODULE flag below). The server-side
# modules (qagame/tvgame) run on the native dedicated server, not in the
# browser, so only the client mod is built here.
set(BUILD_MOD ON CACHE BOOL "Build client mod libraries (cgame/ui) for Emscripten" FORCE)
set(BUILD_CLIENT_MOD ON CACHE BOOL "Build cgame/ui for Emscripten" FORCE)
set(BUILD_SERVER_MOD OFF CACHE BOOL "Server modules run natively, not in the browser" FORCE)
set(BUILD_MOD_PK3 OFF CACHE BOOL "Do not pack a mod pk3 for Emscripten" FORCE)

#-----------------------------------------------------------------
# Allow the cgame/ui SIDE_MODULEs to actually be linked as wasm
#-----------------------------------------------------------------
# Emscripten's CMake platform module (Platform/Emscripten.cmake) sets
#   set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS FALSE)
# which makes CMake silently downgrade every SHARED/MODULE library to a static
# archive: it runs `emar` instead of `emcc`, so no link step happens and the
# `-sSIDE_MODULE=1` flag set in cmake/ETLBuildMod.cmake is never applied. The
# resulting cgame.mp.wasm32.so / ui.mp.wasm32.so are then Unix `ar` archives
# (they start with "!<arch>" / 0x21 0x3C 0x61 0x72) rather than WebAssembly
# side modules (which must start with the "\0asm" magic 0x00 0x61 0x73 0x6D).
# The engine's dlopen()/Emscripten's wasm preload plugin reject those archives
# with "does not start with the WebAssembly magic number", making the browser
# build fail to load its game logic.
#
# emcc does support building wasm side modules, so re-enable shared-library
# support here (before any target is created in cmake/ETLBuildMod.cmake). With
# this, `add_library(cgame MODULE ...)` is linked by emcc as a shared module and
# `-sSIDE_MODULE=1` produces a proper dynamic-link wasm module. On this build
# only cgame/ui are MODULE libraries (renderers are static-linked into the
# engine, qagame/tvgame are server-only and disabled), so this is safe.
set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS TRUE)

#-----------------------------------------------------------------
# Emscripten compiler and linker flags
#-----------------------------------------------------------------
# USE_SDL=2: Use Emscripten's built-in SDL2 port
# LEGACY_GL_EMULATION=1: Emulate fixed-function/immediate-mode desktop GL
#   (glBegin/glEnd, glPushMatrix, glMatrixMode, ...) on top of WebGL. The
#   OpenGL 1.x renderer relies on these, so this is required at link time.
# FULL_ES2=1: Emulate OpenGL ES 2 client-side vertex arrays on top of WebGL.
#   WebGL forbids drawing from CPU-side memory (glVertexAttribPointer /
#   glDrawElements / glDrawArrays with a non-zero offset and no VBO bound),
#   which the renderer's vertex arrays use. Without this the browser logs a
#   flood of "no ARRAY_BUFFER is bound and offset is non-zero" /
#   "no buffer is bound to enabled attribute" INVALID_OPERATION errors and
#   nothing is drawn. FULL_ES2 makes Emscripten upload those client-side
#   arrays into temporary buffer objects automatically. Both the reference
#   web ports (jdarpinian/ioq3 and GMH-Code/Wwasm) enable it for the same
#   reason.
# ALLOW_MEMORY_GROWTH=1: Allow the WASM heap to grow dynamically
# WASM=1: Output WebAssembly (not asm.js)
# ASYNCIFY: Enable async/await support for the main loop. NOTE: Asyncify is a
#   link-time whole-program instrumentation; the dlopen()ed cgame/ui SIDE_MODULEs
#   must also be linked with -sASYNCIFY (see cmake/ETLBuildMod.cmake) or any
#   unwind that crosses a mod frame corrupts the rewind and traps with
#   "memory access out of bounds" at doRewind.
# FETCH=1: Provide the emscripten_fetch API used by src/qcommon/dl_main_web.c
# INITIAL_MEMORY: Set initial memory allocation
# STACK_SIZE: The engine has deep call stacks (renderer -> backend ->
#   SDL_GL_SwapWindow, deeply nested game/ui code); the Emscripten default
#   (64 KiB) overflows into "memory access out of bounds" traps, so give it a
#   generous native stack (8 MiB; the reference web ports use 4-5 MiB).
# ASYNCIFY_STACK_SIZE: Asyncify unwinds/rewinds the whole call stack through
#   the blocking main loop (SDL_GL_SwapWindow -> emscripten_sleep). If this
#   buffer is too small the rewind overruns it and traps with "memory access
#   out of bounds" (seen at doRewind / __synccall / silence_callback). 64 KiB
#   is not enough for this engine's stack depth; 1 MiB still trapped at
#   doRewind in the field, so use a deliberately generous 16 MiB.
# MAXIMUM_MEMORY: With ALLOW_MEMORY_GROWTH the wasm heap grows on demand, but
#   only up to this cap (the Emscripten default is 2 GiB). Loading large maps
#   plus server-downloaded pk3s can exceed that and traps with "memory access
#   out of bounds" instead of growing further. Set the cap to the wasm32
#   architectural maximum of 4 GiB so the engine can use as much memory as it
#   needs.
#-----------------------------------------------------------------
set(EMSCRIPTEN_COMMON_FLAGS "-s USE_SDL=2")
set(EMSCRIPTEN_LINK_FLAGS
	"-s ALLOW_MEMORY_GROWTH=1"
	"-s WASM=1"
	"-s ASYNCIFY"
	"-s FETCH=1"
	"-s INITIAL_MEMORY=2147483648" # 2 GiB up front; grows on demand up to MAXIMUM_MEMORY
	"-s MAXIMUM_MEMORY=4294967296" # 4 GiB heap cap (wasm32 maximum; Emscripten default 2 GiB is too small)
	"-s STACK_SIZE=8388608" # 8 MiB native stack (Emscripten default 64 KiB is too small)
	"-s ASYNCIFY_STACK_SIZE=16777216" # 16 MiB Asyncify rewind buffer (1 MiB still overflowed at doRewind)
	"-s FULL_ES2=1"
	"-s GL_UNSAFE_OPTS=0"
	"-s FORCE_FILESYSTEM=1"
	# The client loads the game logic (cgame/ui) at runtime via dlopen. On wasm
	# that requires dynamic linking: the engine is the MAIN_MODULE and the mods
	# are SIDE_MODULEs (see cmake/ETLBuildMod.cmake). MAIN_MODULE=1 keeps all
	# symbols so the side modules can resolve engine functions at load time.
	"-s MAIN_MODULE=1"
	# Runtime helpers used by the asset bootstrap in src/web/shell.html to fetch
	# the etmain paks into the virtual filesystem before main() runs.
	"-s EXPORTED_RUNTIME_METHODS=['FS','callMain','addRunDependency','removeRunDependency','ccall','cwrap']"
	# emcc's HTML minifier collapses ALL newlines in the inline <script> blocks
	# of the custom shell (src/web/shell.html) without minifying the JS itself,
	# so the first "//" line comment swallows the rest of the statement stream.
	# That silently breaks the whole bootstrap script ("Module is not defined",
	# no TextDecoder workaround, no asset download). Keep the shell unminified.
	"-s MINIFY_HTML=0"
	"-l idbfs.js" # IndexedDB-backed FS so downloaded paks are cached across loads
	"-lwebsocket.js" # WebSocket API used by src/qcommon/net_web.c
)

# LEGACY_GL_EMULATION=1 emulates fixed-function/immediate-mode desktop GL
# (glBegin/glEnd, glPushMatrix, glMatrixMode, ...) on top of WebGL, which the
# OpenGL 1.x renderer relies on. When FEATURE_GL4ES is enabled, gl4es provides
# that translation instead, so Emscripten's emulation must be left off to avoid
# clashing GL implementations.
if(NOT FEATURE_GL4ES)
	list(APPEND EMSCRIPTEN_LINK_FLAGS "-s LEGACY_GL_EMULATION=1")
endif()
string(REPLACE ";" " " EMSCRIPTEN_LINK_FLAGS_STR "${EMSCRIPTEN_LINK_FLAGS}")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${EMSCRIPTEN_COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${EMSCRIPTEN_COMMON_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${EMSCRIPTEN_COMMON_FLAGS} ${EMSCRIPTEN_LINK_FLAGS_STR}")

#-----------------------------------------------------------------
# Platform identification
#-----------------------------------------------------------------
set(ARCH "wasm32")
set(BIN_DESCRIBE "(WebAssembly)")
set(LIB_SUFFIX ".wasm.")

#-----------------------------------------------------------------
# Emscripten-specific compile definitions
#-----------------------------------------------------------------
target_compile_definitions(shared_libraries INTERFACE __EMSCRIPTEN__=1)

#-----------------------------------------------------------------
# Output suffix configuration
# Emscripten produces .html + .js + .wasm
#-----------------------------------------------------------------
set(CMAKE_EXECUTABLE_SUFFIX ".html")

#-----------------------------------------------------------------
# HTML shell template
#-----------------------------------------------------------------
# The custom shell (src/web/shell.html) needs the legacy mod pk3 filename, which
# embeds the project version and is only known after ETLVersion.cmake runs (that
# happens later than this file). The shell is therefore configured and wired up
# as --shell-file in cmake/ETLBuildMod.cmake, not here.

message(STATUS "Emscripten configuration complete")
message(STATUS "  Architecture: ${ARCH}")
message(STATUS "  Memory: 2GB initial, growable to 4GB")
if(FEATURE_GL4ES)
	message(STATUS "  Renderer: OpenGL 1.x (via gl4es -> GLES2/WebGL)")
else()
	message(STATUS "  Renderer: OpenGL 1.x (via WebGL LEGACY_GL_EMULATION)")
endif()
