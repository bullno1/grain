#include <grainr.hpp>
#include <SDL.h>

using namespace grainr;
using namespace std;

bool init(Context& ctx);
void update(Context& ctx);
void render();
void cleanup();

int main(int argc, const char* const argv[])
{
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER);
	SDL_Window* wnd = SDL_CreateWindow("Demo", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 800, 600, SDL_WINDOW_OPENGL);
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
