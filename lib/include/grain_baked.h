#ifndef GRAIN_BAKED_H
#define GRAIN_BAKED_H

/**
 * Runtime support for effects baked by `grainc`.
 *
 * A generated `grain_<effect>.h` is an stb-style single-header module: every
 * include sees the declarations, and exactly one translation unit defines the
 * data by defining `GRAIN_<EFFECT>_IMPLEMENTATION` (or the shared
 * `GRAIN_EFFECT_IMPLEMENTATION`) before including it. The header instantiates
 * a `grain_baked_effect_t` and `grain_<effect>_load` hands it to
 * @ref grain_load_blueprint_baked, which defines the archetype from
 * precompiled bytecode.
 * (@ref grain_blueprint_pool_opts, @ref grain_blueprint_apply, ...).
 *
 * Generated headers also expose one typed parameter handle per param and
 * sampler, used through @ref grain_set / @ref grain_get for type-checked,
 * index-free access.
 */

#include <grain.h>
#include <cute_math3d.h>
#include <string.h>

#define GRAIN_BAKED_VERSION 1

// Sized array types mirroring the GLSL param types. Scalars stay plain
// float / int32_t / uint32_t.
typedef float grain_vec2_t[2];
typedef float grain_vec3_t[3];
typedef float grain_vec4_t[4];
typedef int32_t grain_ivec2_t[2];
typedef int32_t grain_ivec3_t[3];
typedef int32_t grain_ivec4_t[4];
typedef uint32_t grain_uvec2_t[2];
typedef uint32_t grain_uvec3_t[3];
typedef uint32_t grain_uvec4_t[4];
typedef float grain_mat4_t[16];

// ---------------------------------------------------------------------------
// Baked effect descriptor: pure data, instantiated as `const` tables by
// generated headers. Indices reference the flat tables of the effect.
// ---------------------------------------------------------------------------

//! Mirrors grain_module_info_t with table indices instead of pointers
typedef struct {
	const char* name;
	int first_param;
	int num_params;
	int first_sampler;
	int num_samplers;
} grain_baked_module_t;

typedef struct {
	const char* name;
	CF_ShaderInfoDataType type;
	//! Byte offset inside its SSBO stride (update for emitter/affector params,
	//! render for renderer params)
	int offset;
	int first_decorator;
	int num_decorators;
} grain_baked_param_t;

typedef struct {
	const char* name;
	//! Owning module: the binding-migration key on live reload
	const char* module_name;
	int first_decorator;
	int num_decorators;
} grain_baked_sampler_t;

typedef struct {
	const char* name;
	int first_arg;  // into decorator_args
	int num_args;
} grain_baked_decorator_t;

//! One saved param value of the blueprint.
//! `slot` is the canonical slot index: [emitters..][affectors..][renderer].
typedef struct {
	int slot;
	const char* param;
	int num_components;
	int first_value;  // into values
} grain_baked_param_value_t;

//! A saved texture path record; paths are metadata the caller resolves,
//! exactly like grain_blueprint_get_texture
typedef struct {
	int slot;
	const char* sampler;
	const char* path;
} grain_baked_texture_t;

typedef struct {
	uint32_t baked_version;  // GRAIN_BAKED_VERSION

	// Blueprint record
	const char* name;
	float emission_rate;
	int max_systems;
	float max_emission_rate;
	float lifetime_budget;
	int max_burst_size;
	// Measured bounds, system-local; see grain_blueprint_bounds
	bool has_bounds;
	grain_bounds_t bounds;

	// Archetype reflection
	const grain_baked_module_t* emitters;
	int num_emitters;
	const grain_baked_module_t* affectors;
	int num_affectors;
	grain_baked_module_t renderer;
	const grain_baked_param_t* params;
	int num_params;
	const grain_baked_sampler_t* samplers;
	int num_samplers;
	const grain_baked_decorator_t* decorators;
	int num_decorators;
	const grain_decorator_arg_t* decorator_args;
	int num_decorator_args;

	// Archetype layout
	int num_textures;
	int update_size;
	int render_size;
	int birth_texture;
	int birth_channel;
	uint64_t attr_layout_hash;

	// Whole-archetype shaders; the update vertex stage is a builtin
	CF_ShaderBytecode update_frag_bytecode;
	CF_ShaderBytecode render_vert_bytecode;
	CF_ShaderBytecode render_frag_bytecode;

	// Blueprint values
	const grain_baked_param_value_t* param_values;
	int num_param_values;
	const double* values;
	int num_values;
	const grain_baked_texture_t* textures;
	int num_texture_paths;
} grain_baked_effect_t;

