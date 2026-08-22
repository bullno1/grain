#include "internal.h"
#include "dsl.h"
#include <stdarg.h>
#include <stdio.h>

// RGBA32UI
#define GRAIN_TEXTURE_CAPACITY (sizeof(float) * 4)

struct grain_archetype_s {
	grain_archetype_spec_t spec;

	CF_ShaderBytecode update_shader_bytecode;
	CF_ShaderBytecode render_shader_vs_bytecode;
	CF_ShaderBytecode render_shader_fs_bytecode;

	CF_Shader update_shader;
	CF_Shader render_shader;
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
	} else {
		cf_free(module->source);
		grain_dsl_free_module_info(module->info);
	}

	size_t source_len = strlen(source);
	module->source = cf_alloc(source_len + 1);
	memcpy(module->source, source, source_len + 1);
	module->info = module_info;
	map_set(*module_store, module_info->name, module);

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
	const char* packer,
	const char* name, const char* name_suffix
) {
	int lane_idx = *lane_idx_ptr;
	const char* elem_name = NULL;
	switch (lane_idx % 4) {
		case 0: elem_name = "x"; break;
		case 1: elem_name = "y"; break;
		case 2: elem_name = "z"; break;
		case 3: elem_name = "w"; break;
	}

	sfmt_append(*shader, "\tpacked[%d].%s = %s(unpacked.%s%s);\n", lane_idx / 4, elem_name, packer, name, name_suffix);

	*lane_idx_ptr = lane_idx + 1;
}

static void
grain_pack_attr(char** shader, int* lane_idx, grain_dsl_var_t attr_info) {
	switch (attr_info.type) {
		case CSPV_TYPE_SINT:
		case CSPV_TYPE_UINT:
			grain_pack_element(shader, lane_idx, "uint", attr_info.name, "");
			break;
		case CSPV_TYPE_FLOAT:
			grain_pack_element(shader, lane_idx, "floatBitsToUint", attr_info.name, "");
			break;
		case CSPV_TYPE_SINT2:
		case CSPV_TYPE_UINT2:
			grain_pack_element(shader, lane_idx, "uint", attr_info.name, ".x");
			grain_pack_element(shader, lane_idx, "uint", attr_info.name, ".y");
			break;
		case CSPV_TYPE_FLOAT2:
			grain_pack_element(shader, lane_idx, "floatBitsToUint", attr_info.name, ".x");
			grain_pack_element(shader, lane_idx, "floatBitsToUint", attr_info.name, ".y");
			break;
		case CSPV_TYPE_SINT3:
		case CSPV_TYPE_UINT3:
			grain_pack_element(shader, lane_idx, "uint", attr_info.name, ".x");
			grain_pack_element(shader, lane_idx, "uint", attr_info.name, ".y");
			grain_pack_element(shader, lane_idx, "uint", attr_info.name, ".z");
			break;
		case CSPV_TYPE_FLOAT3:
			grain_pack_element(shader, lane_idx, "floatBitsToUint", attr_info.name, ".x");
			grain_pack_element(shader, lane_idx, "floatBitsToUint", attr_info.name, ".y");
			grain_pack_element(shader, lane_idx, "floatBitsToUint", attr_info.name, ".z");
			break;
		case CSPV_TYPE_SINT4:
		case CSPV_TYPE_UINT4:
			grain_pack_element(shader, lane_idx, "uint", attr_info.name, ".x");
			grain_pack_element(shader, lane_idx, "uint", attr_info.name, ".y");
			grain_pack_element(shader, lane_idx, "uint", attr_info.name, ".z");
			grain_pack_element(shader, lane_idx, "uint", attr_info.name, ".w");
			break;
		case CSPV_TYPE_FLOAT4:
			grain_pack_element(shader, lane_idx, "floatBitsToUint", attr_info.name, ".x");
			grain_pack_element(shader, lane_idx, "floatBitsToUint", attr_info.name, ".y");
			grain_pack_element(shader, lane_idx, "floatBitsToUint", attr_info.name, ".z");
			grain_pack_element(shader, lane_idx, "floatBitsToUint", attr_info.name, ".w");
			break;
		case CSPV_TYPE_MAT4:
			grain_pack_element(shader, lane_idx, "floatBitsToUint", attr_info.name, "[0].x");
			grain_pack_element(shader, lane_idx, "floatBitsToUint", attr_info.name, "[0].y");
			grain_pack_element(shader, lane_idx, "floatBitsToUint", attr_info.name, "[0].z");
			grain_pack_element(shader, lane_idx, "floatBitsToUint", attr_info.name, "[0].w");
			grain_pack_element(shader, lane_idx, "floatBitsToUint", attr_info.name, "[1].x");
			grain_pack_element(shader, lane_idx, "floatBitsToUint", attr_info.name, "[1].y");
			grain_pack_element(shader, lane_idx, "floatBitsToUint", attr_info.name, "[1].z");
			grain_pack_element(shader, lane_idx, "floatBitsToUint", attr_info.name, "[1].w");
			grain_pack_element(shader, lane_idx, "floatBitsToUint", attr_info.name, "[2].x");
			grain_pack_element(shader, lane_idx, "floatBitsToUint", attr_info.name, "[2].y");
			grain_pack_element(shader, lane_idx, "floatBitsToUint", attr_info.name, "[2].z");
			grain_pack_element(shader, lane_idx, "floatBitsToUint", attr_info.name, "[2].w");
			grain_pack_element(shader, lane_idx, "floatBitsToUint", attr_info.name, "[3].x");
			grain_pack_element(shader, lane_idx, "floatBitsToUint", attr_info.name, "[3].y");
			grain_pack_element(shader, lane_idx, "floatBitsToUint", attr_info.name, "[3].z");
			grain_pack_element(shader, lane_idx, "floatBitsToUint", attr_info.name, "[3].w");
			break;
		default:
			break;
	}
}

