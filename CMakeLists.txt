set(SOURCES
	"src/grain.c"
	"src/dsl.c"
	"src/gen/update_vert_bytecode.h"
)

if (WIN32)
	set(SOURCES ${SOURCES} "resources.rc")
endif ()

add_library(grain STATIC EXCLUDE_FROM_ALL "${SOURCES}")
target_link_libraries(grain PRIVATE cute)
target_include_directories(grain PUBLIC "include")
target_compile_definitions(grain PRIVATE GRAIN_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/")

compile_vertex_shader(
	${CMAKE_CURRENT_SOURCE_DIR}/src/glsl/update.vert.glsl
	grain_update_vert_bytecode
	${CMAKE_CURRENT_SOURCE_DIR}/src/gen/update_vert_bytecode.h
)
