#include "dsl.h"
#include "internal.h"
#include "resources.rc"

typedef struct {
	const char* name;
	const char* content;
} grain_vfs_entry_t;

typedef struct {
	grain_vfs_entry_t* vfs;
} grain_cspv_ctx_t;

typedef enum {
	GRAIN_INSPECT_MODULE_NAME,
	GRAIN_INSPECT_REQUIRES,
	GRAIN_INSPECT_PARAMS,
} grain_inspect_mode_t;

static const char*
grain_resolve_include(const char* name, void* user) {
	grain_cspv_ctx_t* ctx = user;

	for (int i = 0; ctx->vfs != NULL && ctx->vfs[i].name != NULL; ++i) {
		if (strcmp(name, ctx->vfs[i].name) == 0) {
			return ctx->vfs[i].content;
		}
	}

	return NULL;
}

static bool
grain_dsl_collect_vars(
	grain_t* grain,
	const char* block_name,
	const CSPV_ReflectionMember* members,
	int num_members,
	CK_DYNA grain_dsl_var_t** vars
) {
	const char* ignored = sintern("grain__ignore");
	for (int i = 0; i < num_members; ++i) {
		const CSPV_ReflectionMember* member = &members[i];
		if (member->name == ignored) { continue; }

		grain_dsl_var_t var = {
			.name = member->name,
			.type = member->type,
		};

		if (member->type == CSPV_TYPE_UNKNOWN || member->array_length != 1) {
			grain_set_last_error(
				grain,
				grain_sprintf(
					grain,
					"`%s` block contains member `%s` with an unsupported type",
					block_name,
					member->name
				)
			);
			return false;
		}

		apush(*vars, var);
	}

	return true;
}

static const char*
grain_dsl_materialize(grain_t* grain, xincbin_data_t incbin) {
	char* data = cf_arena_alloc(&grain->arena, incbin.size + 1);
	memcpy(data, incbin.data, incbin.size);
	data[incbin.size] = '\0';
	return data;
}

grain_dsl_module_info_t*
grain_dsl_parse_module(grain_t* grain, const char* source, CSPV_Stage stage) {
	grain_cspv_ctx_t ctx = {
		.vfs = (grain_vfs_entry_t[]){
			{
				.name = "grain/internal/builtins.glsl",
				.content = grain_dsl_materialize(grain, XINCBIN_GET(grain_builtins)),
			},
			{
				.name = "module.glsl",
				.content = source,
			},
			{ 0 }
		},
	};

	char stage_str[sizeof("1")];
	snprintf(stage_str, sizeof(stage_str), "%d", stage);

	CSPV_Options opts = {
		.user = &ctx,
		.include_resolve = grain_resolve_include,

		.defines = (CSPV_Define[]) {
			{ .name = "GRAIN_SHADER_STAGE_VERTEX", .value = "0" },
			{ .name = "GRAIN_SHADER_STAGE_FRAGMENT", .value = "1" },
			{ .name = "GRAIN_SHADER_STAGE_COMPUTE", .value = "2" },
			{ .name = "GRAIN_SHADER_STAGE", .value = stage_str },
		},
		.num_defines = 4,
	};

	CSPV_Result inspect_result = cspv_compile_ex(
		grain_dsl_materialize(grain, XINCBIN_GET(grain_inspect_stub)),
		stage,
		&opts
	);

	if (!inspect_result.success) {
		grain_set_last_error(grain, grain_strcpy(grain, inspect_result.error_message));
		cspv_free(&inspect_result);
		return NULL;
	}

	const char* module_block_name = sintern("Grain__Inspect_Module");
	const char* requires_block_name = sintern("Grain__Inspect_Requires");
	const char* params_block_name = sintern("Grain__Inspect_Params");

	const char* module_name = NULL;
	CK_DYNA grain_dsl_var_t* particle_attrs = NULL;
	CK_DYNA grain_dsl_var_t* module_params = NULL;

	const CSPV_Reflection* reflection = &inspect_result.reflection;

	if (asize(reflection->inputs) != 0) {
		grain_set_last_error(grain, "Particle module cannot declare input");
		goto fail;
	}

	for (int i = 0; i < asize(reflection->uniform_blocks); ++i) {
		const CSPV_ReflectionBlock* uniform_block = &reflection->uniform_blocks[i];
		if (uniform_block->name == module_block_name) {
			if (uniform_block->num_members != 1) {
				grain_set_last_error(grain, "Invalid Module block");
				goto fail;
			}

			module_name = reflection->uniform_members[uniform_block->first_member].name;
		} else if (uniform_block->name == requires_block_name) {
			if (!grain_dsl_collect_vars(
					grain,
					"Requires",
					reflection->uniform_members + uniform_block->first_member, uniform_block->num_members,
					&particle_attrs
			)) {
				goto fail;
			}
		} else if (uniform_block->name == params_block_name) {
			if (!grain_dsl_collect_vars(
					grain,
					"Params",
					reflection->uniform_members + uniform_block->first_member, uniform_block->num_members,
					&module_params
			)) {
				goto fail;
			}
		} else {
			grain_set_last_error(grain, "Particle module cannot declare extra uniform block");
			goto fail;
		}
	}

	if (module_name == NULL) {
		grain_set_last_error(grain, "Missing `Module` block");
		goto fail;
	}

	grain_dsl_module_info_t* module_info = cf_alloc(sizeof(grain_dsl_module_info_t));
	*module_info = (grain_dsl_module_info_t){
		.name = module_name,
		.particle_attrs = particle_attrs,
		.module_params = module_params,
	};
	cspv_free(&inspect_result);
	return module_info;

fail:
	afree(particle_attrs);
	afree(module_params);
	cspv_free(&inspect_result);
	return NULL;
}

#define CUTE_SPIRV_IMPLEMENTATION
#include "cute_spirv.h"

#define XINCBIN_IMPLEMENTATION
#include "resources.rc"
