#include "dsl.h"
#include "internal.h"
#include <string.h>
#include "resources.rc"
#include "gen/update_vert_bytecode.h"

typedef struct {
	const char* name;
	const char* content;
} grain_vfs_entry_t;

typedef struct {
	grain_vfs_entry_t* vfs;
} grain_cspv_ctx_t;

typedef enum {
	GRAIN_COMPILE_INSPECT,
	GRAIN_COMPILE_DESKTOP,
	GRAIN_COMPILE_WEB,
} grain_compile_mode_t;

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
	const char* ignored = sintern("grain_ignore");
	for (int i = 0; i < num_members; ++i) {
		const CSPV_ReflectionMember* member = &members[i];
		if (member->name == ignored) { continue; }

		// `grain_` is reserved
		if (strncmp(member->name, "grain_", 6) == 0) {
			grain_set_last_error(
				grain,
				grain_sprintf(
					grain,
					"`%s` block member `%s` uses the reserved `grain_` prefix",
					block_name,
					member->name
				)
			);
			return false;
		}

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

static CSPV_Result
grain_dsl_compile(
	grain_t* grain,
	grain_vfs_entry_t* vfs,
	CSPV_Stage stage,
	grain_compile_mode_t mode,
	const char* entry
) {
	grain_cspv_ctx_t ctx = {
		.vfs = vfs,
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
			{ .name = "CF_GLES", .value = "1" },
		},
		.num_defines = mode == GRAIN_COMPILE_WEB ? 5 : 4,

		.emit_msl = mode == GRAIN_COMPILE_DESKTOP,
		.emit_hlsl = mode == GRAIN_COMPILE_DESKTOP,
		.emit_glsl300 = mode == GRAIN_COMPILE_WEB,
	};

	return cspv_compile_ex(entry, stage, &opts);
}

