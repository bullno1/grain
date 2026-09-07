#include "emit.h"
#include <cute.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

// ---------------------------------------------------------------------------
// Literal printers
// ---------------------------------------------------------------------------

static void
grainc_emit_cstr(FILE* out, const char* str) {
	if (str == NULL) {
		fprintf(out, "NULL");
		return;
	}
	fputc('"', out);
	for (const char* ch = str; *ch != '\0'; ++ch) {
		switch (*ch) {
			case '"': fputs("\\\"", out); break;
			case '\\': fputs("\\\\", out); break;
			case '\n': fputs("\\n", out); break;
			case '\t': fputs("\\t", out); break;
			case '\r': fputs("\\r", out); break;
			default: fputc(*ch, out); break;
		}
	}
	fputc('"', out);
}

// One source line per C string literal, like cute-shaderc's -oheader output
static void
grainc_emit_source_string(FILE* out, const char* sym, const char* src) {
	fprintf(out, "static const char %s[] =\n", sym);
	const char* line = src;
	while (*line != '\0') {
		const char* end = strchr(line, '\n');
		size_t len = end != NULL ? (size_t)(end - line) : strlen(line);
		fputs("\t\"", out);
		for (size_t i = 0; i < len; ++i) {
			char ch = line[i];
			switch (ch) {
				case '"': fputs("\\\"", out); break;
				case '\\': fputs("\\\\", out); break;
				case '\t': fputs("\\t", out); break;
				case '\r': fputs("\\r", out); break;
				default: fputc(ch, out); break;
			}
		}
		if (end != NULL) { fputs("\\n", out); }
		fputs("\"\n", out);
		if (end == NULL) { break; }
		line = end + 1;
	}
	fputs("\t\"\";\n", out);
}

static void
grainc_emit_bytes(FILE* out, const uint8_t* bytes, size_t size) {
	for (size_t i = 0; i < size; ++i) {
		if (i % 16 == 0) { fputs("\t", out); }
		fprintf(out, "0x%02X,", bytes[i]);
		if (i % 16 == 15 || i == size - 1) { fputs("\n", out); }
	}
}

//! Hexfloats round-trip exactly; the comment carries the readable value
static void
grainc_emit_float(FILE* out, float value) {
	fprintf(out, "%af /* %.9g */", (double)value, (double)value);
}

static void
grainc_emit_double(FILE* out, double value) {
	fprintf(out, "%a /* %.17g */", value, value);
}

static const char*
grainc_decorator_arg_type_name(grain_decorator_arg_type_t type) {
	switch (type) {
		case GRAIN_DECORATOR_ARG_NUMBER: return "GRAIN_DECORATOR_ARG_NUMBER";
		case GRAIN_DECORATOR_ARG_STRING: return "GRAIN_DECORATOR_ARG_STRING";
		case GRAIN_DECORATOR_ARG_IDENT: return "GRAIN_DECORATOR_ARG_IDENT";
		default: return "GRAIN_DECORATOR_ARG_NUMBER";
	}
}

// ---------------------------------------------------------------------------
// Bytecode
// ---------------------------------------------------------------------------