/**
 * Define the archetype (under `baked->name`) and return a blueprint, exactly
 * like grain_load_blueprint but from baked data: no shader compilation, the
 * bytecode is referenced, never owned or freed.
 *
 * Redefinition under a live name follows the usual reload semantics (cleanup +
 * revision bump). Baked effects register no modules, so they are invisible to
 * grain_snapshot_system / grain_save_blueprint.
 *
 * Destroy the result with grain_destroy_blueprint. NULL on error (see
 * grain_get_last_error).
 */
grain_blueprint_t*
grain_load_blueprint_baked(grain_t* grain, const grain_baked_effect_t* baked);

// ---------------------------------------------------------------------------
// Typed parameter handles
//
// Generated headers expose one handle per param/sampler, carrying the flat
// index that grain_get_parameter / grain_set_texture expect. Distinct handle
// types make grain_set / grain_get dispatch to a matching accessor, so a
// wrong value type is a compile error.
// ---------------------------------------------------------------------------

typedef struct { int index; const grain_baked_effect_t* effect; } grain_param_float_t;
typedef struct { int index; const grain_baked_effect_t* effect; } grain_param_vec2_t;
typedef struct { int index; const grain_baked_effect_t* effect; } grain_param_vec3_t;
typedef struct { int index; const grain_baked_effect_t* effect; } grain_param_vec4_t;
typedef struct { int index; const grain_baked_effect_t* effect; } grain_param_int_t;
typedef struct { int index; const grain_baked_effect_t* effect; } grain_param_ivec2_t;
typedef struct { int index; const grain_baked_effect_t* effect; } grain_param_ivec3_t;
typedef struct { int index; const grain_baked_effect_t* effect; } grain_param_ivec4_t;
typedef struct { int index; const grain_baked_effect_t* effect; } grain_param_uint_t;
typedef struct { int index; const grain_baked_effect_t* effect; } grain_param_uvec2_t;
typedef struct { int index; const grain_baked_effect_t* effect; } grain_param_uvec3_t;
typedef struct { int index; const grain_baked_effect_t* effect; } grain_param_uvec4_t;
typedef struct { int index; const grain_baked_effect_t* effect; } grain_param_mat4_t;
typedef struct { int index; const grain_baked_effect_t* effect; } grain_sampler_h_t;

//! Debug aid behind the CF_ASSERTs in the typed accessors: whether the
//! system's archetype plausibly is the one this baked effect defines
bool
grain_baked_system_matches(grain_system_t* system, const grain_baked_effect_t* baked);

//! Pool flavor of grain_baked_system_matches, for sampler handles
bool
grain_baked_pool_matches(grain_pool_t* pool, const grain_baked_effect_t* baked);

//! Scalar params pass by value; grain_get writes through the out pointer
#define GRAIN_BAKED_DEFINE_SCALAR_ACCESSORS(SUFFIX, HANDLE_T, VALUE_T) \
	static inline void \
	grain_set_param_##SUFFIX(grain_system_t* system, HANDLE_T param, VALUE_T value) { \
		CF_ASSERT(grain_baked_system_matches(system, param.effect)); \
		void* target = grain_get_parameter(system, param.index); \
		if (target == NULL) { return; } \
		memcpy(target, &value, sizeof(value)); \
		grain_parameter_modified(system, param.index); \
	} \
	static inline void \
	grain_get_param_##SUFFIX(grain_system_t* system, HANDLE_T param, VALUE_T* out) { \
		CF_ASSERT(grain_baked_system_matches(system, param.effect)); \
		memset(out, 0, sizeof(VALUE_T)); \
		const void* source = grain_get_parameter(system, param.index); \
		if (source != NULL) { memcpy(out, source, sizeof(VALUE_T)); } \
	}

