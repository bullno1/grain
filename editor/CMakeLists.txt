set(SOURCES
	"main.c"
	"libs.c"

	"scenes/particle.c"
	"scenes/editor.c"
)
add_bgame_app(grain-editor "${SOURCES}")
target_link_libraries(grain-editor PRIVATE grain ufa)

if (EMSCRIPTEN)
	target_link_libraries(grain-editor PRIVATE bgame-emscripten-shell)

	target_link_options(grain-editor PRIVATE
		-sMALLOC=emmalloc
	)
endif ()
