project(grainr)

include(FindPkgConfig)
pkg_search_module(GL REQUIRED gl)
pkg_search_module(GLEW REQUIRED glew)

set(SRC
	Context.cpp
	ParticleSystem.cpp
	SystemDefinition.cpp
	Program.cpp
	Shader.cpp
)

add_library(grainr ${SRC})
add_definitions(-DGL_GLEXT_PROTOTYPES)
include_directories(${GL_INCLUDE_DIRS} ${GLEW_INCLUDE_DIRS})
target_include_directories(grainr PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(grainr ${GL_LIBRARIES} ${GLEW_LIBRARIES})