//! Vector/matrix params are sized arrays; they pass and return by decay,
//! so grain_get takes the destination array itself, not its address
#define GRAIN_BAKED_DEFINE_ARRAY_ACCESSORS(SUFFIX, HANDLE_T, VALUE_T) \
	static inline void \
	grain_set_param_##SUFFIX(grain_system_t* system, HANDLE_T param, const VALUE_T value) { \
		CF_ASSERT(grain_baked_system_matches(system, param.effect)); \
		void* target = grain_get_parameter(system, param.index); \
		if (target == NULL) { return; } \
		memcpy(target, value, sizeof(VALUE_T)); \
		grain_parameter_modified(system, param.index); \
	} \
	static inline void \
	grain_get_param_##SUFFIX(grain_system_t* system, HANDLE_T param, VALUE_T out) { \
		CF_ASSERT(grain_baked_system_matches(system, param.effect)); \
		memset(out, 0, sizeof(VALUE_T)); \
		const void* source = grain_get_parameter(system, param.index); \
		if (source != NULL) { memcpy(out, source, sizeof(VALUE_T)); } \
	}

GRAIN_BAKED_DEFINE_SCALAR_ACCESSORS(float, grain_param_float_t, float)
GRAIN_BAKED_DEFINE_SCALAR_ACCESSORS(int, grain_param_int_t, int32_t)
GRAIN_BAKED_DEFINE_SCALAR_ACCESSORS(uint, grain_param_uint_t, uint32_t)
GRAIN_BAKED_DEFINE_ARRAY_ACCESSORS(vec2, grain_param_vec2_t, grain_vec2_t)
GRAIN_BAKED_DEFINE_ARRAY_ACCESSORS(vec3, grain_param_vec3_t, grain_vec3_t)
GRAIN_BAKED_DEFINE_ARRAY_ACCESSORS(vec4, grain_param_vec4_t, grain_vec4_t)
GRAIN_BAKED_DEFINE_ARRAY_ACCESSORS(ivec2, grain_param_ivec2_t, grain_ivec2_t)
GRAIN_BAKED_DEFINE_ARRAY_ACCESSORS(ivec3, grain_param_ivec3_t, grain_ivec3_t)
GRAIN_BAKED_DEFINE_ARRAY_ACCESSORS(ivec4, grain_param_ivec4_t, grain_ivec4_t)
GRAIN_BAKED_DEFINE_ARRAY_ACCESSORS(uvec2, grain_param_uvec2_t, grain_uvec2_t)
GRAIN_BAKED_DEFINE_ARRAY_ACCESSORS(uvec3, grain_param_uvec3_t, grain_uvec3_t)
GRAIN_BAKED_DEFINE_ARRAY_ACCESSORS(uvec4, grain_param_uvec4_t, grain_uvec4_t)
GRAIN_BAKED_DEFINE_ARRAY_ACCESSORS(mat4, grain_param_mat4_t, grain_mat4_t)

// ---------------------------------------------------------------------------
// CF-type conveniences, selected by grain_set / grain_get when the value is a
// CF type. The grain array types above stay the canonical representation.
// ---------------------------------------------------------------------------

static inline void
grain_set_param_vec2_cf(grain_system_t* system, grain_param_vec2_t param, CF_V2 value) {
	grain_set_param_vec2(system, param, (grain_vec2_t){ value.x, value.y });
}

static inline void
grain_get_param_vec2_cf(grain_system_t* system, grain_param_vec2_t param, CF_V2* out) {
	grain_vec2_t value;
	grain_get_param_vec2(system, param, value);
	out->x = value[0];
	out->y = value[1];
}

static inline void
grain_set_param_vec3_cf(grain_system_t* system, grain_param_vec3_t param, CF_V3 value) {
	grain_set_param_vec3(system, param, (grain_vec3_t){ value.x, value.y, value.z });
}

static inline void
grain_get_param_vec3_cf(grain_system_t* system, grain_param_vec3_t param, CF_V3* out) {
	grain_vec3_t value;
	grain_get_param_vec3(system, param, value);
	out->x = value[0];
	out->y = value[1];
	out->z = value[2];
}

// A color can land on either a packed-RGBA8 uint param or a vec4 param; the
// handle's effect carries the shader type, so these check it and convert
// through CF's own cf_color_to_pixel / cf_pixel_to_color conventions.

static inline void
grain_set_param_color_checked(
	grain_system_t* system,
	int index,
	const grain_baked_effect_t* effect,
	CF_Color color
) {
	CF_ASSERT(grain_baked_system_matches(system, effect));
	void* target = grain_get_parameter(system, index);
	if (target == NULL) { return; }
	if (effect->params[index].type == CF_SHADER_INFO_TYPE_UINT) {
		uint32_t packed = cf_color_to_pixel(color).val;
		memcpy(target, &packed, sizeof(packed));
	} else {
		grain_vec4_t value = { color.r, color.g, color.b, color.a };
		memcpy(target, value, sizeof(value));
	}
	grain_parameter_modified(system, index);
}

