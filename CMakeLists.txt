cmake_minimum_required(VERSION 3.25)
project(grain-editor)

# Fix output dir
set(CMAKE_BINARY_DIR ${CMAKE_SOURCE_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${PLATFORM_NAME}/$<CONFIG>-$<IF:$<BOOL:${RELOADABLE}>,reloadable,static>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${PLATFORM_NAME}/$<CONFIG>-$<IF:$<BOOL:${RELOADABLE}>,reloadable,static>)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/${PLATFORM_NAME}/$<CONFIG>-$<IF:$<BOOL:${RELOADABLE}>,reloadable,static>)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_EXTENSIONS OFF)

# In a reloadable build the static libs (bgame, grain, ufa) are linked into the
# app shared object, so everything must be built as PIC.
if (RELOADABLE)
	set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif ()

#include(CheckIPOSupported)
#check_ipo_supported(RESULT IPO_SUPPORTED)
#if (IPO_SUPPORTED AND (NOT RELOADABLE))
	#set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
#endif ()

# Add deps first

add_subdirectory(deps)

# Then everything else to avoid leaking into deps

include("deps/bgame/bgame.cmake")
include("cmake/grainc.cmake")
add_subdirectory(lib)
add_subdirectory(editor)
add_subdirectory(grainc)
add_subdirectory(grain-probe)
add_subdirectory(tests)
add_subdirectory(demos)
