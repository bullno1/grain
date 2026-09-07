// Rain demo: a box emitter along the top rains streaks onto moving platforms.
// The platforms are drawn into an offscreen canvas that doubles as a collision
// surface: a SurfaceBounce affector samples it, bounces drops off it and turns
// each upward bounce into a short-lived V-shaped splash.
//
// The effect is baked from rain.json by grainc: the archetype loads from
// precompiled bytecode, tuned values come from the file, and params are
// addressed through the generated typed handles.
#include <cute.h>
#include <grain_rain.h>
#include <math.h>
#include <stdio.h>

#define WINDOW_WIDTH 960
#define WINDOW_HEIGHT 540

// Upper bound of platform speed the surface canvas can encode in its RG
// channels; must match SurfaceBounce.max_surface_speed in rain.json
#define MAX_SURFACE_SPEED 300.f

typedef struct {
	CF_V2 center;     // rest position
	CF_V2 half_size;
	CF_V2 travel;     // oscillation amplitude per axis
	float period;     // seconds per cycle
	float phase;
} platform_t;

static const platform_t platforms[] = {
	{ .center = { -220.f, -50.f }, .half_size = { 90.f, 8.f }, .travel = { 150.f, 0.f }, .period = 6.f, .phase = 0.f },
	{ .center = { 170.f, 60.f }, .half_size = { 70.f, 8.f }, .travel = { -120.f, 35.f }, .period = 4.5f, .phase = 1.7f },
	{ .center = { 20.f, -180.f }, .half_size = { 140.f, 8.f }, .travel = { 210.f, 0.f }, .period = 8.f, .phase = 3.f },
};
#define NUM_PLATFORMS (int)(sizeof(platforms) / sizeof(platforms[0]))

static CF_V2
platform_position(const platform_t* platform, float time) {
	float angle = time * CF_TAU / platform->period + platform->phase;
	return cf_add_v2(platform->center, cf_mul_v2_f(platform->travel, sinf(angle)));
}

static CF_V2
platform_velocity(const platform_t* platform, float time) {
	float angle = time * CF_TAU / platform->period + platform->phase;
	return cf_mul_v2_f(platform->travel, cosf(angle) * CF_TAU / platform->period);
}

// Draws the platforms as the affector reads them: alpha is coverage, RG the
// platform's velocity with (0.5, 0.5) at rest
static void
draw_surface(float time) {
	for (int i = 0; i < NUM_PLATFORMS; ++i) {
		const platform_t* platform = &platforms[i];
		CF_V2 velocity = platform_velocity(platform, time);
		cf_draw_push_color(cf_make_color_rgba_f(
			0.5f + 0.5f * velocity.x / MAX_SURFACE_SPEED,
			0.5f + 0.5f * velocity.y / MAX_SURFACE_SPEED,
			0.f,
			1.f
		));
		cf_draw_quad_fill(
			cf_make_aabb_center_half_extents(platform_position(platform, time), platform->half_size),
			0.f
		);
		cf_draw_pop_color();
	}
}

static void
draw_platforms(float time) {
	cf_draw_push_color(cf_make_color_rgba_f(0.72f, 0.75f, 0.8f, 1.f));
	for (int i = 0; i < NUM_PLATFORMS; ++i) {
		const platform_t* platform = &platforms[i];
		cf_draw_quad_fill(
			cf_make_aabb_center_half_extents(platform_position(platform, time), platform->half_size),
			2.f
		);
	}
	cf_draw_pop_color();
}

#define CHECK(COND) \
	do { \
		if (!(COND)) { \
			fprintf(stderr, "%s\n", grain_get_last_error(grain)); \
			return 1; \
		} \
	} while (0)

int
main(int argc, char* argv[]) {
	(void)argc;

	CF_Result result = cf_make_app(
		"grain: rain",
		0, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT,
		CF_APP_OPTIONS_WINDOW_POS_CENTERED_BIT,
		argv[0]
	);
	if (cf_is_error(result)) {
		fprintf(stderr, "Could not create app: %s\n", result.details);
		return 1;
	}
	cf_clear_color(0.06f, 0.07f, 0.10f, 1.f);

	// The world is the app canvas: origin at the center, y up
	CF_V2 world_min = cf_v2(-WINDOW_WIDTH * 0.5f, -WINDOW_HEIGHT * 0.5f);
	CF_V2 world_max = cf_v2(WINDOW_WIDTH * 0.5f, WINDOW_HEIGHT * 0.5f);

	// The collision surface covers the whole world at screen resolution.
	// Its clear color is "empty, at rest" so filtered edges blend toward zero
	// velocity rather than toward a bogus one
	CF_Canvas surface = cf_make_canvas(cf_canvas_defaults(WINDOW_WIDTH, WINDOW_HEIGHT));
	cf_canvas_set_clear_color(surface, cf_make_color_rgba_f(0.5f, 0.5f, 0.f, 0.f));
	CF_SamplerParams sampler_params = cf_sampler_defaults();
	sampler_params.filter = CF_FILTER_LINEAR;
	sampler_params.wrap_u = CF_WRAP_MODE_CLAMP_TO_EDGE;
	sampler_params.wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
	CF_Sampler surface_sampler = cf_make_sampler(sampler_params);

	grain_t* grain = grain_create();

	grain_blueprint_t* blueprint = grain_rain_load(grain);
	CHECK(blueprint != NULL);
	grain_pool_t* pool = grain_create_pool(grain, grain_blueprint_pool_opts(blueprint));
	CHECK(pool != NULL);
	grain_system_t* rain = grain_create_system(pool);
	// Everything tuned in rain.json; only the values that depend on the
	// window are set here
	grain_blueprint_apply(blueprint, rain);
	grain_destroy_blueprint(blueprint);

	grain_set(rain, grain_rain.Box.position, cf_v2(0.f, world_max.y + 24.f));
	grain_set(rain, grain_rain.Box.size, cf_v2(WINDOW_WIDTH + 300.f, 16.f));
	grain_set(rain, grain_rain.SurfaceBounce.world_min, world_min);
	grain_set(rain, grain_rain.SurfaceBounce.world_max, world_max);

	float time = 0.f;
	while (cf_app_is_running()) {
		cf_app_update(NULL);
		if (cf_key_just_pressed(CF_KEY_ESCAPE)) {
			break;
		}
		float dt = CF_DELTA_TIME;
		time += dt;

		// 1. Platforms into the collision surface
		draw_surface(time);
		cf_render_to(surface, true);

		// 2. Simulate
		grain_begin_update(grain);
		grain_set(pool, grain_rain.SurfaceBounce.surface, (grain_texture_binding_t){
			.texture = cf_canvas_get_target(surface),
			.sampler = surface_sampler,
		});
		grain_tick(rain, dt);
		grain_end_update(grain);

		// 3. Platforms, then rain on top, onto the screen
		draw_platforms(time);
		cf_render_to(cf_app_get_canvas(), true);

		cf_apply_canvas(cf_app_get_canvas(), false);
		grain_begin_render(grain);
		grain_render(rain);
		grain_end_render(grain);

		cf_app_draw_onto_screen(false);
	}

	grain_destroy_pool(pool);
	grain_destroy(grain);
	cf_destroy_sampler(surface_sampler);
	cf_destroy_canvas(surface);
	cf_destroy_app();
	return 0;
}

// The baked effect's data lives in this translation unit; the include at the
// top only brought in the declarations and the typed handles
#define GRAIN_EFFECT_IMPLEMENTATION
#include <grain_rain.h>