static inline void
grain_set_param_pixel_checked(
	grain_system_t* system,
	int index,
	const grain_baked_effect_t* effect,
	CF_Pixel pixel
) {
	CF_ASSERT(grain_baked_system_matches(system, effect));
	void* target = grain_get_parameter(system, index);
	if (target == NULL) { return; }
	if (effect->params[index].type == CF_SHADER_INFO_TYPE_UINT) {
		memcpy(target, &pixel.val, sizeof(pixel.val));
	} else {
		CF_Color color = cf_pixel_to_color(pixel);
		grain_vec4_t value = { color.r, color.g, color.b, color.a };
		memcpy(target, value, sizeof(value));
	}
	grain_parameter_modified(system, index);
}

static inline CF_Color
grain_get_param_color_checked(
	grain_system_t* system,
	int index,
	const grain_baked_effect_t* effect
) {
	CF_ASSERT(grain_baked_system_matches(system, effect));
	const void* source = grain_get_parameter(system, index);
	if (source == NULL) { return (CF_Color){ 0 }; }
	if (effect->params[index].type == CF_SHADER_INFO_TYPE_UINT) {
		CF_Pixel pixel;
		memcpy(&pixel.val, source, sizeof(pixel.val));
		return cf_pixel_to_color(pixel);
	} else {
		grain_vec4_t value;
		memcpy(value, source, sizeof(value));
		return cf_make_color_rgba_f(value[0], value[1], value[2], value[3]);
	}
}

//! Not through grain_get_param_color_checked: a packed uint must read back
//! bit-exactly, without a float round trip
static inline CF_Pixel
grain_get_param_pixel_checked(
	grain_system_t* system,
	int index,
	const grain_baked_effect_t* effect
) {
	CF_ASSERT(grain_baked_system_matches(system, effect));
	CF_Pixel pixel = { 0 };
	const void* source = grain_get_parameter(system, index);
	if (source == NULL) { return pixel; }
	if (effect->params[index].type == CF_SHADER_INFO_TYPE_UINT) {
		memcpy(&pixel.val, source, sizeof(pixel.val));
		return pixel;
	} else {
		grain_vec4_t value;
		memcpy(value, source, sizeof(value));
		return cf_color_to_pixel(cf_make_color_rgba_f(value[0], value[1], value[2], value[3]));
	}
}

#define GRAIN_BAKED_DEFINE_COLOR_ACCESSORS(SUFFIX, HANDLE_T) \
	static inline void \
	grain_set_param_##SUFFIX##_color(grain_system_t* system, HANDLE_T param, CF_Color color) { \
		grain_set_param_color_checked(system, param.index, param.effect, color); \
	} \
	static inline void \
	grain_set_param_##SUFFIX##_pixel(grain_system_t* system, HANDLE_T param, CF_Pixel pixel) { \
		grain_set_param_pixel_checked(system, param.index, param.effect, pixel); \
	} \
	static inline void \
	grain_get_param_##SUFFIX##_color(grain_system_t* system, HANDLE_T param, CF_Color* out) { \
		*out = grain_get_param_color_checked(system, param.index, param.effect); \
	} \
	static inline void \
	grain_get_param_##SUFFIX##_pixel(grain_system_t* system, HANDLE_T param, CF_Pixel* out) { \
		*out = grain_get_param_pixel_checked(system, param.index, param.effect); \
	}

GRAIN_BAKED_DEFINE_COLOR_ACCESSORS(uint, grain_param_uint_t)
GRAIN_BAKED_DEFINE_COLOR_ACCESSORS(vec4, grain_param_vec4_t)

static inline void
grain_bind_sampler(grain_pool_t* pool, grain_sampler_h_t sampler, grain_texture_binding_t binding) {
	CF_ASSERT(grain_baked_pool_matches(pool, sampler.effect));
	grain_set_texture(pool, sampler.index, binding);
}

//! Sprite flavor of the sampler handle binding; see grain_set_sprite for the
//! per-frame call requirement
static inline void
grain_bind_sprite(grain_pool_t* pool, grain_sampler_h_t sampler, const CF_Sprite* sprite) {
	CF_ASSERT(grain_baked_pool_matches(pool, sampler.effect));
	grain_set_sprite(pool, sampler.index, sprite);
}

