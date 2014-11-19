project(grainc)

set(CMAKE_C_FLAGS_RELWITHDEBINFO "-Os")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-Os")#-O2 leads to segfault

set(SRC
	main.cpp
	grainc.cpp
)

add_executable(grainc ${SRC})
target_link_libraries(grainc glsl_optimizer)
