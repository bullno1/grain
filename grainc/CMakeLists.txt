# grainc is a host tool; cross builds consume its output instead (see
# cmake/grainc.cmake)
if (EMSCRIPTEN)
	return()
endif ()

add_executable(grainc
	"grainc.c"
	"emit.c"
	"emit.h"
)
target_link_libraries(grainc PRIVATE grain cute)
target_include_directories(grainc PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../lib/src"
	"${CMAKE_CURRENT_SOURCE_DIR}/../deps/blibs"
)
