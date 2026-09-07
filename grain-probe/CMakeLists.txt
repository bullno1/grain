# grain-probe: runs an effect offline through the probe and reports or writes
# back pool sizing and bounds. A CF app with a hidden window, so it needs a
# GPU (or Mesa's llvmpipe under SDL_VIDEO_DRIVER=offscreen); never built for
# the web.
if (EMSCRIPTEN)
	return()
endif ()

add_executable(grain-probe "grain-probe.c")
target_link_libraries(grain-probe PRIVATE grain cute)
target_include_directories(grain-probe PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../deps/blibs"
)