static void
grainc_emit_bytecode_data(
	FILE* out,
	const char* sym,
	const CF_ShaderBytecode* bytecode,
	grainc_target_mask_t targets
) {
	// Vulkan, D3D12 and Metal are desktop APIs: their payloads compile away
	// entirely in web builds
	bool emit_spirv = (targets & GRAINC_TARGET_VULKAN) && bytecode->content != NULL;
	bool emit_hlsl = (targets & GRAINC_TARGET_D3D12) && bytecode->hlsl_src != NULL;
	bool emit_msl = (targets & GRAINC_TARGET_METAL) && bytecode->msl_src != NULL;
	if (emit_spirv || emit_hlsl || emit_msl) {
		fputs("#ifndef __EMSCRIPTEN__\n", out);
		if (emit_spirv) {
			fprintf(out, "static const uint8_t %s_content[%zu] = {\n", sym, bytecode->size);
			grainc_emit_bytes(out, bytecode->content, bytecode->size);
			fputs("};\n", out);
		}
		if (emit_hlsl) {
			char name[256];
			snprintf(name, sizeof(name), "%s_hlsl_src", sym);
			grainc_emit_source_string(out, name, bytecode->hlsl_src);
		}
		if (emit_msl) {
			char name[256];
			snprintf(name, sizeof(name), "%s_msl_src", sym);
			grainc_emit_source_string(out, name, bytecode->msl_src);
		}
		fputs("#endif\n", out);
	}
	if (
		(targets & GRAINC_TARGET_GLES3)
		&& bytecode->glsl300_src != NULL
	) {
		char name[256];
		snprintf(name, sizeof(name), "%s_glsl300_src", sym);
		grainc_emit_source_string(out, name, bytecode->glsl300_src);
	}

	// Reflection arrays. CF_ShaderInfo's pointers are not const-qualified, so
	// these are mutable statics.
	const CF_ShaderInfo* info = &bytecode->shader_info;
	if (info->num_images > 0) {
		fprintf(out, "static const char* %s_image_names[] = {\n", sym);
		for (int i = 0; i < info->num_images; ++i) {
			fputs("\t", out);
			grainc_emit_cstr(out, info->image_names[i]);
			fputs(",\n", out);
		}
		fputs("};\n", out);
		fprintf(out, "static int %s_image_binding_slots[] = {", sym);
		for (int i = 0; i < info->num_images; ++i) {
			fprintf(out, " %d,", info->image_binding_slots[i]);
		}
		fputs(" };\n", out);
	}
	if (info->num_uniforms > 0) {
		fprintf(out, "static CF_ShaderUniformInfo %s_uniforms[] = {\n", sym);
		for (int i = 0; i < info->num_uniforms; ++i) {
			const CF_ShaderUniformInfo* uniform = &info->uniforms[i];
			fputs("\t{ .block_name = ", out);
			grainc_emit_cstr(out, uniform->block_name);
			fprintf(
				out, ", .block_index = %d, .block_size = %d, .num_members = %d },\n",
				uniform->block_index, uniform->block_size, uniform->num_members
			);
		}
		fputs("};\n", out);
	}
	if (info->num_uniform_members > 0) {
		fprintf(out, "static CF_ShaderUniformMemberInfo %s_uniform_members[] = {\n", sym);
		for (int i = 0; i < info->num_uniform_members; ++i) {
			const CF_ShaderUniformMemberInfo* member = &info->uniform_members[i];
			fputs("\t{ .name = ", out);
			grainc_emit_cstr(out, member->name);
			fprintf(
				out, ", .type = %s, .offset = %d, .array_length = %d },\n",
				cf_shader_info_data_type_to_string(member->type),
				member->offset, member->array_length
			);
		}
		fputs("};\n", out);
	}
	if (info->num_inputs > 0) {
		fprintf(out, "static CF_ShaderInputInfo %s_inputs[] = {\n", sym);
		for (int i = 0; i < info->num_inputs; ++i) {
			const CF_ShaderInputInfo* input = &info->inputs[i];
			fputs("\t{ .name = ", out);
			grainc_emit_cstr(out, input->name);
			fprintf(
				out, ", .location = %d, .format = %s },\n",
				input->location,
				cf_shader_info_data_type_to_string(input->format)
			);
		}
		fputs("};\n", out);
	}
}

