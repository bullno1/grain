set(SOURCES
	"src/grain.c"
	"src/dsl.c"
	"src/decorator.c"
	"src/baked.c"
	"src/blueprint.c"
	"src/probe.c"
	"src/gen/update_vert_bytecode.h"
)

add_library(grain STATIC EXCLUDE_FROM_ALL "${SOURCES}")
target_link_libraries(grain PRIVATE cute)
target_include_directories(grain PUBLIC "include")

# The stamp header carries the glsl hashes so editing one re-embeds it:
# OBJECT_DEPENDS alone is not enough because ccache never sees .incbin payloads
file(GLOB GRAIN_GLSL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/glsl/*.glsl")
add_custom_command(
	OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/glsl_stamp.h"
	COMMAND ${CMAKE_COMMAND}
		"-DDIR=${CMAKE_CURRENT_SOURCE_DIR}/src/glsl"
		"-DOUT=${CMAKE_CURRENT_BINARY_DIR}/glsl_stamp.h"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/glsl_stamp.cmake"
	DEPENDS ${GRAIN_GLSL_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/glsl_stamp.cmake"
	COMMENT "Stamping embedded glsl"
)
target_sources(grain PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/glsl_stamp.h")
target_include_directories(grain PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

if (MSVC)
	# MSVC falls back to Win32 resources, and a .res inside a static library is
	# never pulled in by the linker (it defines no symbol), so the script is an
	# INTERFACE source compiled into every module that links grain
	target_sources(grain INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src/resources.rc")
	# rc resolves the RCDATA paths against its include path, so the prefix is
	# empty here (the script defines it for rc itself: Visual Studio generators
	# ignore per-language definitions) and the source dir is the include dir
	target_compile_definitions(grain PRIVATE GRAIN_SRC_DIR=)
	target_include_directories(grain INTERFACE
		"$<$<COMPILE_LANGUAGE:RC>:${CMAKE_CURRENT_SOURCE_DIR}/src>"
		# The script #includes glsl_stamp.h so the resource compiler's
		# dependency scan re-embeds the payloads when a glsl file changes
		"$<$<COMPILE_LANGUAGE:RC>:${CMAKE_CURRENT_BINARY_DIR}>"
	)
else ()
	target_compile_definitions(grain PRIVATE GRAIN_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/")
endif ()

compile_vertex_shader(
	${CMAKE_CURRENT_SOURCE_DIR}/src/glsl/update.vert.glsl
	grain_update_vert_bytecode
	${CMAKE_CURRENT_SOURCE_DIR}/src/gen/update_vert_bytecode.h
)
