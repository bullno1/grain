#ifndef GRAIN_H
#define GRAIN_H

#include <stdint.h>
#include <stdbool.h>
#include <cute_graphics.h>
#include <cute_draw.h>
#include <cute_json.h>

typedef struct grain_s grain_t;
typedef struct grain_emitter_s grain_emitter_t;
typedef struct grain_affector_s grain_affector_t;
typedef struct grain_renderer_s grain_renderer_t;
typedef struct grain_archetype_s grain_archetype_t;
typedef struct grain_pool_s grain_pool_t;
typedef struct grain_system_s grain_system_t;
typedef struct grain_blueprint_s grain_blueprint_t;

typedef enum {
	GRAIN_MODULE_INVALID = 0,
	GRAIN_MODULE_EMITTER,
	GRAIN_MODULE_AFFECTOR,
	GRAIN_MODULE_RENDERER,
} grain_module_kind_t;

/**
 * A module along with its kind, as declared in its source
 *
 * Only the union member matching `kind` is valid.
 * `kind` is GRAIN_MODULE_INVALID when definition failed.
 */
typedef struct {
	grain_module_kind_t kind;
	void* module;
} grain_module_ref_t;

typedef struct {
	grain_emitter_t** emitters;
	int num_emitters;

	grain_affector_t** affectors;
	int num_affectors;

	grain_renderer_t* renderer;
} grain_archetype_spec_t;

typedef struct {
	grain_archetype_t* archetype;

	int max_systems;

	float max_emission_rate;  // upper bound for particles/second
	float lifetime_budget;    // max lifetime in seconds
	int max_burst_size;       // upper bound for one burst's particle count; 0 disables bursts
} grain_pool_opts_t;

typedef enum {
	GRAIN_DECORATOR_ARG_NUMBER,
	GRAIN_DECORATOR_ARG_STRING,
	GRAIN_DECORATOR_ARG_IDENT,
} grain_decorator_arg_type_t;

typedef struct {
	int index;          // ordinal among positional arguments; -1 for named arguments
	const char* name;   // NULL for positional arguments
	grain_decorator_arg_type_t type;
	union {
		float number;
		const char* string;
	} value;
} grain_decorator_arg_t;

typedef struct {
	const char* name;
	const grain_decorator_arg_t* args;
	int num_args;
} grain_param_decorator_t;

typedef struct {
	const char* name;
	CF_ShaderInfoDataType type;
	const grain_param_decorator_t* decorators;
	int num_decorators;
} grain_param_info_t;

//! A texture slot declared in a module's `Samplers` block
typedef struct {
	const char* name;
	const grain_param_decorator_t* decorators;
	int num_decorators;
} grain_sampler_info_t;

typedef struct {
	const char* name;
	int first_param;
	int num_params;
	int first_sampler;
	int num_samplers;
} grain_module_info_t;

typedef struct {
	const grain_module_info_t* emitters;
	int num_emitters;

	const grain_module_info_t* affectors;
	int num_affectors;

	const grain_module_info_t  renderer;
	const grain_param_info_t* params;
	const grain_sampler_info_t* samplers;
} grain_archetype_info_t;

grain_t*
grain_create(void);

void
grain_destroy(grain_t* grain);

const char*
grain_get_last_error(grain_t* grain);

/**
 * Define a module of whatever kind its source declares
 *
 * The declared kind is returned so the caller can dispatch on it.
 * Use the typed variants below instead when a specific kind is expected: they
 * reject a module of any other kind.
 */
grain_module_ref_t
grain_define_module(grain_t* grain, const char* source);

grain_emitter_t*
grain_define_emitter(grain_t* grain, const char* source);

grain_affector_t*
grain_define_affector(grain_t* grain, const char* source);

grain_renderer_t*
grain_define_renderer(grain_t* grain, const char* source);

grain_archetype_t*
grain_define_archetype(grain_t* grain, const char* name, grain_archetype_spec_t spec);

grain_archetype_info_t
grain_inspect_archetype(grain_archetype_t* archetype);

const char*
grain_get_emitter_name(grain_emitter_t* emitter);

const char*
grain_get_affector_name(grain_affector_t* affector);

const char*
grain_get_renderer_name(grain_renderer_t* renderer);

//! Linear search of a parameter's decorators by name; NULL if absent.
const grain_param_decorator_t*
grain_find_decorator(const grain_param_info_t* param, const char* name);

//! Linear search of a sampler's decorators by name; NULL if absent.
const grain_param_decorator_t*
grain_find_sampler_decorator(const grain_sampler_info_t* sampler, const char* name);

/**
 * Find a decorator argument
 *
 * Python-style resolution: matches the positional argument with this index or
 * the named argument with this name. Returns false if neither exists so the
 * caller can fall back to a default
 */
bool
grain_find_decorator_arg(
	const grain_param_decorator_t* decorator,
	int index,
	const char* name,
	grain_decorator_arg_t* out
);

grain_pool_t*
grain_create_pool(grain_t* grain, grain_pool_opts_t opts);

