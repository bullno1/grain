#include "shared.h"
#include "blueprint.h"

static btest_suite_t blueprint = {
	.name = "blueprint",
	.init_per_test = test_grain_init,
	.cleanup_per_test = test_grain_cleanup,
};

// The record layer (JSON <-> grain_blueprint_t) is pure and GPU-free.
// Materialization (grain_load_blueprint, grain_blueprint_apply) needs
// archetypes and pools which are out of scope here.

static char*
test_strdup(const char* str) {
	if (str == NULL) { return NULL; }
	size_t len = strlen(str);
	char* copy = cf_alloc(len + 1);
	memcpy(copy, str, len + 1);
	return copy;
}

static CF_JDoc
test_parse(const char* json, grain_blueprint_t* out, bool* ok) {
	CF_JDoc doc = cf_make_json(json, strlen(json));
	*ok = grain_blueprint_parse(test_grain(), cf_json_get_root(doc), out);
	return doc;
}

static const char* valid_blueprint =
	"{"
	"	\"grain_version\": 1,"
	"	\"name\": \"Fire\","
	"	\"emission_rate\": 10.5,"
	"	\"pool\": {"
	"		\"max_systems\": 4,"
	"		\"max_emission_rate\": 512.5,"
	"		\"lifetime_budget\": 16.0"
	"	},"
	"	\"modules\": ["
	"		{"
	"			\"kind\": \"emitter\","
	"			\"name\": \"Point\","
	"			\"path\": \"modules/emitters/Point.glsl\","
	"			\"source\": \"Emitter(Point)\\n\""
	"		},"
	"		{ \"kind\": \"affector\", \"name\": \"Gravity\", \"source\": \"Affector(Gravity)\\n\" },"
	"		{ \"kind\": \"renderer\", \"name\": \"Quad\", \"source\": \"Renderer(Quad)\\n\" }"
	"	],"
	"	\"archetype\": {"
	"		\"emitters\": ["
	"			{"
	"				\"module\": \"Point\","
	"				\"params\": { \"position\": [1.5, -2.5], \"spread\": 0.5 }"
	"			}"
	"		],"
	"		\"affectors\": ["
	"			{ \"module\": \"Gravity\", \"params\": { \"gravity\": [0, -9.81] } }"
	"		],"
	"		\"renderer\": {"
	"			\"module\": \"Quad\","
	"			\"textures\": { \"image\": \"sprites/fire.png\" }"
	"		}"
	"	}"
	"}";

