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
	CF_Mesh dummy_mesh;
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

GRAIN_FORMAT_ATTRIBUTE(2, 3)
const char*
grain_sprintf(grain_t* grain, const char* msg, ...);

const char*
grain_strcpy(grain_t* grain, const char* str);

#endif