static void
grainc_emit_bytecode_init(
	FILE* out,
	const char* sym,
	const CF_ShaderBytecode* bytecode,
	grainc_target_mask_t targets
) {
	fputs("{\n", out);
	bool emit_spirv = (targets & GRAINC_TARGET_VULKAN) && bytecode->content != NULL;
	bool emit_hlsl = (targets & GRAINC_TARGET_D3D12) && bytecode->hlsl_src != NULL;
	bool emit_msl = (targets & GRAINC_TARGET_METAL) && bytecode->msl_src != NULL;
	if (emit_spirv || emit_hlsl || emit_msl) {
		fputs("#ifndef __EMSCRIPTEN__\n", out);
		if (emit_spirv) {
			fprintf(out, "\t\t.content = %s_content,\n", sym);
			fprintf(out, "\t\t.size = %zu,\n", bytecode->size);
		}
		if (emit_hlsl) {
			fprintf(out, "\t\t.hlsl_src = %s_hlsl_src,\n", sym);
			fprintf(out, "\t\t.hlsl_src_size = %zu,\n", bytecode->hlsl_src_size);
		}
		if (emit_msl) {
			fprintf(out, "\t\t.msl_src = %s_msl_src,\n", sym);
			fprintf(out, "\t\t.msl_src_size = %zu,\n", bytecode->msl_src_size);
		}
		fputs("#endif\n", out);
	}
	if (
		(targets & GRAINC_TARGET_GLES3)
		&& bytecode->glsl300_src != NULL
	) {
		fprintf(out, "\t\t.glsl300_src = %s_glsl300_src,\n", sym);
		fprintf(out, "\t\t.glsl300_src_size = %zu,\n", bytecode->glsl300_src_size);
	}

	const CF_ShaderInfo* info = &bytecode->shader_info;
	fputs("\t\t.shader_info = {\n", out);
	fprintf(out, "\t\t\t.num_samplers = %d,\n", info->num_samplers);
	fprintf(out, "\t\t\t.num_storage_textures = %d,\n", info->num_storage_textures);
	fprintf(out, "\t\t\t.num_storage_buffers = %d,\n", info->num_storage_buffers);
	fprintf(out, "\t\t\t.num_readwrite_storage_textures = %d,\n", info->num_readwrite_storage_textures);
	fprintf(out, "\t\t\t.num_readwrite_storage_buffers = %d,\n", info->num_readwrite_storage_buffers);
	fprintf(out, "\t\t\t.num_images = %d,\n", info->num_images);
	if (info->num_images > 0) {
		fprintf(out, "\t\t\t.image_names = %s_image_names,\n", sym);
		fprintf(out, "\t\t\t.image_binding_slots = %s_image_binding_slots,\n", sym);
	}
	fprintf(out, "\t\t\t.num_uniforms = %d,\n", info->num_uniforms);
	if (info->num_uniforms > 0) {
		fprintf(out, "\t\t\t.uniforms = %s_uniforms,\n", sym);
	}
	fprintf(out, "\t\t\t.num_uniform_members = %d,\n", info->num_uniform_members);
	if (info->num_uniform_members > 0) {
		fprintf(out, "\t\t\t.uniform_members = %s_uniform_members,\n", sym);
	}
	fprintf(out, "\t\t\t.num_inputs = %d,\n", info->num_inputs);
	if (info->num_inputs > 0) {
		fprintf(out, "\t\t\t.inputs = %s_inputs,\n", sym);
	}
	fprintf(
		out, "\t\t\t.local_size = { %d, %d, %d },\n",
		info->local_size[0], info->local_size[1], info->local_size[2]
	);
	fputs("\t\t},\n", out);
	fputs("\t}", out);
}

// ---------------------------------------------------------------------------
// Typed descriptor struct
// ---------------------------------------------------------------------------

#define GRAINC_MAX_NAME 128

typedef struct {
	grain_module_kind_t kind;
	const char* module_name;
	char cname[GRAINC_MAX_NAME];
	// The same module in several slots becomes one array field
	CK_DYNA const grain_baked_module_t** instances;
} grainc_field_t;

// Only invalid characters are replaced; casing is preserved
static void
grainc_sanitize(char* dst, size_t cap, const char* src) {
	size_t len = 0;
	for (const char* ch = src; *ch != '\0' && len + 1 < cap; ++ch) {
		if (isalnum((unsigned char)*ch)) {
			dst[len++] = *ch;
		} else {
			dst[len++] = '_';
		}
	}
	if (len == 0) { dst[len++] = 'x'; }
	dst[len] = '\0';
	if (isdigit((unsigned char)dst[0])) {
		memmove(dst + 1, dst, len + 1 < cap ? len + 1 : cap - 2);
		dst[0] = '_';
	}
}

static const char*
grainc_kind_prefix(grain_module_kind_t kind) {
	switch (kind) {
		case GRAIN_MODULE_EMITTER: return "emitter_";
		case GRAIN_MODULE_AFFECTOR: return "affector_";
		case GRAIN_MODULE_RENDERER: return "renderer_";
		default: return "";
	}
}

static void
grainc_add_field(
	CK_DYNA grainc_field_t** fields,
	grain_module_kind_t kind,
	const grain_baked_module_t* module
) {
	for (int i = 0; i < asize(*fields); ++i) {
		grainc_field_t* field = &(*fields)[i];
		if (field->kind == kind && strcmp(field->module_name, module->name) == 0) {
			apush(field->instances, module);
			return;
		}
	}

	grainc_field_t field = {
		.kind = kind,
		.module_name = module->name,
	};
	grainc_sanitize(field.cname, sizeof(field.cname), module->name);
	apush(field.instances, module);

	// Distinct modules of one kind may sanitize to the same field name
	for (bool renamed = true; renamed;) {
		renamed = false;
		for (int i = 0; i < asize(*fields); ++i) {
			grainc_field_t* other = &(*fields)[i];
			if (other->kind == kind && strcmp(other->cname, field.cname) == 0) {
				size_t len = strlen(field.cname);
				if (len + 1 >= sizeof(field.cname)) { break; }
				field.cname[len] = '_';
				field.cname[len + 1] = '\0';
				renamed = true;
			}
		}
	}

	apush(*fields, field);
}

