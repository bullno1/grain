#ifndef GRAIN_H
#define GRAIN_H

#include <stdint.h>

#define GRAIN_DECL_MAYBE(T) typedef struct T##_maybe { bool success; union { T* value; const char* error; }; } T##_maybe;
#define GRAIN_MAYBE(T) T##_maybe

typedef struct grain_s grain_t;
typedef struct grain_emitter_s grain_emitter_t;
typedef struct grain_affector_s grain_affector_t;
typedef struct grain_renderer_s grain_renderer_t;
typedef struct grain_archetype_s grain_archetype_t;
typedef struct grain_pool_s grain_pool_t;
typedef struct grain_system_s grain_system_t;

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
	int max_particles_per_system;
} grain_pool_opts;

grain_t*
grain_create(void);

void
grain_destroy(grain_t* grain);

const char*
grain_get_last_error(grain_t* grain);

grain_emitter_t*
grain_define_emitter(grain_t* grain, const char* source);

grain_affector_t*
grain_define_affector(grain_t* grain, const char* source);

grain_renderer_t*
grain_define_renderer(grain_t* grain, const char* source);

grain_archetype_t*
grain_define_archetype(grain_t* grain, const char* name, grain_archetype_spec_t spec);

grain_pool_t*
grain_create_pool(grain_t* grain, grain_pool_opts opts);

void
grain_destroy_pool(grain_pool_t* pool);

grain_system_t*
grain_create_system(grain_pool_t* pool);

void
grain_destroy_system(grain_system_t* system);

void
grain_tick(grain_system_t* system, float dt_s);

void
grain_set_emission_rate(grain_system_t* system, float particles_per_second);

void
grain_set_emitter_parameter(
	grain_system_t* system,
	int emitter_index,
	const char* name,
	void* value
);

void
grain_set_affector_parameter(
	grain_system_t* system,
	int affector_index,
	const char* name,
	void* value
);

void
grain_set_renderer_parameter(
	grain_pool_t* pool,
	const char* name,
	void* value
);

void
grain_update(grain_t* grain);

void
grain_begin_render(grain_t* grain);

void
grain_render(grain_system_t* system);

void
grain_end_render(grain_t* grain);

#endif
