include(FindPkgConfig)
pkg_search_module(SDL2 REQUIRED sdl2)
pkg_search_module(GL REQUIRED gl)
pkg_search_module(GLEW REQUIRED glew)
find_package(GLM REQUIRED GLM)

include_directories(${SDL2_INCLUDE_DIRS} ${GLM_INCLUDE_DIRS} ${GL_INCLUDE_DIRS} ${GLEW_INCLUDE_DIRS})

add_executable(geyser common.cpp geyser.cpp)
set(GEYSER_RES
	${CMAKE_CURRENT_SOURCE_DIR}/../resources/geyser.affector
	${CMAKE_CURRENT_SOURCE_DIR}/../resources/geyser.emitter
	${CMAKE_CURRENT_SOURCE_DIR}/../resources/point.vsh
	${CMAKE_CURRENT_SOURCE_DIR}/../resources/point.fsh
)
add_custom_target(geyser_resource
	COMMAND grainc -o resources/bin/geyser -I ${CMAKE_CURRENT_SOURCE_DIR}/../resources ${GEYSER_RES}
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_dependencies(geyser geyser_resource)
target_link_libraries(geyser grainr ${SDL2_LIBRARIES} ${GLM_LIBRARIES} ${GL_LIBRARIES} ${GLEW_LIBRARIES})

add_executable(rain common.cpp rain.cpp)
set(RAIN_RES
	${CMAKE_CURRENT_SOURCE_DIR}/../resources/geyser.affector
	${CMAKE_CURRENT_SOURCE_DIR}/../resources/circle_deflector.affector
	${CMAKE_CURRENT_SOURCE_DIR}/../resources/line.emitter
	${CMAKE_CURRENT_SOURCE_DIR}/../resources/point.vsh
	${CMAKE_CURRENT_SOURCE_DIR}/../resources/point.fsh
)
add_custom_target(rain_resource
	COMMAND grainc -o resources/bin/rain -I ${CMAKE_CURRENT_SOURCE_DIR}/../resources ${RAIN_RES}
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_dependencies(rain rain_resource)
target_link_libraries(rain grainr ${SDL2_LIBRARIES} ${GLM_LIBRARIES} ${GL_LIBRARIES} ${GLEW_LIBRARIES})
