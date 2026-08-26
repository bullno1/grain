#include "shared.h"

static btest_suite_t archetype = {
	.name = "archetype",
	.init_per_test = test_grain_init,
	.cleanup_per_test = test_grain_cleanup,
};

// Full archetype composition, headless: the generated update and render
// shaders compile through cute-spirv for both the desktop and the web (GLES)
// variants; only the GPU shader objects are skipped (grain_t::headless).

static const char* emitter_src =
	"Emitter(NoisePoint)\n"
	"Requires(\n"
	"	vec2 position;\n"
	"	vec2 velocity;\n"
	"	float lifetime;\n"
	")\n"
	"Params(\n"
	"	vec2 origin;\n"
	")\n"
	"Samplers(\n"
	"	@filter(nearest)\n"
	"	sampler2D noise;\n"
	")\n"
	"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
	"	particle.position = params.origin;\n"
	"	vec2 n = texture(noise, atlas_uv(noise_uvrect, vec2(rand(), 0.5))).xy;\n"
	"	particle.velocity = (n * 2.0 - 1.0) * 20.0;\n"
	"	particle.lifetime = 2.0;\n"
	"}\n";

static const char* affector_src =
	"Affector(VectorField)\n"
	"Requires(\n"
	"	vec2 position;\n"
	"	vec2 velocity;\n"
	"	float lifetime;\n"
	")\n"
	"Params(\n"
	"	float strength;\n"
	")\n"
	"Samplers(\n"
	"	sampler2D field;\n"
	")\n"
	"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
	"	vec2 uv = particle.position * 0.01 + 0.5;\n"
	"	vec2 force = texture(field, atlas_uv(field_uvrect, uv)).xy * 2.0 - 1.0;\n"
	"	particle.velocity += force * params.strength * ctx.dt;\n"
	"	particle.lifetime -= ctx.dt;\n"
	"}\n";

static const char* affector_no_sampler_src =
	"Affector(VectorField)\n"
	"Requires(\n"
	"	vec2 velocity;\n"
	"	float lifetime;\n"
	")\n"
	"Params(\n"
	"	float strength;\n"
	")\n"
	"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
	"	particle.velocity.y -= params.strength * ctx.dt;\n"
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
	"	grain_Color = texture(image, atlas_uv(image_uvrect, v_uv));\n"
	"	grain_Color.a *= clamp(particle.lifetime, 0.0, 1.0);\n"
	"}\n"
	"#endif\n";

BTEST(archetype, composes_with_samplers) {
	grain_emitter_t* emitter = grain_define_emitter(test_grain(), emitter_src);
	BTEST_ASSERT_EX(emitter != NULL, "%s", grain_get_last_error(test_grain()));
	grain_affector_t* affector = grain_define_affector(test_grain(), affector_src);
	BTEST_ASSERT_EX(affector != NULL, "%s", grain_get_last_error(test_grain()));
	grain_renderer_t* renderer = grain_define_renderer(test_grain(), renderer_src);
	BTEST_ASSERT_EX(renderer != NULL, "%s", grain_get_last_error(test_grain()));

	grain_archetype_t* archetype = grain_define_archetype(
		test_grain(), "Test",
		(grain_archetype_spec_t){
			.emitters = &emitter,
			.num_emitters = 1,
			.affectors = &affector,
			.num_affectors = 1,
			.renderer = renderer,
		}
	);
	BTEST_ASSERT_EX(archetype != NULL, "%s", grain_get_last_error(test_grain()));

	// Slots run in canonical order: emitters, affectors, renderer
	grain_archetype_info_t info = grain_inspect_archetype(archetype);
	BTEST_EXPECT_EQUAL("%d", info.emitters[0].first_sampler, 0);
	BTEST_EXPECT_EQUAL("%d", info.emitters[0].num_samplers, 1);
	BTEST_EXPECT_EQUAL("%d", info.affectors[0].first_sampler, 1);
	BTEST_EXPECT_EQUAL("%d", info.affectors[0].num_samplers, 1);
	BTEST_EXPECT_EQUAL("%d", info.renderer.first_sampler, 2);
	BTEST_EXPECT_EQUAL("%d", info.renderer.num_samplers, 1);

	BTEST_EXPECT(info.samplers[0].name == sintern("noise"));
	BTEST_EXPECT(info.samplers[1].name == sintern("field"));
	BTEST_EXPECT(info.samplers[2].name == sintern("image"));

	// Sampler decorators survive the deep copy into the archetype
	const grain_param_decorator_t* filter =
		grain_find_sampler_decorator(&info.samplers[0], "filter");
	BTEST_ASSERT(filter != NULL);
	grain_decorator_arg_t arg;
	BTEST_ASSERT(grain_find_decorator_arg(filter, 0, "mode", &arg));
	BTEST_EXPECT(arg.value.string == sintern("nearest"));
}

