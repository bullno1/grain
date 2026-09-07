// Firework demo: many shots in flight at once, each made of two particle
// systems from two pools.
//
// * Rising: a steady Trail system follows the rocket, which the CPU
//   integrates and steers toward a random apex.
// * Exploding: at the apex the trail stops emitting and a Burst system in
//   the other pool fires one burst of sparks.
//
// The phase switch is CPU-side; the particles themselves only ever live on
// the GPU. Every live system is a separate grain_system_t, so with several
// shots in the air the demo also exercises many systems per pool, each with
// its own parameters, in one batched draw.
#include <cute.h>
#include <grain.h>
#include <math.h>
#include <stdio.h>
#include "resources.rc"
// Hash stamp of the incbin'd modules: changes when any of them does, so
// caching compilers rebuild (see demos/CMakeLists.txt)
#if __has_include("demo_stamp.h")
#	include "demo_stamp.h"
#endif

#define WINDOW_WIDTH 960
#define WINDOW_HEIGHT 540

#define MAX_SHOTS 12

// Trail pool: sparks live briefly, one system per rising rocket
#define TRAIL_RATE 260.f
#define TRAIL_MIN_LIFETIME 0.35f
#define TRAIL_MAX_LIFETIME 0.8f

// Burst pool: burst-only, so the steady rate is a token minimum
#define BURST_MIN_COUNT 220
#define BURST_MAX_COUNT 420
#define BURST_MIN_LIFETIME 1.1f
#define BURST_MAX_LIFETIME 2.4f

#define ROCKET_GRAVITY 140.f

typedef enum {
	SHOT_FREE,
	SHOT_RISING,
	SHOT_BURSTING,
} shot_phase_t;

typedef struct {
	shot_phase_t phase;
	CF_V2 position;
	CF_V2 velocity;
	float apex_y;

	// Each system stays alive until every particle it emitted has died, so
	// its pool slot is reused clean: grain never clears a slot's particles
	grain_system_t* trail;
	float trail_ttl;    // counts down once the trail stops emitting
	grain_system_t* burst;
	float burst_ttl;
} shot_t;

typedef struct {
	grain_t* grain;
	grain_pool_t* trail_pool;
	grain_pool_t* burst_pool;
	CF_Rnd rnd;
	shot_t shots[MAX_SHOTS];
	float launch_timer;
	float time;
} demo_t;

// Module indices within each archetype, in definition order
enum { TRAIL_EMITTER, TRAIL_LIFETIME };
enum { TRAIL_GRAVITY, TRAIL_INTEGRATE, TRAIL_AGE };
enum { BURST_EMITTER, BURST_LIFETIME };
enum { BURST_GRAVITY, BURST_WIND, BURST_INTEGRATE, BURST_AGE };

static const char*
source_of(xincbin_data_t resource) {
	// xincbin null-terminates its payloads
	return (const char*)resource.data;
}

static uint32_t
pixel_of(CF_Color color) {
	return cf_color_to_pixel(color).val;
}

