#include "shared.h"

static btest_suite_t sampler = {
	.name = "sampler",
	.init_per_test = test_grain_init,
	.cleanup_per_test = test_grain_cleanup,
};

// Definition-time handling of the Samplers block: scanning, decorators, the
// inspect compile against the generated declarations, and the error paths.
// Archetype codegen and actual GPU binding need a GPU and are out of scope.

BTEST(sampler, samplers_block_scanned) {
	grain_affector_t* affector = grain_define_affector(
		test_grain(),
		"Affector(Field)\n"
		"Requires(\n"
		"	vec2 velocity;\n"
		")\n"
		"Params(\n"
		"	float strength;\n"
		")\n"
		"Samplers(\n"
		"	sampler2D field;\n"
		")\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
		"	vec2 force = texture(field, atlas_uv(field_uvrect, vec2(0.5))).xy;\n"
		"	particle.velocity += force * params.strength * ctx.dt;\n"
		"}"
	);
	BTEST_ASSERT_EX(affector != NULL, "%s", grain_get_last_error(test_grain()));

	grain_module_t* stored = (grain_module_t*)affector;
	BTEST_ASSERT_EQUAL("%d", asize(stored->info->samplers), 1);
	BTEST_EXPECT(stored->info->samplers[0] == sintern("field"));
}

BTEST(sampler, sampler_decorators_recorded) {
	grain_affector_t* affector = grain_define_affector(
		test_grain(),
		"Affector(Field)\n"
		"Requires(\n"
		"	vec2 velocity;\n"
		")\n"
		"Params(\n"
		")\n"
		"Samplers(\n"
		"	@filter(nearest)\n"
		"	@wrap(repeat)\n"
		"	sampler2D field;\n"
		")\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
		"	particle.velocity = texture(field, vec2(0.5)).xy;\n"
		"}"
	);
	BTEST_ASSERT_EX(affector != NULL, "%s", grain_get_last_error(test_grain()));

	grain_module_t* stored = (grain_module_t*)affector;
	// Decorators are blanked out of the compiled copy; the declaration itself
	// survives (the archetype compile discards it via `#define Samplers(X)`).
	BTEST_EXPECT(strchr(stored->source, '@') == NULL);
	BTEST_EXPECT(strstr(stored->source, "sampler2D field;") != NULL);

	BTEST_ASSERT_EQUAL("%d", asize(stored->info->decorators), 2);
	BTEST_EXPECT(stored->info->decorators[0].name == sintern("filter"));
	BTEST_EXPECT(stored->info->decorators[0].param == sintern("field"));
	BTEST_EXPECT(stored->info->decorators[1].name == sintern("wrap"));
	BTEST_EXPECT(stored->info->decorators[1].param == sintern("field"));

	grain_decorator_arg_t* args = stored->info->decorator_args;
	BTEST_ASSERT_EQUAL("%d", asize(args), 2);
	BTEST_EXPECT(args[0].value.string == sintern("nearest"));
	BTEST_EXPECT(args[1].value.string == sintern("repeat"));
}

BTEST(sampler, multiple_samplers_ordered) {
	grain_emitter_t* emitter = grain_define_emitter(
		test_grain(),
		"Emitter(Curves)\n"
		"Requires(\n"
		"	vec2 velocity;\n"
		")\n"
		"Params(\n"
		")\n"
		"Samplers(\n"
		"	sampler2D noise;\n"
		"	sampler2D curve;\n"
		")\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
		"	particle.velocity = texture(noise, vec2(0.0)).xy + texture(curve, vec2(1.0)).xy;\n"
		"}"
	);
	BTEST_ASSERT_EX(emitter != NULL, "%s", grain_get_last_error(test_grain()));

	grain_module_t* stored = (grain_module_t*)emitter;
	BTEST_ASSERT_EQUAL("%d", asize(stored->info->samplers), 2);
	BTEST_EXPECT(stored->info->samplers[0] == sintern("noise"));
	BTEST_EXPECT(stored->info->samplers[1] == sintern("curve"));
}

BTEST(sampler, renderer_sampler_both_stages) {
	grain_renderer_t* renderer = grain_define_renderer(
		test_grain(),
		"Renderer(Sprite)\n"
		"Requires(\n"
		"	vec2 position;\n"
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
		"	vec2 corner = quad();\n"
		"	v_uv = corner + 0.5;\n"
		"	gl_Position = grain_transform * vec4(particle.position + corner * params.size, 0.0, 1.0);\n"
		"}\n"
		"#elif GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_FRAGMENT\n"
		"Varying(2) vec2 v_uv;\n"
		"void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
		"	grain_Color = texture(image, atlas_uv(image_uvrect, v_uv));\n"
		"}\n"
		"#endif"
	);
	BTEST_ASSERT_EX(renderer != NULL, "%s", grain_get_last_error(test_grain()));

	grain_module_t* stored = (grain_module_t*)renderer;
	BTEST_ASSERT_EQUAL("%d", asize(stored->info->samplers), 1);
	BTEST_EXPECT(stored->info->samplers[0] == sintern("image"));
}

