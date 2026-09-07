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

if (EMSCRIPTEN)
	# The web build bakes its own effects: grainc is compiled to wasm like
	# everything else and run under node, which the emscripten toolchain
	# registers as CMAKE_CROSSCOMPILING_EMULATOR so custom commands naming the
	# target pick it up automatically (see cmake/grainc.cmake). It never opens
	# a window, so CF's canvas-oriented link options are harmless
	target_link_options(grainc PRIVATE
		-sNODERAWFS=1     # read the effect and module files from the real disk
		-sEXIT_RUNTIME=1  # propagate the exit code to the build system
		-sALLOW_MEMORY_GROWTH=1
	)
endif ()
