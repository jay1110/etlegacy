# Omnibot ET build

add_library(omnibot-et MODULE ${CMAKE_CURRENT_SOURCE_DIR}/ET_BatchBuild.cpp)
target_link_libraries(omnibot-et omni-shared omnibot-common)

set_target_properties(omnibot-et PROPERTIES
    PREFIX ""
    OUTPUT_NAME "omnibot_et${MODULE_SUFFIX}"
    # POSITION_INDEPENDENT_CODE ON
    # MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

if (EMSCRIPTEN)
    # A dlopen()-able wasm side module, named like the engine's other ones
    # (omnibot_et.wasm32.so) so BotLoadLibrary.cpp finds it. The web shell has
    # to compile it up front (FS.createPreloadedFile) just like cgame/ui/qagame,
    # because the browser will not compile a module synchronously on the main
    # thread and dlopen() would fail otherwise.
    set_target_properties(omnibot-et PROPERTIES SUFFIX ".so")
    target_link_options(omnibot-et PRIVATE -sSIDE_MODULE=1)
endif ()

if (WIN32)
    set_target_properties(omnibot-et PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY ""
            LIBRARY_OUTPUT_DIRECTORY_DEBUG ""
            LIBRARY_OUTPUT_DIRECTORY_RELEASE ""
    )
    target_sources(omnibot-et PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../projects/msvc12/Common/Common.rc)
endif ()
