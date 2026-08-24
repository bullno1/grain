#include "internal.h"
#include "dsl.h"
#include "clock.h"
#include <stdarg.h>
#include <stdio.h>

// RGBA32F
#define GRAIN_TEXTURE_CAPACITY (sizeof(float) * 4)

struct grain_archetype_s {
	grain_archetype_spec_t spec;
	grain_dsl_archetype_shaders_t shaders;
	bool own_bytecode;

	CK_DYNA grain_module_info_t* emitters;
	CK_DYNA grain_module_info_t* affectors;
	        grain_module_info_t  renderer;
	CK_DYNA grain_param_info_t* params;
	CK_DYNA int* params_offsets;

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
};

struct grain_system_s {
	grain_pool_t* pool;
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
};

typedef struct {
	const char* first_decl_module_name;
	const char* first_decl_module_type;
	grain_dsl_var_t var_info;
} grain_attr_info_t;

static void
grain_reset_arena(grain_t* grain) {
	cf_arena_reset(&grain->arena);
}

static void
grain_free_module(grain_module_t* module) {
	if (module == NULL) { return; }

	grain_dsl_free_module_info(module->info);
	cf_free((char*)module->source);
	cf_free(module);
}

static void
grain_free_modules(CK_MAP(grain_module_t*)* module_store) {
	for (int i = 0; i < map_size(*module_store); ++i) {
		grain_free_module((*module_store)[i]);
	}
	map_free(*module_store);
}

static void
grain_cleanup_archetype(grain_archetype_t* archetype) {
	if (archetype->own_bytecode) {
		grain_dsl_free_bytecode(archetype->shaders.update_frag_bytecode);
		grain_dsl_free_bytecode(archetype->shaders.render_vert_bytecode);
		grain_dsl_free_bytecode(archetype->shaders.render_frag_bytecode);
	}
	afree(archetype->emitters);
	afree(archetype->affectors);
	afree(archetype->params);
	afree(archetype->params_offsets);
	cf_destroy_shader(archetype->shaders.update_shader);
	cf_destroy_shader(archetype->shaders.render_shader);
}

static void*
grain_define_module(
	grain_t* grain,
	const char* source,
	CK_MAP(grain_module_t*)* module_store
) {
	grain_reset_arena(grain);

	grain_dsl_module_info_t* module_info = grain_dsl_parse_module(
		grain, source, CSPV_STAGE_FRAGMENT
	);
	if (module_info == NULL) { return NULL; }

	grain_module_t* module = map_get(*module_store, module_info->name);
	if (module == NULL) {
		module = cf_alloc(sizeof(grain_module_t));
		memset(module, 0, sizeof(*module));
		map_set(*module_store, module_info->name, module);
	} else {
		cf_free(module->source);
		grain_dsl_free_module_info(module->info);
	}

	size_t source_len = strlen(source);
	module->source = cf_alloc(source_len + 1);
	memcpy(module->source, source, source_len + 1);
	module->info = module_info;

	return module;
}

static bool
grain_collect_attributes(
	grain_t* grain,
	CK_MAP(grain_attr_info_t)* attributes,
	const grain_module_t* module,
	const char* module_type
) {
	for (int i = 0; i < asize(module->info->particle_attrs); ++i) {
		grain_dsl_var_t var_info = module->info->particle_attrs[i];
		grain_attr_info_t* attr_info = map_get_ptr(*attributes, var_info.name);
		if (attr_info == NULL) {
			grain_attr_info_t new_attr = {
				.var_info = var_info,
				.first_decl_module_name = module->info->name,
				.first_decl_module_type = module_type,
			};
			map_set(*attributes, var_info.name, new_attr);
		} else {
			if (attr_info->var_info.type != var_info.type) {
				grain_set_last_error(
					grain,
					grain_sprintf(
						grain,
						"Particle attribute `%s` has conflicting type between %s `%s` and %s `%s`",
						var_info.name,
						attr_info->first_decl_module_type, attr_info->first_decl_module_name,
						module_type, module->info->name
					)
				);
				return false;
			}
		}
	}

	return true;
}

static const char*
grain_type_name(CSPV_DataType type) {
	switch (type) {
		case CSPV_TYPE_UNKNOWN: return "";
		case CSPV_TYPE_SINT:    return "int";
		case CSPV_TYPE_UINT:    return "uint";
		case CSPV_TYPE_FLOAT:   return "float";
		case CSPV_TYPE_SINT2:   return "ivec2";
		case CSPV_TYPE_UINT2:   return "uvec2";
		case CSPV_TYPE_FLOAT2:  return "vec2";
		case CSPV_TYPE_SINT3:   return "ivec3";
		case CSPV_TYPE_UINT3:   return "uvec3";
		case CSPV_TYPE_FLOAT3:  return "vec3";
		case CSPV_TYPE_SINT4:   return "ivec4";
		case CSPV_TYPE_UINT4:   return "uvec4";
		case CSPV_TYPE_FLOAT4:  return "vec4";
		case CSPV_TYPE_MAT4:    return "mat4";
		default: return "";
	}
}

static int
grain_type_size(CSPV_DataType type) {
	switch (type) {
		case CSPV_TYPE_UNKNOWN: return 0;
		case CSPV_TYPE_SINT:
		case CSPV_TYPE_UINT:
		case CSPV_TYPE_FLOAT:
			return sizeof(float);
		case CSPV_TYPE_SINT2:
		case CSPV_TYPE_UINT2:
		case CSPV_TYPE_FLOAT2:
			return sizeof(float) * 2;
		case CSPV_TYPE_SINT3:
		case CSPV_TYPE_UINT3:
		case CSPV_TYPE_FLOAT3:
			return sizeof(float) * 3;
		case CSPV_TYPE_SINT4:
		case CSPV_TYPE_UINT4:
		case CSPV_TYPE_FLOAT4:
			return sizeof(float) * 4;
		case CSPV_TYPE_MAT4:
			return sizeof(float) * 16;
		default: return 0;
	}
}

static int
grain_type_alignment(CSPV_DataType type) {
	switch (type) {
		case CSPV_TYPE_UNKNOWN:
			return 0;
		case CSPV_TYPE_SINT:
		case CSPV_TYPE_UINT:
		case CSPV_TYPE_FLOAT:
			return sizeof(float);
		case CSPV_TYPE_SINT2:
		case CSPV_TYPE_UINT2:
		case CSPV_TYPE_FLOAT2:
			return sizeof(float) * 2;
		case CSPV_TYPE_SINT3:
		case CSPV_TYPE_UINT3:
		case CSPV_TYPE_FLOAT3:
		case CSPV_TYPE_SINT4:
		case CSPV_TYPE_UINT4:
		case CSPV_TYPE_FLOAT4:
		case CSPV_TYPE_MAT4:
			return sizeof(float) * 4;
		default:
			return 0;
	}
}

static void
grain_pack_element(
	char** shader,
	int* lane_idx_ptr,
	const char* name, const char* suffix
) {
	int lane_idx = *lane_idx_ptr;

	const char* elem_name = NULL;
	switch (lane_idx % 4) {
		case 0: elem_name = "x"; break;
		case 1: elem_name = "y"; break;
		case 2: elem_name = "z"; break;
		case 3: elem_name = "w"; break;
	}

	sfmt_append(
		*shader,
		"\tgrain_convert(grain_packed[%d].%s, unpacked.%s%s);\n",
		lane_idx / 4, elem_name,
		name, suffix
	);

	*lane_idx_ptr = lane_idx + 1;
}