BTEST(archetype, same_local_sampler_name_across_modules) {
	grain_emitter_t* emitter = grain_define_emitter(test_grain(), emitter_src);
	BTEST_ASSERT_EX(emitter != NULL, "%s", grain_get_last_error(test_grain()));
	// The affector names its sampler `noise` too: renaming is per include, so
	// the two land in distinct slots without clashing
	grain_affector_t* affector = grain_define_affector(
		test_grain(),
		"Affector(Turbulence)\n"
		"Requires(\n"
		"	vec2 velocity;\n"
		"	float lifetime;\n"
		")\n"
		"Params(\n"
		")\n"
		"Samplers(\n"
		"	sampler2D noise;\n"
		")\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
		"	particle.velocity += texture(noise, atlas_uv(noise_uvrect, vec2(0.5))).xy;\n"
		"	particle.lifetime -= ctx.dt;\n"
		"}\n"
	);
	BTEST_ASSERT_EX(affector != NULL, "%s", grain_get_last_error(test_grain()));
	grain_renderer_t* renderer = grain_define_renderer(test_grain(), renderer_src);
	BTEST_ASSERT_EX(renderer != NULL, "%s", grain_get_last_error(test_grain()));

	grain_archetype_t* archetype = grain_define_archetype(
		test_grain(), "Test",
		(grain_archetype_spec_t){
			.emitters = &emitter,
			.num_emitters = 1,
			.affectors = &affector,
			.num_affectors = 1,
			.renderer = renderer,
		}
	);
	BTEST_ASSERT_EX(archetype != NULL, "%s", grain_get_last_error(test_grain()));

	grain_archetype_info_t info = grain_inspect_archetype(archetype);
	BTEST_EXPECT(info.samplers[0].name == sintern("noise"));
	BTEST_EXPECT(info.samplers[1].name == sintern("noise"));
	BTEST_EXPECT_EQUAL("%d", info.affectors[0].first_sampler, 1);
}

BTEST(archetype, composes_without_samplers) {
	grain_affector_t* affector = grain_define_affector(
		test_grain(), affector_no_sampler_src
	);
	BTEST_ASSERT_EX(affector != NULL, "%s", grain_get_last_error(test_grain()));
	grain_renderer_t* renderer = grain_define_renderer(
		test_grain(),
		"Renderer(Dot)\n"
		"Requires(\n"
		"	vec2 velocity;\n"
		"	float lifetime;\n"
		")\n"
		"Params(\n"
		"	vec2 size;\n"
		")\n"
		"#if GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_VERTEX\n"
		"void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
		"	gl_Position = grain_transform * vec4(particle.velocity + quad() * params.size, 0.0, 1.0);\n"
		"}\n"
		"#elif GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_FRAGMENT\n"
		"void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
		"	grain_Color = vec4(1.0);\n"
		"}\n"
		"#endif\n"
	);
	BTEST_ASSERT_EX(renderer != NULL, "%s", grain_get_last_error(test_grain()));

	grain_archetype_t* archetype = grain_define_archetype(
		test_grain(), "Test",
		(grain_archetype_spec_t){
			.affectors = &affector,
			.num_affectors = 1,
			.renderer = renderer,
		}
	);
	BTEST_ASSERT_EX(archetype != NULL, "%s", grain_get_last_error(test_grain()));

	grain_archetype_info_t info = grain_inspect_archetype(archetype);
	BTEST_EXPECT_EQUAL("%d", info.renderer.num_samplers, 0);
}