// Flat-minimal layout: a kind prefix appears only on a cross-kind name clash
static void
grainc_resolve_cross_kind_clashes(CK_DYNA grainc_field_t* fields) {
	for (int i = 0; i < asize(fields); ++i) {
		bool clashes = false;
		for (int j = 0; j < asize(fields); ++j) {
			if (
				i != j
				&& fields[i].kind != fields[j].kind
				&& strcmp(fields[i].cname, fields[j].cname) == 0
			) {
				clashes = true;
				break;
			}
		}
		if (!clashes) { continue; }

		char prefixed[GRAINC_MAX_NAME];
		snprintf(
			prefixed, sizeof(prefixed), "%s%s",
			grainc_kind_prefix(fields[i].kind), fields[i].cname
		);
		memcpy(fields[i].cname, prefixed, sizeof(prefixed));
	}
}

static CK_DYNA grainc_field_t*
grainc_collect_fields(const grain_baked_effect_t* effect) {
	CK_DYNA grainc_field_t* fields = NULL;
	for (int i = 0; i < effect->num_emitters; ++i) {
		grainc_add_field(&fields, GRAIN_MODULE_EMITTER, &effect->emitters[i]);
	}
	for (int i = 0; i < effect->num_affectors; ++i) {
		grainc_add_field(&fields, GRAIN_MODULE_AFFECTOR, &effect->affectors[i]);
	}
	grainc_add_field(&fields, GRAIN_MODULE_RENDERER, &effect->renderer);
	grainc_resolve_cross_kind_clashes(fields);
	return fields;
}

static void
grainc_free_fields(CK_DYNA grainc_field_t** fields) {
	for (int i = 0; i < asize(*fields); ++i) {
		afree((*fields)[i].instances);
	}
	afree(*fields);
	*fields = NULL;
}

//! The handle type suffix for a param, mirroring the GLSL type name, or NULL
//! for types without a typed handle (they remain reachable through the
//! reflection API)
static const char*
grainc_handle_suffix(const grain_baked_param_t* param) {
	switch (param->type) {
		case CF_SHADER_INFO_TYPE_FLOAT: return "float";
		case CF_SHADER_INFO_TYPE_FLOAT2: return "vec2";
		case CF_SHADER_INFO_TYPE_FLOAT3: return "vec3";
		case CF_SHADER_INFO_TYPE_FLOAT4: return "vec4";
		case CF_SHADER_INFO_TYPE_SINT: return "int";
		case CF_SHADER_INFO_TYPE_SINT2: return "ivec2";
		case CF_SHADER_INFO_TYPE_SINT3: return "ivec3";
		case CF_SHADER_INFO_TYPE_SINT4: return "ivec4";
		case CF_SHADER_INFO_TYPE_UINT: return "uint";
		case CF_SHADER_INFO_TYPE_UINT2: return "uvec2";
		case CF_SHADER_INFO_TYPE_UINT3: return "uvec3";
		case CF_SHADER_INFO_TYPE_UINT4: return "uvec4";
		case CF_SHADER_INFO_TYPE_MAT4: return "mat4";
		default: return NULL;
	}
}

