# Standalone sample programs: plain CF apps, no bgame. Each demo lives in its
# own directory with a main.c, one effect .json per archetype and a modules/
# directory of demo-specific modules.
#
# grain_add_demo(<name>) defines the target grain-demo-<name>. Every
# <effect>.json in the demo's directory is baked by grainc into
# gen/grain_<effect>.h (symbols grain_<effect>_*); the effect files reference
# their modules by path, so the .glsl files stay the source of truth.

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
endfunction ()

grain_add_demo(rain)
grain_add_demo(firework)
