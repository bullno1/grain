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
				.name = "internal/builtins.glsl",
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

grain_dsl_archetype_t*
grain_dsl_compile_archetype(
	grain_t* grain,
	grain_archetype_spec_t spec,
	const char* common_source,
	const char* update_source,
	const char* render_source
) {
	CSPV_Result update_fs_result = { 0 };
	CSPV_Result render_vs_result = { 0 };
	CSPV_Result render_fs_result = { 0 };

	int num_vfs_entries = spec.num_emitters + spec.num_affectors + 6;
	int vfs_index = 0;
	grain_vfs_entry_t* vfs_entries = cf_arena_alloc(&grain->arena, num_vfs_entries * sizeof(grain_vfs_entry_t));
	for (int i = 0; i < spec.num_emitters; ++i) {
		grain_module_t* module = (grain_module_t*)spec.emitters[i];
		vfs_entries[vfs_index++] = (grain_vfs_entry_t){
			.name = grain_sprintf(grain, "emitter/%s", module->info->name),
			.content = module->source,
		};
	}
	for (int i = 0; i < spec.num_affectors; ++i) {
		grain_module_t* module = (grain_module_t*)spec.affectors[i];
		vfs_entries[vfs_index++] = (grain_vfs_entry_t){
			.name = grain_sprintf(grain, "affector/%s", module->info->name),
			.content = module->source,
		};
	}
	vfs_entries[vfs_index++] = (grain_vfs_entry_t){
		.name = "internal/builtins.glsl",
		.content = grain_dsl_materialize(grain, XINCBIN_GET(grain_builtins)),
	};
	vfs_entries[vfs_index++] = (grain_vfs_entry_t){
		.name = grain_sprintf(grain, "renderer/%s", ((grain_module_t*)spec.renderer)->info->name),
		.content = ((grain_module_t*)spec.renderer)->source,
	};
	vfs_entries[vfs_index++] = (grain_vfs_entry_t){
		.name = "archetype/common.glsl",
		.content = common_source,
	};
	vfs_entries[vfs_index++] = (grain_vfs_entry_t){
		.name = "archetype/update.glsl",
		.content = update_source,

	};
	vfs_entries[vfs_index++] = (grain_vfs_entry_t){
		.name = "archetype/update.glsl",
		.content = update_source,

	};
	vfs_entries[vfs_index++] = (grain_vfs_entry_t){ 0 };

	grain_cspv_ctx_t ctx = {
		.vfs = vfs_entries,
	};

	char stage_str[sizeof("1")];
	CSPV_Stage stage;

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

	stage = CSPV_STAGE_FRAGMENT;
	snprintf(stage_str, sizeof(stage_str), "%d", stage);
	update_fs_result = cspv_compile_ex(
		grain_dsl_materialize(grain, XINCBIN_GET(grain_update_fs)),
		stage,
		&opts
	);
	if (!update_fs_result.success) {
		grain_set_last_error(grain, grain_strcpy(grain, update_fs_result.error_message));
		goto fail;
	}
	if (update_fs_result.preprocessed) {
		printf("%s\n", update_fs_result.preprocessed);
	}

fail:
	cspv_free(&update_fs_result);
	cspv_free(&render_vs_result);
	cspv_free(&render_fs_result);
	return NULL;
}

#define CUTE_SPIRV_IMPLEMENTATION
#include "cute_spirv.h"

#define XINCBIN_IMPLEMENTATION
#include "resources.rc"