static void
grain_pack_attr(char** shader, int* lane_idx, grain_dsl_var_t attr_info) {
	switch (attr_info.type) {
		case CSPV_TYPE_SINT:
		case CSPV_TYPE_UINT:
		case CSPV_TYPE_FLOAT:
			grain_pack_element(shader, lane_idx, attr_info.name, "");
			break;
		case CSPV_TYPE_SINT2:
		case CSPV_TYPE_UINT2:
		case CSPV_TYPE_FLOAT2:
			grain_pack_element(shader, lane_idx, attr_info.name, ".x");
			grain_pack_element(shader, lane_idx, attr_info.name, ".y");
			break;
		case CSPV_TYPE_SINT3:
		case CSPV_TYPE_UINT3:
		case CSPV_TYPE_FLOAT3:
			grain_pack_element(shader, lane_idx, attr_info.name, ".x");
			grain_pack_element(shader, lane_idx, attr_info.name, ".y");
			grain_pack_element(shader, lane_idx, attr_info.name, ".z");
			break;
		case CSPV_TYPE_SINT4:
		case CSPV_TYPE_UINT4:
		case CSPV_TYPE_FLOAT4:
			grain_pack_element(shader, lane_idx, attr_info.name, ".x");
			grain_pack_element(shader, lane_idx, attr_info.name, ".y");
			grain_pack_element(shader, lane_idx, attr_info.name, ".z");
			grain_pack_element(shader, lane_idx, attr_info.name, ".w");
			break;
		case CSPV_TYPE_MAT4:
			grain_pack_element(shader, lane_idx, attr_info.name, "[0].x");
			grain_pack_element(shader, lane_idx, attr_info.name, "[0].y");
			grain_pack_element(shader, lane_idx, attr_info.name, "[0].z");
			grain_pack_element(shader, lane_idx, attr_info.name, "[0].w");
			grain_pack_element(shader, lane_idx, attr_info.name, "[1].x");
			grain_pack_element(shader, lane_idx, attr_info.name, "[1].y");
			grain_pack_element(shader, lane_idx, attr_info.name, "[1].z");
			grain_pack_element(shader, lane_idx, attr_info.name, "[1].w");
			grain_pack_element(shader, lane_idx, attr_info.name, "[2].x");
			grain_pack_element(shader, lane_idx, attr_info.name, "[2].y");
			grain_pack_element(shader, lane_idx, attr_info.name, "[2].z");
			grain_pack_element(shader, lane_idx, attr_info.name, "[2].w");
			grain_pack_element(shader, lane_idx, attr_info.name, "[3].x");
			grain_pack_element(shader, lane_idx, attr_info.name, "[3].y");
			grain_pack_element(shader, lane_idx, attr_info.name, "[3].z");
			grain_pack_element(shader, lane_idx, attr_info.name, "[3].w");
			break;
		default:
			break;
	}
}

static void
grain_unpack_element(
	char** shader,
	const char* prefix, const char* name, const char* suffix,
	int* lane_idx_ptr
) {
	int lane_idx = *lane_idx_ptr;

	const char* elem_name = NULL;
	switch (lane_idx % 4) {
		case 0: elem_name = "x"; break;
		case 1: elem_name = "y"; break;
		case 2: elem_name = "z"; break;
		case 3: elem_name = "w"; break;
	}

	sfmt_append(
		*shader,
		"\tgrain_convert(%s%s%s, grain_packed[%d].%s);\n",
		prefix, name, suffix,
		lane_idx / 4, elem_name
	);

	*lane_idx_ptr = lane_idx + 1;
}

static void
grain_unpack_attr(char** shader, char* prefix, int* lane_idx, grain_dsl_var_t attr_info) {
	switch (attr_info.type) {
		case CSPV_TYPE_SINT:
		case CSPV_TYPE_UINT:
		case CSPV_TYPE_FLOAT:
			grain_unpack_element(shader, prefix, attr_info.name, "", lane_idx);
			break;
		case CSPV_TYPE_SINT2:
		case CSPV_TYPE_UINT2:
		case CSPV_TYPE_FLOAT2:
			grain_unpack_element(shader, prefix, attr_info.name, ".x", lane_idx);
			grain_unpack_element(shader, prefix, attr_info.name, ".y", lane_idx);
			break;
		case CSPV_TYPE_SINT3:
		case CSPV_TYPE_UINT3:
		case CSPV_TYPE_FLOAT3:
			grain_unpack_element(shader, prefix, attr_info.name, ".x", lane_idx);
			grain_unpack_element(shader, prefix, attr_info.name, ".y", lane_idx);
			grain_unpack_element(shader, prefix, attr_info.name, ".z", lane_idx);
			break;
		case CSPV_TYPE_SINT4:
		case CSPV_TYPE_UINT4:
		case CSPV_TYPE_FLOAT4:
			grain_unpack_element(shader, prefix, attr_info.name, ".x", lane_idx);
			grain_unpack_element(shader, prefix, attr_info.name, ".y", lane_idx);
			grain_unpack_element(shader, prefix, attr_info.name, ".z", lane_idx);
			grain_unpack_element(shader, prefix, attr_info.name, ".w", lane_idx);
			break;
		case CSPV_TYPE_MAT4:
			grain_unpack_element(shader, prefix, attr_info.name, "[0].x", lane_idx);
			grain_unpack_element(shader, prefix, attr_info.name, "[0].y", lane_idx);
			grain_unpack_element(shader, prefix, attr_info.name, "[0].z", lane_idx);
			grain_unpack_element(shader, prefix, attr_info.name, "[0].w", lane_idx);
			grain_unpack_element(shader, prefix, attr_info.name, "[1].x", lane_idx);
			grain_unpack_element(shader, prefix, attr_info.name, "[1].y", lane_idx);
			grain_unpack_element(shader, prefix, attr_info.name, "[1].z", lane_idx);
			grain_unpack_element(shader, prefix, attr_info.name, "[1].w", lane_idx);
			grain_unpack_element(shader, prefix, attr_info.name, "[2].x", lane_idx);
			grain_unpack_element(shader, prefix, attr_info.name, "[2].y", lane_idx);
			grain_unpack_element(shader, prefix, attr_info.name, "[2].z", lane_idx);
			grain_unpack_element(shader, prefix, attr_info.name, "[2].w", lane_idx);
			grain_unpack_element(shader, prefix, attr_info.name, "[3].x", lane_idx);
			grain_unpack_element(shader, prefix, attr_info.name, "[3].y", lane_idx);
			grain_unpack_element(shader, prefix, attr_info.name, "[3].z", lane_idx);
			grain_unpack_element(shader, prefix, attr_info.name, "[3].w", lane_idx);
			break;
		default:
			break;
	}
}

grain_t*
grain_create(void) {
	grain_t* grain = cf_alloc(sizeof(grain_t));

	*grain = (grain_t){
		.arena = cf_make_arena(16, 64 * 1024),
		.dummy_mesh = cf_make_mesh(4, &(CF_VertexAttribute){}, 0, 0),
	};

	return grain;
}

void
grain_destroy(grain_t* grain) {
	if (grain == NULL) { return; }

	grain_free_modules(&grain->emitters);
	grain_free_modules(&grain->affectors);
	grain_free_modules(&grain->renderers);

	for (int i = 0; i < map_size(grain->archetypes); ++i) {
		grain_archetype_t* archetype = grain->archetypes[i];
		grain_cleanup_archetype(archetype);
		cf_free(archetype);
	}
	map_free(grain->archetypes);

	cf_destroy_arena(&grain->arena);
	cf_destroy_mesh(grain->dummy_mesh);
	cf_free(grain);
}

const char*
grain_get_last_error(grain_t* grain) {
	return grain->last_error;
}

grain_emitter_t*
grain_define_emitter(grain_t* grain, const char* source) {
	return grain_define_module(grain, source, &grain->emitters);
}

grain_affector_t*
grain_define_affector(grain_t* grain, const char* source) {
	return grain_define_module(grain, source, &grain->affectors);
}

grain_renderer_t*
grain_define_renderer(grain_t* grain, const char* source) {
	grain_dsl_module_info_t* vsh_info = grain_dsl_parse_module(
		grain, source, CSPV_STAGE_VERTEX
	);
	if (vsh_info == NULL) {
		return NULL;
	}
	grain_dsl_free_module_info(vsh_info);

	return grain_define_module(grain, source, &grain->renderers);
}

