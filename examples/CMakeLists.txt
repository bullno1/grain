include(FindPkgConfig)
pkg_search_module(SDL2 REQUIRED sdl2)
pkg_search_module(GL REQUIRED gl)
pkg_search_module(GLEW REQUIRED glew)
find_package(GLM REQUIRED GLM)

include_directories(${SDL2_INCLUDE_DIRS} ${GLM_INCLUDE_DIRS} ${GL_INCLUDE_DIRS} ${GLEW_INCLUDE_DIRS})

add_executable(geyser common.cpp geyser.cpp)
target_link_libraries(geyser grainr ${SDL2_LIBRARIES} ${GLM_LIBRARIES} ${GL_LIBRARIES} ${GLEW_LIBRARIES})

add_executable(rain common.cpp rain.cpp)
target_link_libraries(rain grainr ${SDL2_LIBRARIES} ${GLM_LIBRARIES} ${GL_LIBRARIES} ${GLEW_LIBRARIES})
