cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0091 NEW)

project(Omnibot)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_STANDARD 99)

macro(add_cxx_flag flag)
    message(STATUS "Configure with flag '${flag}'")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${flag}")
endmacro()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# MSVC static runtime linkage
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(OMNIBOT_ET "Build the Enemy Territory bots module" ON)
option(OMNIBOT_RTCW "Build the Return to Castle Wolfenstein bots module" ON)
option(OMNIBOT_STATIC_BOOST "Use static linked boost libraries" ON)

# Boost.Filesystem and Boost.Regex are the only Boost libraries here that have
# to be compiled; everything else is header only. Replacing them with
# <filesystem> and <regex> means a cross build - the WebAssembly one above all -
# only needs Boost *headers*, which are architecture independent, instead of a
# full cross-compiled Boost. Requires C++17, so it stays opt-in for the native
# builds that are known to work with the C++11/Boost combination.
if (EMSCRIPTEN)
    set(OMNIBOT_STD_FILESYSTEM_DEFAULT ON)
else ()
    set(OMNIBOT_STD_FILESYSTEM_DEFAULT OFF)
endif ()
option(OMNIBOT_STD_FILESYSTEM "Use std::filesystem/std::regex instead of the compiled Boost libraries" ${OMNIBOT_STD_FILESYSTEM_DEFAULT})

if (OMNIBOT_STD_FILESYSTEM)
    set(CMAKE_CXX_STANDARD 17)
endif ()

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" SYSTEM_PROCESSOR_LOWER)
if (EMSCRIPTEN)
    # Matches ARCH_STRING and the engine's other side modules
    # (cgame.mp.wasm32.so, ...), see vendor/Omnibot/Common/BotLoadLibrary.cpp.
    string(APPEND MODULE_SUFFIX ".wasm32")
elseif (APPLE)
    string(APPEND MODULE_SUFFIX "_mac")
elseif (SYSTEM_PROCESSOR_LOWER MATCHES "(x86)|(amd64)")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        if (WIN32)
            string(APPEND MODULE_SUFFIX "_x64")
        else ()
            string(APPEND MODULE_SUFFIX ".x86_64")
        endif ()
    endif ()
elseif (SYSTEM_PROCESSOR_LOWER STREQUAL "aarch64")
    string(APPEND MODULE_SUFFIX ".aarch64")
endif ()

set(DEPENDENCIES_PATH "${CMAKE_SOURCE_DIR}/dependencies")

add_library(omni-shared INTERFACE)

set(Boost_USE_STATIC_LIBS        ${OMNIBOT_STATIC_BOOST})  # only find static libs
set(Boost_USE_DEBUG_LIBS         OFF)  # ignore debug libs and
set(Boost_USE_RELEASE_LIBS       ON)  # only find release libs
set(Boost_USE_MULTITHREADED      ON)

if (WIN32)
    set(Boost_USE_STATIC_RUNTIME    ON)
else ()
    set(Boost_USE_STATIC_RUNTIME    OFF)
endif ()

if (OMNIBOT_STD_FILESYSTEM)
    find_package(Boost REQUIRED)
    target_link_libraries(omni-shared INTERFACE Boost::boost)
    target_compile_definitions(omni-shared INTERFACE OMNIBOT_USE_STD_FILESYSTEM)
    if (NOT MSVC)
        # C++17 removed the (long ignored) "register" storage class, which the
        # vendored gmscript sources still use - a hard error instead of the
        # warning it is in C++11.
        target_compile_options(omni-shared INTERFACE $<$<COMPILE_LANGUAGE:CXX>:-Wno-register>)
    endif ()
else ()
    find_package(Boost COMPONENTS system filesystem regex date_time REQUIRED)
    target_link_libraries(omni-shared INTERFACE Boost::boost Boost::filesystem Boost::regex Boost::date_time)
endif ()

target_include_directories(omni-shared INTERFACE "${DEPENDENCIES_PATH}/WildMagic5/SDK/include")

if (NOT WIN32)
    add_compile_options(-Wall -Wno-deprecated -fno-strict-aliasing)
endif ()
add_compile_options("$<$<CONFIG:DEBUG>:-D_DEBUG>")

add_subdirectory(Common)

if (OMNIBOT_ET)
    add_subdirectory(ET)
    # Set ET as the startup project in Visual Studio
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT omnibot-et)
endif ()

if (OMNIBOT_RTCW)
    add_subdirectory(RTCW)
    if (NOT OMNIBOT_ET)
        set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT omnibot-rtcw)
    endif ()
endif ()