static void
launch(demo_t* demo) {
	shot_t* shot = NULL;
	for (int i = 0; i < MAX_SHOTS; ++i) {
		if (demo->shots[i].phase == SHOT_FREE) {
			shot = &demo->shots[i];
			break;
		}
	}
	if (shot == NULL) { return; }

	grain_system_t* trail = grain_create_system(demo->trail_pool);
	if (trail == NULL) { return; }  // Pool full: skip this shot

	CF_Rnd* rnd = &demo->rnd;
	float x = cf_rnd_range_float(rnd, -WINDOW_WIDTH * 0.42f, WINDOW_WIDTH * 0.42f);
	*shot = (shot_t){
		.phase = SHOT_RISING,
		.position = cf_v2(x, -WINDOW_HEIGHT * 0.5f - 8.f),
		.velocity = cf_v2(
			cf_rnd_range_float(rnd, -50.f, 50.f),
			cf_rnd_range_float(rnd, 380.f, 520.f)
		),
		.apex_y = cf_rnd_range_float(rnd, -WINDOW_HEIGHT * 0.05f, WINDOW_HEIGHT * 0.4f),
		.trail = trail,
	};

	// Per-system params: set once, only position/velocity change per frame
	grain_set_emission_rate(trail, TRAIL_RATE);
	grain_set_emitter_parameter(trail, TRAIL_EMITTER, "spread", &(float){ 2.f });
	grain_set_emitter_parameter(trail, TRAIL_EMITTER, "min_speed", &(float){ 20.f });
	grain_set_emitter_parameter(trail, TRAIL_EMITTER, "max_speed", &(float){ 70.f });
	grain_set_emitter_parameter(trail, TRAIL_LIFETIME, "min_lifetime", &(float){ TRAIL_MIN_LIFETIME });
	grain_set_emitter_parameter(trail, TRAIL_LIFETIME, "max_lifetime", &(float){ TRAIL_MAX_LIFETIME });
	grain_set_affector_parameter(trail, TRAIL_GRAVITY, "gravity", &(float){ 120.f });
	grain_set_renderer_parameter(trail, "radius", &(float){ 1.f });
	grain_set_renderer_parameter(trail, "glow", &(float){ 2.f });
	grain_set_renderer_parameter(trail, "color", &(uint32_t){ pixel_of(cf_make_color_rgba_f(1.f, 0.85f, 0.5f, 1.f)) });
	grain_set_renderer_parameter(trail, "color2", &(uint32_t){ pixel_of(cf_make_color_rgba_f(1.f, 0.45f, 0.15f, 1.f)) });
	grain_set_renderer_parameter(trail, "fade_time", &(float){ 0.4f });
	grain_set_renderer_parameter(trail, "twinkle", &(float){ 0.3f });
	grain_set_renderer_parameter(trail, "additivity", &(float){ 0.8f });
}

static void
explode(demo_t* demo, shot_t* shot) {
	shot->phase = SHOT_BURSTING;

	// The trail keeps its particles but stops making new ones
	grain_set_emission_rate(shot->trail, 0.f);
	shot->trail_ttl = TRAIL_MAX_LIFETIME;

	grain_system_t* burst = grain_create_system(demo->burst_pool);
	if (burst == NULL) { return; }  // Pool full: a dud
	shot->burst = burst;
	shot->burst_ttl = BURST_MAX_LIFETIME;

	CF_Rnd* rnd = &demo->rnd;
	float hue = cf_rnd_float(rnd);
	float hue2 = fmodf(hue + cf_rnd_range_float(rnd, 0.04f, 0.12f), 1.f);
	CF_Color color = cf_hsv_to_rgb(cf_make_color_rgba_f(hue, 0.85f, 1.f, 1.f));
	CF_Color color2 = cf_hsv_to_rgb(cf_make_color_rgba_f(hue2, 0.5f, 1.f, 1.f));
	float max_speed = cf_rnd_range_float(rnd, 160.f, 260.f);

	grain_set_emission_rate(burst, 0.f);
	grain_set_emitter_parameter(burst, BURST_EMITTER, "position", &shot->position);
	grain_set_emitter_parameter(burst, BURST_EMITTER, "min_speed", &(float){ max_speed * 0.25f });
	grain_set_emitter_parameter(burst, BURST_EMITTER, "max_speed", &max_speed);
	grain_set_emitter_parameter(burst, BURST_EMITTER, "drift", &(CF_V2){ shot->velocity.x * 0.3f, shot->velocity.y * 0.15f });
	grain_set_emitter_parameter(burst, BURST_LIFETIME, "min_lifetime", &(float){ BURST_MIN_LIFETIME });
	grain_set_emitter_parameter(burst, BURST_LIFETIME, "max_lifetime", &(float){ BURST_MAX_LIFETIME });
	grain_set_affector_parameter(burst, BURST_GRAVITY, "gravity", &(float){ 90.f });
	grain_set_affector_parameter(burst, BURST_WIND, "velocity", &(CF_V2){ 0.f, 0.f });
	grain_set_affector_parameter(burst, BURST_WIND, "drag", &(float){ 1.4f });
	grain_set_affector_parameter(burst, BURST_WIND, "gustiness", &(float){ 0.f });
	grain_set_affector_parameter(burst, BURST_WIND, "gust_frequency", &(float){ 0.f });
	grain_set_renderer_parameter(burst, "radius", &(float){ 1.4f });
	grain_set_renderer_parameter(burst, "glow", &(float){ 3.f });
	grain_set_renderer_parameter(burst, "color", &(uint32_t){ pixel_of(color) });
	grain_set_renderer_parameter(burst, "color2", &(uint32_t){ pixel_of(color2) });
	grain_set_renderer_parameter(burst, "fade_time", &(float){ 0.7f });
	grain_set_renderer_parameter(burst, "twinkle", &(float){ 0.7f });
	grain_set_renderer_parameter(burst, "additivity", &(float){ 0.7f });

	grain_burst(burst, cf_rnd_range_int(rnd, BURST_MIN_COUNT, BURST_MAX_COUNT));
}

