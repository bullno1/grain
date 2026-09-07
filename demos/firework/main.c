// Firework demo: many shots in flight at once, each made of two particle
// systems from two pools.
//
// * Rising: a steady Trail system follows the rocket, which the CPU
//   integrates and steers toward a random apex. The rocket's pose is the
//   trail system's transform: the emitter works in the rocket's local frame
//   and the CPU never touches its position params.
// * Exploding: at the apex the trail stops emitting and a Burst system in
//   the other pool, placed by its own transform, fires one burst of sparks.
//
// The phase switch is CPU-side; the particles themselves only ever live on
// the GPU. Every live system is a separate grain_system_t, so with several
// shots in the air the demo also exercises many systems per pool, each with
// its own parameters and transform, in one batched draw.
//
// Both effects are baked by grainc from firework_trail.json and
// firework_burst.json: tuned values come from the files, and the per-shot
// values are set through the generated typed handles.
#include <cute.h>
#include <grain_firework_trail.h>
#include <grain_firework_burst.h>
#include <math.h>
#include <stdio.h>

#define WINDOW_WIDTH 960
#define WINDOW_HEIGHT 540

#define MAX_SHOTS 12

#define BURST_MIN_COUNT 220
#define BURST_MAX_COUNT 420

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
	grain_blueprint_t* trail_blueprint;
	grain_blueprint_t* burst_blueprint;
	grain_pool_t* trail_pool;
	grain_pool_t* burst_pool;
	float trail_lifetime_budget;
	float burst_lifetime_budget;
	CF_Rnd rnd;
	shot_t shots[MAX_SHOTS];
	float launch_timer;
	float time;
} demo_t;

// The rocket's pose as the trail system's local-to-world transform: local +y
// points along the velocity so the emitter can trail sparks behind it
static void
set_rocket_transform(shot_t* shot) {
	float speed = cf_len(shot->velocity);
	// Heading as a rotation of the +y axis; straight up when standing still
	CF_V2 up = speed > 0.f ? cf_div_v2_f(shot->velocity, speed) : cf_v2(0.f, 1.f);
	CF_M3x2 pose = {
		.m = { .x = cf_v2(up.y, -up.x), .y = up },
		.p = shot->position,
	};
	grain_set_transform_2d(shot->trail, pose);
	grain_set(shot->trail, grain_firework_trail.Trail.speed, speed);
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

	// Tuned values and the emission rate come from the effect file; the
	// rocket's pose follows per frame through the transform
	grain_blueprint_apply(demo->trail_blueprint, trail);
	set_rocket_transform(shot);
}

static void
explode(demo_t* demo, shot_t* shot) {
	shot->phase = SHOT_BURSTING;

	// The trail keeps its particles but stops making new ones
	grain_set_emission_rate(shot->trail, 0.f);
	shot->trail_ttl = demo->trail_lifetime_budget;

	grain_system_t* burst = grain_create_system(demo->burst_pool);
	if (burst == NULL) { return; }  // Pool full: a dud
	shot->burst = burst;
	shot->burst_ttl = demo->burst_lifetime_budget;

	CF_Rnd* rnd = &demo->rnd;
	float hue = cf_rnd_float(rnd);
	float hue2 = fmodf(hue + cf_rnd_range_float(rnd, 0.04f, 0.12f), 1.f);
	float max_speed = cf_rnd_range_float(rnd, 160.f, 260.f);

	grain_blueprint_apply(demo->burst_blueprint, burst);
	// The explosion point is the system's transform; the emitter stays at
	// its local origin
	grain_set_transform_2d(burst, cf_make_translation_v2(shot->position));
	grain_set(burst, grain_firework_burst.Burst.min_speed, max_speed * 0.25f);
	grain_set(burst, grain_firework_burst.Burst.max_speed, max_speed);
	grain_set(burst, grain_firework_burst.Burst.drift, cf_v2(shot->velocity.x * 0.3f, shot->velocity.y * 0.15f));
	grain_set(burst, grain_firework_burst.Spark.color, cf_hsv_to_rgb(cf_make_color_rgba_f(hue, 0.85f, 1.f, 1.f)));
	grain_set(burst, grain_firework_burst.Spark.color2, cf_hsv_to_rgb(cf_make_color_rgba_f(hue2, 0.5f, 1.f, 1.f)));

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
			set_rocket_transform(shot);

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

	demo_t demo = {
		.grain = grain,
		.rnd = cf_rnd_seed(0x5eed),
		.launch_timer = 0.5f,
	};

	// The blueprints stay alive: every new shot is initialized from them
	demo.trail_blueprint = grain_firework_trail_load(grain);
	CHECK(demo.trail_blueprint != NULL);
	demo.burst_blueprint = grain_firework_burst_load(grain);
	CHECK(demo.burst_blueprint != NULL);

	grain_pool_opts_t trail_opts = grain_blueprint_pool_opts(demo.trail_blueprint);
	trail_opts.max_systems = MAX_SHOTS;
	demo.trail_pool = grain_create_pool(grain, trail_opts);
	CHECK(demo.trail_pool != NULL);
	demo.trail_lifetime_budget = trail_opts.lifetime_budget;

	grain_pool_opts_t burst_opts = grain_blueprint_pool_opts(demo.burst_blueprint);
	burst_opts.max_systems = MAX_SHOTS;
	demo.burst_pool = grain_create_pool(grain, burst_opts);
	CHECK(demo.burst_pool != NULL);
	demo.burst_lifetime_budget = burst_opts.lifetime_budget;

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
	grain_destroy_blueprint(demo.burst_blueprint);
	grain_destroy_blueprint(demo.trail_blueprint);
	grain_destroy(grain);
	cf_destroy_app();
	return 0;
}

// The baked effects' data lives in this translation unit; the includes at
// the top only brought in the declarations and the typed handles
#define GRAIN_EFFECT_IMPLEMENTATION
#include <grain_firework_trail.h>
#include <grain_firework_burst.h>
