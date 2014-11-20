#include <GL/glew.h>
#include "SystemDefinition.hpp"
#include "ParticleSystem.hpp"

namespace grainr
{

SystemDefinition::SystemDefinition()
{
}

SystemDefinition::~SystemDefinition()
{
	for(std::map<std::string, GLuint>::const_iterator itr = mShaders.begin(); itr != mShaders.end(); ++ itr)
	{
		glDeleteShader(itr->second);
	}
}

ParticleSystem* SystemDefinition::create(size_t width, size_t height) const
{
	return new ParticleSystem(this, width, height);
}

void SystemDefinition::destroy()
{
	delete this;
}

}
