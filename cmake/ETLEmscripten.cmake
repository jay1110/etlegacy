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
set(BUILD_MOD OFF CACHE BOOL "Disable mod building for Emscripten" FORCE)

#-----------------------------------------------------------------
# Emscripten compiler and linker flags
#-----------------------------------------------------------------
# USE_SDL=2: Use Emscripten's built-in SDL2 port
# LEGACY_GL_EMULATION=1: Emulate fixed-function/immediate-mode desktop GL
#   (glBegin/glEnd, glPushMatrix, glMatrixMode, ...) on top of WebGL. The
#   OpenGL 1.x renderer relies on these, so this is required at link time.
# ALLOW_MEMORY_GROWTH=1: Allow the WASM heap to grow dynamically
# WASM=1: Output WebAssembly (not asm.js)
# ASYNCIFY: Enable async/await support for the main loop
# FETCH=1: Provide the emscripten_fetch API used by src/qcommon/dl_main_web.c
# INITIAL_MEMORY: Set initial memory allocation
#-----------------------------------------------------------------
set(EMSCRIPTEN_COMMON_FLAGS "-s USE_SDL=2")
set(EMSCRIPTEN_LINK_FLAGS
	"-s ALLOW_MEMORY_GROWTH=1"
	"-s WASM=1"
	"-s ASYNCIFY"
	"-s FETCH=1"
	"-s INITIAL_MEMORY=536870912" # 512 MiB
	"-s ASYNCIFY_STACK_SIZE=65536"
	"-s GL_UNSAFE_OPTS=0"
	"-s FORCE_FILESYSTEM=1"
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
# Use Emscripten's HTML shell template if available
#-----------------------------------------------------------------
if(EXISTS "${CMAKE_SOURCE_DIR}/src/web/shell.html")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --shell-file ${CMAKE_SOURCE_DIR}/src/web/shell.html")
endif()

message(STATUS "Emscripten configuration complete")
message(STATUS "  Architecture: ${ARCH}")
message(STATUS "  Memory: 512MB initial, growable")
message(STATUS "  Renderer: OpenGL 1.x (via WebGL LEGACY_GL_EMULATION)")