grain_archetype_t*
grain_define_archetype(grain_t* grain, const char* name, grain_archetype_spec_t spec) {
	grain_reset_arena(grain);

	grain_archetype_t* archetype = NULL;
	char* archetype_common = NULL;
	char* archetype_update = NULL;
	char* archetype_render = NULL;

	// Common shader file

	// Collect attributes
	CK_MAP(grain_attr_info_t) particle_attrs = NULL;
	for (int i = 0; i < spec.num_emitters; ++i) {
		if (!grain_collect_attributes(
			grain, &particle_attrs, (grain_module_t*)spec.emitters[i], "emitter"
		)) {
			goto fail;
		}
	}
	for (int i = 0; i < spec.num_affectors; ++i) {
		if (!grain_collect_attributes(
			grain, &particle_attrs, (grain_module_t*)spec.affectors[i], "affector"
		)) {
			goto fail;
		}
	}
	if (!grain_collect_attributes(
		grain, &particle_attrs, (grain_module_t*)spec.renderer, "renderer"
	)) {
		goto fail;
	}

	// Inject birth attribute
	int birth_lane = 0;
	for (int i = 0; i < map_size(particle_attrs); ++i) {
		birth_lane += grain_type_size(particle_attrs[i].var_info.type) / (int)sizeof(float);
	}
	const char* birth_attr_name = sintern("grain_birth");
	grain_attr_info_t birth_attr = {
		.var_info = { .name = birth_attr_name, .type = CSPV_TYPE_FLOAT },
		.first_decl_module_name = "grain",
		.first_decl_module_type = "internal",
	};
	map_set(particle_attrs, birth_attr_name, birth_attr);

	// Attribute type
	sappend(archetype_common, "struct ParticleAttrs {\n");
	for (int i = 0; i < map_size(particle_attrs); ++i) {
		sfmt_append(
			archetype_common,
			"\t %s %s;\n",
			grain_type_name(particle_attrs[i].var_info.type),
			particle_attrs[i].var_info.name
		);
	}
	sappend(archetype_common, "};\n");

	// FNV-1a over the ordered attribute list.
	uint64_t attr_layout_hash = 0xcbf29ce484222325ull;
	for (int i = 0; i < map_size(particle_attrs); ++i) {
		for (const char* c = particle_attrs[i].var_info.name; *c; ++c) {
			attr_layout_hash = (attr_layout_hash ^ (uint64_t)(unsigned char)*c) * 0x100000001b3ull;
		}
		attr_layout_hash = (attr_layout_hash ^ (uint64_t)particle_attrs[i].var_info.type) * 0x100000001b3ull;
	}

	int attr_total_size = 0;
	for (int i = 0; i < map_size(particle_attrs); ++i) {
		attr_total_size += grain_type_size(particle_attrs[i].var_info.type);
	}
	int num_textures = (attr_total_size + GRAIN_TEXTURE_CAPACITY - 1) / GRAIN_TEXTURE_CAPACITY;
	if (num_textures > CF_MAX_CANVAS_TARGETS) {
		grain_set_last_error(grain, "Archetype requires too many storage tetures");
		goto fail;
	}

	// Attribute pack/unpack
	sappend(archetype_common, "\n");
	for (int i = 0; i < num_textures; ++i) {
		sfmt_append(
			archetype_common,
			"layout(set = GRAIN_SAMPLER_SET, binding = %d) uniform sampler2D grain_texture_%d;\n",
			i, i
		);
	}

	sappend(archetype_common, "\n");
	sfmt_append(archetype_common, "void grain_pack_ParticleAttrs(inout vec4[%d] grain_packed, ParticleAttrs unpacked) {\n", num_textures);
	int pack_lane_idx = 0;
	for (int i = 0; i < map_size(particle_attrs); ++i) {
		grain_pack_attr(&archetype_common, &pack_lane_idx, particle_attrs[i].var_info);
	}
	sappend    (archetype_common, "}\n");

	sappend(archetype_common, "\n");
	sfmt_append(archetype_common, "ParticleAttrs grain_unpack_ParticleAttrs(vec4[%d] grain_packed) {\n", num_textures);
	sappend    (archetype_common, "\tParticleAttrs unpacked;\n");
	int unpack_lane_idx = 0;
	for (int i = 0; i < map_size(particle_attrs); ++i) {
		grain_unpack_attr(&archetype_common, "unpacked.", &unpack_lane_idx, particle_attrs[i].var_info);
	}
	sappend    (archetype_common, "\treturn unpacked;\n");
	sappend    (archetype_common, "}\n");

	sappend    (archetype_common, "\n");
	sappend    (archetype_common, "ParticleAttrs grain_load_ParticleAttrs(ivec2 texel) {\n");
	sfmt_append(archetype_common, "\tvec4[%d] grain_packed;\n", num_textures);
	for (int i = 0; i < num_textures; ++i) {
		sfmt_append(archetype_common, "\tgrain_packed[%d] = texelFetch(grain_texture_%d, texel, 0);\n", i, i);
	}
	sappend    (archetype_common, "\treturn grain_unpack_ParticleAttrs(grain_packed);\n");
	sappend    (archetype_common, "}\n");

	// Update shader
	sappend(archetype_update, "#include \"internal/builtins.glsl\"\n");
	sappend(archetype_update, "#include \"archetype/common.glsl\"\n");

	// Import update modules
	sappend(archetype_update, "\n");
	sappend(archetype_update, "#define Module(X)\n");
	sappend(archetype_update, "#define Requires(X)\n");
	for (int i = 0; i < spec.num_emitters; ++i) {
		grain_module_t* module = (grain_module_t*)spec.emitters[i];
		sfmt_append(archetype_update, "#define Params(X) struct %s_%s_ModuleParams { %s };\n", "emitter", module->info->name, asize(module->info->module_params) != 0 ? "X" : "int grain_ignore;");
		sfmt_append(archetype_update, "#define ModuleParams %s_%s_ModuleParams\n", "emitter", module->info->name);
		sfmt_append(archetype_update, "#define process %s_%s_process\n", "emitter", module->info->name);
		sfmt_append(archetype_update, "#include \"emitter/%s\"\n", module->info->name);
		sappend    (archetype_update, "#undef process\n");
		sappend    (archetype_update, "#undef ModuleParams\n");
		sappend    (archetype_update, "#undef Params\n");
	}
	for (int i = 0; i < spec.num_affectors; ++i) {
		grain_module_t* module = (grain_module_t*)spec.affectors[i];
		sfmt_append(archetype_update, "#define Params(X) struct %s_%s_ModuleParams { %s };\n", "affector", module->info->name, asize(module->info->module_params) != 0 ? "X" : "int grain_ignore;");
		sfmt_append(archetype_update, "#define ModuleParams %s_%s_ModuleParams\n", "affector", module->info->name);
		sfmt_append(archetype_update, "#define process %s_%s_process\n", "affector", module->info->name);
		sfmt_append(archetype_update, "#include \"affector/%s\"\n", module->info->name);
		sappend    (archetype_update, "#undef process\n");
		sappend    (archetype_update, "#undef ModuleParams\n");
		sappend    (archetype_update, "#undef Params\n");
	}
	sappend(archetype_update, "#include \"archetype/common.glsl\"\n");

	// SystemParams
	sappend(archetype_update, "\n");
	sappend(archetype_update, "struct SystemParams {\n");
	int offset = 0;
	for (int i = 0; i < spec.num_emitters; ++i) {
		grain_module_t* module = (grain_module_t*)spec.emitters[i];
		for (int j = 0; j < asize(module->info->module_params); ++j) {
			grain_dsl_var_t var = module->info->module_params[j];
			sfmt_append(archetype_update, "\t%s emitter_%d_%s;\n", grain_type_name(var.type), i, var.name);

			int alignment = grain_type_alignment(var.type);
			offset = ((offset + alignment - 1) / alignment) * alignment;
			offset += grain_type_size(var.type);
		}
	}
	for (int i = 0; i < spec.num_affectors; ++i) {
		grain_module_t* module = (grain_module_t*)spec.affectors[i];
		for (int j = 0; j < asize(module->info->module_params); ++j) {
			grain_dsl_var_t var = module->info->module_params[j];
			sfmt_append(archetype_update, "\t%s affector_%d_%s;\n", grain_type_name(var.type), i, var.name);

			int alignment = grain_type_alignment(var.type);
			offset = ((offset + alignment - 1) / alignment) * alignment;
			offset += grain_type_size(var.type);
		}
	}
	// Padding to multiple of 16
	int padded_size = ((offset + 16 - 1) / GRAIN_TEXTURE_CAPACITY) * GRAIN_TEXTURE_CAPACITY;
	for (int i = 0; i < (padded_size - offset) / 4; ++i) {
		sfmt_append(archetype_update, "\tfloat grain_padding_%d;\n", i);
	}
	int update_size = padded_size;
	sappend(archetype_update, "};\n");

	// Loading system data
	sappend    (archetype_update, "\n");
	sappend    (archetype_update, "#ifdef CF_GLES\n");
	sfmt_append(archetype_update, "layout(std430, set = GRAIN_SAMPLER_SET, binding = %d) readonly buffer grain_system_params { uvec4 grain_system_params[]; };\n", num_textures + 0);
	sfmt_append(archetype_update, "layout(std430, set = GRAIN_SAMPLER_SET, binding = %d) readonly buffer grain_system_clocks { uvec4 grain_system_clocks[]; };\n", num_textures + 1);

	// Load system params from the emulated buffer
	sappend(archetype_update, "SystemParams grain_load_SystemParams(uint i) {\n");

	// Calculate size
	int num_uvec4 = padded_size / GRAIN_TEXTURE_CAPACITY;
	sfmt_append(archetype_update, "\tuvec4[%d] grain_packed;\n", num_uvec4);
	for (int i = 0; i < num_uvec4; ++i) {
		sfmt_append(archetype_update, "\tgrain_packed[%d] = grain_system_params[i * %d + %d];\n", i, num_uvec4, i);
	}

	sappend(archetype_update, "\n");
	sappend(archetype_update, "\tSystemParams params;\n");
	offset = 0;
	for (int i = 0; i < spec.num_emitters; ++i) {
		grain_module_t* module = (grain_module_t*)spec.emitters[i];
		if (asize(module->info->module_params) != 0) {
			sappend(archetype_update, "\t{\n");
			char unpack_prefix[1024];
			snprintf(unpack_prefix, sizeof(unpack_prefix), "params.emitter_%d_", i);
			for (int j = 0; j < asize(module->info->module_params); ++j) {
				grain_dsl_var_t var = module->info->module_params[j];
				int alignment = grain_type_alignment(var.type);
				offset = ((offset + alignment - 1) / alignment) * alignment;

				int index = offset / sizeof(float);
				grain_unpack_attr(&archetype_update, unpack_prefix, &index, var);

				offset += grain_type_size(var.type);
			}
			sappend(archetype_update, "\t}\n");
		}
	}
	for (int i = 0; i < spec.num_affectors; ++i) {
		grain_module_t* module = (grain_module_t*)spec.affectors[i];
		if (asize(module->info->module_params) != 0) {
			sappend(archetype_update, "\t{\n");
			char unpack_prefix[1024];
			snprintf(unpack_prefix, sizeof(unpack_prefix), "\t\tparams.affector_%d_", i);
			for (int j = 0; j < asize(module->info->module_params); ++j) {
				grain_dsl_var_t var = module->info->module_params[j];
				int alignment = grain_type_alignment(var.type);
				offset = ((offset + alignment - 1) / alignment) * alignment;

				int index = offset / sizeof(float);
				grain_unpack_attr(&archetype_update, unpack_prefix, &index, var);

				offset += grain_type_size(var.type);
			}
			sappend(archetype_update, "\t}\n");
		}
	}
	sappend(archetype_update, "\treturn params;\n");
	sappend(archetype_update, "}\n");

	sappend    (archetype_update, "SystemClock grain_load_SystemClock(uint i) {\n");
	sappend    (archetype_update, "\treturn grain_unpack_SystemClock(grain_system_clocks[i * 2u], grain_system_clocks[i * 2u + 1u]);\n");
	sappend    (archetype_update, "}\n");
	sappend    (archetype_update, "#else\n");
	sfmt_append(archetype_update, "layout(std430, set = GRAIN_SAMPLER_SET, binding = %d) readonly buffer grain_system_params { SystemParams grain_system_params[]; };\n", num_textures + 0);
	sfmt_append(archetype_update, "layout(std430, set = GRAIN_SAMPLER_SET, binding = %d) readonly buffer grain_system_clocks { SystemClock grain_system_clocks[]; };\n", num_textures + 1);
	sappend    (archetype_update, "SystemParams grain_load_SystemParams(uint i) {\n");
	sappend    (archetype_update, "\treturn grain_system_params[i];\n");
	sappend    (archetype_update, "}\n");
	sappend    (archetype_update, "SystemClock grain_load_SystemClock(uint i) {\n");
	sappend    (archetype_update, "\treturn grain_system_clocks[i];\n");
	sappend    (archetype_update, "}\n");
	sappend    (archetype_update, "#endif\n");

	sappend(archetype_update, "\n");
	for (int i = 0; i < num_textures; ++i) {
		sfmt_append(archetype_update, "layout(location = %d) out vec4 grain_output_%d;\n", i, i);
	}

	sappend(archetype_update, "\n");
	sappend    (archetype_update, "void grain_store(ParticleAttrs particle) {\n");
	sfmt_append(archetype_update, "\tvec4[%d] grain_packed;\n", num_textures);
	sappend    (archetype_update, "\tgrain_pack_ParticleAttrs(grain_packed, particle);\n");
	for (int i = 0; i < num_textures; ++i) {
		sfmt_append(archetype_update, "\tgrain_output_%d = grain_packed[%d];\n", i, i);
	}
	sappend(archetype_update, "}\n");

	sappend(archetype_update, "\n");
	sappend(archetype_update, "void grain_emit(inout ParticleAttrs particle, SystemParams params, Ctx ctx) {\n");
	for (int i = 0; i < spec.num_emitters; ++i) {
		grain_module_t* module = (grain_module_t*)spec.emitters[i];
		sappend    (archetype_update, "\t{\n");
		sfmt_append(archetype_update, "\t\temitter_%s_ModuleParams module_params;\n", module->info->name);
		for (int j = 0; j < asize(module->info->module_params); ++j) {
			grain_dsl_var_t var = module->info->module_params[j];
			sfmt_append(archetype_update, "\t\tmodule_params.%s = params.emitter_%d_%s;\n", var.name, i, var.name);
		}
		sfmt_append(archetype_update, "\t\temitter_%s_process(particle, module_params, ctx);\n", module->info->name);
		sappend    (archetype_update, "\t}\n");
	}
	sappend(archetype_update, "}\n");

	sappend(archetype_update, "\n");
	sappend(archetype_update, "void grain_process(inout ParticleAttrs particle, SystemParams params, Ctx ctx) {\n");
	for (int i = 0; i < spec.num_affectors; ++i) {
		grain_module_t* module = (grain_module_t*)spec.affectors[i];
		sappend    (archetype_update, "\t{\n");
		sfmt_append(archetype_update, "\t\taffector_%s_ModuleParams module_params;\n", module->info->name);
		for (int j = 0; j < asize(module->info->module_params); ++j) {
			grain_dsl_var_t var = module->info->module_params[j];
			sfmt_append(archetype_update, "\t\tmodule_params.%s = params.affector_%d_%s;\n", var.name, i, var.name);
		}
		sfmt_append(archetype_update, "\t\taffector_%s_process(particle, module_params, ctx);\n", module->info->name);
		sappend    (archetype_update, "\t}\n");
	}
	sappend(archetype_update, "}\n");

	// Render shader
	sappend(archetype_render, "#include \"internal/builtins.glsl\"\n");
	sappend(archetype_render, "#include \"archetype/common.glsl\"\n");

	// ModuleParams
	sappend(archetype_render, "\n");
	sappend(archetype_render, "struct ModuleParams {\n");
	offset = 0;
	grain_module_t* render_module = (grain_module_t*)spec.renderer;
	for (int j = 0; j < asize(render_module->info->module_params); ++j) {
		grain_dsl_var_t var = render_module->info->module_params[j];
		sfmt_append(archetype_render, "\t%s %s;\n", grain_type_name(var.type), var.name);

		int alignment = grain_type_alignment(var.type);
		offset = ((offset + alignment - 1) / alignment) * alignment;
		offset += grain_type_size(var.type);
	}
	// Padding to multiple of 16
	padded_size = ((offset + 16 - 1) / GRAIN_TEXTURE_CAPACITY) * GRAIN_TEXTURE_CAPACITY;
	for (int i = 0; i < (padded_size - offset) / 4; ++i) {
		sfmt_append(archetype_render, "\tfloat grain_padding_%d;\n", i);
	}
	int render_size = padded_size;
	sappend(archetype_render, "};\n");

	// Import module
	sappend    (archetype_render, "#define Module(X)\n");
	sappend    (archetype_render, "#define Requires(X)\n");
	sappend    (archetype_render, "#define Params(X)\n");
	sfmt_append(archetype_render, "#include \"renderer/%s\"\n", render_module->info->name);

	// SSBO-s
	sappend    (archetype_render, "\n");
	sappend    (archetype_render, "#ifdef CF_GLES\n");
	sfmt_append(archetype_render, "layout(std430, set = GRAIN_SAMPLER_SET, binding = %d) readonly buffer grain_system_params { uvec4 grain_system_params[]; };\n", num_textures + 0);
	sfmt_append(archetype_render, "layout(std430, set = GRAIN_SAMPLER_SET, binding = %d) readonly buffer grain_system_clocks { uvec4 grain_system_clocks[]; };\n", num_textures + 1);
	sfmt_append(archetype_render, "layout(std430, set = GRAIN_SAMPLER_SET, binding = %d) readonly buffer grain_draw_list { uvec4 grain_draw_list[]; };\n", num_textures + 2);

	// Load system params from the emulated buffer
	sappend(archetype_render, "ModuleParams grain_load_ModuleParams(uint i) {\n");
	// Calculate size
	num_uvec4 = padded_size / GRAIN_TEXTURE_CAPACITY;
	sfmt_append(archetype_render, "\tuvec4[%d] grain_packed;\n", num_uvec4);
	for (int i = 0; i < num_uvec4; ++i) {
		sfmt_append(archetype_render, "\tgrain_packed[%d] = grain_system_params[i * %d + %d];\n", i, num_uvec4, i);
	}

	sappend(archetype_render, "\n");
	sappend(archetype_render, "\tModuleParams params;\n");
	offset = 0;
	if (asize(render_module->info->module_params) != 0) {
		for (int j = 0; j < asize(render_module->info->module_params); ++j) {
			grain_dsl_var_t var = render_module->info->module_params[j];
			int alignment = grain_type_alignment(var.type);
			offset = ((offset + alignment - 1) / alignment) * alignment;

			int index = offset / sizeof(float);
			grain_unpack_attr(&archetype_render, "params.", &index, var);

			offset += grain_type_size(var.type);
		}
	}
	sappend(archetype_render, "\treturn params;\n");
	sappend(archetype_render, "}\n");

	sappend    (archetype_render, "SystemClock grain_load_SystemClock(uint i) {\n");
	sappend    (archetype_render, "\treturn grain_unpack_SystemClock(grain_system_clocks[i * 2u], grain_system_clocks[i * 2u + 1u]);\n");
	sappend    (archetype_render, "}\n");
	sappend    (archetype_render, "uint grain_load_draw_region(uint i) {\n");
	sappend    (archetype_render, "\treturn grain_draw_list[i / 4][i % 4];\n");
	sappend    (archetype_render, "}\n");
	sappend    (archetype_render, "#else\n");
	sfmt_append(archetype_render, "layout(std430, set = GRAIN_SAMPLER_SET, binding = %d) readonly buffer grain_module_params { ModuleParams grain_system_params[]; };\n", num_textures + 0);
	sfmt_append(archetype_render, "layout(std430, set = GRAIN_SAMPLER_SET, binding = %d) readonly buffer grain_system_clocks { SystemClock grain_system_clocks[]; };\n", num_textures + 1);
	sfmt_append(archetype_render, "layout(std430, set = GRAIN_SAMPLER_SET, binding = %d) readonly buffer grain_draw_list { uint grain_draw_list[]; };\n", num_textures + 2);
	sappend    (archetype_render, "ModuleParams grain_load_ModuleParams(uint i) {\n");
	sappend    (archetype_render, "\treturn grain_system_params[i];\n");
	sappend    (archetype_render, "}\n");
	sappend    (archetype_render, "SystemClock grain_load_SystemClock(uint i) {\n");
	sappend    (archetype_render, "\treturn grain_system_clocks[i];\n");
	sappend    (archetype_render, "}\n");
	sappend    (archetype_render, "uint grain_load_draw_region(uint i) {\n");
	sappend    (archetype_render, "\treturn grain_draw_list[i];\n");
	sappend    (archetype_render, "}\n");
	sappend    (archetype_render, "#endif\n");

	/*printf("// Common\n%s\n", archetype_common);*/
	/*printf("// Update\n%s\n", archetype_update);*/
	/*printf("// Render\n%s\n", archetype_render);*/

	grain_dsl_archetype_shaders_t shaders = { 0 };
	if (!grain_dsl_compile_archetype(
		grain,
		spec,
		archetype_common, archetype_update, archetype_render,
		&shaders
	)) {
		goto fail;
	}

	const char* interned_name = sintern(name);
	archetype = map_get(grain->archetypes, interned_name);
	uint32_t revision;
	if (archetype == NULL) {
		archetype = cf_alloc(sizeof(grain_archetype_t));
		map_set(grain->archetypes, interned_name, archetype);
		revision = 1;
	} else {
		grain_cleanup_archetype(archetype);
		revision = archetype->revision + 1;
	}

	*archetype = (grain_archetype_t){
		.spec = spec,
		.shaders = shaders,
		.own_bytecode = true,

		.num_textures = num_textures,
		.update_size = update_size,
		.render_size = render_size,
		.birth_texture = birth_lane / 4,
		.birth_channel = birth_lane % 4,
		.revision = revision,
		.attr_layout_hash = attr_layout_hash,
	};

	// Calculate param offsets
	offset = 0;
	for (int i = 0; i < spec.num_emitters; ++i) {
		grain_module_t* module = (grain_module_t*)spec.emitters[i];
		grain_module_info_t module_info = {
			.name = module->info->name,
			.first_params = asize(archetype->params),
			.num_params = asize(module->info->module_params),
		};
		apush(archetype->emitters, module_info);
		for (int j = 0; j < asize(module->info->module_params); ++j) {
			grain_dsl_var_t var = module->info->module_params[j];

			int alignment = grain_type_alignment(var.type);
			offset = ((offset + alignment - 1) / alignment) * alignment;

			grain_param_info_t param_info = {
				.name = var.name,
				.type = (CF_ShaderInfoDataType)var.type,
			};
			apush(archetype->params, param_info);
			apush(archetype->params_offsets, offset);

			offset += grain_type_size(var.type);
		}
	}
	for (int i = 0; i < spec.num_affectors; ++i) {
		grain_module_t* module = (grain_module_t*)spec.affectors[i];
		grain_module_info_t module_info = {
			.name = module->info->name,
			.first_params = asize(archetype->params),
			.num_params = asize(module->info->module_params),
		};
		apush(archetype->affectors, module_info);
		for (int j = 0; j < asize(module->info->module_params); ++j) {
			grain_dsl_var_t var = module->info->module_params[j];

			int alignment = grain_type_alignment(var.type);
			offset = ((offset + alignment - 1) / alignment) * alignment;

			grain_param_info_t param_info = {
				.name = var.name,
				.type = (CF_ShaderInfoDataType)var.type,
			};
			apush(archetype->params, param_info);
			apush(archetype->params_offsets, offset);

			offset += grain_type_size(var.type);
		}
	}

	offset = 0;
	archetype->renderer = (grain_module_info_t){
		.name = render_module->info->name,
		.first_params = asize(archetype->params),
		.num_params = asize(render_module->info->module_params),
	};
	for (int j = 0; j < asize(render_module->info->module_params); ++j) {
		grain_dsl_var_t var = render_module->info->module_params[j];

		int alignment = grain_type_alignment(var.type);
		offset = ((offset + alignment - 1) / alignment) * alignment;

		grain_param_info_t param_info = {
			.name = var.name,
			.type = (CF_ShaderInfoDataType)var.type,
		};
		apush(archetype->params, param_info);
		apush(archetype->params_offsets, offset);

		offset += grain_type_size(var.type);
	}
fail:
	sfree(archetype_common);
	sfree(archetype_update);
	sfree(archetype_render);
	map_free(particle_attrs);
	return archetype;
}