typedef struct {
	//! .id == 0 resets the slot to grain's built-in fallback (1x1 opaque white)
	CF_Texture texture;
	//! UV rect inside `texture`; leave all four zero for the full texture
	float uv_min[2];
	float uv_max[2];
	//! Optional standalone sampler; .id == 0 keeps the sampler baked into
	//! `texture`. How a caller honors @filter/@wrap decorator hints at bind time.
	CF_Sampler sampler;
} grain_texture_binding_t;

/**
 * Bind a texture to one of this pool's sampler slots.
 *
 * Bindings are per-pool: every system in the pool samples the same texture.
 * Module code reads the slot's `<name>_uvrect` as (uv_min, uv_max)
 * and can remap unit UVs with `atlas_uv`.
 *
 * The binding survives live reload as long as the module keeps a sampler of
 * the same name; a slot whose sampler disappears is unbound.
 *
 * @param sampler_index Index into grain_archetype_info_t::samplers
 *        (grain_module_info_t::first_sampler + i).
 */
void
grain_set_texture(grain_pool_t* pool, int sampler_index, grain_texture_binding_t binding);

/**
 * Bind an atlased sprite's current image to one of this pool's sampler slots.
 *
 * Include cute_draw.h before grain.h to enable this helper; the library
 * itself only depends on cute_graphics.h.
 *
 * Call it every frame while the binding is live: the sprite can animate and
 * CF's dynamic atlas can reshuffle, so the CF_TemporaryImage this reads is
 * only valid until the next cf_render_to / cf_app_draw_onto_screen.
 */
static inline void
grain_set_sprite(grain_pool_t* pool, int sampler_index, const CF_Sprite* sprite) {
	CF_TemporaryImage image = cf_fetch_image(sprite);
	grain_set_texture(pool, sampler_index, (grain_texture_binding_t){
		.texture = image.tex,
		.uv_min = { image.u.x, image.u.y },
		.uv_max = { image.v.x, image.v.y },
	});
}

grain_pool_t*
grain_get_pool(grain_system_t* system);

//! The options this pool was created with
grain_pool_opts_t
grain_get_pool_opts(grain_pool_t* pool);

/**
 * Grain's default render state for pools.
 *
 * * Premultiplied-alpha "over" blending, matching both CF's own draw pipeline
 *   and the premultiplied pixels of its sprite atlas
 * * Depth test is LESS_EQUAL
 * * Depth write is off
 * * No culling
 *
 * When the target has a depth buffer, opaque geometry occludes particles
 * while particles never occlude anything.
 *
 * Under the premultiplied convention additive blending is a shader decision,
 * not a state change: a renderer module that pushes alpha toward zero while
 * keeping color emits additively, and can vary this per particle.
 */
CF_RenderState
grain_render_state_defaults(void);

/**
 * Override the render state of a pool.
 *
 * Start from @ref grain_render_state_defaults and tweak.
 * primitive_type is owned by grain and is overwritten.
 */
void
grain_set_render_state(grain_pool_t* pool, CF_RenderState render_state);

//! The state last set through @ref grain_set_render_state, or the defaults
CF_RenderState
grain_get_render_state(grain_pool_t* pool);

void
grain_destroy_pool(grain_pool_t* pool);

grain_system_t*
grain_create_system(grain_pool_t* pool);

void
grain_destroy_system(grain_system_t* system);

grain_archetype_t*
grain_get_archetype(grain_system_t* system);

void
grain_begin_update(grain_t* grain);

void
grain_tick(grain_system_t* system, float dt_s);

void
grain_set_emission_rate(grain_system_t* system, float particles_per_second);

/**
 * Queue `count` particles to be emitted in one instant at the next update pass,
 * alongside steady emission.
 *
 * Calls within a frame accumulate; the accumulated total is clamped to the
 * pool's max_burst_size. Burst particles draw from the same slot ring as steady
 * emission: keep the total burst count within any lifetime_budget window under
 * max_burst_size, or the oldest particles may be recycled early.
 */
void
grain_burst(grain_system_t* system, int count);

void
grain_set_emitter_parameter(
	grain_system_t* system,
	int emitter_index,
	const char* name,
	const void* value
);

void
grain_set_affector_parameter(
	grain_system_t* system,
	int affector_index,
	const char* name,
	const void* value
);

void
grain_set_renderer_parameter(
	grain_system_t* system,
	const char* name,
	const void* value
);

/**
 * Get a raw pointer to a parameter inside the CPU-side buffer of this system.
 *
 * The pointer is transient: valid only until the next call into the library
 * that touches this system's pool.
 *
 * Writes through the pointer must be followed by @ref grain_parameter_modified
 * or they may never reach the GPU.
 *
 * @param system The particle system.
 * @param param_index Index into grain_archetype_info_t::params (grain_module_info_t::first_param + i).
 * @return Pointer to the parameter, or NULL if param_index is out of range.
 */
void*
grain_get_parameter(grain_system_t* system, int param_index);

