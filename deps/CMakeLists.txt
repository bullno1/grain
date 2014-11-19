set(CMAKE_C_FLAGS_RELWITHDEBINFO "")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "")#-O2 leads to segfault
add_subdirectory(glsl-optimizer EXCLUDE_FROM_ALL)
target_include_directories(glsl_optimizer PUBLIC "glsl-optimizer/src/glsl")