BTEST(blueprint, parse_valid) {
	grain_blueprint_t bp = { 0 };
	bool ok;
	CF_JDoc doc = test_parse(valid_blueprint, &bp, &ok);
	BTEST_ASSERT_EX(ok, "%s", grain_get_last_error(test_grain()));

	BTEST_EXPECT(bp.name == sintern("Fire"));
	BTEST_EXPECT_EQUAL("%f", bp.emission_rate, 10.5f);
	BTEST_EXPECT_EQUAL("%d", bp.max_systems, 4);
	BTEST_EXPECT_EQUAL("%f", bp.max_emission_rate, 512.5f);
	BTEST_EXPECT_EQUAL("%f", bp.lifetime_budget, 16.f);
	// Absent in a pre-burst blueprint: defaults to 0 (bursts disabled)
	BTEST_EXPECT_EQUAL("%d", bp.max_burst_size, 0);

	BTEST_ASSERT_EQUAL("%d", asize(bp.modules), 3);
	BTEST_EXPECT_EQUAL("%d", bp.modules[0].ref.kind, GRAIN_MODULE_EMITTER);
	BTEST_EXPECT(bp.modules[0].name == sintern("Point"));
	BTEST_EXPECT(strcmp(bp.modules[0].source, "Emitter(Point)\n") == 0);
	BTEST_EXPECT(strcmp(bp.modules[0].path, "modules/emitters/Point.glsl") == 0);
	BTEST_EXPECT_EQUAL("%d", bp.modules[1].ref.kind, GRAIN_MODULE_AFFECTOR);
	BTEST_EXPECT(bp.modules[1].path == NULL);
	BTEST_EXPECT_EQUAL("%d", bp.modules[2].ref.kind, GRAIN_MODULE_RENDERER);

	BTEST_ASSERT_EQUAL("%d", asize(bp.emitter_slots), 1);
	BTEST_EXPECT(bp.emitter_slots[0].module == sintern("Point"));
	BTEST_ASSERT_EQUAL("%d", asize(bp.emitter_slots[0].params), 2);
	grain_blueprint_param_t* position = &bp.emitter_slots[0].params[0];
	BTEST_EXPECT(position->name == sintern("position"));
	BTEST_EXPECT_EQUAL("%d", position->num_components, 2);
	BTEST_EXPECT_EQUAL("%f", position->components[0], 1.5);
	BTEST_EXPECT_EQUAL("%f", position->components[1], -2.5);
	grain_blueprint_param_t* spread = &bp.emitter_slots[0].params[1];
	BTEST_EXPECT(spread->name == sintern("spread"));
	BTEST_EXPECT_EQUAL("%d", spread->num_components, 1);
	BTEST_EXPECT_EQUAL("%f", spread->components[0], 0.5);

	BTEST_ASSERT_EQUAL("%d", asize(bp.affector_slots), 1);
	BTEST_EXPECT(bp.affector_slots[0].module == sintern("Gravity"));

	BTEST_EXPECT(bp.renderer_slot.module == sintern("Quad"));
	BTEST_EXPECT_EQUAL("%d", asize(bp.renderer_slot.params), 0);

	// Slots without a `textures` key parse to none (optional on load)
	BTEST_EXPECT_EQUAL("%d", asize(bp.emitter_slots[0].textures), 0);
	BTEST_ASSERT_EQUAL("%d", asize(bp.renderer_slot.textures), 1);
	BTEST_EXPECT(bp.renderer_slot.textures[0].sampler_name == sintern("image"));
	BTEST_EXPECT(strcmp(bp.renderer_slot.textures[0].path, "sprites/fire.png") == 0);

	// The flattened accessors see the same records
	BTEST_ASSERT_EQUAL("%d", grain_blueprint_num_textures(&bp), 1);
	grain_blueprint_texture_info_t texture_info = grain_blueprint_get_texture(&bp, 0);
	BTEST_EXPECT_EQUAL("%d", texture_info.kind, GRAIN_MODULE_RENDERER);
	BTEST_EXPECT_EQUAL("%d", texture_info.module_index, 0);
	BTEST_EXPECT(texture_info.module_name == sintern("Quad"));
	BTEST_EXPECT(texture_info.sampler_name == sintern("image"));
	BTEST_EXPECT(strcmp(texture_info.path, "sprites/fire.png") == 0);

	grain_blueprint_cleanup(&bp);
	cf_destroy_json(doc);
}

BTEST(blueprint, parse_bad_texture_value) {
	grain_blueprint_t bp = { 0 };
	bool ok;
	CF_JDoc doc = test_parse(
		"{"
		"	\"grain_version\": 1,"
		"	\"pool\": { \"max_emission_rate\": 1, \"lifetime_budget\": 1 },"
		"	\"modules\": ["
		"		{ \"kind\": \"renderer\", \"name\": \"Quad\", \"source\": \"x\" }"
		"	],"
		"	\"archetype\": {"
		"		\"renderer\": { \"module\": \"Quad\", \"textures\": { \"image\": 5 } }"
		"	}"
		"}",
		&bp, &ok
	);
	BTEST_EXPECT(!ok);
	GRAIN_EXPECT_ERROR_CONTAINS("`Quad.image` must be a path string");

	grain_blueprint_cleanup(&bp);
	cf_destroy_json(doc);
}

BTEST(blueprint, parse_missing_version) {
	grain_blueprint_t bp = { 0 };
	bool ok;
	CF_JDoc doc = test_parse("{ \"name\": \"Fire\" }", &bp, &ok);
	BTEST_EXPECT(!ok);
	GRAIN_EXPECT_ERROR_CONTAINS("grain_version");

	grain_blueprint_cleanup(&bp);
	cf_destroy_json(doc);
}

BTEST(blueprint, parse_future_version) {
	grain_blueprint_t bp = { 0 };
	bool ok;
	CF_JDoc doc = test_parse("{ \"grain_version\": 999 }", &bp, &ok);
	BTEST_EXPECT(!ok);
	GRAIN_EXPECT_ERROR_CONTAINS("Unsupported blueprint version");

	grain_blueprint_cleanup(&bp);
	cf_destroy_json(doc);
}

BTEST(blueprint, parse_missing_pool) {
	grain_blueprint_t bp = { 0 };
	bool ok;
	CF_JDoc doc = test_parse("{ \"grain_version\": 1 }", &bp, &ok);
	BTEST_EXPECT(!ok);
	GRAIN_EXPECT_ERROR_CONTAINS("pool");

	grain_blueprint_cleanup(&bp);
	cf_destroy_json(doc);
}