/**
 * Type-checked parameter/texture write through a generated handle:
 *
 *     grain_set(system, grain_fire.Fan[1].strength, 2.0f);
 *     grain_set(system, grain_fire.Point.position, (grain_vec2_t){ 0.f, 60.f });
 *     grain_set(system, grain_fire.Point.position, cf_v2(0.f, 60.f));
 *     grain_set(system, grain_fire.Circle.start_color, cf_make_pixel_rgb(255, 80, 0));
 *     grain_set(pool, grain_fire.Flame.image, binding);  // samplers bind pools
 *
 * A value of the wrong type is a compile error. The value is variadic so
 * compound literals with commas need no extra parentheses.
 *
 * CF types are accepted alongside the grain array types: CF_V2 / CF_V3 for
 * vec2 / vec3 params, and CF_Color / CF_Pixel for color params of either
 * representation -- the wrapper checks whether the param is a packed uint or
 * a vec4 and converts accordingly.
 */
#define grain_set(target, param, ...) _Generic((param), \
	grain_param_float_t: grain_set_param_float, \
	grain_param_vec2_t: _Generic((__VA_ARGS__), \
		CF_V2: grain_set_param_vec2_cf, \
		default: grain_set_param_vec2), \
	grain_param_vec3_t: _Generic((__VA_ARGS__), \
		CF_V3: grain_set_param_vec3_cf, \
		default: grain_set_param_vec3), \
	grain_param_vec4_t: _Generic((__VA_ARGS__), \
		CF_Color: grain_set_param_vec4_color, \
		CF_Pixel: grain_set_param_vec4_pixel, \
		default: grain_set_param_vec4), \
	grain_param_int_t: grain_set_param_int, \
	grain_param_ivec2_t: grain_set_param_ivec2, \
	grain_param_ivec3_t: grain_set_param_ivec3, \
	grain_param_ivec4_t: grain_set_param_ivec4, \
	grain_param_uint_t: _Generic((__VA_ARGS__), \
		CF_Color: grain_set_param_uint_color, \
		CF_Pixel: grain_set_param_uint_pixel, \
		default: grain_set_param_uint), \
	grain_param_uvec2_t: grain_set_param_uvec2, \
	grain_param_uvec3_t: grain_set_param_uvec3, \
	grain_param_uvec4_t: grain_set_param_uvec4, \
	grain_param_mat4_t: grain_set_param_mat4, \
	grain_sampler_h_t: grain_bind_sampler \
)((target), (param), __VA_ARGS__)

/**
 * Typed parameter read through a generated handle, written to `out`:
 * the destination array for vector/matrix params, a pointer for scalars
 * and CF types.
 *
 *     grain_vec2_t position;
 *     grain_get(system, grain_fire.Point.position, position);
 *     CF_V2 p;
 *     grain_get(system, grain_fire.Point.position, &p);
 *     float drag;
 *     grain_get(system, grain_fire.Wind.drag, &drag);
 */
#define grain_get(system, param, out) _Generic((param), \
	grain_param_float_t: grain_get_param_float, \
	grain_param_vec2_t: _Generic((out), \
		CF_V2*: grain_get_param_vec2_cf, \
		default: grain_get_param_vec2), \
	grain_param_vec3_t: _Generic((out), \
		CF_V3*: grain_get_param_vec3_cf, \
		default: grain_get_param_vec3), \
	grain_param_vec4_t: _Generic((out), \
		CF_Color*: grain_get_param_vec4_color, \
		CF_Pixel*: grain_get_param_vec4_pixel, \
		default: grain_get_param_vec4), \
	grain_param_int_t: grain_get_param_int, \
	grain_param_ivec2_t: grain_get_param_ivec2, \
	grain_param_ivec3_t: grain_get_param_ivec3, \
	grain_param_ivec4_t: grain_get_param_ivec4, \
	grain_param_uint_t: _Generic((out), \
		CF_Color*: grain_get_param_uint_color, \
		CF_Pixel*: grain_get_param_uint_pixel, \
		default: grain_get_param_uint), \
	grain_param_uvec2_t: grain_get_param_uvec2, \
	grain_param_uvec3_t: grain_get_param_uvec3, \
	grain_param_uvec4_t: grain_get_param_uvec4, \
	grain_param_mat4_t: grain_get_param_mat4 \
)((system), (param), (out))

#endif
