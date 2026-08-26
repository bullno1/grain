#include "shared.h"

static btest_suite_t module = {
	.name = "decorator/module",
	.init_per_test = test_grain_init,
	.cleanup_per_test = test_grain_cleanup,
};

// The full definition pipeline: strip -> compile the stripped source with
// cute-spirv -> reflect -> validate bindings -> store.

BTEST(module, decorated_module_compiles_and_stores) {
	grain_emitter_t* emitter = grain_define_emitter(
		test_grain(),
		"Emitter(Gravity)\n"
		"Requires(\n"
		"	vec2 velocity;\n"
		")\n"
		"Params(\n"
		"	@range(0, max=100)\n"
		"	@description(\"How much to push\")\n"
		"	@widget(type=SLIDER)\n"
		"	float gravity;\n"
		")\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
		"	particle.velocity.y -= params.gravity * ctx.dt;\n"
		"}"
	);
	BTEST_ASSERT_EX(emitter != NULL, "%s", grain_get_last_error(test_grain()));

	grain_module_t* stored = (grain_module_t*)emitter;
	// The stored source is the stripped copy: nothing for the archetype
	// compiler to trip on later.
	BTEST_EXPECT(strchr(stored->source, '@') == NULL);

	BTEST_ASSERT_EQUAL("%d", asize(stored->info->decorators), 3);
	BTEST_EXPECT(stored->info->decorators[0].name == sintern("range"));
	BTEST_EXPECT(stored->info->decorators[0].param == sintern("gravity"));
	BTEST_EXPECT(stored->info->decorators[1].name == sintern("description"));
	BTEST_EXPECT(stored->info->decorators[2].name == sintern("widget"));

	grain_decorator_arg_t* args = stored->info->decorator_args;
	BTEST_EXPECT_EQUAL("%d", args[0].index, 0);
	BTEST_EXPECT_EQUAL("%f", args[0].value.number, 0.f);
	BTEST_EXPECT(args[1].name == sintern("max"));
	BTEST_EXPECT_EQUAL("%f", args[1].value.number, 100.f);
	BTEST_EXPECT(args[2].value.string == sintern("How much to push"));
	BTEST_EXPECT_EQUAL("%d", args[3].type, GRAIN_DECORATOR_ARG_IDENT);
	BTEST_EXPECT(args[3].name == sintern("type"));
	BTEST_EXPECT(args[3].value.string == sintern("SLIDER"));
}

BTEST(module, redefinition_replaces_decorators) {
	grain_emitter_t* emitter = grain_define_emitter(
		test_grain(),
		"Emitter(Gravity)\n"
		"Requires(\n"
		"	vec2 velocity;\n"
		")\n"
		"Params(\n"
		"	@range(0, max=100) float gravity;\n"
		")\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {}"
	);
	BTEST_ASSERT_EX(emitter != NULL, "%s", grain_get_last_error(test_grain()));

	// Live reload: same module name, new decorators
	grain_emitter_t* redefined = grain_define_emitter(
		test_grain(),
		"Emitter(Gravity)\n"
		"Requires(\n"
		"	vec2 velocity;\n"
		")\n"
		"Params(\n"
		"	@range(min=0, max=50) float gravity;\n"
		")\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {}"
	);
	BTEST_ASSERT_EX(redefined != NULL, "%s", grain_get_last_error(test_grain()));
	BTEST_EXPECT(redefined == emitter);  // same module slot

	grain_module_t* stored = (grain_module_t*)redefined;
	BTEST_ASSERT_EQUAL("%d", asize(stored->info->decorators), 1);
	BTEST_EXPECT_EQUAL("%f", stored->info->decorator_args[1].value.number, 50.f);
}