static void
grainc_emit_params_type(
	FILE* out,
	const char* prefix,
	const grain_baked_effect_t* effect,
	CK_DYNA grainc_field_t* fields
) {
	int num_members = 0;
	fputs("typedef struct {\n", out);
	for (int i = 0; i < asize(fields); ++i) {
		const grainc_field_t* field = &fields[i];
		const grain_baked_module_t* module = field->instances[0];

		fputs("\tstruct {\n", out);
		int num_module_members = 0;
		for (int j = 0; j < module->num_params; ++j) {
			const grain_baked_param_t* param = &effect->params[module->first_param + j];
			const char* suffix = grainc_handle_suffix(param);
			if (suffix == NULL) { continue; }

			char member[GRAINC_MAX_NAME];
			grainc_sanitize(member, sizeof(member), param->name);
			fprintf(out, "\t\tgrain_param_%s_t %s;\n", suffix, member);
			++num_module_members;
		}
		for (int j = 0; j < module->num_samplers; ++j) {
			const grain_baked_sampler_t* sampler = &effect->samplers[module->first_sampler + j];
			char member[GRAINC_MAX_NAME];
			grainc_sanitize(member, sizeof(member), sampler->name);
			fprintf(out, "\t\tgrain_sampler_h_t %s;\n", member);
			++num_module_members;
		}
		if (num_module_members == 0) {
			// A module without params or samplers has no handles; C forbids
			// empty structs
			fputs("\t\tint unused_;\n", out);
		}

		if (asize(field->instances) > 1) {
			fprintf(out, "\t} %s[%d];\n", field->cname, asize(field->instances));
		} else {
			fprintf(out, "\t} %s;\n", field->cname);
		}
		++num_members;
	}
	if (num_members == 0) {
		fputs("\tint unused_;\n", out);
	}
	fprintf(out, "} %s_params_t;\n", prefix);
}

// static const in the declaration section: the struct is a handful of tiny
// handles, and a visible initializer lets the indices constant-fold at every
// call site
static void
grainc_emit_params_init(
	FILE* out,
	const char* prefix,
	const grain_baked_effect_t* effect,
	CK_DYNA grainc_field_t* fields
) {
	fprintf(out, "static const %s_params_t %s = {\n", prefix, prefix);
	for (int i = 0; i < asize(fields); ++i) {
		const grainc_field_t* field = &fields[i];
		bool is_array = asize(field->instances) > 1;

		fprintf(out, "\t.%s = {%s\n", field->cname, is_array ? "{" : "");
		for (int k = 0; k < asize(field->instances); ++k) {
			const grain_baked_module_t* module = field->instances[k];
			const char* indent = is_array ? "\t\t\t" : "\t\t";
			if (k > 0) { fputs("\t}, {\n", out); }
			int num_members = 0;
			for (int j = 0; j < module->num_params; ++j) {
				const grain_baked_param_t* param = &effect->params[module->first_param + j];
				if (grainc_handle_suffix(param) == NULL) { continue; }

				char member[GRAINC_MAX_NAME];
				grainc_sanitize(member, sizeof(member), param->name);
				fprintf(
					out, "%s.%s = { %d, &%s_effect },\n",
					indent, member, module->first_param + j, prefix
				);
				++num_members;
			}
			for (int j = 0; j < module->num_samplers; ++j) {
				const grain_baked_sampler_t* sampler = &effect->samplers[module->first_sampler + j];
				char member[GRAINC_MAX_NAME];
				grainc_sanitize(member, sizeof(member), sampler->name);
				fprintf(
					out, "%s.%s = { %d, &%s_effect },\n",
					indent, member, module->first_sampler + j, prefix
				);
				++num_members;
			}
			// `{ }` is C23-only; keep the handle-less placeholder portable
			if (num_members == 0) {
				fprintf(out, "%s0,\n", indent);
			}
		}
		fprintf(out, "\t}%s,\n", is_array ? "}" : "");
	}
	fputs("};\n", out);
}

// ---------------------------------------------------------------------------
// Baked tables
// ---------------------------------------------------------------------------

static void
grainc_emit_module_table(
	FILE* out,
	const char* prefix,
	const char* table,
	const grain_baked_module_t* modules,
	int num_modules
) {
	if (num_modules == 0) { return; }
	fprintf(out, "static const grain_baked_module_t %s_%s[] = {\n", prefix, table);
	for (int i = 0; i < num_modules; ++i) {
		const grain_baked_module_t* module = &modules[i];
		fputs("\t{ .name = ", out);
		grainc_emit_cstr(out, module->name);
		fprintf(
			out,
			", .first_param = %d, .num_params = %d, .first_sampler = %d, .num_samplers = %d },\n",
			module->first_param, module->num_params,
			module->first_sampler, module->num_samplers
		);
	}
	fputs("};\n", out);
}

