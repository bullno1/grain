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

GRAIN_FORMAT_ATTRIBUTE(2, 3)
const char*
grain_sprintf(grain_t* grain, const char* msg, ...);

const char*
grain_strcpy(grain_t* grain, const char* str);

#endif