BTEST(module, renderer_params_support_decorators) {
	// The renderer path strips once but parses twice (vertex + fragment)
	grain_renderer_t* renderer = grain_define_renderer(
		test_grain(),
		"Renderer(Quad)\n"
		"Requires(\n"
		"	vec2 position;\n"
		")\n"
		"Params(\n"
		"	vec2 size;\n"
		"	@color uint color;\n"
		")\n"
		"#if GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_VERTEX\n"
		"void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
		"	gl_Position = grain_transform * vec4(particle.position + quad() * params.size, 0.0, 1.0);\n"
		"}\n"
		"#elif GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_FRAGMENT\n"
		"void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
		"	grain_Color = unpackUnorm4x8(params.color);\n"
		"}\n"
		"#endif"
	);
	BTEST_ASSERT_EX(renderer != NULL, "%s", grain_get_last_error(test_grain()));

	grain_module_t* stored = (grain_module_t*)renderer;
	BTEST_ASSERT_EQUAL("%d", asize(stored->info->decorators), 1);
	BTEST_EXPECT(stored->info->decorators[0].name == sintern("color"));
	BTEST_EXPECT(stored->info->decorators[0].param == sintern("color"));
}

BTEST(module, qualified_declaration_binds_to_the_member) {
	grain_emitter_t* emitter = grain_define_emitter(
		test_grain(),
		"Emitter(Qualified)\n"
		"Requires(\n"
		"	vec2 velocity;\n"
		")\n"
		"Params(\n"
		"	@range(0, max=100) highp float gravity;\n"
		")\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {}"
	);
	BTEST_ASSERT_EX(emitter != NULL, "%s", grain_get_last_error(test_grain()));

	grain_module_t* stored = (grain_module_t*)emitter;
	BTEST_ASSERT_EQUAL("%d", asize(stored->info->decorators), 1);
	BTEST_EXPECT(stored->info->decorators[0].param == sintern("gravity"));
}

BTEST(module, decorator_outside_params_is_a_compile_error) {
	// The scanner only reads the Params block, so a stray `@` elsewhere must
	// reach the compiler and fail there instead of being silently accepted.
	grain_emitter_t* emitter = grain_define_emitter(
		test_grain(),
		"Emitter(Bad)\n"
		"Requires(\n"
		"	@color vec2 velocity;\n"
		")\n"
		"Params(\n"
		"	float gravity;\n"
		")\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {}"
	);
	BTEST_EXPECT(emitter == NULL);
}

BTEST(module, malformed_decorator_fails_with_grain_error) {
	grain_emitter_t* emitter = grain_define_emitter(
		test_grain(),
		"Emitter(Bad2)\n"
		"Requires(\n"
		"	vec2 velocity;\n"
		")\n"
		"Params(\n"
		"	@range(min=0, float gravity;\n"
		")\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {}"
	);
	BTEST_EXPECT(emitter == NULL);
	// `float` parses as an ident value, so `gravity` is the offending token
	GRAIN_EXPECT_ERROR_CONTAINS("Expected `,` or `)`");
}

BTEST(module, internal_builtins_are_not_visible_to_modules) {
	// grain/internal.glsl is included after the module, and the compiler is
	// single-pass, so internal functions are undeclared where user code compiles.
	grain_emitter_t* emitter = grain_define_emitter(
		test_grain(),
		"Emitter(Sneaky)\n"
		"Requires(\n"
		"	vec2 velocity;\n"
		")\n"
		"Params(\n"
		"	float gravity;\n"
		")\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
		"	grain_srand(0u, 1u);\n"
		"}"
	);
	BTEST_EXPECT(emitter == NULL);
	GRAIN_EXPECT_ERROR_CONTAINS("unknown function 'grain_srand'");
}

BTEST(module, modules_cannot_include_internal_paths) {
	// The inspect VFS only resolves grain/api.glsl, grain/internal.glsl (already
	// included by the stub), and the module itself; archetype and other-module
	// paths fail at definition time.
	grain_emitter_t* emitter = grain_define_emitter(
		test_grain(),
		"#include \"archetype/attrs.glsl\"\n"
		"Emitter(Sneaky2)\n"
		"Requires(\n"
		"	vec2 velocity;\n"
		")\n"
		"Params(\n"
		"	float gravity;\n"
		")\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {}"
	);
	BTEST_EXPECT(emitter == NULL);
	GRAIN_EXPECT_ERROR_CONTAINS("cannot open include file \"archetype/attrs.glsl\"");
}