static void
grainc_emit_tables(FILE* out, const char* prefix, const grain_baked_effect_t* effect) {
	grainc_emit_module_table(out, prefix, "emitters", effect->emitters, effect->num_emitters);
	grainc_emit_module_table(out, prefix, "affectors", effect->affectors, effect->num_affectors);

	if (effect->num_params > 0) {
		fprintf(out, "static const grain_baked_param_t %s_params[] = {\n", prefix);
		for (int i = 0; i < effect->num_params; ++i) {
			const grain_baked_param_t* param = &effect->params[i];
			fputs("\t{ .name = ", out);
			grainc_emit_cstr(out, param->name);
			fprintf(
				out, ", .type = %s, .offset = %d, .first_decorator = %d, .num_decorators = %d },\n",
				cf_shader_info_data_type_to_string(param->type),
				param->offset, param->first_decorator, param->num_decorators
			);
		}
		fputs("};\n", out);
	}

	if (effect->num_samplers > 0) {
		fprintf(out, "static const grain_baked_sampler_t %s_samplers[] = {\n", prefix);
		for (int i = 0; i < effect->num_samplers; ++i) {
			const grain_baked_sampler_t* sampler = &effect->samplers[i];
			fputs("\t{ .name = ", out);
			grainc_emit_cstr(out, sampler->name);
			fputs(", .module_name = ", out);
			grainc_emit_cstr(out, sampler->module_name);
			fprintf(
				out, ", .first_decorator = %d, .num_decorators = %d },\n",
				sampler->first_decorator, sampler->num_decorators
			);
		}
		fputs("};\n", out);
	}

	if (effect->num_decorators > 0) {
		fprintf(out, "static const grain_baked_decorator_t %s_decorators[] = {\n", prefix);
		for (int i = 0; i < effect->num_decorators; ++i) {
			const grain_baked_decorator_t* decorator = &effect->decorators[i];
			fputs("\t{ .name = ", out);
			grainc_emit_cstr(out, decorator->name);
			fprintf(
				out, ", .first_arg = %d, .num_args = %d },\n",
				decorator->first_arg, decorator->num_args
			);
		}
		fputs("};\n", out);
	}

	if (effect->num_decorator_args > 0) {
		fprintf(out, "static const grain_decorator_arg_t %s_decorator_args[] = {\n", prefix);
		for (int i = 0; i < effect->num_decorator_args; ++i) {
			const grain_decorator_arg_t* arg = &effect->decorator_args[i];
			fprintf(out, "\t{ .index = %d, .name = ", arg->index);
			grainc_emit_cstr(out, arg->name);
			fprintf(out, ", .type = %s, ", grainc_decorator_arg_type_name(arg->type));
			if (arg->type == GRAIN_DECORATOR_ARG_NUMBER) {
				fputs(".value.number = ", out);
				grainc_emit_float(out, arg->value.number);
			} else {
				fputs(".value.string = ", out);
				grainc_emit_cstr(out, arg->value.string);
			}
			fputs(" },\n", out);
		}
		fputs("};\n", out);
	}

	if (effect->num_param_values > 0) {
		fprintf(out, "static const double %s_values[] = {\n", prefix);
		for (int i = 0; i < effect->num_values; ++i) {
			fputs("\t", out);
			grainc_emit_double(out, effect->values[i]);
			fputs(",\n", out);
		}
		fputs("};\n", out);

		fprintf(out, "static const grain_baked_param_value_t %s_param_values[] = {\n", prefix);
		for (int i = 0; i < effect->num_param_values; ++i) {
			const grain_baked_param_value_t* value = &effect->param_values[i];
			fprintf(out, "\t{ .slot = %d, .param = ", value->slot);
			grainc_emit_cstr(out, value->param);
			fprintf(
				out, ", .num_components = %d, .first_value = %d },\n",
				value->num_components, value->first_value
			);
		}
		fputs("};\n", out);
	}

	if (effect->num_texture_paths > 0) {
		fprintf(out, "static const grain_baked_texture_t %s_textures[] = {\n", prefix);
		for (int i = 0; i < effect->num_texture_paths; ++i) {
			const grain_baked_texture_t* texture = &effect->textures[i];
			fprintf(out, "\t{ .slot = %d, .sampler = ", texture->slot);
			grainc_emit_cstr(out, texture->sampler);
			fputs(", .path = ", out);
			grainc_emit_cstr(out, texture->path);
			fputs(" },\n", out);
		}
		fputs("};\n", out);
	}
}

