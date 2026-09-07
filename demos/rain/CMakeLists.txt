# grain-demo-rain: a standalone CF app (no bgame) showing canvas-driven
# collision: rain streaks bouncing off moving platforms
if (EMSCRIPTEN)
	# Plain cf_app_is_running loop; the web build needs a main-loop shell
	return()
endif ()

set(SOURCES "main.c")
if (WIN32)
	set(SOURCES ${SOURCES} "resources.rc")
endif ()

add_executable(grain-demo-rain "${SOURCES}")
target_link_libraries(grain-demo-rain PRIVATE grain cute blibs)

# Module sources are incbin'd: the stock ones from modules/ and this demo's own
set(RAIN_MODULE_DIRS
	"${CMAKE_SOURCE_DIR}/modules/emitters"
	"${CMAKE_SOURCE_DIR}/modules/affectors"
	"${CMAKE_CURRENT_SOURCE_DIR}/modules"
)
set(RAIN_MODULE_SOURCES)
foreach (DIR ${RAIN_MODULE_DIRS})
	file(GLOB DIR_SOURCES CONFIGURE_DEPENDS "${DIR}/*.glsl")
	list(APPEND RAIN_MODULE_SOURCES ${DIR_SOURCES})
endforeach ()

# Same scheme as lib/: OBJECT_DEPENDS re-embeds on edit, the stamp header
# defeats ccache, which never sees .incbin payloads
add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rain_stamp.h"
	COMMAND ${CMAKE_COMMAND}
		"-DDIR=$<JOIN:${RAIN_MODULE_DIRS},$<SEMICOLON>>"
		"-DOUT=${CMAKE_CURRENT_BINARY_DIR}/rain_stamp.h"
		-P "${CMAKE_SOURCE_DIR}/cmake/glsl_stamp.cmake"
	DEPENDS ${RAIN_MODULE_SOURCES} "${CMAKE_SOURCE_DIR}/cmake/glsl_stamp.cmake"
	COMMENT "Stamping rain demo modules"
	VERBATIM
)
target_sources(grain-demo-rain PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/rain_stamp.h")
target_include_directories(grain-demo-rain PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
if (WIN32)
	set_source_files_properties("resources.rc" PROPERTIES OBJECT_DEPENDS "${RAIN_MODULE_SOURCES}")
else ()
	set_source_files_properties("main.c" PROPERTIES OBJECT_DEPENDS "${RAIN_MODULE_SOURCES}")
endif ()

if (MSVC)
	# The resource compiler resolves the paths against its include dirs
	target_compile_definitions(grain-demo-rain PRIVATE RAIN_DEMO_DIR= GRAIN_MODULES_DIR=)
	target_include_directories(grain-demo-rain PRIVATE
		"$<$<COMPILE_LANGUAGE:RC>:${CMAKE_CURRENT_SOURCE_DIR}>"
		"$<$<COMPILE_LANGUAGE:RC>:${CMAKE_SOURCE_DIR}/modules>"
	)
else ()
	target_compile_definitions(grain-demo-rain PRIVATE
		RAIN_DEMO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/"
		GRAIN_MODULES_DIR="${CMAKE_SOURCE_DIR}/modules/"
	)
endif ()
