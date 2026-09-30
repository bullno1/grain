# Standalone sample programs: plain CF apps, no bgame. grain_add_demo(<name>)
# defines grain-demo-<name> and bakes every <effect>.json in its directory
# into gen/grain_<effect>.h.

function (grain_add_demo NAME)
	set(TARGET "grain-demo-${NAME}")
	set(DEMO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${NAME}")
	set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/${NAME}/gen")

	set(SOURCES "${DEMO_DIR}/main.c")
	file(GLOB EFFECTS CONFIGURE_DEPENDS "${DEMO_DIR}/*.json")
	foreach (EFFECT ${EFFECTS})
		get_filename_component(EFFECT_NAME "${EFFECT}" NAME_WE)
		set(HEADER "${GEN_DIR}/grain_${EFFECT_NAME}.h")
		grain_compile_effect("${EFFECT}" "${EFFECT_NAME}" "${HEADER}")
		list(APPEND SOURCES "${HEADER}")
	endforeach ()

	add_executable(${TARGET} "${SOURCES}")
	target_link_libraries(${TARGET} PRIVATE grain cute)
	target_include_directories(${TARGET} PRIVATE "${GEN_DIR}")

	if (EMSCRIPTEN)
		# Same shell as the editor. CF's link options already bring ASYNCIFY,
		# which lets the plain cf_app_is_running loop run in the browser
		set_target_properties(${TARGET} PROPERTIES SUFFIX ".html")
		target_compile_options(${TARGET} PRIVATE -gsplit-dwarf)
		target_link_libraries(${TARGET} PRIVATE bgame-emscripten-shell)
		target_link_options(${TARGET} PRIVATE
			-sMALLOC=emmalloc
			-gseparate-dwarf
		)
	endif ()
endfunction ()

grain_add_demo(rain)
grain_add_demo(firework)
