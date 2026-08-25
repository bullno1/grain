#ifndef GRAIN_H
#define GRAIN_H

#include <stdint.h>
#include <stdbool.h>
#include <cute_graphics.h>

typedef struct grain_s grain_t;
typedef struct grain_emitter_s grain_emitter_t;
typedef struct grain_affector_s grain_affector_t;
typedef struct grain_renderer_s grain_renderer_t;
typedef struct grain_archetype_s grain_archetype_t;
typedef struct grain_pool_s grain_pool_t;
typedef struct grain_system_s grain_system_t;

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
	union {
		grain_emitter_t* emitter;
		grain_affector_t* affector;
		grain_renderer_t* renderer;
	};
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
	int first_params;
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
 * @param param_index Index into grain_archetype_info_t::params (grain_module_info_t::first_params + i).
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

#endif
