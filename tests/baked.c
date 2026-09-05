#include "shared.h"
#include "baked.h"
#include "blueprint.h"

static btest_suite_t baked = {
	.name = "baked",
	.init_per_test = test_grain_init,
	.cleanup_per_test = test_grain_cleanup,
};

// Round trip of the baked path, fully headless: modules + archetype compile
// in one grain (CPU-side codegen), grain_bake flattens them, and
// grain_load_blueprint_baked reconstructs an equivalent archetype + blueprint
// in a second grain without touching the compiler.

static const char* emitter_src =
	"Emitter(Point)\n"
	"Requires(\n"
	"	vec2 position;\n"
	"	vec2 velocity;\n"
	"	float lifetime;\n"
	")\n"
	"Params(\n"
	"	vec2 origin;\n"
	"	@range(min=0, step=0.1)\n"
	"	float speed;\n"
	")\n"
	"Samplers(\n"
	"	@filter(nearest)\n"
	"	sampler2D noise;\n"
	")\n"
	"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
	"	particle.position = params.origin;\n"
	"	vec2 n = texture(noise, atlas_uv(noise_uvrect, vec2(rand(), 0.5))).xy;\n"
	"	particle.velocity = (n * 2.0 - 1.0) * params.speed;\n"
	"	particle.lifetime = 2.0;\n"
	"}\n";

static const char* affector_src =
	"Affector(Drag)\n"
	"Requires(\n"
	"	vec2 velocity;\n"
	"	float lifetime;\n"
	")\n"
	"Params(\n"
	"	float strength;\n"
	")\n"
	"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
	"	particle.velocity *= 1.0 - params.strength * ctx.dt;\n"
	"	particle.lifetime -= ctx.dt;\n"
	"}\n";

static const char* renderer_src =
	"Renderer(Sprite)\n"
	"Requires(\n"
	"	vec2 position;\n"
	"	float lifetime;\n"
	")\n"
	"Params(\n"
	"	vec2 size;\n"
	"	@color\n"
	"	uint tint;\n"
	")\n"
	"Samplers(\n"
	"	sampler2D image;\n"
	")\n"
	"#if GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_VERTEX\n"
	"Varying(2) vec2 v_uv;\n"
	"void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
	"	if (particle.lifetime > 0.0) {\n"
	"		vec2 corner = quad();\n"
	"		v_uv = vec2(corner.x + 0.5, 0.5 - corner.y);\n"
	"		gl_Position = grain_transform * vec4(particle.position + corner * params.size, 0.0, 1.0);\n"
	"	} else {\n"
	"		cull();\n"
	"	}\n"
	"}\n"
	"#elif GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_FRAGMENT\n"
	"Varying(2) vec2 v_uv;\n"
	"void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
	"	grain_Color = premultiply(unpackUnorm4x8(params.tint))\n"
	"		* texture(image, atlas_uv(image_uvrect, v_uv));\n"
	"	grain_Color *= clamp(particle.lifetime, 0.0, 1.0);\n"
	"}\n"
	"#endif\n";

static char*
test_strdup(const char* str) {
	size_t len = strlen(str);
	char* copy = cf_alloc(len + 1);
	memcpy(copy, str, len + 1);
	return copy;
}