// Advances every shot and ticks its systems. Call inside a grain update window
static void
update_shots(demo_t* demo, float dt) {
	for (int i = 0; i < MAX_SHOTS; ++i) {
		shot_t* shot = &demo->shots[i];
		if (shot->phase == SHOT_FREE) { continue; }

		if (shot->phase == SHOT_RISING) {
			shot->velocity.y -= ROCKET_GRAVITY * dt;
			shot->position = cf_add_v2(shot->position, cf_mul_v2_f(shot->velocity, dt));
			grain_set_emitter_parameter(shot->trail, TRAIL_EMITTER, "position", &shot->position);
			grain_set_emitter_parameter(shot->trail, TRAIL_EMITTER, "velocity", &shot->velocity);

			if (shot->position.y >= shot->apex_y || shot->velocity.y <= 0.f) {
				explode(demo, shot);
			}
		} else {
			// Retire each system once its last particle can have died
			shot->trail_ttl -= dt;
			if (shot->trail != NULL && shot->trail_ttl <= 0.f) {
				grain_destroy_system(shot->trail);
				shot->trail = NULL;
			}
			shot->burst_ttl -= dt;
			if (shot->burst != NULL && shot->burst_ttl <= 0.f) {
				grain_destroy_system(shot->burst);
				shot->burst = NULL;
			}
			if (shot->trail == NULL && shot->burst == NULL) {
				shot->phase = SHOT_FREE;
				continue;
			}
		}

		if (shot->trail != NULL) { grain_tick(shot->trail, dt); }
		if (shot->burst != NULL) { grain_tick(shot->burst, dt); }
	}
}

static void
render_shots(demo_t* demo) {
	for (int i = 0; i < MAX_SHOTS; ++i) {
		shot_t* shot = &demo->shots[i];
		if (shot->trail != NULL) { grain_render(shot->trail); }
		if (shot->burst != NULL) { grain_render(shot->burst); }
	}
}

