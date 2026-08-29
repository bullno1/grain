#include <bgame/entrypoint.h>
#include <bgame/allocator.h>
#include <bgame/allocator/tracked.h>
#include <bgame/scene.h>
#include <bgame/asset.h>
#include <blog.h>
#include <cute.h>
#include <SDL3/SDL_video.h>
#include <stdio.h>
#include <dcimgui.h>

#ifdef __EMSCRIPTEN__
#	include <emscripten/html5.h>
#endif

static const char* WINDOW_TITLE = "grain-editor";
BGAME_VAR(bool, app_created) = false;

static void
report_allocator_stats(
	const char* name,
	bgame_allocator_stats_t stats,
	void* userdata
) {
	BLOG_DEBUG("%s: Total %zu, Peak %zu", name, stats.total, stats.peak);
}

static void
handle_resize(void) {
	int window_width, window_height;
	SDL_GetWindowSize(cf_app_get_window(), &window_width, &window_height);
	BLOG_INFO("Window size: %d x %d", window_width, window_height);

	float display_scale = SDL_GetWindowDisplayScale(cf_app_get_window());
	BLOG_INFO("Display scale: %f", display_scale);

	int backbuffer_width, backbuffer_height;
    SDL_GetWindowSizeInPixels(cf_app_get_window(), &backbuffer_width, &backbuffer_height);
	BLOG_INFO("Backbuffer size: %d x %d", backbuffer_width, backbuffer_height);

	int canvas_width  = cf_round((float)backbuffer_width  / display_scale);
	int canvas_height = cf_round((float)backbuffer_height / display_scale);
	BLOG_INFO("Canvas size: %d x %d", canvas_width, canvas_height);

	cf_app_set_canvas_size(canvas_width, canvas_height);
	cf_draw_projection(cf_ortho_2d(0.f, 0.f, canvas_width, canvas_height));
}

static void
init(int argc, const char** argv) {
	// Cute Framework
	if (!app_created) {
		int width  = 1280;
		int height = 720;

		BLOG_INFO("Creating app");
		int options =
			  CF_APP_OPTIONS_WINDOW_POS_CENTERED_BIT
			| CF_APP_OPTIONS_FILE_SYSTEM_DONT_DEFAULT_MOUNT_BIT
			| CF_APP_OPTIONS_RESIZABLE_BIT
			;
		CF_Result result = cf_make_app(WINDOW_TITLE, 0, 0, 0, width, height, options, argv[0]);
		if (result.code != CF_RESULT_SUCCESS) {
			BLOG_FATAL("Could not create app: %s", result.details);
			abort();
		}

#ifdef __EMSCRIPTEN__
		cf_fs_mount("/assets", "/assets", true);
#else
		char assets_dir[1024];
		snprintf(assets_dir, sizeof(assets_dir), "%sassets", cf_fs_get_base_directory());
		cf_fs_mount(assets_dir, "/assets", true);
#endif

#ifdef __EMSCRIPTEN__
		// SDL's Emscripten backend adopts the CSS-driven canvas size at creation
		// but never emits SDL_EVENT_WINDOW_RESIZED for it, so CF's cached
		// app->w/h would stay at the requested size. Push the real size back
		// through cf_app_set_size to synthesize that event.
		int window_width, window_height;
		SDL_GetWindowSize(cf_app_get_window(), &window_width, &window_height);
		cf_app_set_size(window_width, window_height);
#endif

		cf_app_init_imgui();
		static char s_ini_path[1024];
		// after cf_app_init_imgui(), before the main loop:
		const char* user_dir = cf_fs_get_user_directory("bullno1", "grain");
		snprintf(s_ini_path, sizeof(s_ini_path), "%simgui.ini", user_dir);
		ImGui_GetIO()->IniFilename = s_ini_path;

		cf_fs_mount(user_dir, "/user", true);
		cf_fs_set_write_directory(user_dir);

		bgame_push_scene("editor");

		app_created = true;
	}

	cf_set_fixed_timestep(60);
	cf_app_set_present_mode(CF_PRESENT_MODE_VSYNC);

	cf_app_set_icon("/assets/icon.png");
}

static void
update(void) {
	if (cf_app_was_resized()) {
		handle_resize();
	}

	bgame_scene_update();
}

static void
cleanup(void) {
	bgame_clear_scene_stack();
	cf_destroy_app();

	BLOG_DEBUG("--- Allocator stats ---");
	bgame_enumerate_tracked_allocators(report_allocator_stats, NULL);
}

static void
after_reload(void) {
	bgame_scene_after_reload();
}

BGAME_APP {
	.init = init,
	.cleanup = cleanup,
	.update = update,
	.before_reload = bgame_scene_before_reload,
	.after_reload = after_reload,
};
