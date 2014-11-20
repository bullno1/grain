#ifndef GRAINR_CONTEXT_HPP
#define GRAINR_CONTEXT_HPP

#include <iosfwd>
#include <GL/gl.h>

namespace grainr
{

class SystemDefinition;
class ParticleSystem;
class Program;

class Context
{
	friend class ParticleSystem;
	friend class Program;
public:
	Context();
	~Context();

	SystemDefinition* load(const char* filename, std::ostream& err) const;
	void update(float dt);

private:
	Context(Context& other);

	GLuint mQuadBuff;
	GLuint mUpdateVAO;
	GLuint mUpdateVsh;
	GLuint mQuadVsh;
	float mTime;
	float mDt;
};

}

#endif
