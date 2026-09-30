#ifndef GRAIN_H
#define GRAIN_H

#include <stdint.h>
#include <stdbool.h>
#include <cute_graphics.h>
#include <cute_draw.h>
#include <cute_json.h>
#include <cute_math3d.h>

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
 * A module along with its declared kind. Only the union member matching `kind`
 * is valid; `kind` is GRAIN_MODULE_INVALID when definition failed.
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
 * Define a module of whatever kind its source declares. The typed variants
 * below reject a module of any other kind.
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
 * Find a decorator argument by position or by name, Python-style.
 * Returns false if neither exists.
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
 * Bind a texture to one of this pool's sampler slots; every system in the pool
 * samples it. Module code reads the slot's `<name>_uvrect` as (uv_min, uv_max).
 * The binding survives live reload while the module keeps a sampler of the
 * same name.
 *
 * @param sampler_index Index into grain_archetype_info_t::samplers
 */
void
grain_set_texture(grain_pool_t* pool, int sampler_index, grain_texture_binding_t binding);

/**
 * Bind an atlased sprite's current image to one of this pool's sampler slots.
 * Available when cute_draw.h is included before grain.h. Call every frame
 * while the binding is live: the sprite can animate and the atlas reshuffle.
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
 * Grain's default render state for pools: premultiplied-alpha "over" blending,
 * LESS_EQUAL depth test without depth write, no culling. Additive blending is
 * a shader decision under this convention: emit color with alpha near zero.
 */
CF_RenderState
grain_render_state_defaults(void);

/**
 * Override the render state of a pool. Start from
 * @ref grain_render_state_defaults; primitive_type is overwritten by grain.
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
 * Queue `count` particles to be emitted at once in the next update pass.
 * Calls within a frame accumulate, clamped to the pool's max_burst_size.
 * Bursting more than that within a lifetime_budget window recycles live particles.
 */
void
grain_burst(grain_system_t* system, int count);

/**
 * Set a system's local-to-world transform; identity by default. Instance
 * state: not a module param and not saved by blueprints. Modules read it as
 * `ctx.transform` and through `to_world` / `to_world_dir`.
 *
 * The bundled modules apply it at emission, so moving the system leaves
 * emitted particles where they are. A renderer may apply it instead so every
 * particle moves rigidly with the system; emitters and affectors then work in
 * local coordinates and must not apply it.
 */
void
grain_set_transform(grain_system_t* system, CF_M4x4 transform);

CF_M4x4
grain_get_transform(grain_system_t* system);

//! Widen a 2D affine transform, e.g. from CF's draw stack, into a CF_M4x4
static inline CF_M4x4
grain_m4x4_from_m3x2(CF_M3x2 m) {
	CF_M4x4 out = cf_m4_identity();
	out.elements[0]  = m.m.x.x; out.elements[1]  = m.m.x.y;
	out.elements[4]  = m.m.y.x; out.elements[5]  = m.m.y.y;
	out.elements[12] = m.p.x;   out.elements[13] = m.p.y;
	return out;
}

//! @ref grain_set_transform for 2D callers
static inline void
grain_set_transform_2d(grain_system_t* system, CF_M3x2 transform) {
	grain_set_transform(system, grain_m4x4_from_m3x2(transform));
}

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
 * Get a raw pointer to a parameter, valid until the next call into the library
 * that touches this system's pool. Writes must be followed by
 * @ref grain_parameter_modified.
 *
 * @param param_index Index into grain_archetype_info_t::params
 * @return NULL if param_index is out of range.
 */
void*
grain_get_parameter(grain_system_t* system, int param_index);

/** Flag a parameter for re-upload after a write through @ref grain_get_parameter */
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

/**
 * The camera an effect is authored for: a blueprint hint for editors. The
 * library always uploads both the 2D and 3D transform families.
 */
typedef enum {
	GRAIN_VIEW_2D = 0,
	GRAIN_VIEW_3D,
} grain_view_t;

// ---- Probe: read a system's particles back from the GPU ----

typedef struct grain_probe_s grain_probe_t;

//! Axis-aligned bounds. Empty when min > max on any axis.
typedef struct {
	float min[3];
	float max[3];
} grain_bounds_t;

//! One pool slot as the renderer saw it
typedef struct {
	bool live;              // the renderer did not cull it
	float age;              // seconds since birth; 0 when not live
	grain_bounds_t bounds;  // of its rendered geometry, world space (see grain_probe_system)
} grain_probe_slot_t;

typedef struct {
	float elapsed;          // the system clock at capture
	float emit_cursor;      // slot the emission counter points at, [0, num_slots)

	int num_slots;          // == the pool's pool_size
	const grain_probe_slot_t* slots;

	// Aggregates over live slots
	int num_live;
	float max_age;          // 0 when nothing is live
	grain_bounds_t bounds;  // empty when nothing is live
} grain_probe_result_t;

