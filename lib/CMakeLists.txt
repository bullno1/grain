set(SOURCES
	"src/grain.c"
	"src/dsl.c"
	"src/decorator.c"
	"src/baked.c"
	"src/blueprint.c"
	"src/gen/update_vert_bytecode.h"
)

add_library(grain STATIC EXCLUDE_FROM_ALL "${SOURCES}")
target_link_libraries(grain PRIVATE cute)
target_include_directories(grain PUBLIC "include")

# The glsl files are incbin'd at compile time. dsl.c includes a generated
# stamp header carrying their hashes so that editing one re-embeds it:
# OBJECT_DEPENDS alone is not enough because ccache hashes only the
# preprocessed source, never .incbin payloads
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
	# MSVC has no .incbin, so xincbin.h falls back to Win32 resources and
	# looks them up in the module holding the lib's code. A resource inside a
	# static library never reaches that module: the linker only pulls archive
	# members that resolve a symbol, and a compiled .res defines none. The
	# script is therefore an INTERFACE source, compiled into every executable
	# or DLL that links grain.
	target_sources(grain INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src/resources.rc")
	# rc resolves the RCDATA paths against its include path, so the prefix is
	# empty here and the source dir is the include dir instead
	target_compile_definitions(grain PRIVATE GRAIN_SRC_DIR=)
	target_compile_definitions(grain INTERFACE "$<$<COMPILE_LANGUAGE:RC>:GRAIN_SRC_DIR=>")
	target_include_directories(grain INTERFACE
		"$<$<COMPILE_LANGUAGE:RC>:${CMAKE_CURRENT_SOURCE_DIR}/src>"
		# For glsl_stamp.h: the script includes it so that the resource
		# compiler's dependency scan (which only sees #includes) re-embeds the
		# payloads when a glsl file changes
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
