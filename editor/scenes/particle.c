#include <cute.h>
#include <blog.h>
#define BGAME_SCENE_NAME particle
#include <bgame/utils.h>
#include <grain.h>

SCENE_VAR(grain_t*, grain)
SCENE_VAR(grain_pool_t*, pool)
SCENE_VAR(grain_system_t*, psystem)

static void
init(void) {
	cf_clear_color(0.5f, 0.5f, 0.5f, 0.5f);

	if (bgame_current_scene_state() == BGAME_SCENE_INITIALIZING) {
		grain = grain_create();
	}

	grain_emitter_t* point_emitter = grain_define_emitter(
		grain,
		"Module(Point)\n"
		"Requires(\n"
		"	vec2 position;\n"
		"	vec2 velocity;\n"
		")\n"
		"Params(\n"
		"	vec2 position;\n"
		"	float min_speed;\n"
		"	float max_speed;\n"
		"	float min_angle;\n"
		"	float max_angle;\n"
		")\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
		"	particle.position = params.position;\n"
		"	float speed = mix(params.min_speed, params.max_speed, rand());\n"
		"	float angle = mix(params.min_angle, params.max_angle, rand());\n"
		"	particle.velocity = vec2(cos(angle), sin(angle)) * speed;\n"
		"}"
	);
	if (point_emitter == NULL) {
		BLOG_ERROR("%s", grain_get_last_error(grain));
	}

	grain_emitter_t* lifetime_init = grain_define_emitter(
		grain,
		"Module(Lifetime)\n"
		"Requires(\n"
		"	float lifetime;\n"
		")\n"
		"Params(\n"
		"	float min_lifetime;\n"
		"	float max_lifetime;\n"
		")\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
		"	particle.lifetime = mix(params.min_lifetime, params.max_lifetime, rand());\n"
		"}"
	);
	if (lifetime_init == NULL) {
		BLOG_ERROR("%s", grain_get_last_error(grain));
	}

	grain_affector_t* age_affector = grain_define_affector(
		grain,
		"Module(Age)\n"
		"Requires(\n"
		"	float lifetime;\n"
		")\n"
		"Params(\n"
		")\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
		"	particle.lifetime -= ctx.dt;\n"
		"}"
	);
	if (age_affector == NULL) {
		BLOG_ERROR("%s", grain_get_last_error(grain));
	}

	grain_affector_t* integrate_affector = grain_define_affector(
		grain,
		"Module(Integrate)\n"
		"Requires(\n"
		"	vec2 position;\n"
		"	vec2 velocity;\n"
		")\n"
		"Params(\n"
		")\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
		"	particle.position += particle.velocity * ctx.dt;\n"
		"}"
	);
	if (integrate_affector == NULL) {
		BLOG_ERROR("%s", grain_get_last_error(grain));
	}

	grain_affector_t* gravity_affector = grain_define_affector(
		grain,
		"Module(Gravity)\n"
		"Requires(\n"
		"	vec2 velocity;\n"
		")\n"
		"Params(\n"
		"	@range(0, max=100)\n"
		"	@description(\"How much to push the particles down\")\n"
		"	@widget(type=SLIDER)\n"
		"	float gravity;\n"
		")\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
		"	particle.velocity.y -= params.gravity * ctx.dt;\n"
		"}"
	);
	if (gravity_affector == NULL) {
		BLOG_ERROR("%s", grain_get_last_error(grain));
	}

	grain_renderer_t* renderer = grain_define_renderer(
		grain,
		"Module(Quad)\n"
		"Requires(\n"
		"	vec2 position;\n"
		"	float lifetime;\n"
		")\n"
		"Params(\n"
		"	vec2 size;\n"
		"	@color uint color;\n"
		")\n"
		"#if GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_VERTEX\n"
		"void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
		"	if (particle.lifetime > 0.0) {\n"
		"		gl_Position = grain_transform * vec4(particle.position + quad() * params.size, 0.0, 1.0);\n"
		"	} else {\n"
		"		cull();\n"
		"	}\n"
		"}\n"
		"#elif GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_FRAGMENT\n"
		"void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
		"	grain_Color = unpackUnorm4x8(params.color);\n"
		"	grain_Color.a = clamp(particle.lifetime, 0.0, 1.0);\n"
		"}\n"
		"#endif"
	);
	if (renderer == NULL) {
		BLOG_ERROR("%s", grain_get_last_error(grain));
	}

	grain_archetype_t* archetype = grain_define_archetype(
		grain,
		"test",
		(grain_archetype_spec_t){
			.emitters = (grain_emitter_t*[]){
				point_emitter,
				lifetime_init,
			},
			.num_emitters = 2,

			.affectors = (grain_affector_t*[]){
				age_affector,
				gravity_affector,
				integrate_affector,
			},
			.num_affectors = 3,

			.renderer = renderer,
		}
	);
	if (archetype == NULL) {
		BLOG_ERROR("%s", grain_get_last_error(grain));
	} else {
		// Exercise the decorator reflection API
		grain_archetype_info_t info = grain_inspect_archetype(archetype);
		for (int i = 0; i < info.num_affectors + 1; ++i) {
			const grain_module_info_t* module = i < info.num_affectors
				? &info.affectors[i]
				: &info.renderer;
			for (int j = 0; j < module->num_params; ++j) {
				const grain_param_info_t* param = &info.params[module->first_params + j];
				for (int k = 0; k < param->num_decorators; ++k) {
					BLOG_INFO(
						"%s.%s: @%s (%d args)",
						module->name, param->name,
						param->decorators[k].name, param->decorators[k].num_args
					);
				}
			}
		}

		// Mixed positional/named lookup: @range(0, max=100)
		const grain_param_info_t* gravity = &info.params[info.affectors[1].first_params];
		const grain_param_decorator_t* range = grain_find_decorator(gravity, "range");
		grain_decorator_arg_t arg;
		float min = grain_find_decorator_arg(range, 0, "min", &arg) ? arg.value.number : -1.f;
		float max = grain_find_decorator_arg(range, 1, "max", &arg) ? arg.value.number : -1.f;
		float step = grain_find_decorator_arg(range, 2, "step", &arg) ? arg.value.number : 0.5f;
		BLOG_INFO("gravity range: min=%f max=%f step(default)=%f", min, max, step);
		if (grain_find_decorator_arg(grain_find_decorator(gravity, "description"), 0, NULL, &arg)) {
			BLOG_INFO("gravity description: %s", arg.value.string);
		}
	}

	if (pool == NULL) {
		pool = grain_create_pool(grain, (grain_pool_opts_t){
			.archetype = archetype,
			.max_systems = 16,
			.max_emission_rate = 200.f,
			.lifetime_budget = 20.f,  // must cover max_lifetime below
		});
		if (pool == NULL) {
			BLOG_ERROR("%s", grain_get_last_error(grain));
		}
	}

	if (psystem == NULL) {
		psystem = grain_create_system(pool);
	}
}

