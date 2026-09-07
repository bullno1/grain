# Build-pipeline integration for grainc: bakes effect .json files into
# single-header C modules at build time.

set(GRAINC_EXECUTABLE "" CACHE FILEPATH
	"Native grainc binary, required when cross-compiling")

# grain_compile_effect(<input.json> <name> <output.h> [extra grainc flags...])
#
# Adds a custom command producing <output.h> from <input.json> with the effect
# named <name> (symbols become grain_<name>_*). List <output.h> in a target's
# sources to hook up the dependency.
#
# Modules the effect references by `path` (without an embedded `source`) are
# read by grainc relative to the .json and tracked through a depfile, so
# editing one of those .glsl files re-bakes the header.
function (grain_compile_effect INPUT NAME OUTPUT)
	if (GRAINC_EXECUTABLE)
		set(GRAINC_CMD "${GRAINC_EXECUTABLE}")
		set(GRAINC_DEP "${GRAINC_EXECUTABLE}")
	elseif (EMSCRIPTEN)
		message(FATAL_ERROR
			"grain_compile_effect needs a native grainc when cross-compiling; "
			"set GRAINC_EXECUTABLE")
	else ()
		set(GRAINC_CMD grainc)
		set(GRAINC_DEP grainc)
	endif ()

	get_filename_component(OUTPUT_DIR "${OUTPUT}" DIRECTORY)
	add_custom_command(
		OUTPUT "${OUTPUT}"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${OUTPUT_DIR}"
		COMMAND ${GRAINC_CMD} "--name=${NAME}" ${ARGN}
			"--depfile=${OUTPUT}.d" -o "${OUTPUT}" "${INPUT}"
		DEPENDS "${INPUT}" ${GRAINC_DEP}
		DEPFILE "${OUTPUT}.d"
		COMMENT "Baking grain effect ${NAME}"
		VERBATIM
	)
endfunction ()