static void
grain_unpack_element(
	char** shader,
	const char* prefix,
	int* lane_idx_ptr,
	const char* unpacker,
	const char* name, const char* name_suffix
) {
	int lane_idx = *lane_idx_ptr;
	const char* elem_name = NULL;
	switch (lane_idx % 4) {
		case 0: elem_name = "x"; break;
		case 1: elem_name = "y"; break;
		case 2: elem_name = "z"; break;
		case 3: elem_name = "w"; break;
	}

	sfmt_append(*shader, "%s%s%s = %s(packed[%d].%s);\n", prefix, name, name_suffix, unpacker, lane_idx / 4, elem_name);

	*lane_idx_ptr = lane_idx + 1;
}

static void
grain_unpack_attr(char** shader, char* prefix, int* lane_idx, grain_dsl_var_t attr_info) {
	switch (attr_info.type) {
		case CSPV_TYPE_SINT:
			grain_unpack_element(shader, prefix, lane_idx, "int", attr_info.name, "");
			break;
		case CSPV_TYPE_UINT:
			grain_unpack_element(shader, prefix, lane_idx, "uint", attr_info.name, "");
			break;
		case CSPV_TYPE_FLOAT:
			grain_unpack_element(shader, prefix, lane_idx, "uintBitsToFloat", attr_info.name, "");
			break;
		case CSPV_TYPE_SINT2:
			grain_unpack_element(shader, prefix, lane_idx, "int", attr_info.name, ".x");
			grain_unpack_element(shader, prefix, lane_idx, "int", attr_info.name, ".y");
			break;
		case CSPV_TYPE_UINT2:
			grain_unpack_element(shader, prefix, lane_idx, "uint", attr_info.name, ".x");
			grain_unpack_element(shader, prefix, lane_idx, "uint", attr_info.name, ".y");
			break;
		case CSPV_TYPE_FLOAT2:
			grain_unpack_element(shader, prefix, lane_idx, "uintBitsToFloat", attr_info.name, ".x");
			grain_unpack_element(shader, prefix, lane_idx, "uintBitsToFloat", attr_info.name, ".y");
			break;
		case CSPV_TYPE_SINT3:
			grain_unpack_element(shader, prefix, lane_idx, "int", attr_info.name, ".x");
			grain_unpack_element(shader, prefix, lane_idx, "int", attr_info.name, ".y");
			grain_unpack_element(shader, prefix, lane_idx, "int", attr_info.name, ".z");
			break;
		case CSPV_TYPE_UINT3:
			grain_unpack_element(shader, prefix, lane_idx, "uint", attr_info.name, ".x");
			grain_unpack_element(shader, prefix, lane_idx, "uint", attr_info.name, ".y");
			grain_unpack_element(shader, prefix, lane_idx, "uint", attr_info.name, ".z");
			break;
		case CSPV_TYPE_FLOAT3:
			grain_unpack_element(shader, prefix, lane_idx, "uintBitsToFloat", attr_info.name, ".x");
			grain_unpack_element(shader, prefix, lane_idx, "uintBitsToFloat", attr_info.name, ".y");
			grain_unpack_element(shader, prefix, lane_idx, "uintBitsToFloat", attr_info.name, ".z");
			break;
		case CSPV_TYPE_SINT4:
			grain_unpack_element(shader, prefix, lane_idx, "int", attr_info.name, ".x");
			grain_unpack_element(shader, prefix, lane_idx, "int", attr_info.name, ".y");
			grain_unpack_element(shader, prefix, lane_idx, "int", attr_info.name, ".z");
			grain_unpack_element(shader, prefix, lane_idx, "int", attr_info.name, ".w");
			break;
		case CSPV_TYPE_UINT4:
			grain_unpack_element(shader, prefix, lane_idx, "uint", attr_info.name, ".x");
			grain_unpack_element(shader, prefix, lane_idx, "uint", attr_info.name, ".y");
			grain_unpack_element(shader, prefix, lane_idx, "uint", attr_info.name, ".z");
			grain_unpack_element(shader, prefix, lane_idx, "uint", attr_info.name, ".w");
			break;
		case CSPV_TYPE_FLOAT4:
			grain_unpack_element(shader, prefix, lane_idx, "uintBitsToFloat", attr_info.name, ".x");
			grain_unpack_element(shader, prefix, lane_idx, "uintBitsToFloat", attr_info.name, ".y");
			grain_unpack_element(shader, prefix, lane_idx, "uintBitsToFloat", attr_info.name, ".z");
			grain_unpack_element(shader, prefix, lane_idx, "uintBitsToFloat", attr_info.name, ".w");
			break;
		case CSPV_TYPE_MAT4:
			grain_unpack_element(shader, prefix, lane_idx, "uintBitsToFloat", attr_info.name, "[0].x");
			grain_unpack_element(shader, prefix, lane_idx, "uintBitsToFloat", attr_info.name, "[0].y");
			grain_unpack_element(shader, prefix, lane_idx, "uintBitsToFloat", attr_info.name, "[0].z");
			grain_unpack_element(shader, prefix, lane_idx, "uintBitsToFloat", attr_info.name, "[0].w");
			grain_unpack_element(shader, prefix, lane_idx, "uintBitsToFloat", attr_info.name, "[1].x");
			grain_unpack_element(shader, prefix, lane_idx, "uintBitsToFloat", attr_info.name, "[1].y");
			grain_unpack_element(shader, prefix, lane_idx, "uintBitsToFloat", attr_info.name, "[1].z");
			grain_unpack_element(shader, prefix, lane_idx, "uintBitsToFloat", attr_info.name, "[1].w");
			grain_unpack_element(shader, prefix, lane_idx, "uintBitsToFloat", attr_info.name, "[2].x");
			grain_unpack_element(shader, prefix, lane_idx, "uintBitsToFloat", attr_info.name, "[2].y");
			grain_unpack_element(shader, prefix, lane_idx, "uintBitsToFloat", attr_info.name, "[2].z");
			grain_unpack_element(shader, prefix, lane_idx, "uintBitsToFloat", attr_info.name, "[2].w");
			grain_unpack_element(shader, prefix, lane_idx, "uintBitsToFloat", attr_info.name, "[3].x");
			grain_unpack_element(shader, prefix, lane_idx, "uintBitsToFloat", attr_info.name, "[3].y");
			grain_unpack_element(shader, prefix, lane_idx, "uintBitsToFloat", attr_info.name, "[3].z");
			grain_unpack_element(shader, prefix, lane_idx, "uintBitsToFloat", attr_info.name, "[3].w");
			break;
		default:
			break;
	}
}