// The rocket head is CPU state, so it is drawn with CF
static void
draw_rockets(demo_t* demo) {
	cf_draw_push_color(cf_make_color_rgba_f(1.f, 0.95f, 0.8f, 1.f));
	for (int i = 0; i < MAX_SHOTS; ++i) {
		shot_t* shot = &demo->shots[i];
		if (shot->phase == SHOT_RISING) {
			cf_draw_circle_fill2(shot->position, 2.f);
		}
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
		"grain: firework",
		0, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT,
		CF_APP_OPTIONS_WINDOW_POS_CENTERED_BIT,
		argv[0]
	);
	if (cf_is_error(result)) {
		fprintf(stderr, "Could not create app: %s\n", result.details);
		return 1;
	}
	cf_clear_color(0.02f, 0.02f, 0.05f, 1.f);

	grain_t* grain = grain_create();

	grain_emitter_t* lifetime = grain_define_emitter(grain, source_of(XINCBIN_GET(firework_emitter_lifetime)));
	CHECK(lifetime != NULL);
	grain_emitter_t* trail = grain_define_emitter(grain, source_of(XINCBIN_GET(firework_emitter_trail)));
	CHECK(trail != NULL);
	grain_emitter_t* burst = grain_define_emitter(grain, source_of(XINCBIN_GET(firework_emitter_burst)));
	CHECK(burst != NULL);

	grain_affector_t* gravity = grain_define_affector(grain, source_of(XINCBIN_GET(firework_affector_gravity)));
	CHECK(gravity != NULL);
	grain_affector_t* wind = grain_define_affector(grain, source_of(XINCBIN_GET(firework_affector_wind)));
	CHECK(wind != NULL);
	grain_affector_t* integrate = grain_define_affector(grain, source_of(XINCBIN_GET(firework_affector_integrate)));
	CHECK(integrate != NULL);
	grain_affector_t* age = grain_define_affector(grain, source_of(XINCBIN_GET(firework_affector_age)));
	CHECK(age != NULL);

	grain_renderer_t* spark = grain_define_renderer(grain, source_of(XINCBIN_GET(firework_renderer_spark)));
	CHECK(spark != NULL);

	// The same Spark renderer serves both archetypes
	grain_archetype_t* trail_archetype = grain_define_archetype(grain, "Trail", (grain_archetype_spec_t){
		.emitters = (grain_emitter_t*[]){ trail, lifetime },
		.num_emitters = 2,
		.affectors = (grain_affector_t*[]){ gravity, integrate, age },
		.num_affectors = 3,
		.renderer = spark,
	});
	CHECK(trail_archetype != NULL);
	grain_archetype_t* burst_archetype = grain_define_archetype(grain, "Burst", (grain_archetype_spec_t){
		.emitters = (grain_emitter_t*[]){ burst, lifetime },
		.num_emitters = 2,
		.affectors = (grain_affector_t*[]){ gravity, wind, integrate, age },
		.num_affectors = 4,
		.renderer = spark,
	});
	CHECK(burst_archetype != NULL);

	demo_t demo = {
		.grain = grain,
		.rnd = cf_rnd_seed(0x5eed),
		.launch_timer = 0.5f,
	};
	demo.trail_pool = grain_create_pool(grain, (grain_pool_opts_t){
		.archetype = trail_archetype,
		.max_systems = MAX_SHOTS,
		.max_emission_rate = TRAIL_RATE,
		.lifetime_budget = TRAIL_MAX_LIFETIME,
		.max_burst_size = 0,
	});
	CHECK(demo.trail_pool != NULL);
	demo.burst_pool = grain_create_pool(grain, (grain_pool_opts_t){
		.archetype = burst_archetype,
		.max_systems = MAX_SHOTS,
		// Burst-only pool; the rate must still be positive
		.max_emission_rate = 1.f,
		.lifetime_budget = BURST_MAX_LIFETIME,
		.max_burst_size = BURST_MAX_COUNT,
	});
	CHECK(demo.burst_pool != NULL);

	while (cf_app_is_running()) {
		cf_app_update(NULL);
		if (cf_key_just_pressed(CF_KEY_ESCAPE)) {
			break;
		}
		float dt = CF_DELTA_TIME;
		demo.time += dt;

		grain_begin_update(grain);

		demo.launch_timer -= dt;
		if (demo.launch_timer <= 0.f) {
			launch(&demo);
			demo.launch_timer = cf_rnd_range_float(&demo.rnd, 0.3f, 1.f);
		}
		if (cf_key_just_pressed(CF_KEY_SPACE)) {
			// A volley
			for (int i = 0; i < 5; ++i) { launch(&demo); }
		}

		update_shots(&demo, dt);

		grain_end_update(grain);

		draw_rockets(&demo);
		cf_render_to(cf_app_get_canvas(), true);

		cf_apply_canvas(cf_app_get_canvas(), false);
		grain_begin_render(grain);
		render_shots(&demo);
		grain_end_render(grain);

		cf_app_draw_onto_screen(false);
	}

	grain_destroy_pool(demo.burst_pool);
	grain_destroy_pool(demo.trail_pool);
	grain_destroy(grain);
	cf_destroy_app();
	return 0;
}

#define XINCBIN_IMPLEMENTATION
#include "resources.rc"
