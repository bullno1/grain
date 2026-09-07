#ifndef GRAIN_INTERNAL_H
#define GRAIN_INTERNAL_H

#include <grain.h>
#include <cute.h>
#include "dsl.h"
#include "clock.h"

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

	// Generated sources kept for the lazily compiled probe shader; unset for
	// baked archetypes, which cannot be probed
	bool has_probe_sources;
	grain_dsl_probe_sources_t probe_sources;
};

typedef struct {
	CF_StorageBuffer gpu;
	void* cpu;
	bool dirty;
} grain_ssbo_t;

typedef struct {
	const char* module_name;
	const char* name;
	CF_ShaderInfoDataType type;
	int offset;
	int size;
} grain_param_slot_t;

typedef struct {
	CK_DYNA grain_param_slot_t* slots;
	int stride;
} grain_param_layout_t;

typedef struct {
	const char* module_name;  // interned, keys migration across reloads
	const char* name;         // interned sampler local name
	grain_texture_binding_t binding;
	bool bound;
} grain_pool_sampler_t;

struct grain_system_s {
	grain_pool_t* pool;
	CF_M4x4 transform;  // local -> world, see grain_set_transform
};

struct grain_pool_s {
	grain_t* grain;
	grain_pool_opts_t opts;

	grain_ssbo_t update_ssbo;
	grain_ssbo_t render_ssbo;
	grain_ssbo_t clock_ssbo;
	grain_ssbo_t draw_list;

	CF_Canvas canvases[2];
	CF_Material material;
	bool pingpong;

	grain_system_t* systems;
	grain_particle_clock_t* clocks;

	grain_pool_t* update_next;
	bool queued_for_update;

	grain_pool_t* render_next;
	bool queued_for_render;

	int pool_size;
	int num_draws;

	// Layout snapshot to detect reload
	uint32_t archetype_revision;
	uint64_t attr_layout_hash;
	grain_param_layout_t update_layout;
	grain_param_layout_t render_layout;

	// Parallel to the archetype's sampler slots
	CK_DYNA grain_pool_sampler_t* sampler_bindings;

	CF_RenderState render_state;

	// Probe pass state, created on the first grain_probe_system (probe.c);
	// zero handles until then
	CF_Canvas probe_canvas;
	grain_ssbo_t probe_list;
};

// Shared between grain.c and probe.c

void
grain_init_ssbo(grain_ssbo_t* ssbo, int size);

void
grain_cleanup_ssbo(grain_ssbo_t* ssbo);

//! Upload the first `size` bytes if anything was written since the last sync
void
grain_sync_ssbo(grain_ssbo_t* ssbo, int size);

//! CPU-side slot `index`, marking the buffer dirty
void*
grain_index_ssbo(grain_ssbo_t* ssbo, int item_size, int index);

//! Catch the pool up with its archetype after a redefinition
void
grain_reconcile_pool(grain_pool_t* pool);

//! Index of the highest allocated system, 0 when none
int
grain_find_system_hwm(grain_pool_t* pool);

//! Bind the attribute textures of the pool's current read canvas to its material
void
grain_bind_pool_textures(grain_pool_t* pool);

//! Release the probe canvas and buffer if they exist (probe.c)
void
grain_cleanup_pool_probe(grain_pool_t* pool);

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
