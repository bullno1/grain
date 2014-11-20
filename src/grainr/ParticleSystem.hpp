#ifndef GRAINR_PARTICLE_SYSTEM_HPP
#define GRAINR_PARTICLE_SYSTEM_HPP

#include <iosfwd>
#include <vector>
#include <GL/gl.h>

namespace grainr
{

class SystemDefinition;
class Emitter;
class Affector;
class Renderer;

class ParticleSystem
{
	friend class SystemDefinition;
	friend class Program;
	friend class Emitter;
public:
	void destroy();

	Emitter* createEmitter(const char* name, std::ostream& err);
	Affector* createAffector(const char* name, std::ostream& err);
	Renderer* createRenderer(const char* name, std::ostream& err);
	void render(GLenum primType, GLsizei count);
private:
	ParticleSystem(const SystemDefinition* def, size_t width, size_t height);
	~ParticleSystem();

	void flip();

	std::vector<GLenum> mOddTargets;
	std::vector<GLenum> mEvenTargets;
	std::vector<GLuint> mOddTextures;
	std::vector<GLuint> mEvenTextures;
	GLuint mFbo;
	size_t mTexWidth;
	size_t mTexHeight;
	bool mFlipFlag;
	const SystemDefinition* mDef;
};

}

#endif
