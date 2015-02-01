include(FindPkgConfig)
pkg_search_module(SDL2 REQUIRED sdl2)
pkg_search_module(GL REQUIRED gl)
pkg_search_module(GLEW REQUIRED glew)
find_package(GLM REQUIRED GLM)

include_directories(${SDL2_INCLUDE_DIRS} ${GLM_INCLUDE_DIRS} ${GL_INCLUDE_DIRS} ${GLEW_INCLUDE_DIRS})

set(RES_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../resources)
set(RES_OUT_DIR ${RES_SRC_DIR}/bin)

function(add_demo DEMO_NAME)
	set(RES ${RES_OUT_DIR}/${DEMO_NAME})
	add_executable(${DEMO_NAME} common.cpp ${DEMO_NAME}.cpp ${RES})
	add_custom_command(
		OUTPUT ${RES}
		COMMAND grainc ARGS -o ${RES} -I ${RES_SRC_DIR} ${ARGN}
		DEPENDS ${ARGN}
		VERBATIM
	)
	target_link_libraries(${DEMO_NAME} grainr ${SDL2_LIBRARIES} ${GLM_LIBRARIES} ${GL_LIBRARIES} ${GLEW_LIBRARIES})
endfunction(add_demo)

add_demo(geyser
	${RES_SRC_DIR}/geyser.affector
	${RES_SRC_DIR}/geyser.emitter
	${RES_SRC_DIR}/point.vsh
	${RES_SRC_DIR}/point.fsh
)

add_demo(rain
	${RES_SRC_DIR}/geyser.affector
	${RES_SRC_DIR}/circle_deflector.affector
	${RES_SRC_DIR}/line.emitter
	${RES_SRC_DIR}/point.vsh
	${RES_SRC_DIR}/point.fsh
)
