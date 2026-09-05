#ifndef GRAIN_INTERNAL_H
#define GRAIN_INTERNAL_H

#include <grain.h>
#include <cute.h>
#include "dsl.h"

#if defined(__GNUC__) || defined(__clang__)
#	define GRAIN_FORMAT_ATTRIBUTE(FMT, VA) __attribute__((format(printf, FMT, VA)))
#else
#	define GRAIN_FORMAT_ATTRIBUTE(FMT, VA)
#endif

typedef struct {
	grain_dsl_module_info_t* info;
	// The compiled copy: decorators are blanked out of it
	char* source;
	// The source as the author wrote it, decorators intact; what serialization embeds
	char* original_source;
} grain_module_t;

struct grain_archetype_s {
	grain_archetype_spec_t spec;
	grain_dsl_archetype_shaders_t shaders;
	bool own_bytecode;

	CK_DYNA grain_module_info_t* emitters;
	CK_DYNA grain_module_info_t* affectors;
	        grain_module_info_t  renderer;
	CK_DYNA grain_param_info_t* params;
	CK_DYNA int* params_offsets;

	// Sampler slots in canonical order (emitters, affectors, renderer); slot j
	// is bound as `grain_sampler_<j>` at binding num_textures + j.
	CK_DYNA grain_sampler_info_t* samplers;
	// Parallel to `samplers`: interned owning-module names, for binding
	// migration and blueprint capture.
	CK_DYNA const char** sampler_module_names;

	// Deep copies of module decorators: the archetype outlives module
	// redefinitions. Exact-size so the pointers handed out never move.
	grain_param_decorator_t* param_decorators;
	grain_decorator_arg_t* param_decorator_args;

	int num_textures;
	int update_size;
	int render_size;

	// Texture/channel of the hidden birth-time lane (see grain_define_archetype).
	int birth_texture;
	int birth_channel;

	// Bumped on every redefinition.
	uint32_t revision;

	// Hash of the attribute list (name, type)
	uint64_t attr_layout_hash;
};

struct grain_s {
	// Set only by the headless tests (which build this struct by hand):
	// archetype definition then compiles all shaders on the CPU but skips the
	// GPU shader objects, so the full codegen is verifiable without a GPU.
	bool headless;

	CF_Mesh dummy_mesh;
	// Bound to every sampler slot without a user texture: CF requires all
	// declared samplers fed. Opaque white, so unbound slots multiply to a
	// visible tint instead of silently rendering nothing.
	CF_Texture fallback_texture;
	CF_Arena arena;
	const char* last_error;
	int render_gen;

	CF_ShaderBytecode update_vert_bytecode;

	CK_MAP(grain_module_t*) emitters;
	CK_MAP(grain_module_t*) affectors;
	CK_MAP(grain_module_t*) renderers;
	CK_MAP(grain_archetype_t*) archetypes;

	grain_pool_t* update_list;
	grain_pool_t* render_list;
};

void
grain_set_last_error(grain_t* grain, const char* message);

//! Destroy every archetype; shared between grain_destroy and test cleanup
void
grain_free_archetypes(grain_t* grain);

//! Free everything an archetype owns (bytecode only if own_bytecode); not the
//! struct itself
void
grain_cleanup_archetype(grain_archetype_t* archetype);

/**
 * Find-or-recycle the archetype registered under `interned_name` (must be
 * sintern'ed). An existing archetype is cleaned up in place so pools can
 * reconcile against the bumped revision; a new one is uninitialized. Either
 * way the caller must fully (re)initialize the struct, with `*out_revision`
 * as its revision.
 */
grain_archetype_t*
grain_upsert_archetype(grain_t* grain, const char* interned_name, uint32_t* out_revision);

GRAIN_FORMAT_ATTRIBUTE(2, 3)
const char*
grain_sprintf(grain_t* grain, const char* msg, ...);

const char*
grain_strcpy(grain_t* grain, const char* str);

#endif
