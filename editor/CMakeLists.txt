set(SOURCES
	"main.c"

	"scenes/particle.c"
)
add_bgame_app(grain-editor "${SOURCES}")
target_link_libraries(grain-editor PRIVATE grain)

if (EMSCRIPTEN)
	target_link_libraries(grain-editor PRIVATE bgame-emscripten-shell)

	target_link_options(grain-editor PRIVATE
		-sMALLOC=emmalloc
		--preload-file "${CMAKE_CURRENT_LIST_DIR}/../assets@/assets"
	)
endif ()
