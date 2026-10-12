# cute_framework, remodule, blibs and clay
bgame_add_dependencies()

# --- ufa ---
# Link the same SDL3 as cute_framework
get_target_property(CUTE_LINK_LIBS cute INTERFACE_LINK_LIBRARIES)
foreach (LIB IN LISTS CUTE_LINK_LIBS)
	if (LIB MATCHES "^SDL3::SDL3")
		set(UFA_SDL_TARGET "${LIB}")
	endif ()
endforeach ()
add_subdirectory("ufa" EXCLUDE_FROM_ALL)
# Make ufa reloadable
target_link_libraries(ufa PRIVATE bgame)
target_compile_definitions(ufa PRIVATE
	UFA_FN_WRAPPER_INCLUDE=<bgame/reloadable.h>
	UFA_FN_WRAPPER=BGAME_FN_WRAPPER
)
