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
set(FEATURE_RENDERER1 OFF CACHE BOOL "Disable renderer1 for Emscripten" FORCE)
set(FEATURE_RENDERER_GLES ON CACHE BOOL "Enable GLES renderer for Emscripten" FORCE)
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
set(BUILD_SERVER OFF CACHE BOOL "Disable dedicated server for Emscripten" FORCE)

#-----------------------------------------------------------------
# Emscripten compiler and linker flags
#-----------------------------------------------------------------
# USE_SDL=2: Use Emscripten's built-in SDL2 port
# FULL_ES2=1: Provide full OpenGL ES 2.0 API (maps to WebGL 1.0)
# ALLOW_MEMORY_GROWTH=1: Allow the WASM heap to grow dynamically
# WASM=1: Output WebAssembly (not asm.js)
# ASYNCIFY: Enable async/await support for the main loop
# INITIAL_MEMORY: Set initial memory allocation
#-----------------------------------------------------------------
set(EMSCRIPTEN_COMMON_FLAGS "-s USE_SDL=2 -s FULL_ES2=1")
set(EMSCRIPTEN_LINK_FLAGS
	"-s ALLOW_MEMORY_GROWTH=1"
	"-s WASM=1"
	"-s ASYNCIFY"
	"-s INITIAL_MEMORY=536870912" # 512MB
	"-s ASYNCIFY_STACK_SIZE=65536"
	"-s GL_UNSAFE_OPTS=0"
	"-s FORCE_FILESYSTEM=1"
)
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
message(STATUS "  Renderer: OpenGL ES (WebGL 1.0)")