grain_t*
grain_create(void) {
	grain_t* grain = cf_alloc(sizeof(grain_t));

	*grain = (grain_t){
		.arena = cf_make_arena(16, 4096),
		.dummy_mesh = cf_make_mesh(0, &(CF_VertexAttribute){}, 0, 0),
	};

	return grain;
}

void
grain_destroy(grain_t* grain) {
	if (grain == NULL) { return; }

	grain_free_modules(&grain->emitters);
	grain_free_modules(&grain->affectors);
	grain_free_modules(&grain->renderers);

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
			"layout(set = GRAIN_SAMPLER_SET, binding = %d) uniform usampler2D grain__texture_%d;\n",
			i, i
		);
	}

	sappend(archetype_common, "\n");
	sfmt_append(archetype_common, "void grain__pack_ParticleAttrs(out uvec4[%d] packed, ParticleAttrs unpacked) {\n", num_textures);
	int pack_lane_idx = 0;
	for (int i = 0; i < map_size(particle_attrs); ++i) {
		grain_pack_attr(&archetype_common, &pack_lane_idx, particle_attrs[i].var_info);
	}
	sappend    (archetype_common, "}\n");

	sappend(archetype_common, "\n");
	sfmt_append(archetype_common, "ParticleAttrs grain__unpack_ParticleAttrs(uvec4[%d] packed) {\n", num_textures);
	sappend    (archetype_common, "\tParticleAttrs unpacked;\n");
	int unpack_lane_idx = 0;
	for (int i = 0; i < map_size(particle_attrs); ++i) {
		grain_unpack_attr(&archetype_common, "\tunpacked.", &unpack_lane_idx, particle_attrs[i].var_info);
	}
	sappend    (archetype_common, "\treturn unpacked;\n");
	sappend    (archetype_common, "}\n");

	sappend    (archetype_common, "\n");
	sappend    (archetype_common, "ParticleAttrs grain__load_ParticleAttrs(ivec2 texel) {\n");
	sfmt_append(archetype_common, "\tuvec4[%d] packed;\n", num_textures);
	for (int i = 0; i < num_textures; ++i) {
		sfmt_append(archetype_common, "\tpacked[%d] = texelFetch(grain__texture_%d, texel, 0);\n", i, i);
	}
	sappend    (archetype_common, "\treturn grain__unpack_ParticleAttrs(packed);\n");
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
		sfmt_append(archetype_update, "#define Params(X) struct %s_%s_ModuleParams { %s };\n", "emitter", module->info->name, asize(module->info->module_params) != 0 ? "X" : "int grain__ingore;");
		sfmt_append(archetype_update, "#define ModuleParams %s_%s_ModuleParams\n", "emitter", module->info->name);
		sfmt_append(archetype_update, "#define process %s_%s_process\n", "emitter", module->info->name);
		sfmt_append(archetype_update, "#include \"emitter/%s\"\n", module->info->name);
		sappend    (archetype_update, "#undef process\n");
		sappend    (archetype_update, "#undef ModuleParams\n");
		sappend    (archetype_update, "#undef Params\n");
	}
	for (int i = 0; i < spec.num_affectors; ++i) {
		grain_module_t* module = (grain_module_t*)spec.affectors[i];
		sfmt_append(archetype_update, "#define Params(X) struct %s_%s_ModuleParams { %s };\n", "affector", module->info->name, asize(module->info->module_params) != 0 ? "X" : "int grain__ingore;");
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
		sfmt_append(archetype_update, "\tfloat grain__padding_%d;\n", i);
	}
	sappend(archetype_update, "};\n");

	// Loading system data
	sappend    (archetype_update, "\n");
	sappend    (archetype_update, "#ifdef CF_GLES\n");
	sfmt_append(archetype_update, "layout(std430, set = GRAIN_SAMPLER_SET, binding = %d) readonly buffer grain__system_params { uvec4 grain__system_params[]; };\n", num_textures + 0);
	sfmt_append(archetype_update, "layout(std430, set = GRAIN_SAMPLER_SET, binding = %d) readonly buffer grain__system_clocks { uvec4 grain__system_clocks[]; };\n", num_textures + 1);

	// Load system params from the emulated buffer
	sappend(archetype_update, "SystemParams grain__load_SystemParams(uint i) {\n");

	// Calculate size
	int num_uvec4 = padded_size / GRAIN_TEXTURE_CAPACITY;
	sfmt_append(archetype_update, "\tuvec4[%d] packed;\n", num_uvec4);
	for (int i = 0; i < num_uvec4; ++i) {
		sfmt_append(archetype_update, "\tpacked[%d] = grain__system_params[i * %d + %d];\n", i, num_uvec4, i);
	}

	sappend(archetype_update, "\n");
	sappend(archetype_update, "\tSystemParams params;\n");
	offset = 0;
	for (int i = 0; i < spec.num_emitters; ++i) {
		grain_module_t* module = (grain_module_t*)spec.emitters[i];
		if (asize(module->info->module_params) != 0) {
			sappend(archetype_update, "\t{\n");
			char unpack_prefix[1024];
			snprintf(unpack_prefix, sizeof(unpack_prefix), "\t\tparams.emitter_%d_", i);
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

	sappend    (archetype_update, "SystemClock grain__load_SystemClock(uint i) {\n");
	sappend    (archetype_update, "\treturn grain__unpack_SystemClock(grain__system_clocks[i]);\n");
	sappend    (archetype_update, "}\n");
	sappend    (archetype_update, "#else\n");
	sfmt_append(archetype_update, "layout(std430, set = GRAIN_SAMPLER_SET, binding = %d) readonly buffer grain__system_params { SystemParams grain__system_params[]; };\n", num_textures + 0);
	sfmt_append(archetype_update, "layout(std430, set = GRAIN_SAMPLER_SET, binding = %d) readonly buffer grain__system_clocks { SystemClock grain__system_clocks[]; };\n", num_textures + 1);
	sappend    (archetype_update, "SystemParams grain__load_SystemParams(uint i) {\n");
	sappend    (archetype_update, "\treturn grain__system_params[i];\n");
	sappend    (archetype_update, "}\n");
	sappend    (archetype_update, "SystemClock grain__load_SystemClock(uint i) {\n");
	sappend    (archetype_update, "\treturn grain__system_clocks[i];\n");
	sappend    (archetype_update, "}\n");
	sappend    (archetype_update, "#endif\n");

	sappend(archetype_update, "\n");
	for (int i = 0; i < num_textures; ++i) {
		sfmt_append(archetype_update, "layout(location = %d) out uvec4 grain__output_%d;\n", i, i);
	}

	sappend(archetype_update, "\n");
	sappend    (archetype_update, "void grain__store_ParticleAttrs(ParticleAttrs particle) {\n");
	sfmt_append(archetype_update, "\tuvec4[%d] packed;\n", num_textures);
	sappend    (archetype_update, "\tgrain__pack_ParticleAttrs(packed, particle);\n");
	for (int i = 0; i < num_textures; ++i) {
		sfmt_append(archetype_update, "\tgrain__output_%d = packed[%d];\n", i, i);
	}
	sappend(archetype_update, "}\n");

	sappend(archetype_update, "\n");
	sappend(archetype_update, "void grain__emit(inout ParticleAttrs particle, SystemParams params, Ctx ctx) {\n");
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
	sappend(archetype_update, "void grain__process(inout ParticleAttrs particle, SystemParams params, Ctx ctx) {\n");
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
		sfmt_append(archetype_render, "\tfloat grain__padding_%d;\n", i);
	}
	sappend(archetype_render, "};\n");

	// Import module
	sappend    (archetype_render, "#define Module(X)\n");
	sappend    (archetype_render, "#define Requires(X)\n");
	sappend    (archetype_render, "#define Params(X)\n");
	sfmt_append(archetype_render, "#include \"renderer/%s\"\n", render_module->info->name);

	// SSBO-s
	sappend    (archetype_render, "\n");
	sappend    (archetype_render, "#ifdef CF_GLES\n");
	sfmt_append(archetype_render, "layout(std430, set = GRAIN_SAMPLER_SET, binding = %d) readonly buffer grain__system_params { uvec4 grain__system_params[]; };\n", num_textures + 0);
	sfmt_append(archetype_render, "layout(std430, set = GRAIN_SAMPLER_SET, binding = %d) readonly buffer grain__system_clocks { uvec4 grain__system_clocks[]; };\n", num_textures + 1);
	sfmt_append(archetype_render, "layout(std430, set = GRAIN_SAMPLER_SET, binding = %d) readonly buffer grain__draw_list { uvec4 grain__draw_list[]; };\n", num_textures + 2);

	// Load system params from the emulated buffer
	sappend(archetype_render, "ModuleParams grain__load_ModuleParams(uint i) {\n");
	// Calculate size
	num_uvec4 = padded_size / GRAIN_TEXTURE_CAPACITY;
	sfmt_append(archetype_render, "\tuvec4[%d] packed;\n", num_uvec4);
	for (int i = 0; i < num_uvec4; ++i) {
		sfmt_append(archetype_render, "\tpacked[%d] = grain__system_params[i * %d + %d];\n", i, num_uvec4, i);
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
			grain_unpack_attr(&archetype_render, "\tparams.", &index, var);

			offset += grain_type_size(var.type);
		}
	}
	sappend(archetype_render, "\treturn params;\n");
	sappend(archetype_render, "}\n");

	sappend    (archetype_render, "SystemClock grain__load_SystemClock(uint i) {\n");
	sappend    (archetype_render, "\treturn grain__unpack_SystemClock(grain__system_clocks[i]);\n");
	sappend    (archetype_render, "}\n");
	sappend    (archetype_render, "uint grain__load_draw_region(uint i) {\n");
	sappend    (archetype_render, "\treturn grain__draw_list[i / 4][i % 4];\n");
	sappend    (archetype_render, "}\n");
	sappend    (archetype_render, "#else\n");
	sfmt_append(archetype_render, "layout(std430, set = GRAIN_SAMPLER_SET, binding = %d) readonly buffer grain__module_params { ModuleParams grain__system_params[]; };\n", num_textures + 0);
	sfmt_append(archetype_render, "layout(std430, set = GRAIN_SAMPLER_SET, binding = %d) readonly buffer grain__system_clocks { SystemClock grain__system_clocks[]; };\n", num_textures + 1);
	sfmt_append(archetype_render, "layout(std430, set = GRAIN_SAMPLER_SET, binding = %d) readonly buffer grain__draw_list { uint grain__draw_list[]; };\n", num_textures + 2);
	sappend    (archetype_render, "ModuleParams grain__load_ModuleParams(uint i) {\n");
	sappend    (archetype_render, "\treturn grain__system_params[i];\n");
	sappend    (archetype_render, "}\n");
	sappend    (archetype_render, "SystemClock grain__load_SystemClock(uint i) {\n");
	sappend    (archetype_render, "\treturn grain__system_clocks[i];\n");
	sappend    (archetype_render, "}\n");
	sappend    (archetype_render, "uint grain__load_draw_region(uint i) {\n");
	sappend    (archetype_render, "\treturn grain__draw_list[i];\n");
	sappend    (archetype_render, "}\n");
	sappend    (archetype_render, "#endif\n");

	printf("// Common\n%s\n", archetype_common);
	printf("// Update\n%s\n", archetype_update);
	printf("// Render\n%s\n", archetype_render);

	grain_dsl_archetype_t* dsl_archetype = grain_dsl_compile_archetype(
		grain,
		spec,
		archetype_common, archetype_update, archetype_render
	);
	if (dsl_archetype == NULL) {
		goto fail;
	}

	archetype = (void*)0x01;
fail:
	sfree(archetype_common);
	sfree(archetype_update);
	sfree(archetype_render);
	map_free(particle_attrs);
	return archetype;
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