BTEST(archetype, redefinition_shifts_slots) {
	grain_emitter_t* emitter = grain_define_emitter(test_grain(), emitter_src);
	BTEST_ASSERT_EX(emitter != NULL, "%s", grain_get_last_error(test_grain()));
	grain_affector_t* affector = grain_define_affector(test_grain(), affector_src);
	BTEST_ASSERT_EX(affector != NULL, "%s", grain_get_last_error(test_grain()));
	grain_renderer_t* renderer = grain_define_renderer(test_grain(), renderer_src);
	BTEST_ASSERT_EX(renderer != NULL, "%s", grain_get_last_error(test_grain()));

	grain_archetype_spec_t spec = {
		.emitters = &emitter,
		.num_emitters = 1,
		.affectors = &affector,
		.num_affectors = 1,
		.renderer = renderer,
	};
	grain_archetype_t* archetype = grain_define_archetype(test_grain(), "Test", spec);
	BTEST_ASSERT_EX(archetype != NULL, "%s", grain_get_last_error(test_grain()));
	BTEST_EXPECT_EQUAL(
		"%d", grain_inspect_archetype(archetype).renderer.first_sampler, 2
	);

	// Live reload: the affector loses its sampler and the renderer slides down
	affector = grain_define_affector(test_grain(), affector_no_sampler_src);
	BTEST_ASSERT_EX(affector != NULL, "%s", grain_get_last_error(test_grain()));
	spec.affectors = &affector;
	archetype = grain_define_archetype(test_grain(), "Test", spec);
	BTEST_ASSERT_EX(archetype != NULL, "%s", grain_get_last_error(test_grain()));

	grain_archetype_info_t info = grain_inspect_archetype(archetype);
	BTEST_EXPECT_EQUAL("%d", info.affectors[0].num_samplers, 0);
	BTEST_EXPECT_EQUAL("%d", info.renderer.first_sampler, 1);
}

BTEST(archetype, too_many_samplers) {
	grain_affector_t* affector = grain_define_affector(
		test_grain(),
		"Affector(Hungry)\n"
		"Requires(\n"
		"	vec2 velocity;\n"
		"	float lifetime;\n"
		")\n"
		"Params(\n"
		")\n"
		"Samplers(\n"
		"	sampler2D t0;\n"
		"	sampler2D t1;\n"
		"	sampler2D t2;\n"
		"	sampler2D t3;\n"
		"	sampler2D t4;\n"
		"	sampler2D t5;\n"
		"	sampler2D t6;\n"
		"	sampler2D t7;\n"
		")\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
		"	particle.velocity += texture(t0, vec2(0.5)).xy + texture(t1, vec2(0.5)).xy\n"
		"		+ texture(t2, vec2(0.5)).xy + texture(t3, vec2(0.5)).xy\n"
		"		+ texture(t4, vec2(0.5)).xy + texture(t5, vec2(0.5)).xy\n"
		"		+ texture(t6, vec2(0.5)).xy + texture(t7, vec2(0.5)).xy;\n"
		"	particle.lifetime -= ctx.dt;\n"
		"}\n"
	);
	BTEST_ASSERT_EX(affector != NULL, "%s", grain_get_last_error(test_grain()));
	grain_renderer_t* renderer = grain_define_renderer(test_grain(), renderer_src);
	BTEST_ASSERT_EX(renderer != NULL, "%s", grain_get_last_error(test_grain()));

	// 8 user samplers + 1 attribute texture busts the 8-per-stage web budget
	grain_archetype_t* archetype = grain_define_archetype(
		test_grain(), "Test",
		(grain_archetype_spec_t){
			.affectors = &affector,
			.num_affectors = 1,
			.renderer = renderer,
		}
	);
	BTEST_EXPECT(archetype == NULL);
	GRAIN_EXPECT_ERROR_CONTAINS("too many samplers");
}