bool
grain_dsl_compile_for_cf(
	grain_t* grain,
	grain_vfs_entry_t* vfs,
	CSPV_Stage stage,
	const char* entry,
	CF_ShaderBytecode* out
) {
	CSPV_Result desktop_result = grain_dsl_compile(grain, vfs, stage, GRAIN_COMPILE_DESKTOP, entry);
	if (!desktop_result.success) {
		grain_set_last_error(grain, grain_strcpy(grain, desktop_result.error_message));
		return false;
	}

	CSPV_Result web_result = grain_dsl_compile(grain, vfs, stage, GRAIN_COMPILE_WEB, entry);
	if (!web_result.success) {
		grain_set_last_error(grain, grain_strcpy(grain, web_result.error_message));
		cspv_free(&desktop_result);
		return false;
	}

	CSPV_Result r = desktop_result;
	r.glsl300 = web_result.glsl300;

	// Lifted from cute-shaderc

	// Bytecode blob (cf_alloc'd; freed with cf_free() by cute_shader_free_result).
	size_t bytecode_size = r.word_count * sizeof(uint32_t);
	void* bytecode = cf_alloc(bytecode_size);
	memcpy(bytecode, r.spirv, bytecode_size);

	// GLSL 300 es source, from cute_spirv's transpiler backend (requested via
	// opts.emit_glsl300 above; NULL when skipped).
	char* glsl300_src = NULL;
	size_t glsl300_src_size = 0;
	if (r.glsl300) {
		glsl300_src_size = strlen(r.glsl300);
		glsl300_src = (char*)cf_alloc(glsl300_src_size + 1);
		memcpy(glsl300_src, r.glsl300, glsl300_src_size + 1);
	}

	// HLSL source likewise.
	char* hlsl_src = NULL;
	size_t hlsl_src_size = 0;
	if (r.hlsl) {
		hlsl_src_size = strlen(r.hlsl);
		hlsl_src = (char*)cf_alloc(hlsl_src_size + 1);
		memcpy(hlsl_src, r.hlsl, hlsl_src_size + 1);
	}

	// MSL source likewise.
	char* msl_src = NULL;
	size_t msl_src_size = 0;
	if (r.msl) {
		msl_src_size = strlen(r.msl);
		msl_src = (char*)cf_alloc(msl_src_size + 1);
		memcpy(msl_src, r.msl, msl_src_size + 1);
	}

	// Reflection: map CSPV_Reflection to CF_ShaderInfo. Arrays are cf_alloc'd (freed by
	// cute_shader_free_result); names are interned strings from the compiler, which
	// are immortal -- no copies, and free_result must not free them.
	CSPV_Reflection* rf = &r.reflection;

	int num_samplers = (int)asize(rf->samplers);
	int num_storage_textures = 0;
	int num_readwrite_storage_textures = 0;
	for (int i = 0; i < (int)asize(rf->storage_images); ++i) {
		if (rf->storage_images[i].readonly) ++num_storage_textures;
		else ++num_readwrite_storage_textures;
	}
	int num_storage_buffers = 0;
	int num_readwrite_storage_buffers = 0;
	for (int i = 0; i < (int)asize(rf->storage_buffers); ++i) {
		if (rf->storage_buffers[i].readonly) ++num_storage_buffers;
		else ++num_readwrite_storage_buffers;
	}

	// Combined samplers, sorted by binding so array index matches the SDL_GPU slot.
	int num_images = num_samplers;
	const char** image_names = NULL;
	int* image_binding_slots = NULL;
	if (num_images > 0) {
		image_names = (const char**)cf_alloc(sizeof(char*) * num_images);
		image_binding_slots = (int*)cf_alloc(sizeof(int) * num_images);
		for (int i = 0; i < num_images; ++i) {
			image_names[i] = rf->samplers[i].name;
			image_binding_slots[i] = rf->samplers[i].binding;
		}
		for (int i = 0; i < num_images - 1; ++i) {
			for (int j = i + 1; j < num_images; ++j) {
				if (image_binding_slots[j] < image_binding_slots[i]) {
					const char* tn = image_names[i]; image_names[i] = image_names[j]; image_names[j] = tn;
					int tb = image_binding_slots[i]; image_binding_slots[i] = image_binding_slots[j]; image_binding_slots[j] = tb;
				}
			}
		}
	}

	int num_uniforms = (int)asize(rf->uniform_blocks);
	CF_ShaderUniformInfo* uniforms = NULL;
	if (num_uniforms > 0) {
		uniforms = (CF_ShaderUniformInfo*)cf_alloc(sizeof(CF_ShaderUniformInfo) * num_uniforms);
		for (int i = 0; i < num_uniforms; ++i) {
			uniforms[i].block_name = rf->uniform_blocks[i].name;
			uniforms[i].block_index = rf->uniform_blocks[i].binding;
			uniforms[i].block_size = rf->uniform_blocks[i].size;
			uniforms[i].num_members = rf->uniform_blocks[i].num_members;
		}
	}

	int num_uniform_members = (int)asize(rf->uniform_members);
	CF_ShaderUniformMemberInfo* uniform_members = NULL;
	if (num_uniform_members > 0) {
		uniform_members = (CF_ShaderUniformMemberInfo*)cf_alloc(sizeof(CF_ShaderUniformMemberInfo) * num_uniform_members);
		for (int i = 0; i < num_uniform_members; ++i) {
			uniform_members[i].name = rf->uniform_members[i].name;
			uniform_members[i].type = (CF_ShaderInfoDataType)rf->uniform_members[i].type;
			uniform_members[i].offset = rf->uniform_members[i].offset;
			uniform_members[i].array_length = rf->uniform_members[i].array_length;
		}
	}

	int num_inputs = (int)asize(rf->inputs);
	CF_ShaderInputInfo* inputs = NULL;
	if (num_inputs > 0) {
		inputs = (CF_ShaderInputInfo*)cf_alloc(sizeof(CF_ShaderInputInfo) * num_inputs);
		for (int i = 0; i < num_inputs; ++i) {
			inputs[i].name = rf->inputs[i].name;
			inputs[i].location = rf->inputs[i].location;
			inputs[i].format = (CF_ShaderInfoDataType)rf->inputs[i].type;
		}
	}

	// Captured before cspv_free wipes the result.
	int local_size[3] = { r.reflection.local_size[0], r.reflection.local_size[1], r.reflection.local_size[2] };

	cspv_free(&desktop_result);
	cspv_free(&web_result);

	out->content = (uint8_t*)bytecode;
	out->size = bytecode_size;
	out->glsl300_src = glsl300_src;
	out->glsl300_src_size = glsl300_src_size;
	out->hlsl_src = hlsl_src;
	out->hlsl_src_size = hlsl_src_size;
	out->msl_src = msl_src;
	out->msl_src_size = msl_src_size;
	out->shader_info.local_size[0] = local_size[0];
	out->shader_info.local_size[1] = local_size[1];
	out->shader_info.local_size[2] = local_size[2];
	out->shader_info.num_samplers = num_samplers;
	out->shader_info.num_storage_textures = num_storage_textures;
	out->shader_info.num_storage_buffers = num_storage_buffers;
	out->shader_info.num_readwrite_storage_textures = num_readwrite_storage_textures;
	out->shader_info.num_readwrite_storage_buffers = num_readwrite_storage_buffers;
	out->shader_info.num_images = num_images;
	out->shader_info.image_names = image_names;
	out->shader_info.image_binding_slots = image_binding_slots;
	out->shader_info.num_uniforms = num_uniforms;
	out->shader_info.uniforms = uniforms;
	out->shader_info.num_uniform_members = num_uniform_members;
	out->shader_info.uniform_members = uniform_members;
	out->shader_info.num_inputs = num_inputs;
	out->shader_info.inputs = inputs;
	return true;
}

