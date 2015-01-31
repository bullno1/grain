project(grainc)

set(CMAKE_C_FLAGS_RELWITHDEBINFO "-Os")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-Os")#-O2 leads to segfault

set(SRC
	main.cpp
	DataType.cpp
	ScriptType.cpp
	Script.cpp
	Compiler.cpp
	CompileTask.cpp
)

set(GENERATED_SRC
	"${CMAKE_CURRENT_BINARY_DIR}/builtins.c"
)

add_custom_command(
	OUTPUT ${GENERATED_SRC}
	COMMAND embed ARGS "builtins" "${CMAKE_CURRENT_SOURCE_DIR}/builtins.glsl"
	MAIN_DEPENDENCY "${CMAKE_CURRENT_SOURCE_DIR}/builtins.glsl"
	VERBATIM
)
add_executable(grainc ${SRC} ${GENERATED_SRC})
target_link_libraries(grainc glsl_optimizer)