static void
grainc_emit_effect_init(
	FILE* out,
	const char* prefix,
	const grain_baked_effect_t* effect,
	grainc_target_mask_t targets
) {
	fprintf(out, "const grain_baked_effect_t %s_effect = {\n", prefix);
	fputs("\t.baked_version = GRAIN_BAKED_VERSION,\n", out);

	fputs("\t.name = ", out);
	grainc_emit_cstr(out, effect->name);
	fputs(",\n", out);
	fputs("\t.emission_rate = ", out);
	grainc_emit_float(out, effect->emission_rate);
	fputs(",\n", out);
	fprintf(out, "\t.max_systems = %d,\n", effect->max_systems);
	fputs("\t.max_emission_rate = ", out);
	grainc_emit_float(out, effect->max_emission_rate);
	fputs(",\n", out);
	fputs("\t.lifetime_budget = ", out);
	grainc_emit_float(out, effect->lifetime_budget);
	fputs(",\n", out);
	fprintf(out, "\t.max_burst_size = %d,\n", effect->max_burst_size);
	if (effect->has_bounds) {
		fputs("\t.has_bounds = true,\n", out);
		fputs("\t.bounds = {\n", out);
		for (int corner = 0; corner < 2; ++corner) {
			const float* values = corner == 0 ? effect->bounds.min : effect->bounds.max;
			fprintf(out, "\t\t.%s = { ", corner == 0 ? "min" : "max");
			for (int i = 0; i < 3; ++i) {
				grainc_emit_float(out, values[i]);
				fputs(i < 2 ? ", " : " },\n", out);
			}
		}
		fputs("\t},\n", out);
	}

	if (effect->num_emitters > 0) {
		fprintf(out, "\t.emitters = %s_emitters,\n", prefix);
		fprintf(out, "\t.num_emitters = %d,\n", effect->num_emitters);
	}
	if (effect->num_affectors > 0) {
		fprintf(out, "\t.affectors = %s_affectors,\n", prefix);
		fprintf(out, "\t.num_affectors = %d,\n", effect->num_affectors);
	}
	fputs("\t.renderer = { .name = ", out);
	grainc_emit_cstr(out, effect->renderer.name);
	fprintf(
		out,
		", .first_param = %d, .num_params = %d, .first_sampler = %d, .num_samplers = %d },\n",
		effect->renderer.first_param, effect->renderer.num_params,
		effect->renderer.first_sampler, effect->renderer.num_samplers
	);
	if (effect->num_params > 0) {
		fprintf(out, "\t.params = %s_params,\n", prefix);
		fprintf(out, "\t.num_params = %d,\n", effect->num_params);
	}
	if (effect->num_samplers > 0) {
		fprintf(out, "\t.samplers = %s_samplers,\n", prefix);
		fprintf(out, "\t.num_samplers = %d,\n", effect->num_samplers);
	}
	if (effect->num_decorators > 0) {
		fprintf(out, "\t.decorators = %s_decorators,\n", prefix);
		fprintf(out, "\t.num_decorators = %d,\n", effect->num_decorators);
	}
	if (effect->num_decorator_args > 0) {
		fprintf(out, "\t.decorator_args = %s_decorator_args,\n", prefix);
		fprintf(out, "\t.num_decorator_args = %d,\n", effect->num_decorator_args);
	}

	fprintf(out, "\t.num_textures = %d,\n", effect->num_textures);
	fprintf(out, "\t.update_size = %d,\n", effect->update_size);
	fprintf(out, "\t.render_size = %d,\n", effect->render_size);
	fprintf(out, "\t.birth_texture = %d,\n", effect->birth_texture);
	fprintf(out, "\t.birth_channel = %d,\n", effect->birth_channel);
	fprintf(out, "\t.attr_layout_hash = 0x%016" PRIx64 "ull,\n", effect->attr_layout_hash);

	char sym[GRAINC_MAX_NAME + 32];
	snprintf(sym, sizeof(sym), "%s_update_frag", prefix);
	fprintf(out, "\t.update_frag_bytecode = ");
	grainc_emit_bytecode_init(out, sym, &effect->update_frag_bytecode, targets);
	fputs(",\n", out);
	snprintf(sym, sizeof(sym), "%s_render_vert", prefix);
	fprintf(out, "\t.render_vert_bytecode = ");
	grainc_emit_bytecode_init(out, sym, &effect->render_vert_bytecode, targets);
	fputs(",\n", out);
	snprintf(sym, sizeof(sym), "%s_render_frag", prefix);
	fprintf(out, "\t.render_frag_bytecode = ");
	grainc_emit_bytecode_init(out, sym, &effect->render_frag_bytecode, targets);
	fputs(",\n", out);

	if (effect->num_param_values > 0) {
		fprintf(out, "\t.param_values = %s_param_values,\n", prefix);
		fprintf(out, "\t.num_param_values = %d,\n", effect->num_param_values);
		fprintf(out, "\t.values = %s_values,\n", prefix);
		fprintf(out, "\t.num_values = %d,\n", effect->num_values);
	}
	if (effect->num_texture_paths > 0) {
		fprintf(out, "\t.textures = %s_textures,\n", prefix);
		fprintf(out, "\t.num_texture_paths = %d,\n", effect->num_texture_paths);
	}
	fputs("};\n", out);
}