void
grain_dsl_free_bytecode(CF_ShaderBytecode bytecode) {
	// Reflection names are interned strings (immortal) -- only the arrays are freed.
	CF_ShaderInfo* shader_info = &bytecode.shader_info;
	cf_free(shader_info->inputs);
	cf_free(shader_info->uniform_members);
	cf_free(shader_info->uniforms);
	cf_free(shader_info->image_names);
	cf_free(shader_info->image_binding_slots);

	cf_free((void*)bytecode.glsl300_src);
	cf_free((void*)bytecode.hlsl_src);
	cf_free((void*)bytecode.msl_src);
	cf_free((void*)bytecode.content);
}

grain_dsl_module_info_t*
grain_dsl_parse_module(
	grain_t* grain,
	const char* source,
	CSPV_Stage stage,
	CK_DYNA const char** samplers
) {
	// The module body references its samplers by their local names, so the
	// inspect compile declares them up front. Bindings 8+ stay clear of the
	// grain_Inspect_* dummy blocks; the uvrect global stands in for the slot's
	// UV rect the archetype composition provides.
	const char* prelude_fmt =
		"layout(set = 0, binding = %d) uniform sampler2D %s;\n"
		"vec4 %s_uvrect;\n";
	const char* samplers_prelude = "";
	if (asize(samplers) > 0) {
		size_t prelude_size = 1;
		for (int i = 0; i < asize(samplers); ++i) {
			prelude_size += (size_t)snprintf(
				NULL, 0, prelude_fmt, 8 + i, samplers[i], samplers[i]
			);
		}
		char* prelude = cf_arena_alloc(&grain->arena, prelude_size);
		size_t offset = 0;
		for (int i = 0; i < asize(samplers); ++i) {
			offset += (size_t)snprintf(
				prelude + offset, prelude_size - offset,
				prelude_fmt, 8 + i, samplers[i], samplers[i]
			);
		}
		samplers_prelude = prelude;
	}

	grain_vfs_entry_t vfs[] = {
		{
			.name = "grain/api.glsl",
			.content = grain_dsl_materialize(grain, XINCBIN_GET(grain_api)),
		},
		{
			.name = "grain/internal.glsl",
			.content = grain_dsl_materialize(grain, XINCBIN_GET(grain_internal)),
		},
		{
			.name = "grain/samplers.glsl",
			.content = samplers_prelude,
		},
		{
			.name = "module.glsl",
			.content = source,
		},
		{ 0 }
	};

	CSPV_Result inspect_result = grain_dsl_compile(
		grain,
		vfs,
		stage,
		GRAIN_COMPILE_INSPECT,
		grain_dsl_materialize(grain, XINCBIN_GET(grain_inspect_stub))
	);

	if (!inspect_result.success) {
		grain_set_last_error(grain, grain_strcpy(grain, inspect_result.error_message));
		cspv_free(&inspect_result);
		return NULL;
	}

	const char* emitter_block_name = sintern("grain_Inspect_Emitter");
	const char* affector_block_name = sintern("grain_Inspect_Affector");
	const char* renderer_block_name = sintern("grain_Inspect_Renderer");
	const char* requires_block_name = sintern("grain_Inspect_Requires");
	const char* params_block_name = sintern("grain_Inspect_Params");

	const char* module_name = NULL;
	grain_module_kind_t module_kind = GRAIN_MODULE_INVALID;
	CK_DYNA grain_dsl_var_t* particle_attrs = NULL;
	CK_DYNA grain_dsl_var_t* module_params = NULL;

	const CSPV_Reflection* reflection = &inspect_result.reflection;

	if (asize(reflection->inputs) != 0) {
		grain_set_last_error(grain, "Particle module cannot declare input");
		goto fail;
	}

	// Raw sampler declarations would collide with grain's managed bindings
	for (int i = 0; i < asize(reflection->samplers); ++i) {
		const char* sampler_name = reflection->samplers[i].name;
		bool declared = false;
		for (int j = 0; j < asize(samplers); ++j) {
			if (samplers[j] == sampler_name) {
				declared = true;
				break;
			}
		}
		if (!declared) {
			grain_set_last_error(grain, grain_sprintf(
				grain,
				"Sampler `%s` must be declared in a `Samplers` block",
				sampler_name
			));
			goto fail;
		}
	}

	for (int i = 0; i < asize(reflection->uniform_blocks); ++i) {
		const CSPV_ReflectionBlock* uniform_block = &reflection->uniform_blocks[i];
		grain_module_kind_t declared_kind = GRAIN_MODULE_INVALID;
		if (uniform_block->name == emitter_block_name) {
			declared_kind = GRAIN_MODULE_EMITTER;
		} else if (uniform_block->name == affector_block_name) {
			declared_kind = GRAIN_MODULE_AFFECTOR;
		} else if (uniform_block->name == renderer_block_name) {
			declared_kind = GRAIN_MODULE_RENDERER;
		}

		if (declared_kind != GRAIN_MODULE_INVALID) {
			if (uniform_block->num_members != 1) {
				grain_set_last_error(grain, "Invalid module declaration");
				goto fail;
			}
			if (module_kind != GRAIN_MODULE_INVALID) {
				grain_set_last_error(grain, "Multiple module declarations");
				goto fail;
			}

			module_name = reflection->uniform_members[uniform_block->first_member].name;
			module_kind = declared_kind;
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
		grain_set_last_error(grain, "Missing module declaration: `Emitter`, `Affector`, or `Renderer`");
		goto fail;
	}

	grain_dsl_module_info_t* module_info = cf_alloc(sizeof(grain_dsl_module_info_t));
	*module_info = (grain_dsl_module_info_t){
		.name = module_name,
		.kind = module_kind,
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

bool
grain_dsl_compile_archetype(
	grain_t* grain,
	grain_archetype_spec_t spec,
	const char* attrs_source,
	const char* archetype_internal_source,
	const char* update_source,
	const char* render_source,
	grain_dsl_archetype_shaders_t* out
) {
	int num_vfs_entries = spec.num_emitters + spec.num_affectors + 8;
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
		.name = "grain/api.glsl",
		.content = grain_dsl_materialize(grain, XINCBIN_GET(grain_api)),
	};
	vfs_entries[vfs_index++] = (grain_vfs_entry_t){
		.name = "grain/internal.glsl",
		.content = grain_dsl_materialize(grain, XINCBIN_GET(grain_internal)),
	};
	vfs_entries[vfs_index++] = (grain_vfs_entry_t){
		.name = grain_sprintf(grain, "renderer/%s", ((grain_module_t*)spec.renderer)->info->name),
		.content = ((grain_module_t*)spec.renderer)->source,
	};
	vfs_entries[vfs_index++] = (grain_vfs_entry_t){
		.name = "archetype/attrs.glsl",
		.content = attrs_source,
	};
	vfs_entries[vfs_index++] = (grain_vfs_entry_t){
		.name = "archetype/internal.glsl",
		.content = archetype_internal_source,
	};
	vfs_entries[vfs_index++] = (grain_vfs_entry_t){
		.name = "archetype/update.glsl",
		.content = update_source,

	};
	vfs_entries[vfs_index++] = (grain_vfs_entry_t){
		.name = "archetype/render.glsl",
		.content = render_source,

	};
	vfs_entries[vfs_index++] = (grain_vfs_entry_t){ 0 };

	CF_ShaderBytecode update_fs_bytecode = { 0 };
	CF_ShaderBytecode render_vs_bytecode = { 0 };
	CF_ShaderBytecode render_fs_bytecode = { 0 };

	if (!grain_dsl_compile_for_cf(
		grain,
		vfs_entries,
		CSPV_STAGE_FRAGMENT,
		grain_dsl_materialize(grain, XINCBIN_GET(grain_update_fs)),
		&update_fs_bytecode
	)) {
		goto fail;
	}

	if (!grain_dsl_compile_for_cf(
		grain,
		vfs_entries,
		CSPV_STAGE_VERTEX,
		grain_dsl_materialize(grain, XINCBIN_GET(grain_render_vs)),
		&render_vs_bytecode
	)) {
		goto fail;
	}

	if (!grain_dsl_compile_for_cf(
		grain,
		vfs_entries,
		CSPV_STAGE_FRAGMENT,
		grain_dsl_materialize(grain, XINCBIN_GET(grain_render_fs)),
		&render_fs_bytecode
	)) {
		goto fail;
	}

	out->update_frag_bytecode = update_fs_bytecode;
	out->render_vert_bytecode = render_vs_bytecode;
	out->render_frag_bytecode = render_fs_bytecode;
	if (!grain->headless) {
		out->update_shader = cf_make_shader_from_bytecode(grain_update_vert_bytecode, update_fs_bytecode);
		out->render_shader = cf_make_shader_from_bytecode(render_vs_bytecode, render_fs_bytecode);
	}
	return true;

fail:
	grain_dsl_free_bytecode(update_fs_bytecode);
	grain_dsl_free_bytecode(render_vs_bytecode);
	grain_dsl_free_bytecode(render_fs_bytecode);
	return false;
}

#define CUTE_SPIRV_IMPLEMENTATION
#include "cute_spirv.h"

#define XINCBIN_IMPLEMENTATION
#include "resources.rc"