grain_archetype_info_t
grain_inspect_archetype(grain_archetype_t* archetype) {
	return (grain_archetype_info_t){
		.emitters = archetype->emitters,
		.num_emitters = asize(archetype->emitters),
		.affectors = archetype->affectors,
		.num_affectors = asize(archetype->affectors),
		.renderer = archetype->renderer,
		.params = archetype->params,
	};
}

void
grain_set_last_error(grain_t* grain, const char* message) {
	grain->last_error = message;
}

GRAIN_FORMAT_ATTRIBUTE(2, 3)
const char*
grain_sprintf(grain_t* grain, const char* msg, ...) {
	static char buf[1024];

	va_list arg, arg_copy;
	va_start(arg, msg);
	va_copy(arg_copy, arg);

	int len = vsnprintf(buf, sizeof(buf), msg, arg);
	char* output = cf_arena_alloc(&grain->arena, len + 1);
	if (len < (int)sizeof(buf)) {
		memcpy(output, buf, len + 1);
	} else {
		snprintf(output, len + 1, msg, arg_copy);
	}

	va_end(arg_copy);
	va_end(arg);

	return output;
}

const char*
grain_strcpy(grain_t* grain, const char* str) {
	if (str == NULL) { return NULL; }

	size_t len = strlen(str);
	char* copy = cf_arena_alloc(&grain->arena, len + 1);
	memcpy(copy, str, len + 1);
	return copy;
}

