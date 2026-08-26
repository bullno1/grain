if (EMSCRIPTEN)
	return()
endif ()

set(SOURCES
	"main.c"
	"shared.h"
	"scanner.c"
	"find.c"
	"module.c"
	"kind.c"
	"blueprint.c"
	"clock.c"
)

add_executable(grain-tests "${SOURCES}")
target_link_libraries(grain-tests PRIVATE grain cute)
target_include_directories(grain-tests PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../lib/src"
	"${CMAKE_CURRENT_SOURCE_DIR}/../deps/blibs"
)
