if (EMSCRIPTEN)
	return()
endif ()

# Bake the sample effects at build time; the generated headers are consumed
# by baked_codegen.c / baked_codegen_impl.c to prove the emitted C compiles,
# links (two TUs, one implementation) and loads
grain_compile_effect(
	"${CMAKE_CURRENT_SOURCE_DIR}/../samples/fire.json"
	fire
	"${CMAKE_CURRENT_BINARY_DIR}/gen/grain_fire.h"
)
grain_compile_effect(
	"${CMAKE_CURRENT_SOURCE_DIR}/../samples/snow.json"
	snow
	"${CMAKE_CURRENT_BINARY_DIR}/gen/grain_snow.h"
)

set(SOURCES
	"main.c"
	"shared.h"
	"scanner.c"
	"find.c"
	"module.c"
	"kind.c"
	"sampler.c"
	"archetype.c"
	"baked.c"
	"baked_codegen.c"
	"baked_codegen_impl.c"
	"blueprint.c"
	"clock.c"
	"samples.c"
	"${CMAKE_CURRENT_BINARY_DIR}/gen/grain_fire.h"
	"${CMAKE_CURRENT_BINARY_DIR}/gen/grain_snow.h"
)

add_executable(grain-tests "${SOURCES}")
target_link_libraries(grain-tests PRIVATE grain cute)
target_compile_definitions(grain-tests PRIVATE
	GRAIN_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../samples"
)
target_include_directories(grain-tests PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../lib/src"
	"${CMAKE_CURRENT_SOURCE_DIR}/../deps/blibs"
	"${CMAKE_CURRENT_BINARY_DIR}/gen"
)