static void
grain_init_ssbo(grain_ssbo_t* ssbo, int size) {
	ssbo->gpu = cf_make_storage_buffer(cf_storage_buffer_defaults(size));
	ssbo->cpu = cf_alloc(size);
	ssbo->dirty = false;
}

static void
grain_cleanup_ssbo(grain_ssbo_t* ssbo) {
	cf_destroy_storage_buffer(ssbo->gpu);
	cf_free(ssbo->cpu);
}

static void
grain_sync_ssbo(grain_ssbo_t* ssbo, int size) {
	if (!ssbo->dirty) { return; }

	cf_update_storage_buffer(ssbo->gpu, ssbo->cpu, size);
	ssbo->dirty = false;
}

static void*
grain_index_ssbo(grain_ssbo_t* ssbo, int item_size, int index) {
	char* cpu_mem = (char*)ssbo->cpu + item_size * index;
	ssbo->dirty = true;
	return cpu_mem;
}

static void
grain_capture_layout_from_modules(
	grain_param_layout_t* layout,
	grain_archetype_t* archetype,
	grain_module_info_t* modules,
	int num_modules
) {
	for (int module_index = 0; module_index < asize(modules); ++module_index) {
		grain_module_info_t* module = &modules[module_index];
		for (int param_index = 0; param_index < module->num_params; ++param_index) {
			grain_param_info_t param = archetype->params[module->first_params + param_index];
			apush(layout->slots, ((grain_param_slot_t){
				.module_name = module->name,
				.name = param.name,
				.type = param.type,
				.offset = archetype->params_offsets[module->first_params + param_index],
				.size = grain_type_size((CSPV_DataType)param.type),
			}));
		}
	}
}

