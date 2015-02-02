#include <GL/glew.h>
#include "Shader.hpp"
#include <sstream>
#include <iostream>

using namespace std;

namespace grainr
{

GLuint createShader(GLenum shaderType, const char* source, std::ostream& err)
{
	GLuint handle = glCreateShader(shaderType);
	glShaderSource(handle, 1, &source, NULL);
	glCompileShader(handle);
	GLint logSize, status;
	glGetShaderiv(handle, GL_INFO_LOG_LENGTH, &logSize);
	glGetShaderiv(handle, GL_COMPILE_STATUS, &status);
	if(status == GL_FALSE)
	{
		if(logSize > 0)
		{
			GLchar* infoLog = new GLchar[logSize + 1];
			glGetShaderInfoLog(handle, (GLsizei)logSize, NULL, infoLog);
			err << infoLog << std::endl;
			delete[] infoLog;
		}
		glDeleteShader(handle);
		return 0;
	}
	return handle;
}

GLuint createProgram(GLuint vsh, GLuint fsh, size_t numOutputs, std::ostream& err)
{
	GLuint prog = glCreateProgram();
	glAttachShader(prog, vsh);
	glAttachShader(prog, fsh);
	for(size_t i = 0; i < numOutputs; ++i)
	{
		stringstream ss;
		ss << "_gr_out";
		ss << i;
		glBindFragDataLocation(prog, i, ss.str().c_str());
	}

	glLinkProgram(prog);
	GLint logSize, status;
	glGetProgramiv(prog, GL_LINK_STATUS, &status);
	if(status == GL_FALSE)
	{
		glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logSize);
		if(logSize > 0)
		{
			GLchar* infoLog = new GLchar[logSize + 1];
			glGetProgramInfoLog(prog, (GLsizei)logSize, NULL, infoLog);
			err << infoLog << endl;
			delete[] infoLog;
		}
		glDeleteProgram(prog);
		return 0;
	}

	for(size_t i = 0; i < numOutputs; ++i)
	{
		glUseProgram(prog);
		stringstream ss;
		ss << "_gr_tex";
		ss << i;
		glUniform1i(glGetUniformLocation(prog, ss.str().c_str()), i);
	}

	return prog;
}

}
