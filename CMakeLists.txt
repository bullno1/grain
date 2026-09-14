cmake_minimum_required(VERSION 3.25)
project(grain-editor)

include("deps/bgame/cmake/prelude.cmake")

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
