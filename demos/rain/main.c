// Rain demo: a box emitter along the top rains streaks onto moving platforms.
// The platforms are drawn into an offscreen canvas that doubles as a collision
// surface: a SurfaceBounce affector samples it, bounces drops off it and turns
// each upward bounce into a short-lived V-shaped splash.
#include <cute.h>
#include <grain.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "resources.rc"
// Hash stamp of the incbin'd modules: changes when any of them does, so
// caching compilers rebuild (see CMakeLists.txt)
#if __has_include("demo_stamp.h")
#	include "demo_stamp.h"
#endif

#define WINDOW_WIDTH 960
#define WINDOW_HEIGHT 540

// Upper bound of platform speed the surface canvas can encode in its RG channels
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

static const char*
source_of(xincbin_data_t resource) {
	// xincbin null-terminates its payloads
	return (const char*)resource.data;
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

	grain_emitter_t* box = grain_define_emitter(grain, source_of(XINCBIN_GET(rain_emitter_box)));
	CHECK(box != NULL);
	grain_emitter_t* lifetime = grain_define_emitter(grain, source_of(XINCBIN_GET(rain_emitter_lifetime)));
	CHECK(lifetime != NULL);
	grain_emitter_t* raindrop = grain_define_emitter(grain, source_of(XINCBIN_GET(rain_emitter_raindrop)));
	CHECK(raindrop != NULL);

	grain_affector_t* gravity = grain_define_affector(grain, source_of(XINCBIN_GET(rain_affector_gravity)));
	CHECK(gravity != NULL);
	grain_affector_t* integrate = grain_define_affector(grain, source_of(XINCBIN_GET(rain_affector_integrate)));
	CHECK(integrate != NULL);
	grain_affector_t* surface_bounce = grain_define_affector(grain, source_of(XINCBIN_GET(rain_affector_surface_bounce)));
	CHECK(surface_bounce != NULL);
	grain_affector_t* age = grain_define_affector(grain, source_of(XINCBIN_GET(rain_affector_age)));
	CHECK(age != NULL);

	grain_renderer_t* streak = grain_define_renderer(grain, source_of(XINCBIN_GET(rain_renderer_streak)));
	CHECK(streak != NULL);

	// Module order is the index used for parameters below
	enum { EMITTER_BOX, EMITTER_LIFETIME, EMITTER_RAINDROP };
	enum { AFFECTOR_GRAVITY, AFFECTOR_INTEGRATE, AFFECTOR_SURFACE_BOUNCE, AFFECTOR_AGE };
	grain_archetype_t* archetype = grain_define_archetype(grain, "Rain", (grain_archetype_spec_t){
		.emitters = (grain_emitter_t*[]){ box, lifetime, raindrop },
		.num_emitters = 3,
		// Bounce after integration so a drop never renders inside a platform
		.affectors = (grain_affector_t*[]){ gravity, integrate, surface_bounce, age },
		.num_affectors = 4,
		.renderer = streak,
	});
	CHECK(archetype != NULL);

	grain_archetype_info_t info = grain_inspect_archetype(archetype);
	int surface_slot = info.affectors[AFFECTOR_SURFACE_BOUNCE].first_sampler;
	CHECK(
		info.affectors[AFFECTOR_SURFACE_BOUNCE].num_samplers == 1
		&& strcmp(info.samplers[surface_slot].name, "surface") == 0
	);

	const float emission_rate = 900.f;
	const float max_lifetime = 1.6f;
	grain_pool_t* pool = grain_create_pool(grain, (grain_pool_opts_t){
		.archetype = archetype,
		.max_systems = 1,
		.max_emission_rate = emission_rate,
		.lifetime_budget = max_lifetime,
		.max_burst_size = 0,
	});
	CHECK(pool != NULL);
	grain_system_t* rain = grain_create_system(pool);

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

		grain_set_emission_rate(rain, emission_rate);
		grain_set_emitter_parameter(rain, EMITTER_BOX, "position", &(CF_V2){ 0.f, world_max.y + 24.f });
		grain_set_emitter_parameter(rain, EMITTER_BOX, "size", &(CF_V2){ WINDOW_WIDTH + 300.f, 16.f });
		grain_set_emitter_parameter(rain, EMITTER_LIFETIME, "min_lifetime", &(float){ 1.2f });
		grain_set_emitter_parameter(rain, EMITTER_LIFETIME, "max_lifetime", &(float){ max_lifetime });
		grain_set_emitter_parameter(rain, EMITTER_RAINDROP, "min_speed", &(float){ 520.f });
		grain_set_emitter_parameter(rain, EMITTER_RAINDROP, "max_speed", &(float){ 720.f });
		grain_set_emitter_parameter(rain, EMITTER_RAINDROP, "slant", &(float){ 0.18f });

		grain_set_affector_parameter(rain, AFFECTOR_GRAVITY, "gravity", &(float){ 250.f });
		grain_set_affector_parameter(rain, AFFECTOR_SURFACE_BOUNCE, "world_min", &world_min);
		grain_set_affector_parameter(rain, AFFECTOR_SURFACE_BOUNCE, "world_max", &world_max);
		grain_set_affector_parameter(rain, AFFECTOR_SURFACE_BOUNCE, "max_surface_speed", &(float){ MAX_SURFACE_SPEED });
		grain_set_affector_parameter(rain, AFFECTOR_SURFACE_BOUNCE, "bounciness", &(float){ 0.3f });
		grain_set_affector_parameter(rain, AFFECTOR_SURFACE_BOUNCE, "scatter", &(float){ 90.f });
		grain_set_affector_parameter(rain, AFFECTOR_SURFACE_BOUNCE, "splash_lifetime", &(float){ 0.35f });

		grain_set_renderer_parameter(rain, "thickness", &(float){ 1.6f });
		grain_set_renderer_parameter(rain, "stretch", &(float){ 0.022f });
		grain_set_renderer_parameter(rain, "max_length", &(float){ 26.f });
		CF_Pixel color = cf_color_to_pixel(cf_make_color_rgba_f(0.68f, 0.8f, 1.f, 0.85f));
		grain_set_renderer_parameter(rain, "color", &color.val);
		grain_set_renderer_parameter(rain, "splash_angle", &(float){ 0.6f });
		grain_set_renderer_parameter(rain, "splash_spread", &(float){ 110.f });
		grain_set_renderer_parameter(rain, "splash_length", &(float){ 4.f });

		grain_set_texture(pool, surface_slot, (grain_texture_binding_t){
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

#define XINCBIN_IMPLEMENTATION
#include "resources.rc"
