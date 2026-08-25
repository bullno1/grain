#include "shared.h"

static btest_suite_t kind = {
	.name = "module/kind",
	.init_per_test = test_grain_init,
	.cleanup_per_test = test_grain_cleanup,
};

// One well-formed module of each kind

static const char* emitter_source =
	"Emitter(Point)\n"
	"Requires(\n"
	"	vec2 position;\n"
	")\n"
	"Params(\n"
	"	vec2 position;\n"
	")\n"
	"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
	"	particle.position = params.position;\n"
	"}";

static const char* affector_source =
	"Affector(Gravity)\n"
	"Requires(\n"
	"	vec2 velocity;\n"
	")\n"
	"Params(\n"
	"	float gravity;\n"
	")\n"
	"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
	"	particle.velocity.y -= params.gravity * ctx.dt;\n"
	"}";

static const char* renderer_source =
	"Renderer(Quad)\n"
	"Requires(\n"
	"	vec2 position;\n"
	")\n"
	"Params(\n"
	"	vec2 size;\n"
	")\n"
	"#if GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_VERTEX\n"
	"void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
	"	gl_Position = grain_transform * vec4(particle.position + quad() * params.size, 0.0, 1.0);\n"
	"}\n"
	"#elif GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_FRAGMENT\n"
	"void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
	"	grain_Color = vec4(1.0);\n"
	"}\n"
	"#endif";

BTEST(kind, typed_define_rejects_wrong_kind) {
	BTEST_EXPECT(grain_define_affector(test_grain(), emitter_source) == NULL);
	GRAIN_EXPECT_ERROR_CONTAINS("`Point` is declared as an emitter, cannot be defined as an affector");

	BTEST_EXPECT(grain_define_renderer(test_grain(), affector_source) == NULL);
	GRAIN_EXPECT_ERROR_CONTAINS("`Gravity` is declared as an affector, cannot be defined as a renderer");

	BTEST_EXPECT(grain_define_emitter(test_grain(), renderer_source) == NULL);
	GRAIN_EXPECT_ERROR_CONTAINS("`Quad` is declared as a renderer, cannot be defined as an emitter");

	// A rejected module must not be registered anywhere, not even under its
	// declared kind: otherwise a failed reload could silently shadow a module
	BTEST_EXPECT_EQUAL("%d", map_size(test_grain()->emitters), 0);
	BTEST_EXPECT_EQUAL("%d", map_size(test_grain()->affectors), 0);
	BTEST_EXPECT_EQUAL("%d", map_size(test_grain()->renderers), 0);
}

BTEST(kind, generic_define_dispatches_on_declared_kind) {
	grain_module_ref_t emitter_ref = grain_define_module(test_grain(), emitter_source);
	BTEST_ASSERT_EX(emitter_ref.kind == GRAIN_MODULE_EMITTER, "%s", grain_get_last_error(test_grain()));
	BTEST_EXPECT(grain_get_emitter_name(emitter_ref.module) == sintern("Point"));

	grain_module_ref_t affector_ref = grain_define_module(test_grain(), affector_source);
	BTEST_ASSERT_EX(affector_ref.kind == GRAIN_MODULE_AFFECTOR, "%s", grain_get_last_error(test_grain()));
	BTEST_EXPECT(grain_get_affector_name(affector_ref.module) == sintern("Gravity"));

	grain_module_ref_t renderer_ref = grain_define_module(test_grain(), renderer_source);
	BTEST_ASSERT_EX(renderer_ref.kind == GRAIN_MODULE_RENDERER, "%s", grain_get_last_error(test_grain()));
	BTEST_EXPECT(grain_get_renderer_name(renderer_ref.module) == sintern("Quad"));

	BTEST_EXPECT_EQUAL("%d", map_size(test_grain()->emitters), 1);
	BTEST_EXPECT_EQUAL("%d", map_size(test_grain()->affectors), 1);
	BTEST_EXPECT_EQUAL("%d", map_size(test_grain()->renderers), 1);

	// Generic and typed define resolve to the same module slot
	BTEST_EXPECT(grain_define_emitter(test_grain(), emitter_source) == emitter_ref.module);
}

BTEST(kind, generic_define_reports_failure_as_invalid) {
	grain_module_ref_t ref = grain_define_module(test_grain(), "this is not a module");
	BTEST_EXPECT(ref.kind == GRAIN_MODULE_INVALID);
	BTEST_EXPECT(grain_get_last_error(test_grain()) != NULL);
}

BTEST(kind, missing_declaration_is_an_error) {
	grain_module_ref_t ref = grain_define_module(
		test_grain(),
		"Requires(\n"
		"	vec2 velocity;\n"
		")\n"
		"Params(\n"
		"	float gravity;\n"
		")\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {}"
	);
	BTEST_EXPECT(ref.kind == GRAIN_MODULE_INVALID);
	GRAIN_EXPECT_ERROR_CONTAINS("Missing module declaration");
}

BTEST(kind, multiple_declarations_is_an_error) {
	grain_module_ref_t ref = grain_define_module(
		test_grain(),
		"Emitter(Confused)\n"
		"Affector(Confused)\n"
		"Requires(\n"
		"	vec2 velocity;\n"
		")\n"
		"Params(\n"
		"	float gravity;\n"
		")\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {}"
	);
	BTEST_EXPECT(ref.kind == GRAIN_MODULE_INVALID);
	GRAIN_EXPECT_ERROR_CONTAINS("Multiple module declarations");
}

BTEST(kind, legacy_module_keyword_is_rejected) {
	// `Module` is no longer a macro, so it reaches the compiler as a bare
	// function call and fails there
	grain_module_ref_t ref = grain_define_module(
		test_grain(),
		"Module(Old)\n"
		"Requires(\n"
		"	vec2 velocity;\n"
		")\n"
		"Params(\n"
		"	float gravity;\n"
		")\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {}"
	);
	BTEST_EXPECT(ref.kind == GRAIN_MODULE_INVALID);
	BTEST_EXPECT(grain_get_last_error(test_grain()) != NULL);
}
