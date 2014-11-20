#ifndef GRAINR_SYSTEM_DEFINITION_HPP
#define GRAINR_SYSTEM_DEFINITION_HPP

#include <GL/gl.h>
#include <map>
#include <string>

namespace grainr
{

class ParticleSystem;
class Context;
class Program;

class SystemDefinition
{
	friend class Context;
	friend class ParticleSystem;
	friend class Program;
public:
	ParticleSystem* create(size_t width, size_t height) const;
	void destroy();
private:
	SystemDefinition();
	~SystemDefinition();

	size_t mNumTextures;
	std::map<std::string, GLuint> mShaders;
	const Context* mContext;
};

}

#endif
