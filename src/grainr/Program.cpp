#include <GL/glew.h>
#include "Program.hpp"
#include "ParticleSystem.hpp"
#include "SystemDefinition.hpp"
#include "Context.hpp"
#include <iostream>

namespace grainr
{

Program::Program()
{}

Program::~Program()
{
	glDeleteProgram(mHandle);
}

void Program::destroy()
{
	delete this;
}

GLint Program::getUniformLocation(const char* name)
{
	return glGetUniformLocation(mHandle, name);
}

void Program::prepare()
{
	glUseProgram(mHandle);
}

void Program::run()
{
	const Context* context = mSystem->mDef->mContext;
	glUniform1f(getUniformLocation("_gr_time"), context->mTime);
	glUniform1f(getUniformLocation("dt"), context->mDt);

	mSystem->flip();
	glViewport(0, 0, mSystem->mTexWidth, mSystem->mTexHeight);
	glBindVertexArray(context->mUpdateVAO);
	glDrawArrays(GL_QUADS, 0, 4);
}

void Program::setParamFloat(const char* name, float value)
{
	glUniform1f(getUniformLocation(name), value);
}

void Program::setParamVec2(const char* name, float* vec)
{
	glUniform2fv(getUniformLocation(name), 1, vec);
}

void Program::setParamVec3(const char* name, float* vec)
{
	glUniform3fv(getUniformLocation(name), 1, vec);
}

void Program::setParamVec4(const char* name, float* vec)
{
	glUniform4fv(getUniformLocation(name), 1, vec);
}

Emitter::Emitter()
{}

Emitter::~Emitter()
{}

void Emitter::setRate(float rate)
{
	glUniform1f(getUniformLocation("_gr_chance"), rate);
}

Affector::Affector()
{}

Affector::~Affector()
{}

Renderer::Renderer()
{}

Renderer::~Renderer()
{}

}