static void
cleanup(void) {
	grain_destroy_pool(pool);  // All systems are destroyed
	grain_destroy(grain);
}

static void
fixed_update(void* userdata) {
}

static void
update(void) {
	cf_app_update(fixed_update);

	grain_begin_update(grain);

	grain_set_emission_rate(psystem, 200.f);
	grain_set_emitter_parameter(psystem, 0, "position", &(CF_V2){ 0.f, 0.f });
	grain_set_emitter_parameter(psystem, 0, "min_speed", &(float){ 100.f });
	grain_set_emitter_parameter(psystem, 0, "max_speed", &(float){ 150.f });
	grain_set_emitter_parameter(psystem, 0, "min_angle", &(float){ CF_PI * 0.25f });
	grain_set_emitter_parameter(psystem, 0, "max_angle", &(float){ CF_PI * 0.75f });
	grain_set_emitter_parameter(psystem, 1, "min_lifetime", &(float){ 5.f });
	grain_set_emitter_parameter(psystem, 1, "max_lifetime", &(float){ 10.f });

	grain_set_affector_parameter(psystem, 1, "gravity", &(float){ 30.8f });

	grain_set_renderer_parameter(psystem, "size", &(CF_V2){ 3.f, 3.f });
	CF_Pixel blue = cf_color_to_pixel(cf_color_blue());
	grain_set_renderer_parameter(psystem, "color", &blue.val);

	grain_tick(psystem, CF_DELTA_TIME);

	grain_end_update(grain);

	cf_apply_canvas(cf_app_get_canvas(), true);
	grain_begin_render(grain);
	grain_render(psystem);
	grain_end_render(grain);

	cf_app_draw_onto_screen(false);
}

SCENE {
	.init = init,
	.update = update,
	.cleanup = cleanup,
};
