#include "internal.h"
#include "dsl.h"
#include <stdarg.h>
#include <stdio.h>

// RGBA32F
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

	// Assemble the ParticleAttrs type
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

	char* shader = NULL;

	sappend(shader, "struct ParticleAttrs {\n");
	for (int i = 0; i < map_size(particle_attrs); ++i) {
		sappend(shader, "\t");
		sappend(shader, grain_type_name(particle_attrs[i].var_info.type));
		sappend(shader, " ");
		sappend(shader, particle_attrs[i].var_info.name);
		sappend(shader, ";\n");
	}
	sappend(shader, "};\n");
	printf("%s\n", shader);

	int attr_total_size = 0;
	for (int i = 0; i < map_size(particle_attrs); ++i) {
		attr_total_size += grain_type_size(particle_attrs[i].var_info.type);
	}
	int num_textures = (attr_total_size + GRAIN_TEXTURE_CAPACITY - 1) / GRAIN_TEXTURE_CAPACITY;
	printf("Num textures = %d\n", num_textures);
	if (num_textures > CF_MAX_CANVAS_TARGETS) {
		grain_set_last_error(grain, "Archetype requires too many storage tetures");
		goto fail;
	}

	sfree(shader);

	map_free(particle_attrs);
	return (grain_archetype_t*)(0x01);

fail:
	map_free(particle_attrs);
	return NULL;
}

void
grain_set_last_error(grain_t* grain, const char* message) {
	grain->last_error = message;
}

void
grain_reset_arena(grain_t* grain) {
	cf_arena_reset(&grain->arena);
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