// ---------------------------------------------------------------------------
// Whole header
// ---------------------------------------------------------------------------

void
grainc_emit_header(
	FILE* out,
	const char* source_name,
	const char* prefix,
	const grain_baked_effect_t* effect,
	grainc_target_mask_t targets
) {
	char prefix_upper[GRAINC_MAX_NAME];
	size_t prefix_len = strlen(prefix);
	if (prefix_len >= sizeof(prefix_upper)) { prefix_len = sizeof(prefix_upper) - 1; }
	for (size_t i = 0; i < prefix_len; ++i) {
		prefix_upper[i] = (char)toupper((unsigned char)prefix[i]);
	}
	prefix_upper[prefix_len] = '\0';

	CK_DYNA grainc_field_t* fields = grainc_collect_fields(effect);

	// stb-style layout: the declarations sit under an include guard, the
	// implementation outside it under its own once-guard, so a translation
	// unit can include the header plainly at the top and again with the
	// implementation macro defined at the bottom
	fprintf(out, "// Generated by grainc from %s. Do not edit.\n", source_name);
	fprintf(out, "#ifndef %s_H\n#define %s_H\n\n", prefix_upper, prefix_upper);
	fputs("#include <grain_baked.h>\n\n", out);
	fputs("#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n", out);

	grainc_emit_params_type(out, prefix, effect, fields);
	fputs("\n", out);
	fprintf(out, "extern const grain_baked_effect_t %s_effect;\n\n", prefix);
	grainc_emit_params_init(out, prefix, effect, fields);
	fputs("\n", out);
	fputs("// Defines the archetype and returns the effect's blueprint; see\n", out);
	fputs("// grain_load_blueprint_baked\n", out);
	fprintf(out, "grain_blueprint_t*\n%s_load(grain_t* grain);\n\n", prefix);

	fputs("#ifdef __cplusplus\n}\n#endif\n\n", out);
	fprintf(out, "#endif // %s_H\n\n", prefix_upper);

	fprintf(
		out,
		"#if (defined(%s_IMPLEMENTATION) || defined(GRAIN_EFFECT_IMPLEMENTATION)) \\\n"
		"\t&& !defined(%s_IMPLEMENTED)\n"
		"#define %s_IMPLEMENTED\n\n",
		prefix_upper, prefix_upper, prefix_upper
	);
	fputs("#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n", out);

	char sym[GRAINC_MAX_NAME + 32];
	snprintf(sym, sizeof(sym), "%s_update_frag", prefix);
	grainc_emit_bytecode_data(out, sym, &effect->update_frag_bytecode, targets);
	snprintf(sym, sizeof(sym), "%s_render_vert", prefix);
	grainc_emit_bytecode_data(out, sym, &effect->render_vert_bytecode, targets);
	snprintf(sym, sizeof(sym), "%s_render_frag", prefix);
	grainc_emit_bytecode_data(out, sym, &effect->render_frag_bytecode, targets);
	fputs("\n", out);

	grainc_emit_tables(out, prefix, effect);
	fputs("\n", out);
	grainc_emit_effect_init(out, prefix, effect, targets);
	fputs("\n", out);

	fprintf(out, "grain_blueprint_t*\n%s_load(grain_t* grain) {\n", prefix);
	fprintf(out, "\treturn grain_load_blueprint_baked(grain, &%s_effect);\n}\n\n", prefix);

	fputs("#ifdef __cplusplus\n}\n#endif\n\n", out);
	fprintf(out, "#endif // %s_IMPLEMENTATION\n", prefix_upper);

	grainc_free_fields(&fields);
}
