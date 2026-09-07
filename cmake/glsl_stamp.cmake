# Writes a header whose content changes whenever any embedded glsl changes.
# dsl.c includes it so compiler caches (ccache) see incbin'd content updates,
# which never appear in the preprocessed source on their own.
file(GLOB files "${DIR}/*.glsl")
set(stamp "// Generated; do not edit\n")
foreach(f ${files})
	file(SHA1 "${f}" hash)
	get_filename_component(name "${f}" NAME)
	string(APPEND stamp "// ${name} ${hash}\n")
endforeach()

if(EXISTS "${OUT}")
	file(READ "${OUT}" old)
else()
	set(old "")
endif()
if(NOT stamp STREQUAL old)
	file(WRITE "${OUT}" "${stamp}")
endif()
