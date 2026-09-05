#ifndef GRAIN_BAKED_INTERNAL_H
#define GRAIN_BAKED_INTERNAL_H

// Exposed for internal tools (grainc, tests), like blueprint.h

#include <grain_baked.h>
#include <cute.h>

//! Owns the flat arrays a grain_bake'd descriptor points into
typedef struct {
	CK_DYNA grain_baked_module_t* emitters;
	CK_DYNA grain_baked_module_t* affectors;
	CK_DYNA grain_baked_param_t* params;
	CK_DYNA grain_baked_sampler_t* samplers;
	CK_DYNA grain_baked_decorator_t* decorators;
	CK_DYNA grain_decorator_arg_t* decorator_args;
	CK_DYNA grain_baked_param_value_t* param_values;
	CK_DYNA double* values;
	CK_DYNA grain_baked_texture_t* textures;
} grain_bake_scratch_t;

/**
 * Flatten a materialized blueprint (one whose archetype exists, i.e. from
 * grain_load_blueprint) into a baked descriptor.
 *
 * `out` points into `scratch` (and into interned strings / the archetype's
 * bytecode), so it is valid until the scratch is freed or the archetype is
 * redefined. The inverse of grain_load_blueprint_baked.
 */
bool
grain_bake(
	grain_t* grain,
	grain_blueprint_t* blueprint,
	grain_baked_effect_t* out,
	grain_bake_scratch_t* scratch
);

void
grain_bake_scratch_free(grain_bake_scratch_t* scratch);

#endif
