# Standalone sample programs: plain CF apps, no bgame. Each demo lives in its
# own directory with a main.c, a resources.rc listing the incbin'd module
# sources and a modules/ directory of demo-specific modules.
#
# grain_add_demo(<name>) defines the target grain-demo-<name>. Sources see
# GRAIN_DEMO_DIR (the demo's directory) and GRAIN_MODULES_DIR (the repo's
# modules/) as string prefixes for XINCBIN paths.

if (EMSCRIPTEN)
	# The demos run a plain cf_app_is_running loop; the web build needs a
	# main-loop shell
	return()
endif ()

function (grain_add_demo NAME)
	set(TARGET "grain-demo-${NAME}")
	set(DEMO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${NAME}")
	set(BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/${NAME}")

	set(SOURCES "${DEMO_DIR}/main.c")
	if (WIN32)
		set(SOURCES ${SOURCES} "${DEMO_DIR}/resources.rc")
	endif ()

	add_executable(${TARGET} "${SOURCES}")
	target_link_libraries(${TARGET} PRIVATE grain cute blibs)

	# Module sources are incbin'd: the stock ones from modules/ and the demo's own
	set(MODULE_DIRS
		"${CMAKE_SOURCE_DIR}/modules/emitters"
		"${CMAKE_SOURCE_DIR}/modules/affectors"
		"${CMAKE_SOURCE_DIR}/modules/renderer"
		"${DEMO_DIR}/modules"
	)
	set(MODULE_SOURCES)
	foreach (DIR ${MODULE_DIRS})
		file(GLOB DIR_SOURCES CONFIGURE_DEPENDS "${DIR}/*.glsl")
		list(APPEND MODULE_SOURCES ${DIR_SOURCES})
	endforeach ()

	# Same scheme as lib/: OBJECT_DEPENDS re-embeds on edit, the stamp header
	# defeats ccache, which never sees .incbin payloads
	set(STAMP "${BINARY_DIR}/demo_stamp.h")
	add_custom_command(
		OUTPUT "${STAMP}"
		COMMAND ${CMAKE_COMMAND}
			"-DDIR=$<JOIN:${MODULE_DIRS},$<SEMICOLON>>"
			"-DOUT=${STAMP}"
			-P "${CMAKE_SOURCE_DIR}/cmake/glsl_stamp.cmake"
		DEPENDS ${MODULE_SOURCES} "${CMAKE_SOURCE_DIR}/cmake/glsl_stamp.cmake"
		COMMENT "Stamping ${NAME} demo modules"
		VERBATIM
	)
	target_sources(${TARGET} PRIVATE "${STAMP}")
	target_include_directories(${TARGET} PRIVATE "${BINARY_DIR}")
	if (WIN32)
		set_source_files_properties("${DEMO_DIR}/resources.rc" PROPERTIES OBJECT_DEPENDS "${MODULE_SOURCES}")
	else ()
		set_source_files_properties("${DEMO_DIR}/main.c" PROPERTIES OBJECT_DEPENDS "${MODULE_SOURCES}")
	endif ()

	if (MSVC)
		# The resource compiler resolves the paths against its include dirs
		target_compile_definitions(${TARGET} PRIVATE GRAIN_DEMO_DIR= GRAIN_MODULES_DIR=)
		target_include_directories(${TARGET} PRIVATE
			"$<$<COMPILE_LANGUAGE:RC>:${DEMO_DIR}>"
			"$<$<COMPILE_LANGUAGE:RC>:${CMAKE_SOURCE_DIR}/modules>"
		)
	else ()
		target_compile_definitions(${TARGET} PRIVATE
			GRAIN_DEMO_DIR="${DEMO_DIR}/"
			GRAIN_MODULES_DIR="${CMAKE_SOURCE_DIR}/modules/"
		)
	endif ()
endfunction ()

grain_add_demo(rain)
grain_add_demo(firework)
