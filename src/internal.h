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
	char* source;
} grain_module_t;

struct grain_s {
	CF_Mesh dummy_mesh;
	CF_Arena arena;
	const char* last_error;

	CK_MAP(grain_module_t*) emitters;
	CK_MAP(grain_module_t*) affectors;
	CK_MAP(grain_module_t*) renderers;
};

void
grain_set_last_error(grain_t* grain, const char* message);

void
grain_reset_arena(grain_t* grain);

GRAIN_FORMAT_ATTRIBUTE(2, 3)
const char*
grain_sprintf(grain_t* grain, const char* msg, ...);

const char*
grain_strcpy(grain_t* grain, const char* str);

#endif
