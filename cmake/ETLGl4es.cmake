#-----------------------------------------------------------------
# gl4es - desktop OpenGL 1.x/2.x over GLES2/WebGL
#
# When FEATURE_GL4ES is enabled (Emscripten browser build), the OpenGL1
# renderer's desktop GL calls are translated to WebGL/GLES2 by gl4es
# (https://github.com/ptitSeb/gl4es), which also supplies the desktop GL entry
# points. This replaces both Emscripten's LEGACY_GL_EMULATION and the desktop
# GLEW loader. gl4es is fetched and built as a static library (libGL.a) via
# ExternalProject and exposed through the bundled_gl4es_int interface target.
#
# This lives in the main repository (not the libs submodule) so that the wiring
# ships with the browser-build changes.
#-----------------------------------------------------------------

if(NOT (FEATURE_GL4ES AND BUILD_CLIENT))
	return()
endif()

include(ExternalProject)

add_library(bundled_gl4es_int INTERFACE)

set(GL4ES_PREFIX "${CMAKE_BINARY_DIR}/gl4es")
set(GL4ES_SOURCE_DIR "${GL4ES_PREFIX}/src/bundled_gl4es")
# gl4es forces its archive output to <source>/lib regardless of the build dir.
set(GL4ES_LIBRARY "${GL4ES_SOURCE_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}GL${CMAKE_STATIC_LIBRARY_SUFFIX}")

set(GL4ES_CMAKE_ARGS
	-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
	-DSTATICLIB=ON
	-DNOX11=ON
	-DNOEGL=ON
	-DNO_GBM=ON
	-DNO_LOADER=ON
)

# Forward the (Emscripten) toolchain so gl4es is built for the same target.
if(CMAKE_TOOLCHAIN_FILE)
	list(APPEND GL4ES_CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
endif()

ExternalProject_Add(bundled_gl4es
	GIT_REPOSITORY https://github.com/ptitSeb/gl4es.git
	GIT_TAG 17f0894e19d1553e4176276c759915dab44c08e2
	PREFIX "${GL4ES_PREFIX}"
	BUILD_BYPRODUCTS "${GL4ES_LIBRARY}"
	CMAKE_ARGS ${GL4ES_CMAKE_ARGS}
	INSTALL_COMMAND ""
)

add_dependencies(bundled_gl4es_int bundled_gl4es)
target_link_libraries(bundled_gl4es_int INTERFACE "${GL4ES_LIBRARY}")
target_include_directories(bundled_gl4es_int INTERFACE "${GL4ES_SOURCE_DIR}/include")
