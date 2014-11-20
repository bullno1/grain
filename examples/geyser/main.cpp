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

bool init(Context& ctx);
void update(Context& ctx);
void render();
void cleanup();

int main(int argc, const char* const argv[])
{
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER);
	SDL_Window* wnd = SDL_CreateWindow("geyser", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 800, 600, SDL_WINDOW_OPENGL);
	SDL_GLContext glCtx = SDL_GL_CreateContext(wnd);
	glewInit();
	if(SDL_GL_SetSwapInterval(-1) < 0)
		SDL_GL_SetSwapInterval(1);

	grainr::Context ctx;
	if(!init(ctx))
	{
		cleanup();
		SDL_Quit();
		return 1;
	}

	bool running = true;
	SDL_Event event;
	double lastTime = (double)SDL_GetTicks();
	double targetTime = 1000.0 / 60.0;

	while(running)
	{
		while(SDL_PollEvent(&event))
		{
			switch(event.type)
			{
				case SDL_QUIT:
					running = false;
					break;
			}
		}

		update(ctx);

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glViewport(0, 0, 800, 600);
		render();
		SDL_GL_SwapWindow(wnd);

		double currentTime = (double)SDL_GetTicks();
		double frameTime = currentTime - lastTime;
		if(frameTime > targetTime)
			SDL_Delay((Uint32)(frameTime - targetTime));
		lastTime = currentTime;
	}

	cleanup();
	SDL_Quit();
	return 0;
}

SystemDefinition* sysDef = NULL;
ParticleSystem* sys = NULL;
Emitter* emitter = NULL;
Affector* affector = NULL;
Renderer* renderer = NULL;
GLuint vao;
GLuint buff;

bool init(Context& ctx)
{
	sysDef = ctx.load("resources/geyser", cerr);
	if(sysDef == NULL) return false;

	sys = sysDef->create(256, 256);
	emitter = sys->createEmitter("geyser", cerr);
	if(emitter == NULL) return false;

	affector = sys->createAffector("geyser", cerr);
	if(affector == NULL) return false;

	renderer = sys->createRenderer("point", cerr);
	if(renderer == NULL) return false;

	renderer->prepare();
	mat4x4 proj = glm::ortho(-400.0f, 400.0f, -300.0f, 600.0f, -1.0f, 1.0f);
	mat4x4 id;
	glUniformMatrix4fv(renderer->getUniformLocation("uMVP"), 1, false, value_ptr(id * id * proj));

	glGenBuffers(1, &buff);
	glBindBuffer(GL_ARRAY_BUFFER, buff);
	float quad[] = {
		-1.0f,  1.0f,
		 1.0f,  1.1f,
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
	if(sys) sys->destroy();
	if(sysDef) sysDef->destroy();
}

void update(Context& ctx)
{
	ctx.update(3.0f / 60.0f);

	emitter->prepare();
	emitter->setParamFloat("min_life", 17.0f);
	emitter->setParamFloat("max_life", 18.5f);
	emitter->setParamFloat("min_speed", 18.0f);
	emitter->setParamFloat("max_speed", 21.0f);
	emitter->setParamFloat("min_angle", 0.4f * M_PI);
	emitter->setParamFloat("max_angle", 0.6f * M_PI);
	emitter->setRate(0.0001);
	emitter->run();

	affector->prepare();
	glUniform2f(affector->getUniformLocation("gravity"), 0.0f, -290.8f);
	affector->run();
}

void render()
{
	renderer->prepare();
	glBindVertexArray(vao);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	sys->render(GL_POINTS, 1);
}