BTEST(sampler, redefinition_replaces_samplers) {
	grain_affector_t* affector = grain_define_affector(
		test_grain(),
		"Affector(Field)\n"
		"Requires(\n"
		"	vec2 velocity;\n"
		")\n"
		"Params(\n"
		")\n"
		"Samplers(\n"
		"	sampler2D field;\n"
		")\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
		"	particle.velocity = texture(field, vec2(0.5)).xy;\n"
		"}"
	);
	BTEST_ASSERT_EX(affector != NULL, "%s", grain_get_last_error(test_grain()));

	affector = grain_define_affector(
		test_grain(),
		"Affector(Field)\n"
		"Requires(\n"
		"	vec2 velocity;\n"
		")\n"
		"Params(\n"
		")\n"
		"Samplers(\n"
		"	sampler2D flow;\n"
		"	sampler2D turbulence;\n"
		")\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
		"	particle.velocity =\n"
		"		texture(flow, vec2(0.5)).xy + texture(turbulence, vec2(0.5)).xy;\n"
		"}"
	);
	BTEST_ASSERT_EX(affector != NULL, "%s", grain_get_last_error(test_grain()));

	grain_module_t* stored = (grain_module_t*)affector;
	BTEST_ASSERT_EQUAL("%d", asize(stored->info->samplers), 2);
	BTEST_EXPECT(stored->info->samplers[0] == sintern("flow"));
	BTEST_EXPECT(stored->info->samplers[1] == sintern("turbulence"));
}

static void
expect_define_error(const char* source, const char* error_fragment) {
	grain_module_ref_t ref = grain_define_module(test_grain(), source);
	BTEST_EXPECT(ref.kind == GRAIN_MODULE_INVALID);
	GRAIN_EXPECT_ERROR_CONTAINS(error_fragment);
}

BTEST(sampler, rogue_sampler_outside_block) {
	expect_define_error(
		"Affector(Rogue)\n"
		"Requires(\n"
		"	vec2 velocity;\n"
		")\n"
		"Params(\n"
		")\n"
		"layout(set = 0, binding = 15) uniform sampler2D rogue;\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
		"	particle.velocity = texture(rogue, vec2(0.5)).xy;\n"
		"}",
		"must be declared in a `Samplers` block"
	);
}

static void
expect_samplers_error(const char* samplers_body, const char* error_fragment) {
	char source[1024];
	snprintf(
		source, sizeof(source),
		"Affector(T)\n"
		"Requires(\n"
		"	vec2 velocity;\n"
		")\n"
		"Params(\n"
		")\n"
		"Samplers(\n%s\n)\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {}\n",
		samplers_body
	);
	expect_define_error(source, error_fragment);
}

BTEST(sampler, error_non_sampler_declaration) {
	expect_samplers_error("float x;", "Only `sampler2D` declarations are allowed");
}

BTEST(sampler, error_multi_declarator) {
	expect_samplers_error("sampler2D a, b;", "Only one sampler per declaration");
}

BTEST(sampler, error_duplicate) {
	expect_samplers_error(
		"sampler2D field;\n	sampler2D field;", "Duplicate sampler `field`"
	);
}

BTEST(sampler, error_reserved_prefix) {
	expect_samplers_error(
		"sampler2D grain_tex;", "uses the reserved `grain_` prefix"
	);
}

BTEST(sampler, error_empty_block) {
	expect_samplers_error("", "Empty `Samplers` block");
}

BTEST(sampler, error_dangling_decorator) {
	expect_samplers_error("@filter(nearest)", "not attached");
}

BTEST(sampler, error_name_collides_with_param) {
	expect_define_error(
		"Affector(T)\n"
		"Requires(\n"
		"	vec2 velocity;\n"
		")\n"
		"Params(\n"
		"	float field;\n"
		")\n"
		"Samplers(\n"
		"	sampler2D field;\n"
		")\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {}\n",
		"same name as a module parameter"
	);
}

BTEST(sampler, error_name_collides_with_attribute) {
	expect_define_error(
		"Affector(T)\n"
		"Requires(\n"
		"	vec2 field;\n"
		")\n"
		"Params(\n"
		")\n"
		"Samplers(\n"
		"	sampler2D field;\n"
		")\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {}\n",
		"same name as a particle attribute"
	);
}
