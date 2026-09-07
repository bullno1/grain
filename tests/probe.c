#include "shared.h"
#include <float.h>

static btest_suite_t probe = {
	.name = "probe",
	.init_per_test = test_grain_init,
	.cleanup_per_test = test_grain_cleanup,
};

// The probe pass itself needs a pool and a GPU. What is testable headlessly
// is the shader it compiles (both the desktop and the web variant, through
// cute-spirv) and the pure bounds arithmetic.

BTEST(probe, bounds_empty_and_union) {
	grain_bounds_t empty = grain_bounds_empty();
	BTEST_EXPECT(grain_bounds_is_empty(empty));

	grain_bounds_t a = { .min = { -1.f, 0.f, 2.f }, .max = { 1.f, 3.f, 2.f } };
	BTEST_EXPECT(!grain_bounds_is_empty(a));
	// A flat box is not empty
	grain_bounds_t flat = { .min = { 0.f, 0.f, 0.f }, .max = { 0.f, 0.f, 0.f } };
	BTEST_EXPECT(!grain_bounds_is_empty(flat));

	// Empty absorbs
	grain_bounds_t acc = grain_bounds_empty();
	grain_bounds_union(&acc, a);
	for (int i = 0; i < 3; ++i) {
		BTEST_EXPECT_EQUAL("%f", acc.min[i], a.min[i]);
		BTEST_EXPECT_EQUAL("%f", acc.max[i], a.max[i]);
	}

	// Union with empty is a no-op
	grain_bounds_union(&acc, empty);
	for (int i = 0; i < 3; ++i) {
		BTEST_EXPECT_EQUAL("%f", acc.min[i], a.min[i]);
		BTEST_EXPECT_EQUAL("%f", acc.max[i], a.max[i]);
	}

	grain_bounds_t b = { .min = { -5.f, 1.f, -1.f }, .max = { 0.f, 1.f, 4.f } };
	grain_bounds_union(&acc, b);
	BTEST_EXPECT_EQUAL("%f", acc.min[0], -5.f);
	BTEST_EXPECT_EQUAL("%f", acc.min[1], 0.f);
	BTEST_EXPECT_EQUAL("%f", acc.min[2], -1.f);
	BTEST_EXPECT_EQUAL("%f", acc.max[0], 1.f);
	BTEST_EXPECT_EQUAL("%f", acc.max[1], 3.f);
	BTEST_EXPECT_EQUAL("%f", acc.max[2], 4.f);
}

static const char* emitter_src =
	"Emitter(Point)\n"
	"Requires(\n"
	"	vec2 position;\n"
	"	float lifetime;\n"
	")\n"
	"Params(\n"
	"	vec2 origin;\n"
	")\n"
	"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
	"	particle.position = params.origin + vec2(rand(), rand());\n"
	"	particle.lifetime = 2.0;\n"
	"}\n";

static const char* affector_src =
	"Affector(Age)\n"
	"Requires(\n"
	"	float lifetime;\n"
	")\n"
	"Params(\n"
	")\n"
	"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
	"	particle.lifetime -= ctx.dt;\n"
	"}\n";

// Uses quad() and uv_quad(), which the probe pass redirects from the vertex
// index to its per-instance corner, and a sampler, so the probe's sampler
// bindings line up with the render stage's
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
	"Varying(0) vec2 v_uv;\n"
	"void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
	"	if (particle.lifetime > 0.0) {\n"
	"		v_uv = uv_quad();\n"
	"		gl_Position = grain_transform * vec4(particle.position + quad() * params.size, 0.0, 1.0);\n"
	"	} else {\n"
	"		cull();\n"
	"	}\n"
	"}\n"
	"#elif GRAIN_SHADER_STAGE == GRAIN_SHADER_STAGE_FRAGMENT\n"
	"Varying(0) vec2 v_uv;\n"
	"void process(ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
	"	grain_Color = texture(image, atlas_uv(image_uvrect, v_uv));\n"
	"}\n"
	"#endif\n";

static grain_archetype_t*
define_test_archetype(void) {
	grain_emitter_t* emitter = grain_define_emitter(test_grain(), emitter_src);
	if (emitter == NULL) { return NULL; }
	grain_affector_t* affector = grain_define_affector(test_grain(), affector_src);
	if (affector == NULL) { return NULL; }
	grain_renderer_t* renderer = grain_define_renderer(test_grain(), renderer_src);
	if (renderer == NULL) { return NULL; }

	return grain_define_archetype(
		test_grain(), "Test",
		(grain_archetype_spec_t){
			.emitters = &emitter,
			.num_emitters = 1,
			.affectors = &affector,
			.num_affectors = 1,
			.renderer = renderer,
		}
	);
}

BTEST(probe, shader_compiles_from_kept_sources) {
	grain_archetype_t* archetype = define_test_archetype();
	BTEST_ASSERT_EX(archetype != NULL, "%s", grain_get_last_error(test_grain()));

	// Definition keeps what the probe needs and compiles nothing extra yet
	BTEST_ASSERT(archetype->has_probe_sources);
	BTEST_EXPECT(strcmp(archetype->probe_sources.renderer_name, "Sprite") == 0);
	BTEST_EXPECT(archetype->shaders.probe_vert_bytecode.content == NULL);

	// What grain_probe_system does on first use
	bool ok = grain_dsl_compile_probe(
		test_grain(), &archetype->probe_sources, &archetype->shaders
	);
	BTEST_ASSERT_EX(ok, "%s", grain_get_last_error(test_grain()));
	BTEST_EXPECT(archetype->shaders.probe_vert_bytecode.content != NULL);
	BTEST_EXPECT(archetype->shaders.probe_frag_bytecode.content != NULL);

	// The probe vertex stage binds the same storage buffers as the render
	// stage: params, clocks, and the region list in the draw list's slot
	const CF_ShaderInfo* info = &archetype->shaders.probe_vert_bytecode.shader_info;
	BTEST_EXPECT_EQUAL("%d", info->num_storage_buffers, 3);
	BTEST_EXPECT_EQUAL(
		"%d", info->num_images,
		archetype->shaders.render_vert_bytecode.shader_info.num_images
	);

	// Redefinition drops the compiled probe along with everything else
	grain_archetype_t* redefined = define_test_archetype();
	BTEST_ASSERT_EX(redefined != NULL, "%s", grain_get_last_error(test_grain()));
	BTEST_EXPECT(redefined == archetype);
	BTEST_EXPECT(archetype->shaders.probe_vert_bytecode.content == NULL);
	BTEST_EXPECT(archetype->has_probe_sources);
}
