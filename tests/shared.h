#ifndef GRAIN_TEST_SHARED_H
#define GRAIN_TEST_SHARED_H

#include <string.h>
#include <btest.h>
#include "internal.h"

/**
 * A grain_t without any GPU resources.
 *
 * Module definition only needs the arena (shader compilation is CPU-side via
 * cute-spirv), so everything up to grain_define_* is testable headlessly.
 * Archetypes and pools create GPU objects and are out of reach here.
 */
static grain_t test_grain_storage;

static inline grain_t*
test_grain(void) {
	return &test_grain_storage;
}

static inline void
test_grain_init(void) {
	test_grain_storage = (grain_t){
		// Archetype definition compiles shaders on the CPU but skips the GPU
		// shader objects, so full codegen runs headlessly (see internal.h)
		.headless = true,
		.arena = cf_make_arena(16, 64 * 1024),
	};
}

// Mirrors the static grain_free_modules in grain.c
static inline void
test_grain_free_modules(CK_MAP(grain_module_t*)* module_store) {
	for (int i = 0; i < map_size(*module_store); ++i) {
		grain_module_t* module = (*module_store)[i];
		cf_free(module->source);
		cf_free(module->original_source);
		grain_dsl_free_module_info(module->info);
		cf_free(module);
	}
	map_free(*module_store);
	*module_store = NULL;
}

static inline void
test_grain_cleanup(void) {
	test_grain_free_modules(&test_grain_storage.emitters);
	test_grain_free_modules(&test_grain_storage.affectors);
	test_grain_free_modules(&test_grain_storage.renderers);
	grain_free_archetypes(&test_grain_storage);
	cf_destroy_arena(&test_grain_storage.arena);
}

#define GRAIN_EXPECT_ERROR_CONTAINS(FRAGMENT) \
	do { \
		const char* grain__error = grain_get_last_error(test_grain()); \
		BTEST_EXPECT_EX( \
			grain__error != NULL && strstr(grain__error, (FRAGMENT)) != NULL, \
			"error is \"%s\", expected to contain \"%s\"", \
			grain__error != NULL ? grain__error : "(null)", (FRAGMENT) \
		); \
	} while (0)

#endif