BTEST(blueprint, parse_slot_referencing_missing_module) {
	grain_blueprint_t bp = { 0 };
	bool ok;
	CF_JDoc doc = test_parse(
		"{"
		"	\"grain_version\": 1,"
		"	\"pool\": { \"max_emission_rate\": 1, \"lifetime_budget\": 1 },"
		"	\"modules\": ["
		"		{ \"kind\": \"renderer\", \"name\": \"Quad\", \"source\": \"x\" }"
		"	],"
		"	\"archetype\": {"
		"		\"emitters\": [ { \"module\": \"Missing\" } ],"
		"		\"renderer\": { \"module\": \"Quad\" }"
		"	}"
		"}",
		&bp, &ok
	);
	BTEST_EXPECT(!ok);
	GRAIN_EXPECT_ERROR_CONTAINS("`Missing`");
	GRAIN_EXPECT_ERROR_CONTAINS("not embedded");

	grain_blueprint_cleanup(&bp);
	cf_destroy_json(doc);
}

BTEST(blueprint, parse_bad_param_value) {
	grain_blueprint_t bp = { 0 };
	bool ok;
	CF_JDoc doc = test_parse(
		"{"
		"	\"grain_version\": 1,"
		"	\"pool\": { \"max_emission_rate\": 1, \"lifetime_budget\": 1 },"
		"	\"modules\": ["
		"		{ \"kind\": \"renderer\", \"name\": \"Quad\", \"source\": \"x\" }"
		"	],"
		"	\"archetype\": {"
		"		\"renderer\": { \"module\": \"Quad\", \"params\": { \"tint\": \"red\" } }"
		"	}"
		"}",
		&bp, &ok
	);
	BTEST_EXPECT(!ok);
	GRAIN_EXPECT_ERROR_CONTAINS("`Quad.tint`");

	grain_blueprint_cleanup(&bp);
	cf_destroy_json(doc);
}

