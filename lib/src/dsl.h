#ifndef GRAIN_DSL_H
#define GRAIN_DSL_H

#include <grain.h>
#include <cute.h>
#include "decorator.h"
#define CSPV_API static
#include "cute_spirv.h"

typedef struct {
	const char* name;
	CSPV_DataType type;
} grain_dsl_var_t;

typedef struct {
	const char* name;
	grain_module_kind_t kind;
	CK_DYNA grain_dsl_var_t* particle_attrs;
	CK_DYNA grain_dsl_var_t* module_params;
	CK_DYNA grain_decorator_t* decorators;
	CK_DYNA grain_decorator_arg_t* decorator_args;
	//! Interned names from the Samplers block, in declaration order
	CK_DYNA const char** samplers;
} grain_dsl_module_info_t;

typedef struct {
	CF_Shader update_shader;
	CF_Shader render_shader;
	CF_ShaderBytecode update_frag_bytecode;
	CF_ShaderBytecode render_vert_bytecode;
	CF_ShaderBytecode render_frag_bytecode;

	// Compiled on the first grain_probe_system of the archetype (see
	// grain_dsl_compile_probe); zero until then and for baked archetypes
	CF_Shader probe_shader;
	CF_ShaderBytecode probe_vert_bytecode;
	CF_ShaderBytecode probe_frag_bytecode;
} grain_dsl_archetype_shaders_t;

/**
 * Everything the probe pass needs to compile the render stage again later,
 * snapshotted at archetype definition so it matches the running render shader
 * even if the renderer module is redefined in between. All owned copies.
 */
typedef struct {
	char* renderer_name;
	char* renderer_source;
	char* attrs_source;
	char* archetype_internal_source;
	char* render_source;
} grain_dsl_probe_sources_t;

// `samplers` (scanned from the Samplers block, not owned) is declared to the
// inspect compile so the module body can reference them, and reflected sampler
// declarations outside the list are rejected.
grain_dsl_module_info_t*
grain_dsl_parse_module(
	grain_t* grain,
	const char* source,
	CSPV_Stage stage,
	CK_DYNA const char** samplers
);

bool
grain_dsl_compile_archetype(
	grain_t* grain,
	grain_archetype_spec_t spec,
	const char* attrs_source,
	const char* archetype_internal_source,
	const char* update_source,
	const char* render_source,
	grain_dsl_archetype_shaders_t* out
);

/**
 * Compile the probe variant of the render stage into `out->probe_*`.
 *
 * Headless mode skips the GPU shader object, like grain_dsl_compile_archetype.
 */
bool
grain_dsl_compile_probe(
	grain_t* grain,
	const grain_dsl_probe_sources_t* sources,
	grain_dsl_archetype_shaders_t* out
);

//! Deep copy of the generated sources a probe compile needs
grain_dsl_probe_sources_t
grain_dsl_copy_probe_sources(
	grain_archetype_spec_t spec,
	const char* attrs_source,
	const char* archetype_internal_source,
	const char* render_source
);

void
grain_dsl_free_probe_sources(grain_dsl_probe_sources_t* sources);

void
grain_dsl_free_bytecode(CF_ShaderBytecode bytecode);

//! The prebaked bytecode of the shared update vertex shader (a fullscreen
//! stub); the incbin'd constant itself is private to dsl.c
CF_ShaderBytecode
grain_dsl_builtin_update_vert(void);

static inline void
grain_dsl_free_module_info(grain_dsl_module_info_t* module_info) {
	if (module_info == NULL) { return; }

	afree(module_info->particle_attrs);
	afree(module_info->module_params);
	afree(module_info->decorators);
	afree(module_info->decorator_args);
	afree(module_info->samplers);
	cf_free(module_info);
}

#endif