// Compose the source effect in test_grain() and fill a hand-built blueprint
// record around its archetype; returns the archetype or NULL
static grain_archetype_t*
test_build_source_effect(grain_blueprint_t* blueprint) {
	grain_emitter_t* emitter = grain_define_emitter(test_grain(), emitter_src);
	if (emitter == NULL) { return NULL; }
	grain_affector_t* affector = grain_define_affector(test_grain(), affector_src);
	if (affector == NULL) { return NULL; }
	grain_renderer_t* renderer = grain_define_renderer(test_grain(), renderer_src);
	if (renderer == NULL) { return NULL; }

	grain_archetype_t* archetype = grain_define_archetype(
		test_grain(), "BakedFx",
		(grain_archetype_spec_t){
			.emitters = &emitter,
			.num_emitters = 1,
			.affectors = &affector,
			.num_affectors = 1,
			.renderer = renderer,
		}
	);
	if (archetype == NULL) { return NULL; }

	*blueprint = (grain_blueprint_t){
		.name = sintern("BakedFx"),
		.emission_rate = 42.5f,
		.max_systems = 3,
		.max_emission_rate = 512.f,
		.lifetime_budget = 16.f,
		.max_burst_size = 128,
		.archetype = archetype,
	};

	grain_blueprint_slot_t emitter_slot = { .module = sintern("Point") };
	grain_blueprint_param_t origin = {
		.name = sintern("origin"),
		.num_components = 2,
		.components = { 1.5, -2.5 },
	};
	apush(emitter_slot.params, origin);
	apush(blueprint->emitter_slots, emitter_slot);

	grain_blueprint_slot_t affector_slot = { .module = sintern("Drag") };
	grain_blueprint_param_t strength = {
		.name = sintern("strength"),
		.num_components = 1,
		.components = { 9.81 },
	};
	apush(affector_slot.params, strength);
	apush(blueprint->affector_slots, affector_slot);

	blueprint->renderer_slot.module = sintern("Sprite");
	grain_blueprint_param_t tint = {
		.name = sintern("tint"),
		.num_components = 1,
		// A packed color that only survives through int64 handling
		.components = { 4278255607.0 },
	};
	apush(blueprint->renderer_slot.params, tint);
	grain_blueprint_texture_t image = {
		.sampler_name = sintern("image"),
		.path = test_strdup("sprites/fire.png"),
	};
	apush(blueprint->renderer_slot.textures, image);

	return archetype;
}

static void
test_expect_equal_decorators(
	const grain_param_decorator_t* actual,
	int num_actual,
	const grain_param_decorator_t* expected,
	int num_expected
) {
	BTEST_ASSERT_EQUAL("%d", num_actual, num_expected);
	for (int i = 0; i < num_actual; ++i) {
		BTEST_EXPECT(actual[i].name == expected[i].name);
		BTEST_ASSERT_EQUAL("%d", actual[i].num_args, expected[i].num_args);
		for (int j = 0; j < actual[i].num_args; ++j) {
			const grain_decorator_arg_t* a = &actual[i].args[j];
			const grain_decorator_arg_t* e = &expected[i].args[j];
			BTEST_EXPECT_EQUAL("%d", a->index, e->index);
			BTEST_EXPECT(a->name == e->name);
			BTEST_ASSERT_EQUAL("%d", a->type, e->type);
			if (a->type == GRAIN_DECORATOR_ARG_NUMBER) {
				BTEST_EXPECT_EQUAL("%f", a->value.number, e->value.number);
			} else {
				BTEST_EXPECT(a->value.string == e->value.string);
			}
		}
	}
}

static void
test_expect_equal_module_info(
	const grain_module_info_t* actual,
	const grain_module_info_t* expected
) {
	BTEST_EXPECT(actual->name == expected->name);
	BTEST_EXPECT_EQUAL("%d", actual->first_param, expected->first_param);
	BTEST_EXPECT_EQUAL("%d", actual->num_params, expected->num_params);
	BTEST_EXPECT_EQUAL("%d", actual->first_sampler, expected->first_sampler);
	BTEST_EXPECT_EQUAL("%d", actual->num_samplers, expected->num_samplers);
}