static void
grain_capture_layout(grain_param_layout_t* layout, grain_archetype_t* archetype, bool render) {
	layout->stride = render ? archetype->render_size : archetype->update_size;

	if (render) {
		grain_capture_layout_from_modules(layout, archetype, &archetype->renderer, 1);
	} else {
		grain_capture_layout_from_modules(layout, archetype, archetype->emitters, asize(archetype->emitters));
		grain_capture_layout_from_modules(layout, archetype, archetype->affectors, asize(archetype->affectors));
	}
}

static void
grain_migrate_ssbo(
	grain_ssbo_t* ssbo,
	const grain_param_layout_t* old_layout,
	const grain_param_layout_t* new_layout,
	int max_systems
) {
	char* old_cpu = ssbo->cpu;
	char* new_cpu = cf_alloc(new_layout->stride * max_systems);

	for (int new_slot_index = 0; new_slot_index < asize(new_layout->slots); ++new_slot_index) {
		const grain_param_slot_t* dst = &new_layout->slots[new_slot_index];
		// Find the matching param and copy
		for (int old_slot_index = 0; old_slot_index < asize(old_layout->slots); ++old_slot_index) {
			const grain_param_slot_t* src = &old_layout->slots[old_slot_index];
			if (
				src->module_name == dst->module_name
				&&
				src->name == dst->name  // interned
				&&
				src->type == dst->type
			) {
				for (int system_index = 0; system_index < max_systems; ++system_index) {
					memcpy(
						new_cpu + system_index * new_layout->stride + dst->offset,
						old_cpu + system_index * old_layout->stride + src->offset,
						dst->size
					);
				}
				break;
			}
		}
	}

	cf_destroy_storage_buffer(ssbo->gpu);
	cf_free(old_cpu);
	ssbo->gpu = cf_make_storage_buffer(cf_storage_buffer_defaults(new_layout->stride * max_systems));
	ssbo->cpu = new_cpu;
	ssbo->dirty = true;
}

static void
grain_make_pool_canvases(grain_pool_t* pool) {
	grain_archetype_t* archetype = pool->opts.archetype;

	CF_CanvasParams canvas_params = cf_canvas_defaults(pool->pool_size, pool->opts.max_systems);
	canvas_params.target_count = archetype->num_textures;
	for (int i = 0; i < archetype->num_textures; ++i) {
		canvas_params.targets[i].allocate_mipmaps = false;
		canvas_params.targets[i].filter = CF_FILTER_NEAREST;
		canvas_params.targets[i].wrap_u = CF_WRAP_MODE_CLAMP_TO_EDGE;
		canvas_params.targets[i].wrap_v = CF_WRAP_MODE_CLAMP_TO_EDGE;
		canvas_params.targets[i].pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
	}
	pool->canvases[0] = cf_make_canvas(canvas_params);
	pool->canvases[1] = cf_make_canvas(canvas_params);

	// A negative birth time marks a slot as never born. Everything else clears to zero
	// so stale VRAM can never masquerade as a particle.
	CF_Color never_born = { 0 };
	((float*)&never_born)[archetype->birth_channel] = -1.f;
	for (int i = 0; i < 2; ++i) {
		cf_canvas_set_clear_color2(pool->canvases[i], archetype->birth_texture, never_born);
		cf_clear_canvas(pool->canvases[i]);
	}
}

