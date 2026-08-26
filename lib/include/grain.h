#ifndef GRAIN_H
#define GRAIN_H

#include <stdint.h>
#include <stdbool.h>
#include <cute_graphics.h>
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

typedef struct {
	const char* name;
	int first_param;
	int num_params;
} grain_module_info_t;

typedef struct {
	const grain_module_info_t* emitters;
	int num_emitters;

	const grain_module_info_t* affectors;
	int num_affectors;

	const grain_module_info_t  renderer;
	const grain_param_info_t* params;
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

grain_pool_t*
grain_get_pool(grain_system_t* system);

//! The options this pool was created with
grain_pool_opts_t
grain_get_pool_opts(grain_pool_t* pool);

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
 * Serialize a particle system into a JSON value inside the caller's document.
 *
 * The value is a closure: module sources, archetype composition, pool config
 * and current param values.
 * It is not attached to the document; use cf_json_set_root for a standalone
 * file or nest it inside a bigger object.
 *
 * @return The blueprint object, or a zero CF_JVal on failure
 *         (see @ref grain_get_last_error).
 */
CF_JVal
grain_save_system(grain_t* grain, grain_system_t* system, grain_save_opts_t opts, CF_JDoc doc);

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

#endif
