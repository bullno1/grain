# cute_framework, remodule, blibs and clay
bgame_add_dependencies()

# --- ufa ---
add_subdirectory("ufa" EXCLUDE_FROM_ALL)
# Make ufa reloadable
target_link_libraries(ufa PRIVATE bgame)
target_compile_definitions(ufa PRIVATE
	UFA_FN_WRAPPER_INCLUDE=<bgame/reloadable.h>
	UFA_FN_WRAPPER=BGAME_FN_WRAPPER
)