static void
grain_reconcile_pool(grain_pool_t* pool) {
	grain_archetype_t* archetype = pool->opts.archetype;
	if (pool->archetype_revision == archetype->revision) { return; }

	grain_param_layout_t new_update = { 0 };
	grain_param_layout_t new_render = { 0 };
	grain_capture_layout(&new_update, archetype, false);
	grain_capture_layout(&new_render, archetype, true);

	// Migrate system parameters
	grain_migrate_ssbo(&pool->update_ssbo, &pool->update_layout, &new_update, pool->opts.max_systems);
	grain_migrate_ssbo(&pool->render_ssbo, &pool->render_layout, &new_render, pool->opts.max_systems);

	afree(pool->update_layout.slots);
	afree(pool->render_layout.slots);
	pool->update_layout = new_update;
	pool->render_layout = new_render;

	// If attribute layout changed, reset all particles
	if (pool->attr_layout_hash != archetype->attr_layout_hash) {
		cf_destroy_canvas(pool->canvases[0]);
		cf_destroy_canvas(pool->canvases[1]);
		grain_make_pool_canvases(pool);
		pool->attr_layout_hash = archetype->attr_layout_hash;
	}

	pool->archetype_revision = archetype->revision;
}

grain_pool_t*
grain_create_pool(grain_t* grain, grain_pool_opts_t opts) {
	if (opts.max_emission_rate <= 0.f || opts.lifetime_budget <= 0.f) {
		grain_set_last_error(grain, "max_emission_rate and lifetime_budget must be positive");
		return NULL;
	}
	if ((double)opts.lifetime_budget > GRAIN_MAX_LIFETIME_BUDGET) {
		grain_set_last_error(grain, grain_sprintf(
			grain, "lifetime_budget of %.1fs exceeds the %.1fs float precision budget",
			opts.lifetime_budget, GRAIN_MAX_LIFETIME_BUDGET
		));
		return NULL;
	}

	int pool_size = (int)ceil((double)opts.max_emission_rate * (double)opts.lifetime_budget);
	if (pool_size < 1) { pool_size = 1; }

	grain_pool_t* pool = cf_alloc(sizeof(grain_pool_t));
	*pool = (grain_pool_t){
		.grain = grain,
		.opts = opts,
		.pool_size = pool_size,
	};

	grain_init_ssbo(&pool->update_ssbo, opts.archetype->update_size * opts.max_systems);
	grain_init_ssbo(&pool->render_ssbo, opts.archetype->render_size * opts.max_systems);
	grain_init_ssbo(&pool->clock_ssbo, sizeof(grain_clock_entry_t) * opts.max_systems);
	grain_init_ssbo(&pool->draw_list, sizeof(uint32_t) * opts.max_systems);

	grain_make_pool_canvases(pool);
	grain_capture_layout(&pool->update_layout, opts.archetype, false);
	grain_capture_layout(&pool->render_layout, opts.archetype, true);
	pool->archetype_revision = opts.archetype->revision;
	pool->attr_layout_hash = opts.archetype->attr_layout_hash;

	pool->material = cf_make_material();
	cf_material_set_uniform_vs(pool->material, "grain_pool_size", &pool_size, CF_UNIFORM_TYPE_INT, 1);
	cf_material_set_uniform_fs(pool->material, "grain_pool_size", &pool_size, CF_UNIFORM_TYPE_INT, 1);
	// TODO: allow override
	CF_RenderState render_state = cf_render_state_defaults();
	render_state.primitive_type = CF_PRIMITIVE_TYPE_TRIANGLESTRIP;
	cf_material_set_render_state(pool->material, render_state);

	pool->systems = cf_alloc(sizeof(grain_system_t) * opts.max_systems);
	memset(pool->systems, 0, sizeof(grain_system_t) * opts.max_systems);

	pool->clocks = cf_alloc(sizeof(grain_particle_clock_t) * opts.max_systems);

	return pool;
}

void
grain_destroy_pool(grain_pool_t* pool) {
	afree(pool->update_layout.slots);
	afree(pool->render_layout.slots);
	cf_free(pool->clocks);
	cf_free(pool->systems);
	cf_destroy_material(pool->material);
	cf_destroy_canvas(pool->canvases[0]);
	cf_destroy_canvas(pool->canvases[1]);
	grain_cleanup_ssbo(&pool->update_ssbo);
	grain_cleanup_ssbo(&pool->render_ssbo);
	grain_cleanup_ssbo(&pool->clock_ssbo);
	grain_cleanup_ssbo(&pool->draw_list);
	cf_free(pool);
}

grain_system_t*
grain_create_system(grain_pool_t* pool) {
	for (int i = 0; i < pool->opts.max_systems; ++i) {
		if (pool->systems[i].pool == NULL) {
			grain_system_t* system = &pool->systems[i];
			system->pool = pool;
			grain_init_clock(&pool->clocks[i], pool->opts.lifetime_budget, 0.0);
			return system;
		}
	}

	return NULL;
}

void
grain_destroy_system(grain_system_t* system) {
	if (system == NULL) { return; }

	system->pool = NULL;
}

grain_archetype_t*
grain_get_archetype(grain_system_t* system) {
	return system->pool->opts.archetype;
}

static void
grain_touch(grain_pool_t* pool) {
	// Link pool to update list
	if (!pool->queued_for_update) {
		grain_t* grain = pool->grain;
		pool->update_next = grain->update_list;
		grain->update_list = pool;
		pool->queued_for_update = true;
	}
}

void
grain_begin_update(grain_t* grain) {
	grain->update_list = NULL;
}

void
grain_tick(grain_system_t* system, float dt_s) {
	grain_pool_t* pool = system->pool;
	int index = system - pool->systems;

	grain_advance_clock(&pool->clocks[index], dt_s);
	grain_touch(system->pool);
}

void
grain_set_emission_rate(grain_system_t* system, float particles_per_second) {
	grain_pool_t* pool = system->pool;
	int index = system - pool->systems;

	// The pool was sized for max_emission_rate; exceeding it would recycle live slots.
	if (particles_per_second > pool->opts.max_emission_rate) {
		particles_per_second = pool->opts.max_emission_rate;
	}
	if (particles_per_second < 0.f) { particles_per_second = 0.f; }

	grain_set_clock_rate(&pool->clocks[index], particles_per_second);
}

static int
grain_find_system_hwm(grain_pool_t* pool) {
	for (int i = pool->opts.max_systems - 1; i >= 0; --i) {
		if (pool->systems[i].pool != NULL) {
			return i;
		}
	}
	return 0;
}

static void
grain_update_pool(grain_t* grain, grain_pool_t* pool) {
	grain_reconcile_pool(pool);

	int system_hwm = grain_find_system_hwm(pool);
	grain_sync_ssbo(&pool->update_ssbo, (system_hwm + 1) * pool->opts.archetype->update_size);

	// Each system's window is everything since its last snapshot. A system that did
	// not tick gets dt = 0 and an empty window, so this pass leaves it exactly as is.
	for (int i = 0; i <= system_hwm; ++i) {
		grain_clock_entry_t* entry = grain_index_ssbo(&pool->clock_ssbo, sizeof(grain_clock_entry_t), i);
		if (pool->systems[i].pool != NULL) {
			*entry = grain_snapshot_clock(&pool->clocks[i], pool->pool_size);
		} else {
			*entry = (grain_clock_entry_t){ 0 };
		}
	}
	grain_sync_ssbo(&pool->clock_ssbo, (system_hwm + 1) * sizeof(grain_clock_entry_t));

	CF_Canvas src_canvas = pool->canvases[pool->pingpong ? 0 : 1];
	CF_Canvas dst_canvas = pool->canvases[pool->pingpong ? 1 : 0];

	cf_apply_canvas(dst_canvas, true);
	cf_apply_mesh(grain->dummy_mesh);
	for (int i = 0; i < pool->opts.archetype->num_textures; ++i) {
		char name[256];
		snprintf(name, sizeof(name), "grain_texture_%d", i);
		cf_material_set_texture_vs(pool->material, name, cf_canvas_get_target2(src_canvas, i));
		cf_material_set_texture_fs(pool->material, name, cf_canvas_get_target2(src_canvas, i));
	}
	cf_apply_shader(pool->opts.archetype->shaders.update_shader, pool->material);
	CF_StorageBuffer fs_buffers[] = {
		pool->update_ssbo.gpu,
		pool->clock_ssbo.gpu,
	};
	cf_apply_fs_storage_buffers(fs_buffers, CF_ARRAY_SIZE(fs_buffers));
	cf_apply_scissor(0, 0, pool->pool_size, system_hwm + 1);
	cf_draw_elements_range(0, 3, 1);

	pool->pingpong = !pool->pingpong;
}

