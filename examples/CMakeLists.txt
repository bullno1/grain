include(FindPkgConfig)
pkg_search_module(SDL2 REQUIRED sdl2)
pkg_search_module(GL REQUIRED gl)
pkg_search_module(GLEW REQUIRED glew)
find_package(GLM REQUIRED GLM)

include_directories(${SDL2_INCLUDE_DIRS} ${GLM_INCLUDE_DIRS} ${GL_INCLUDE_DIRS} ${GLEW_INCLUDE_DIRS})

set(RES_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../resources)
set(RES_OUT_DIR ${RES_SRC_DIR}/bin)

set(GEYSER_RES ${RES_OUT_DIR}/geyser)
add_executable(geyser common.cpp geyser.cpp ${GEYSER_RES})
set(GEYSER_RES_SRC
	${RES_SRC_DIR}/geyser.affector
	${RES_SRC_DIR}/geyser.emitter
	${RES_SRC_DIR}/point.vsh
	${RES_SRC_DIR}/point.fsh
)
add_custom_command(
	OUTPUT ${GEYSER_RES}
	COMMAND grainc ARGS -o ${GEYSER_RES} -I ${RES_SRC_DIR} ${GEYSER_RES_SRC}
	DEPENDS ${GEYSER_RES_SRC}
	VERBATIM
)
target_link_libraries(geyser grainr ${SDL2_LIBRARIES} ${GLM_LIBRARIES} ${GL_LIBRARIES} ${GLEW_LIBRARIES})

set(RAIN_RES ${RES_OUT_DIR}/rain)
add_executable(rain common.cpp rain.cpp ${RAIN_RES})
set(RAIN_RES_SRC
	${RES_SRC_DIR}/geyser.affector
	${RES_SRC_DIR}/circle_deflector.affector
	${RES_SRC_DIR}/line.emitter
	${RES_SRC_DIR}/point.vsh
	${RES_SRC_DIR}/point.fsh
)
add_custom_command(
	OUTPUT ${RAIN_RES}
	COMMAND grainc ARGS -o ${RAIN_RES} -I ${RES_SRC_DIR} ${RAIN_RES_SRC}
	DEPENDS ${RAIN_RES_SRC}
	VERBATIM
)
target_link_libraries(rain grainr ${SDL2_LIBRARIES} ${GLM_LIBRARIES} ${GL_LIBRARIES} ${GLEW_LIBRARIES})
