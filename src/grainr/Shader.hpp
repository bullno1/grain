#ifndef GRAINR_SHADER_HPP
#define GRAINR_SHADER_HPP

#include <GL/gl.h>
#include <iosfwd>

namespace grainr
{

GLuint createShader(GLenum shaderType, const char* source, std::ostream& err);
GLuint createProgram(GLuint vsh, GLuint fsh, size_t numOutputs, std::ostream& err);

}

#endif