static bool
grain_find_param(
	grain_archetype_t* archetype,
	grain_module_info_t* module,
	const char* name,
	int* offset, int* size
) {
	const char* interned_name = sintern(name);
	for (int i = 0; i < module->num_params; ++i) {
		grain_param_info_t param_info = archetype->params[module->first_params + i];
		if (param_info.name == interned_name) {
			*offset = archetype->params_offsets[module->first_params + i];
			*size = grain_type_size((CSPV_DataType)param_info.type);
			return true;
		}
	}

	return false;
}

static grain_ssbo_t*
grain_resolve_param(grain_system_t* system, int param_index, void** value) {
	grain_pool_t* pool = system->pool;
	grain_archetype_t* archetype = pool->opts.archetype;
	if (param_index < 0 || param_index >= asize(archetype->params)) { return NULL; }

	grain_reconcile_pool(pool);

	bool render = param_index >= archetype->renderer.first_params;
	grain_ssbo_t* ssbo = render ? &pool->render_ssbo : &pool->update_ssbo;
	if (value != NULL) {
		int stride = render ? archetype->render_size : archetype->update_size;
		int index = system - pool->systems;
		*value = (char*)ssbo->cpu + stride * index + archetype->params_offsets[param_index];
	}
	return ssbo;
}

void*
grain_get_parameter(grain_system_t* system, int param_index) {
	void* value = NULL;
	grain_resolve_param(system, param_index, &value);
	return value;
}

void
grain_parameter_modified(grain_system_t* system, int param_index) {
	grain_ssbo_t* ssbo = grain_resolve_param(system, param_index, NULL);
	if (ssbo != NULL) { ssbo->dirty = true; }
}

void
grain_set_emitter_parameter(
	grain_system_t* system,
	int emitter_index,
	const char* name,
	const void* value
) {
	grain_pool_t* pool = system->pool;
	int index = system - pool->systems;
	grain_archetype_t* archetype = pool->opts.archetype;
	if (emitter_index >= asize(archetype->emitters)) { return; }
	grain_reconcile_pool(pool);

	int param_offset;
	int	param_size;
	if (!grain_find_param(
		archetype,
		&archetype->emitters[emitter_index],
		name,
		&param_offset, &param_size
	)) {
		return;
	}

	char* update_params = grain_index_ssbo(&pool->update_ssbo, archetype->update_size, index);
	memcpy(update_params + param_offset, value, param_size);
}

void
grain_set_affector_parameter(
	grain_system_t* system,
	int affector_index,
	const char* name,
	const void* value
) {
	grain_pool_t* pool = system->pool;
	int index = system - pool->systems;
	grain_archetype_t* archetype = pool->opts.archetype;
	if (affector_index >= asize(archetype->affectors)) { return; }
	grain_reconcile_pool(pool);

	int param_offset;
	int	param_size;
	if (!grain_find_param(
		archetype,
		&archetype->affectors[affector_index],
		name,
		&param_offset, &param_size
	)) {
		return;
	}

	char* update_params = grain_index_ssbo(&pool->update_ssbo, archetype->update_size, index);
	memcpy(update_params + param_offset, value, param_size);
}

void
grain_set_renderer_parameter(
	grain_system_t* system,
	const char* name,
	const void* value
) {
	grain_pool_t* pool = system->pool;
	int index = system - pool->systems;
	grain_archetype_t* archetype = pool->opts.archetype;
	grain_reconcile_pool(pool);

	int param_offset;
	int	param_size;
	if (!grain_find_param(
		archetype,
		&archetype->renderer,
		name,
		&param_offset, &param_size
	)) {
		return;
	}

	char* render_params = grain_index_ssbo(&pool->render_ssbo, archetype->render_size, index);
	memcpy(render_params + param_offset, value, param_size);
}

void
grain_end_update(grain_t* grain) {
	for (
		grain_pool_t* itr = grain->update_list;
		itr != NULL;
	) {
		grain_update_pool(grain, itr);

		grain_pool_t* next = itr->update_next;
		itr->update_next = NULL;
		itr->queued_for_update = false;
		itr = next;
	}

	grain->update_list = NULL;
}

void
grain_begin_render(grain_t* grain) {
	grain->render_list = NULL;
}

static CF_M3x2
grain_current_transform(void) {
	int w, h;
	cf_app_get_size(&w, &h);
	CF_M3x2 projection = cf_ortho_2d(0.f, 0.f, (float)w, (float)h);
	return cf_mul_m32(projection, cf_draw_peek());
}

static void
grain_transform_to_mat4(CF_M3x2 transform, float* out) {
	out[0]  = transform.m.x.x; out[1]  = transform.m.x.y; out[2]  = 0.f; out[3]  = 0.f;
	out[4]  = transform.m.y.x; out[5]  = transform.m.y.y; out[6]  = 0.f; out[7]  = 0.f;
	out[8]  = 0.f;             out[9]  = 0.f;             out[10] = 1.f; out[11] = 0.f;
	out[12] = transform.p.x;   out[13] = transform.p.y;   out[14] = 0.f; out[15] = 1.f;
}

void
grain_render(grain_system_t* system) {
	grain_pool_t* pool = system->pool;
	grain_t* grain = pool->grain;
	int index = system - pool->systems;

	uint32_t* draw_list_item = grain_index_ssbo(&pool->draw_list, sizeof(uint32_t), pool->num_draws++);
	*draw_list_item = index;

	if (!pool->queued_for_render) {
		pool->render_next = grain->render_list;
		grain->render_list = pool;
		pool->queued_for_render = true;
	}
}

static void
grain_render_pool(grain_t* grain, grain_pool_t* pool, CF_M3x2 transform) {
	grain_reconcile_pool(pool);

	int system_hwm = grain_find_system_hwm(pool);
	grain_sync_ssbo(&pool->render_ssbo, pool->opts.archetype->render_size * (system_hwm + 1));
	grain_sync_ssbo(&pool->draw_list, sizeof(uint32_t) * pool->num_draws);

	cf_apply_mesh(grain->dummy_mesh);
	CF_Canvas src_canvas = pool->canvases[pool->pingpong ? 0 : 1];
	for (int i = 0; i < pool->opts.archetype->num_textures; ++i) {
		char name[256];
		snprintf(name, sizeof(name), "grain_texture_%d", i);
		cf_material_set_texture_vs(pool->material, name, cf_canvas_get_target2(src_canvas, i));
		cf_material_set_texture_fs(pool->material, name, cf_canvas_get_target2(src_canvas, i));
	}

	float transform_mat4[16];
	grain_transform_to_mat4(transform, transform_mat4);
	cf_material_set_uniform_vs(pool->material, "grain_transform", transform_mat4, CF_UNIFORM_TYPE_MAT4, 1);

	cf_apply_shader(pool->opts.archetype->shaders.render_shader, pool->material);
	CF_StorageBuffer storage_buffers[] = {
		pool->render_ssbo.gpu,
		pool->clock_ssbo.gpu,
		pool->draw_list.gpu,
	};
	cf_apply_vs_storage_buffers(storage_buffers, CF_ARRAY_SIZE(storage_buffers));
	cf_apply_fs_storage_buffers(storage_buffers, CF_ARRAY_SIZE(storage_buffers));
	cf_draw_elements_range(0, 4, pool->num_draws * pool->pool_size);

	pool->num_draws = 0;
}

void
grain_end_render(grain_t* grain) {
	CF_M3x2 transform = grain_current_transform();

	for (
		grain_pool_t* itr = grain->render_list;
		itr != NULL;
	) {
		grain_render_pool(grain, itr, transform);

		grain_pool_t* next = itr->render_next;
		itr->render_next = NULL;
		itr->queued_for_render = false;
		itr = next;
	}

	grain->render_list = NULL;
}
