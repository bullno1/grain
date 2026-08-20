set(SOURCES
	"src/grain.c"
	"src/dsl.c"
)

if (WIN32)
	set(SOURCES ${SOURCES} "resources.rc")
endif ()

add_library(grain STATIC EXCLUDE_FROM_ALL "${SOURCES}")
target_link_libraries(grain PRIVATE cute)
target_include_directories(grain PUBLIC "include")
target_compile_definitions(grain PRIVATE GRAIN_SRC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/")
