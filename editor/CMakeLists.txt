set(SOURCES
	"main.c"
	"libs.c"
	"debug_draw.c"
	"pref.c"

	"scenes/particle.c"
	"scenes/editor.c"
)

if (WIN32)
	# Not resources.rc: Visual Studio names a .res after its script alone, and
	# grain brings its own resources.rc
	set(SOURCES ${SOURCES} "editor_resources.rc")
endif ()

add_bgame_app(grain-editor "${SOURCES}")
target_link_libraries(grain-editor PRIVATE grain ufa)

if (MSVC)
	target_compile_definitions(grain-editor PRIVATE PROJECT_DIR=)
	target_include_directories(grain-editor PRIVATE "$<$<COMPILE_LANGUAGE:RC>:${CMAKE_SOURCE_DIR}>")
else ()
	target_compile_definitions(grain-editor PRIVATE PROJECT_DIR="${CMAKE_SOURCE_DIR}/")
endif ()

if (EMSCRIPTEN)
	target_link_libraries(grain-editor PRIVATE bgame-emscripten-shell)

	target_link_options(grain-editor PRIVATE
		-sMALLOC=emmalloc
		"SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets@/assets"
	)
endif ()

set_property(TARGET grain-editor APPEND PROPERTY LINK_DEPENDS "${CMAKE_SOURCE_DIR}/ATTRIBUTIONS.md")
