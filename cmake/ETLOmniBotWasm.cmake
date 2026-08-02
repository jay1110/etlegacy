#-----------------------------------------------------------------
# Omni-bot for the Emscripten/WebAssembly build
#
# On every other platform the bot library is a prebuilt binary downloaded from
# the ET: Legacy mirror (see cmake/ETLInstallOmniBot.cmake). No such build
# exists for WebAssembly, so the browser build compiles the vendored Omni-bot
# tree (vendor/omni-bot/source/Omnibot) itself, as a wasm SIDE_MODULE that the
# server game module (qagame) loads through dlopen() at runtime.
#
# The result, omnibot_et.wasm32.so, is copied to the top of the build directory
# next to the engine's other side modules (cgame.mp.wasm32.so, ...) so the
# packaging step in .github/workflows/emscripten.yml picks it up the same way.
#-----------------------------------------------------------------

if(NOT (EMSCRIPTEN AND FEATURE_OMNIBOT AND BUILD_SERVER_MOD))
	return()
endif()

include(ExternalProject)

set(OMNIBOT_WASM_SOURCE_DIR "${PROJECT_SOURCE_DIR}/vendor/omni-bot/source/Omnibot")
set(OMNIBOT_WASM_DATA_DIR "${PROJECT_SOURCE_DIR}/vendor/omni-bot/data")

if(NOT EXISTS "${OMNIBOT_WASM_SOURCE_DIR}/CMakeLists.txt")
	message(FATAL_ERROR
		"FEATURE_OMNIBOT is enabled for the WebAssembly build but the vendored "
		"Omni-bot sources are missing at ${OMNIBOT_WASM_SOURCE_DIR}")
endif()

#-----------------------------------------------------------------
# Boost headers
#-----------------------------------------------------------------
# With OMNIBOT_STD_FILESYSTEM (the Emscripten default) the remaining Boost
# usage is header only and therefore architecture independent, so the *host*
# Boost headers can be used for the cross build. Emscripten's toolchain file
# restricts find_path() to its own sysroot, which has no Boost, hence
# NO_CMAKE_FIND_ROOT_PATH. Override with -DOMNIBOT_BOOST_INCLUDEDIR=<dir>.
if(NOT OMNIBOT_BOOST_INCLUDEDIR)
	find_path(OMNIBOT_BOOST_INCLUDEDIR
		NAMES boost/version.hpp
		PATHS /usr/include /usr/local/include /opt/local/include /opt/homebrew/include
		NO_CMAKE_FIND_ROOT_PATH
	)
endif()

if(NOT OMNIBOT_BOOST_INCLUDEDIR)
	message(FATAL_ERROR
		"Omni-bot needs the (header-only) Boost headers to build for WebAssembly "
		"but none were found. Install them (e.g. 'apt-get install libboost-dev') "
		"or point at them with -DOMNIBOT_BOOST_INCLUDEDIR=<dir containing boost/>, "
		"or disable the bots with -DFEATURE_OMNIBOT=OFF.")
endif()
message(STATUS "Omni-bot (wasm): using Boost headers from ${OMNIBOT_BOOST_INCLUDEDIR}")

set(OMNIBOT_WASM_PREFIX "${CMAKE_BINARY_DIR}/omni-bot")
set(OMNIBOT_WASM_BUILD_DIR "${OMNIBOT_WASM_PREFIX}/build")
set(OMNIBOT_WASM_MODULE_NAME "omnibot_et.wasm32.so")
set(OMNIBOT_WASM_BUILT_MODULE "${OMNIBOT_WASM_BUILD_DIR}/ET/${OMNIBOT_WASM_MODULE_NAME}")
# Next to cgame.mp.wasm32.so / ui.mp.wasm32.so / qagame.mp.wasm32.so.
set(OMNIBOT_WASM_MODULE "${CMAKE_BINARY_DIR}/${OMNIBOT_WASM_MODULE_NAME}" CACHE INTERNAL "")

set(OMNIBOT_WASM_CMAKE_ARGS
	-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
	# Only the ET bots are of interest; RTCW would just double the build time.
	-DOMNIBOT_ET=ON
	-DOMNIBOT_RTCW=OFF
	# No compiled Boost exists for wasm; use std::filesystem/std::regex instead.
	-DOMNIBOT_STD_FILESYSTEM=ON
	-DOMNIBOT_BOOST_INCLUDEDIR=${OMNIBOT_BOOST_INCLUDEDIR}
)

# NOTE: this builds with Emscripten's default exception model
# (DISABLE_EXCEPTION_CATCHING=1), so the bot's catch blocks are dropped while
# throw still works - a std::regex_error or a bad std::filesystem::path escapes
# into JS instead of being logged. Enabling them needs -fwasm-exceptions on this
# module *and* on the engine's main module (JS exceptions cannot work across
# dlopen), which raises the minimum browser version. See plan.md, section 7.

# Build for the same (Emscripten) target as the engine.
if(CMAKE_TOOLCHAIN_FILE)
	list(APPEND OMNIBOT_WASM_CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
endif()

ExternalProject_Add(omnibot_wasm
	SOURCE_DIR "${OMNIBOT_WASM_SOURCE_DIR}"
	PREFIX "${OMNIBOT_WASM_PREFIX}"
	BINARY_DIR "${OMNIBOT_WASM_BUILD_DIR}"
	BUILD_BYPRODUCTS "${OMNIBOT_WASM_BUILT_MODULE}" "${OMNIBOT_WASM_MODULE}"
	CMAKE_ARGS ${OMNIBOT_WASM_CMAKE_ARGS}
	INSTALL_COMMAND ""
	# Publish the side module where the packaging step expects it.
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${OMNIBOT_WASM_BUILT_MODULE}" "${OMNIBOT_WASM_MODULE}"
)

#-----------------------------------------------------------------
# Bot data (scripts + navigation meshes)
#-----------------------------------------------------------------
# Omni-bot resolves its data directory from the *location of the loaded
# library* (Utils::GetBaseFolder() takes the parent of the library path), so
# global_scripts/ and et/ have to sit next to omnibot_et.wasm32.so. They are
# shipped as one zip that the web shell downloads on demand and unpacks into
# the browser filesystem - about 5 MB, so it is deliberately NOT part of the
# always-downloaded engine image.
set(OMNIBOT_WASM_DATA_PACK "${CMAKE_BINARY_DIR}/omni-bot-data.zip" CACHE INTERNAL "")

add_custom_command(
	OUTPUT "${OMNIBOT_WASM_DATA_PACK}"
	COMMAND ${CMAKE_COMMAND} -E rm -f "${OMNIBOT_WASM_DATA_PACK}"
	COMMAND ${CMAKE_COMMAND} -E tar "cf" "${OMNIBOT_WASM_DATA_PACK}" --format=zip -- global_scripts et
	WORKING_DIRECTORY "${OMNIBOT_WASM_DATA_DIR}"
	COMMENT "Packing Omni-bot data (global_scripts, et) for the browser build"
	VERBATIM
)
add_custom_target(omnibot_wasm_data ALL DEPENDS "${OMNIBOT_WASM_DATA_PACK}")

message(STATUS "Omni-bot (wasm): building ${OMNIBOT_WASM_MODULE_NAME} + omni-bot-data.zip")
