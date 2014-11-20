#ifndef GRAINR_PROGRAM_HPP
#define GRAINR_PROGRAM_HPP

#include <GL/gl.h>

namespace grainr
{

class ParticleSystem;

//TODO: state cache
class Program
{
	friend class ParticleSystem;
public:
	void prepare();
	void setParamFloat(const char* name, float value);
	void setParamVec2(const char* name, float* vec);
	void setParamVec3(const char* name, float* vec);
	void setParamVec4(const char* name, float* vec);
	GLint getUniformLocation(const char* name);
	void run();
	void destroy();

protected:
	Program();
	virtual ~Program();

	GLuint mHandle;
	ParticleSystem* mSystem;
};

class Emitter: public Program
{
	friend class ParticleSystem;
public:
	void setRate(float rate);

private:
	Emitter();
	virtual ~Emitter();
};

class Affector: public Program
{
	friend class ParticleSystem;
public:
private:
	Affector();
	virtual ~Affector();
};

class Renderer: public Program
{
	friend class ParticleSystem;
public:
private:
	Renderer();
	virtual ~Renderer();
};

}

#endif