BTEST(baked, round_trip) {
	grain_blueprint_t source_bp = { 0 };
	grain_archetype_t* source = test_build_source_effect(&source_bp);
	BTEST_ASSERT_EX(source != NULL, "%s", grain_get_last_error(test_grain()));

	grain_baked_effect_t effect;
	grain_bake_scratch_t scratch = { 0 };
	BTEST_ASSERT_EX(
		grain_bake(test_grain(), &source_bp, &effect, &scratch),
		"%s", grain_get_last_error(test_grain())
	);

	// Load into a second, independent headless grain
	grain_t grain_b = {
		.headless = true,
		.arena = cf_make_arena(16, 64 * 1024),
	};
	grain_blueprint_t* loaded_bp = grain_load_blueprint_baked(&grain_b, &effect);
	BTEST_ASSERT_EX(loaded_bp != NULL, "%s", grain_get_last_error(&grain_b));
	grain_archetype_t* loaded = grain_blueprint_archetype(loaded_bp);
	BTEST_ASSERT(loaded != NULL);
	BTEST_EXPECT(loaded == map_get(grain_b.archetypes, sintern("BakedFx")));

	// Reflection trees match, down to interned pointers
	BTEST_ASSERT_EQUAL("%d", asize(loaded->emitters), asize(source->emitters));
	test_expect_equal_module_info(&loaded->emitters[0], &source->emitters[0]);
	BTEST_ASSERT_EQUAL("%d", asize(loaded->affectors), asize(source->affectors));
	test_expect_equal_module_info(&loaded->affectors[0], &source->affectors[0]);
	test_expect_equal_module_info(&loaded->renderer, &source->renderer);

	BTEST_ASSERT_EQUAL("%d", asize(loaded->params), asize(source->params));
	for (int i = 0; i < asize(source->params); ++i) {
		BTEST_EXPECT(loaded->params[i].name == source->params[i].name);
		BTEST_EXPECT_EQUAL("%d", loaded->params[i].type, source->params[i].type);
		BTEST_EXPECT_EQUAL("%d", loaded->params_offsets[i], source->params_offsets[i]);
		test_expect_equal_decorators(
			loaded->params[i].decorators, loaded->params[i].num_decorators,
			source->params[i].decorators, source->params[i].num_decorators
		);
	}

	BTEST_ASSERT_EQUAL("%d", asize(loaded->samplers), asize(source->samplers));
	for (int i = 0; i < asize(source->samplers); ++i) {
		BTEST_EXPECT(loaded->samplers[i].name == source->samplers[i].name);
		BTEST_EXPECT(loaded->sampler_module_names[i] == source->sampler_module_names[i]);
		test_expect_equal_decorators(
			loaded->samplers[i].decorators, loaded->samplers[i].num_decorators,
			source->samplers[i].decorators, source->samplers[i].num_decorators
		);
	}

	// Layout scalars
	BTEST_EXPECT_EQUAL("%d", loaded->num_textures, source->num_textures);
	BTEST_EXPECT_EQUAL("%d", loaded->update_size, source->update_size);
	BTEST_EXPECT_EQUAL("%d", loaded->render_size, source->render_size);
	BTEST_EXPECT_EQUAL("%d", loaded->birth_texture, source->birth_texture);
	BTEST_EXPECT_EQUAL("%d", loaded->birth_channel, source->birth_channel);
	BTEST_EXPECT(loaded->attr_layout_hash == source->attr_layout_hash);

	// Bytecode is referenced, never copied or owned
	BTEST_EXPECT(!loaded->own_bytecode);
	BTEST_EXPECT(
		loaded->shaders.update_frag_bytecode.content
		== source->shaders.update_frag_bytecode.content
	);
	BTEST_EXPECT(
		loaded->shaders.render_vert_bytecode.content
		== source->shaders.render_vert_bytecode.content
	);
	BTEST_EXPECT(
		loaded->shaders.render_frag_bytecode.content
		== source->shaders.render_frag_bytecode.content
	);

	// The blueprint record round-trips
	grain_pool_opts_t pool_opts = grain_blueprint_pool_opts(loaded_bp);
	BTEST_EXPECT(pool_opts.archetype == loaded);
	BTEST_EXPECT_EQUAL("%d", pool_opts.max_systems, 3);
	BTEST_EXPECT_EQUAL("%f", pool_opts.max_emission_rate, 512.f);
	BTEST_EXPECT_EQUAL("%f", pool_opts.lifetime_budget, 16.f);
	BTEST_EXPECT_EQUAL("%d", pool_opts.max_burst_size, 128);
	BTEST_EXPECT_EQUAL("%f", grain_blueprint_emission_rate(loaded_bp), 42.5f);

	BTEST_ASSERT_EQUAL("%d", asize(loaded_bp->emitter_slots), 1);
	BTEST_EXPECT(loaded_bp->emitter_slots[0].module == sintern("Point"));
	BTEST_ASSERT_EQUAL("%d", asize(loaded_bp->emitter_slots[0].params), 1);
	grain_blueprint_param_t* origin = &loaded_bp->emitter_slots[0].params[0];
	BTEST_EXPECT(origin->name == sintern("origin"));
	BTEST_EXPECT_EQUAL("%d", origin->num_components, 2);
	BTEST_EXPECT_EQUAL("%f", origin->components[0], 1.5);
	BTEST_EXPECT_EQUAL("%f", origin->components[1], -2.5);

	BTEST_ASSERT_EQUAL("%d", asize(loaded_bp->renderer_slot.params), 1);
	grain_blueprint_param_t* tint = &loaded_bp->renderer_slot.params[0];
	BTEST_EXPECT(tint->name == sintern("tint"));
	// Packed colors survive exactly
	BTEST_EXPECT((uint32_t)tint->components[0] == 4278255607u);

	BTEST_ASSERT_EQUAL("%d", grain_blueprint_num_textures(loaded_bp), 1);
	grain_blueprint_texture_info_t texture = grain_blueprint_get_texture(loaded_bp, 0);
	BTEST_EXPECT_EQUAL("%d", texture.kind, GRAIN_MODULE_RENDERER);
	BTEST_EXPECT(texture.module_name == sintern("Sprite"));
	BTEST_EXPECT(texture.sampler_name == sintern("image"));
	BTEST_EXPECT(strcmp(texture.path, "sprites/fire.png") == 0);

	grain_destroy_blueprint(loaded_bp);
	grain_bake_scratch_free(&scratch);
	grain_blueprint_cleanup(&source_bp);
	grain_free_archetypes(&grain_b);
	cf_destroy_arena(&grain_b.arena);
}