/**
 * Flag the buffer that owns this parameter for re-upload.
 *
 * @param system The particle system.
 * @param param_index Index into grain_archetype_info_t::params.
 */
void
grain_parameter_modified(grain_system_t* system, int param_index);

void
grain_end_update(grain_t* grain);

void
grain_begin_render(grain_t* grain);

void
grain_render(grain_system_t* system);

void
grain_end_render(grain_t* grain);

typedef struct {
	//! Saved as the archetype name when the blueprint is loaded; defaults to "Effect"
	const char* name;
	float emission_rate;

	/**
	 * Optional source path lookup, for reopening in an editor.
	 *
	 * Called once per distinct module; return NULL to omit the path.
	 * The library stores the path verbatim and never resolves it.
	 */
	const char* (*module_path)(void* userdata, grain_module_kind_t kind, const char* module_name);

	/**
	 * Optional texture path lookup, one call per sampler slot; return NULL to
	 * omit the binding.
	 *
	 * The library stores the path verbatim and never resolves it: on load the
	 * caller reads the records back (@ref grain_blueprint_get_texture) and
	 * binds with @ref grain_set_texture. `module_index` disambiguates the same
	 * module occupying several slots; the renderer always passes 0.
	 */
	const char* (*texture_path)(
		void* userdata,
		grain_module_kind_t kind,
		int module_index,
		const char* module_name,
		const char* sampler_name
	);

	void* userdata;
} grain_save_opts_t;

//! A module embedded in a blueprint
typedef struct {
	//! Only valid after grain_load_blueprint; the module is defined in its grain_t
	grain_module_ref_t ref;
	const char* name;
	//! Embedded snapshot of the source, decorators intact
	const char* source;
	//! NULL if none was saved
	const char* path;
} grain_blueprint_module_t;

/**
 * Snapshot a particle system into a blueprint: module sources, archetype
 * composition, pool config and current param values.
 *
 * The inverse of @ref grain_blueprint_apply. The blueprint owns copies of
 * everything it holds; destroy it with @ref grain_destroy_blueprint.
 *
 * @return NULL on failure (see @ref grain_get_last_error).
 */
grain_blueprint_t*
grain_snapshot_system(grain_t* grain, grain_system_t* system, grain_save_opts_t opts);

/**
 * Serialize a blueprint into a JSON value inside the caller's document.
 *
 * The inverse of @ref grain_load_blueprint. The value is not attached to the
 * document; use cf_json_set_root for a standalone file or nest it inside a
 * bigger object.
 *
 * The document borrows the blueprint's strings without copying.
 * Keep the blueprint alive until the document has been serialized or destroyed.
 */
CF_JVal
grain_save_blueprint(grain_blueprint_t* blueprint, CF_JDoc doc);

/**
 * Load a blueprint from a JSON value.
 *
 * All embedded modules are defined (redefinition follows the usual live-reload
 * rules) along with the archetype, under its saved name.
 * Everything the blueprint keeps is copied: the document can be destroyed as
 * soon as this returns.
 *
 * @return NULL on failure (see @ref grain_get_last_error).
 */
grain_blueprint_t*
grain_load_blueprint(grain_t* grain, CF_JVal val);

//! Modules and the archetype it defined outlive the blueprint
void
grain_destroy_blueprint(grain_blueprint_t* blueprint);

const char*
grain_blueprint_name(grain_blueprint_t* blueprint);

float
grain_blueprint_emission_rate(grain_blueprint_t* blueprint);

grain_archetype_t*
grain_blueprint_archetype(grain_blueprint_t* blueprint);

//! Saved pool config with `archetype` filled in; tweak max_systems before grain_create_pool
grain_pool_opts_t
grain_blueprint_pool_opts(grain_blueprint_t* blueprint);

/**
 * Write the saved param values and emission rate into a system.
 *
 * The system does not have to use the blueprint's archetype: modules are
 * matched by name and params by name and component count, with values
 * converted to the parameter's current type.
 * Unmatched params keep whatever value the system already has.
 */
void
grain_blueprint_apply(grain_blueprint_t* blueprint, grain_system_t* system);

int
grain_blueprint_num_modules(grain_blueprint_t* blueprint);

grain_blueprint_module_t
grain_blueprint_get_module(grain_blueprint_t* blueprint, int index);

//! A texture binding saved in a blueprint, as path metadata only
typedef struct {
	grain_module_kind_t kind;
	//! Position within its kind's slot list; 0 for the renderer
	int module_index;
	const char* module_name;
	const char* sampler_name;
	const char* path;
} grain_blueprint_texture_info_t;

/**
 * Saved texture bindings, flattened over all slots.
 *
 * grain_blueprint_apply never touches textures -- the library has no pixels
 * and never reads files. Resolve each record's path yourself and bind through
 * @ref grain_set_texture (the slot index is
 * grain_module_info_t::first_sampler + the sampler's position in its module).
 */
int
grain_blueprint_num_textures(grain_blueprint_t* blueprint);

grain_blueprint_texture_info_t
grain_blueprint_get_texture(grain_blueprint_t* blueprint, int index);

#endif
