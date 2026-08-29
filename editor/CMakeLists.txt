set(SOURCES
	"main.c"
	"libs.c"
	"debug_draw.c"
	"pref.c"

	"scenes/particle.c"
	"scenes/editor.c"
)
add_bgame_app(grain-editor "${SOURCES}")
target_link_libraries(grain-editor PRIVATE grain ufa)

if (EMSCRIPTEN)
	target_link_libraries(grain-editor PRIVATE bgame-emscripten-shell)

	target_link_options(grain-editor PRIVATE
		-sMALLOC=emmalloc
		"SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets@/assets"
		"SHELL:--preload-file ${CMAKE_SOURCE_DIR}/ATTRIBUTIONS.md@/assets/ATTRIBUTIONS.md"
	)
	set_property(TARGET grain-editor APPEND PROPERTY LINK_DEPENDS
		"${CMAKE_SOURCE_DIR}/assets/icon.png"
		"${CMAKE_SOURCE_DIR}/ATTRIBUTIONS.md"
	)
else ()
	# main.c mounts <exe dir>/assets at /assets
	add_custom_target(grain-editor-assets
		COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_SOURCE_DIR}/assets ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/ATTRIBUTIONS.md ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/ATTRIBUTIONS.md
	)
	add_dependencies(grain-editor grain-editor-assets)
endif ()
