# Build-pipeline integration for grainc: bakes effect .json files into
# single-header C modules at build time.

set(GRAINC_EXECUTABLE "" CACHE FILEPATH
	"Prebuilt grainc to bake with instead of the one this build produces")

# grain_compile_effect(<input.json> <name> <output.h> [extra grainc flags...])
#
# Bakes <output.h> from <input.json>; list <output.h> in a target's sources.
# Modules referenced by `path` are tracked through a depfile. Naming the grainc
# target lets CMAKE_CROSSCOMPILING_EMULATOR run the wasm build under node;
# GRAINC_EXECUTABLE substitutes any prebuilt tool.
function (grain_compile_effect INPUT NAME OUTPUT)
	if (GRAINC_EXECUTABLE)
		set(GRAINC_CMD "${GRAINC_EXECUTABLE}")
		set(GRAINC_DEP "${GRAINC_EXECUTABLE}")
	else ()
		set(GRAINC_CMD grainc)
		set(GRAINC_DEP grainc)
	endif ()

	# A web build only ever loads the GLES payload: skip the desktop
	# cross-compiles unless the caller picked targets explicitly
	set(GRAINC_FLAGS ${ARGN})
	if (EMSCRIPTEN AND NOT "${ARGN}" MATCHES "--target")
		list(APPEND GRAINC_FLAGS "--target=web")
	endif ()

	get_filename_component(OUTPUT_DIR "${OUTPUT}" DIRECTORY)
	add_custom_command(
		OUTPUT "${OUTPUT}"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${OUTPUT_DIR}"
		COMMAND ${GRAINC_CMD} "--name=${NAME}" ${GRAINC_FLAGS}
			"--depfile=${OUTPUT}.d" -o "${OUTPUT}" "${INPUT}"
		DEPENDS "${INPUT}" ${GRAINC_DEP}
		DEPFILE "${OUTPUT}.d"
		COMMENT "Baking grain effect ${NAME}"
		VERBATIM
	)
endfunction ()