/**
 * Capture a system's particles as the renderer sees them, with the view
 * transforms at identity so positions come out in world space. The system's
 * own transform applies, so probing at identity measures system-local space,
 * which is what blueprint bounds store. Call between grain_end_update and the
 * next grain_begin_update. Intended for editors and offline tools, not
 * per-frame use. Baked archetypes cannot be probed.
 *
 * @return NULL on failure (see @ref grain_get_last_error).
 */
grain_probe_t*
grain_probe_system(grain_system_t* system);

typedef enum {
	GRAIN_PROBE_PENDING = 0,  // the copy has not landed yet; poll again next frame
	GRAIN_PROBE_READY,        // `out` points at the result
	GRAIN_PROBE_FAILED,       // final; the reason is in grain_get_last_error
} grain_probe_status_t;

/**
 * Poll a probe after presenting the frame that captured it. On
 * GRAIN_PROBE_READY `*out` is the result, valid until grain_destroy_probe.
 * `out` may be NULL.
 */
grain_probe_status_t
grain_probe_poll(grain_probe_t* probe, const grain_probe_result_t** out);

void
grain_destroy_probe(grain_probe_t* probe);

// ---- Bounds helpers ----

grain_bounds_t
grain_bounds_empty(void);

bool
grain_bounds_is_empty(grain_bounds_t bounds);

//! Grow `bounds` to cover `other`; either may be empty
void
grain_bounds_union(grain_bounds_t* bounds, grain_bounds_t other);

typedef struct {
	//! Saved as the archetype name when the blueprint is loaded; defaults to "Effect"
	const char* name;
	float emission_rate;
	//! Defaults to GRAIN_VIEW_2D
	grain_view_t view;

	/**
	 * Optional source path lookup, called once per distinct module; return NULL
	 * to omit. Stored verbatim, never resolved.
	 */
	const char* (*module_path)(void* userdata, grain_module_kind_t kind, const char* module_name);

	/**
	 * Optional texture path lookup, one call per sampler slot; return NULL to
	 * omit. Stored verbatim, never resolved. `module_index` disambiguates the
	 * same module occupying several slots.
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
 * Snapshot a system into a blueprint: module sources, archetype composition,
 * pool config and current param values. Destroy with @ref grain_destroy_blueprint.
 *
 * @return NULL on failure (see @ref grain_get_last_error).
 */
grain_blueprint_t*
grain_snapshot_system(grain_t* grain, grain_system_t* system, grain_save_opts_t opts);

/**
 * Serialize a blueprint into an unattached JSON value inside the caller's
 * document. The document borrows the blueprint's strings: keep the blueprint
 * alive until the document has been serialized or destroyed.
 */
CF_JVal
grain_save_blueprint(grain_blueprint_t* blueprint, CF_JDoc doc);

/**
 * Load a blueprint from a JSON value, defining its modules and archetype under
 * their saved names. The document may be destroyed as soon as this returns.
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

//! GRAIN_VIEW_2D when the blueprint predates the `view` field
grain_view_t
grain_blueprint_view(grain_blueprint_t* blueprint);

grain_archetype_t*
grain_blueprint_archetype(grain_blueprint_t* blueprint);

//! Saved pool config with `archetype` filled in; tweak max_systems before grain_create_pool
grain_pool_opts_t
grain_blueprint_pool_opts(grain_blueprint_t* blueprint);

/**
 * Saved bounds in system-local space; false when the blueprint has none.
 * The library never culls with them: the caller tests them against its view.
 */
bool
grain_blueprint_bounds(grain_blueprint_t* blueprint, grain_bounds_t* out);

//! Store bounds to be saved with the blueprint; an empty bounds clears them
void
grain_blueprint_set_bounds(grain_blueprint_t* blueprint, grain_bounds_t bounds);

/**
 * Overwrite the saved pool config. `archetype` is ignored; pools already
 * created are untouched.
 */
void
grain_blueprint_set_pool_opts(grain_blueprint_t* blueprint, grain_pool_opts_t opts);

void
grain_blueprint_set_emission_rate(grain_blueprint_t* blueprint, float emission_rate);

/**
 * Write the saved param values and emission rate into a system. Modules are
 * matched by name and params by name and component count; unmatched params
 * keep their value.
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
 * Saved texture bindings, flattened over all slots. The library never reads
 * files: resolve each path yourself and bind through @ref grain_set_texture.
 */
int
grain_blueprint_num_textures(grain_blueprint_t* blueprint);

grain_blueprint_texture_info_t
grain_blueprint_get_texture(grain_blueprint_t* blueprint, int index);

#endif