BTEST(baked, reload_bumps_revision) {
	grain_blueprint_t source_bp = { 0 };
	grain_archetype_t* source = test_build_source_effect(&source_bp);
	BTEST_ASSERT_EX(source != NULL, "%s", grain_get_last_error(test_grain()));

	grain_baked_effect_t effect;
	grain_bake_scratch_t scratch = { 0 };
	BTEST_ASSERT(grain_bake(test_grain(), &source_bp, &effect, &scratch));

	grain_t grain_b = {
		.headless = true,
		.arena = cf_make_arena(16, 64 * 1024),
	};
	grain_blueprint_t* first = grain_load_blueprint_baked(&grain_b, &effect);
	BTEST_ASSERT(first != NULL);
	grain_archetype_t* archetype = grain_blueprint_archetype(first);
	BTEST_EXPECT_EQUAL("%u", archetype->revision, 1u);
	grain_destroy_blueprint(first);

	// Redefinition under a live name follows reload semantics; cleanup must
	// not touch the referenced bytecode (own_bytecode == false)
	grain_blueprint_t* second = grain_load_blueprint_baked(&grain_b, &effect);
	BTEST_ASSERT(second != NULL);
	BTEST_EXPECT(grain_blueprint_archetype(second) == archetype);
	BTEST_EXPECT_EQUAL("%u", archetype->revision, 2u);
	// The source's bytecode survived the baked archetype's cleanup
	BTEST_EXPECT(source->shaders.update_frag_bytecode.content != NULL);

	grain_destroy_blueprint(second);
	grain_bake_scratch_free(&scratch);
	grain_blueprint_cleanup(&source_bp);
	grain_free_archetypes(&grain_b);
	cf_destroy_arena(&grain_b.arena);
}

BTEST(baked, version_mismatch) {
	grain_baked_effect_t effect = {
		.baked_version = GRAIN_BAKED_VERSION + 1,
		.name = "Future",
	};
	BTEST_EXPECT(grain_load_blueprint_baked(test_grain(), &effect) == NULL);
	GRAIN_EXPECT_ERROR_CONTAINS("version");
}
