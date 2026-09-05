#include "shared.h"
#include <grain_fire.h>
#include <grain_snow.h>

static btest_suite_t baked_codegen = {
	.name = "baked_codegen",
	.init_per_test = test_grain_init,
	.cleanup_per_test = test_grain_cleanup,
};

// Consumes the headers grainc bakes from samples/ at build time. This TU has
// no implementation macro: linking proves the single-header contract
// (declarations everywhere, data in the one TU that defines
// GRAIN_EFFECT_IMPLEMENTATION -- see baked_codegen_impl.c).

BTEST(baked_codegen, fire_loads) {
	grain_blueprint_t* blueprint = grain_fire_load(test_grain());
	BTEST_ASSERT_EX(blueprint != NULL, "%s", grain_get_last_error(test_grain()));

	BTEST_EXPECT(grain_blueprint_name(blueprint) == sintern("Effect"));
	BTEST_EXPECT_EQUAL("%f", grain_blueprint_emission_rate(blueprint), 188.f);
	grain_pool_opts_t pool_opts = grain_blueprint_pool_opts(blueprint);
	BTEST_EXPECT_EQUAL("%d", pool_opts.max_systems, 1);
	BTEST_EXPECT_EQUAL("%f", pool_opts.max_emission_rate, 512.f);
	BTEST_EXPECT_EQUAL("%f", pool_opts.lifetime_budget, 16.f);
	BTEST_EXPECT_EQUAL("%d", pool_opts.max_burst_size, 256);

	grain_archetype_t* archetype = grain_blueprint_archetype(blueprint);
	BTEST_ASSERT(archetype != NULL);
	grain_archetype_info_t info = grain_inspect_archetype(archetype);
	BTEST_ASSERT_EQUAL("%d", info.num_emitters, 2);
	BTEST_EXPECT(info.emitters[0].name == sintern("Point"));
	BTEST_EXPECT(info.emitters[1].name == sintern("Lifetime"));

	// Typed handles carry live flat-table indices; field names keep the
	// modules' own casing
	BTEST_EXPECT(grain_fire.Point.position.effect == &grain_fire_effect);
	const grain_param_info_t* position = &info.params[grain_fire.Point.position.index];
	BTEST_EXPECT(position->name == sintern("position"));
	BTEST_EXPECT_EQUAL("%d", position->type, CF_SHADER_INFO_TYPE_FLOAT2);
	// @color is not special: a uint param is a plain uint handle
	const grain_param_info_t* start_color = &info.params[grain_fire.Circle.start_color.index];
	BTEST_EXPECT(start_color->name == sintern("start_color"));
	BTEST_EXPECT_EQUAL("%d", start_color->type, CF_SHADER_INFO_TYPE_UINT);

	// Compile-only: grain_set / grain_get dispatch and type-check through
	// _Generic; running them needs a pool + system (GPU)
	if (0) {
		grain_set(NULL, grain_fire.Point.position, (grain_vec2_t){ 1.f, 2.f });
		grain_set(NULL, grain_fire.Wind.drag, 0.5f);
		grain_set(NULL, grain_fire.Circle.start_color, 4278255607u);
		grain_vec2_t position_value;
		grain_get(NULL, grain_fire.Point.position, position_value);
		float drag_value;
		grain_get(NULL, grain_fire.Wind.drag, &drag_value);

		// CF-type overloads select CF-aware accessors on the same handles
		grain_set(NULL, grain_fire.Point.position, cf_v2(1.f, 2.f));
		grain_set(NULL, grain_fire.Circle.start_color, cf_make_pixel_rgb(255, 80, 0));
		grain_set(NULL, grain_fire.Circle.start_color, cf_make_color_rgb_f(1.f, 0.3f, 0.f));
		CF_V2 cf_position;
		grain_get(NULL, grain_fire.Point.position, &cf_position);
		CF_Color cf_color;
		grain_get(NULL, grain_fire.Circle.start_color, &cf_color);
		CF_Pixel cf_pixel;
		grain_get(NULL, grain_fire.Circle.start_color, &cf_pixel);
	}

	grain_destroy_blueprint(blueprint);
}

BTEST(baked_codegen, snow_reloads_over_fire) {
	grain_blueprint_t* fire = grain_fire_load(test_grain());
	BTEST_ASSERT_EX(fire != NULL, "%s", grain_get_last_error(test_grain()));
	grain_archetype_t* fire_archetype = grain_blueprint_archetype(fire);
	grain_destroy_blueprint(fire);

	// Both samples are named "Effect", so loading snow redefines the live
	// archetype -- the normal reload path, now from static bytecode
	grain_blueprint_t* snow = grain_snow_load(test_grain());
	BTEST_ASSERT_EX(snow != NULL, "%s", grain_get_last_error(test_grain()));
	grain_archetype_t* snow_archetype = grain_blueprint_archetype(snow);
	BTEST_EXPECT(snow_archetype == fire_archetype);
	BTEST_EXPECT_EQUAL("%u", snow_archetype->revision, 2u);

	// Sampler handles: snow's Sprite renderer declares `image`
	grain_archetype_info_t info = grain_inspect_archetype(snow_archetype);
	BTEST_ASSERT_EQUAL("%d", info.renderer.num_samplers, 1);
	BTEST_EXPECT_EQUAL("%d", grain_snow.Sprite.image.index, info.renderer.first_sampler);
	BTEST_EXPECT(info.samplers[grain_snow.Sprite.image.index].name == sintern("image"));

	// The samples save no texture paths; the slot binds at runtime instead
	// (texture-path records are covered by the baked round-trip test)
	BTEST_EXPECT_EQUAL("%d", grain_blueprint_num_textures(snow), 0);

	if (0) {
		grain_texture_binding_t binding = { 0 };
		grain_set(NULL, grain_snow.Sprite.image, binding);
		grain_set(NULL, grain_snow.Sprite.size, (grain_vec2_t){ 8.f, 8.f });
	}

	grain_destroy_blueprint(snow);
}