BTEST(blueprint, emit_parse_round_trip) {
	grain_blueprint_t bp = {
		.name = sintern("RoundTrip"),
		.emission_rate = 3.5f,
		.max_systems = 2,
		.max_emission_rate = 100.f,
		.lifetime_budget = 8.f,
		.max_burst_size = 32,
	};

	apush(bp.modules, ((grain_blueprint_module_t){
		.ref = { .kind = GRAIN_MODULE_EMITTER },
		.name = sintern("Point"),
		.source = test_strdup("Emitter(Point)\n@range(0)\nfloat spread;\n"),
		.path = test_strdup("emitters/Point.glsl"),
	}));
	apush(bp.modules, ((grain_blueprint_module_t){
		.ref = { .kind = GRAIN_MODULE_RENDERER },
		.name = sintern("Quad"),
		.source = test_strdup("Renderer(Quad)\n"),
	}));

	grain_blueprint_slot_t emitter_slot = { .module = sintern("Point") };
	apush(emitter_slot.params, ((grain_blueprint_param_t){
		.name = sintern("spread"),
		.type = CF_SHADER_INFO_TYPE_FLOAT,
		.num_components = 1,
		.components = { 0.25 },
	}));
	apush(emitter_slot.params, ((grain_blueprint_param_t){
		.name = sintern("count"),
		.type = CF_SHADER_INFO_TYPE_SINT2,
		.num_components = 2,
		.components = { -3, 7 },
	}));
	apush(emitter_slot.params, ((grain_blueprint_param_t){
		.name = sintern("color"),
		.type = CF_SHADER_INFO_TYPE_UINT,
		.num_components = 1,
		// Larger than INT32_MAX to exercise the u64 path
		.components = { 4278190080.0 },
	}));
	apush(emitter_slot.textures, ((grain_blueprint_texture_t){
		.sampler_name = sintern("noise"),
		.path = test_strdup("textures/noise.png"),
	}));
	apush(bp.emitter_slots, emitter_slot);

	bp.renderer_slot.module = sintern("Quad");
	apush(bp.renderer_slot.textures, ((grain_blueprint_texture_t){
		.sampler_name = sintern("image"),
		.path = test_strdup("sprites/fire.png"),
	}));

	// Emit, print, re-read and re-parse: the full text round trip
	CF_JDoc out_doc = cf_make_json(NULL, 0);
	CF_JVal root = grain_blueprint_emit(&bp, out_doc);
	BTEST_ASSERT(root.id != 0);
	cf_json_set_root(out_doc, root);
	char* json_text = cf_json_to_string(out_doc);

	grain_blueprint_t parsed = { 0 };
	bool ok;
	CF_JDoc in_doc = test_parse(json_text, &parsed, &ok);
	BTEST_ASSERT_EX(ok, "%s", grain_get_last_error(test_grain()));

	BTEST_EXPECT(parsed.name == bp.name);
	BTEST_EXPECT_EQUAL("%f", parsed.emission_rate, bp.emission_rate);
	BTEST_EXPECT_EQUAL("%d", parsed.max_systems, bp.max_systems);
	BTEST_EXPECT_EQUAL("%f", parsed.max_emission_rate, bp.max_emission_rate);
	BTEST_EXPECT_EQUAL("%f", parsed.lifetime_budget, bp.lifetime_budget);
	BTEST_EXPECT_EQUAL("%d", parsed.max_burst_size, bp.max_burst_size);

	BTEST_ASSERT_EQUAL("%d", asize(parsed.modules), asize(bp.modules));
	for (int i = 0; i < asize(bp.modules); ++i) {
		BTEST_EXPECT_EQUAL("%d", parsed.modules[i].ref.kind, bp.modules[i].ref.kind);
		BTEST_EXPECT(parsed.modules[i].name == bp.modules[i].name);
		BTEST_EXPECT(strcmp(parsed.modules[i].source, bp.modules[i].source) == 0);
		if (bp.modules[i].path != NULL) {
			BTEST_EXPECT(strcmp(parsed.modules[i].path, bp.modules[i].path) == 0);
		} else {
			BTEST_EXPECT(parsed.modules[i].path == NULL);
		}
	}

	BTEST_ASSERT_EQUAL("%d", asize(parsed.emitter_slots), 1);
	BTEST_EXPECT(parsed.emitter_slots[0].module == sintern("Point"));
	BTEST_ASSERT_EQUAL("%d", asize(parsed.emitter_slots[0].params), 3);
	for (int i = 0; i < 3; ++i) {
		grain_blueprint_param_t* expected = &bp.emitter_slots[0].params[i];
		grain_blueprint_param_t* actual = &parsed.emitter_slots[0].params[i];
		BTEST_EXPECT(actual->name == expected->name);
		BTEST_ASSERT_EQUAL("%d", actual->num_components, expected->num_components);
		for (int c = 0; c < expected->num_components; ++c) {
			BTEST_EXPECT_EQUAL("%f", actual->components[c], expected->components[c]);
		}
	}

	BTEST_EXPECT(parsed.renderer_slot.module == sintern("Quad"));

	BTEST_ASSERT_EQUAL("%d", asize(parsed.emitter_slots[0].textures), 1);
	BTEST_EXPECT(parsed.emitter_slots[0].textures[0].sampler_name == sintern("noise"));
	BTEST_EXPECT(
		strcmp(parsed.emitter_slots[0].textures[0].path, "textures/noise.png") == 0
	);
	BTEST_ASSERT_EQUAL("%d", asize(parsed.renderer_slot.textures), 1);
	BTEST_EXPECT(parsed.renderer_slot.textures[0].sampler_name == sintern("image"));
	BTEST_EXPECT(
		strcmp(parsed.renderer_slot.textures[0].path, "sprites/fire.png") == 0
	);
	BTEST_EXPECT_EQUAL("%d", grain_blueprint_num_textures(&parsed), 2);

	grain_blueprint_cleanup(&parsed);
	grain_blueprint_cleanup(&bp);
	sfree(json_text);
	cf_destroy_json(in_doc);
	cf_destroy_json(out_doc);
}

BTEST(blueprint, original_source_keeps_decorators) {
	const char* source =
		"Emitter(Decorated)\n"
		"Requires(\n"
		"	vec2 velocity;\n"
		")\n"
		"Params(\n"
		"	@range(0, max=100)\n"
		"	float push;\n"
		")\n"
		"void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx) {\n"
		"	particle.velocity.y -= params.push * ctx.dt;\n"
		"}";
	grain_emitter_t* emitter = grain_define_emitter(test_grain(), source);
	BTEST_ASSERT_EX(emitter != NULL, "%s", grain_get_last_error(test_grain()));

	grain_module_t* stored = (grain_module_t*)emitter;
	// The compiled copy is stripped, but what serialization embeds is verbatim
	BTEST_EXPECT(strchr(stored->source, '@') == NULL);
	BTEST_EXPECT(strcmp(stored->original_source, source) == 0);
}
