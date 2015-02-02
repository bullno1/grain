#include <iostream>
#include <grainr.hpp>
#include <SDL.h>
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtc/type_ptr.hpp>

using namespace grainr;
using namespace std;
using namespace glm;

SystemDefinition* sysDef = NULL;
ParticleSystem* sys = NULL;
Emitter* emitter = NULL;
Affector* affector = NULL;
Affector* deflector = NULL;
Renderer* renderer = NULL;
GLuint vao;
GLuint buff;

bool init(Context& ctx)
{
	sysDef = ctx.load("resources/bin/rain", cerr);
	if(sysDef == NULL) return false;

	sys = sysDef->create(256, 256);
	emitter = sys->createEmitter("line", cerr);
	if(emitter == NULL) return false;

	affector = sys->createAffector("geyser", cerr);
	if(affector == NULL) return false;

	renderer = sys->createRenderer("quad", cerr);
	if(renderer == NULL) return false;

	deflector = sys->createAffector("circle_deflector", cerr);
	if(deflector == NULL) return false;

	renderer->prepare();
	mat4x4 proj = glm::ortho(-400.0f, 400.0f, -300.0f, 300.0f, -1.0f, 1.0f);
	mat4x4 id;
	glUniformMatrix4fv(renderer->getUniformLocation("uMVP"), 1, false, value_ptr(id * id * proj));

	glGenBuffers(1, &buff);
	glBindBuffer(GL_ARRAY_BUFFER, buff);
	float quad[] = {
		-1.0f,  1.0f,
		 1.0f,  1.0f,
		 1.0f, -1.0,
		-1.0f, -1.0f
	};
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	glEnableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, buff);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
	glBindVertexArray(0);

	return true;
}

void cleanup()
{
	if(renderer) renderer->destroy();
	if(affector) affector->destroy();
	if(emitter) emitter->destroy();
	if(deflector) deflector->destroy();
	if(sys) sys->destroy();
	if(sysDef) sysDef->destroy();
}

void update(Context& ctx)
{
	ctx.update(3.0f / 60.0f);

	emitter->prepare();
	emitter->setParamFloat("min_life", 23.0f);
	emitter->setParamFloat("max_life", 29.0f);
	emitter->setParamFloat("max_horizontal_speed", 3.10f);
	emitter->setParamFloat("width", 800.0f);
	emitter->setParamFloat("height", 300.0f);
	emitter->setRate(0.002);
	emitter->run();

	deflector->prepare();
	deflector->setParamFloat("radius", 30.0f);
	int x, y;
	SDL_GetMouseState(&x, &y);
	GLint uniformLoc = deflector->getUniformLocation("center");
	glUniform2f(uniformLoc, x - 400, -(y - 300));
	deflector->run();
	glUniform2f(uniformLoc, 0, 0);
	deflector->run();

	affector->prepare();
	glUniform2f(affector->getUniformLocation("gravity"), 0.0f, -1.8f);
	affector->run();
}

void render()
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	renderer->prepare();
	glBindVertexArray(vao);
	sys->render(GL_TRIANGLE_FAN, 4);
}
