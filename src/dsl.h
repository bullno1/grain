#ifndef GRAIN_DSL_H
#define GRAIN_DSL_H

#include <grain.h>
#include <cute.h>
#define CSPV_API static
#include "cute_spirv.h"

typedef struct {
	const char* name;
	CSPV_DataType type;
} grain_dsl_var_t;

typedef struct {
	const char* name;
	CK_DYNA grain_dsl_var_t* particle_attrs;
	CK_DYNA grain_dsl_var_t* module_params;
} grain_dsl_module_info_t;

typedef struct {
	CF_Shader update_shader;
	CF_Shader render_shader;
	CF_ShaderBytecode update_frag_bytecode;
	CF_ShaderBytecode render_vert_bytecode;
	CF_ShaderBytecode render_frag_bytecode;
} grain_dsl_archetype_t;

grain_dsl_module_info_t*
grain_dsl_parse_module(grain_t* grain, const char* source, CSPV_Stage stage);

grain_dsl_archetype_t*
grain_dsl_compile_archetype(
	grain_t* grain,
	grain_archetype_spec_t spec,
	const char* common_source,
	const char* update_source,
	const char* render_source
);

static inline void
grain_dsl_free_module_info(grain_dsl_module_info_t* module_info) {
	if (module_info == NULL) { return; }

	afree(module_info->particle_attrs);
	afree(module_info->module_params);
	cf_free(module_info);
}

#endif
