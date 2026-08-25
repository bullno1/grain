#include "internal.h"
#include "blueprint.h"
#include <stdio.h>
#include <stdlib.h>

static const char*
grain_blueprint_kind_to_name(grain_module_kind_t kind) {
	switch (kind) {
		case GRAIN_MODULE_EMITTER: return "emitter";
		case GRAIN_MODULE_AFFECTOR: return "affector";
		case GRAIN_MODULE_RENDERER: return "renderer";
		default: return NULL;
	}
}

static grain_module_kind_t
grain_blueprint_kind_from_name(const char* name) {
	if (strcmp(name, "emitter") == 0) { return GRAIN_MODULE_EMITTER; }
	if (strcmp(name, "affector") == 0) { return GRAIN_MODULE_AFFECTOR; }
	if (strcmp(name, "renderer") == 0) { return GRAIN_MODULE_RENDERER; }
	return GRAIN_MODULE_INVALID;
}

static int
grain_blueprint_type_components(CF_ShaderInfoDataType type) {
	switch (type) {
		case CF_SHADER_INFO_TYPE_SINT:
		case CF_SHADER_INFO_TYPE_UINT:
		case CF_SHADER_INFO_TYPE_FLOAT:
			return 1;
		case CF_SHADER_INFO_TYPE_SINT2:
		case CF_SHADER_INFO_TYPE_UINT2:
		case CF_SHADER_INFO_TYPE_FLOAT2:
			return 2;
		case CF_SHADER_INFO_TYPE_SINT3:
		case CF_SHADER_INFO_TYPE_UINT3:
		case CF_SHADER_INFO_TYPE_FLOAT3:
			return 3;
		case CF_SHADER_INFO_TYPE_SINT4:
		case CF_SHADER_INFO_TYPE_UINT4:
		case CF_SHADER_INFO_TYPE_FLOAT4:
			return 4;
		case CF_SHADER_INFO_TYPE_MAT4:
			return 16;
		default:
			return 0;
	}
}

typedef enum {
	GRAIN_BLUEPRINT_BASE_FLOAT,
	GRAIN_BLUEPRINT_BASE_SINT,
	GRAIN_BLUEPRINT_BASE_UINT,
} grain_blueprint_base_t;

static grain_blueprint_base_t
grain_blueprint_type_base(CF_ShaderInfoDataType type) {
	switch (type) {
		case CF_SHADER_INFO_TYPE_SINT:
		case CF_SHADER_INFO_TYPE_SINT2:
		case CF_SHADER_INFO_TYPE_SINT3:
		case CF_SHADER_INFO_TYPE_SINT4:
			return GRAIN_BLUEPRINT_BASE_SINT;
		case CF_SHADER_INFO_TYPE_UINT:
		case CF_SHADER_INFO_TYPE_UINT2:
		case CF_SHADER_INFO_TYPE_UINT3:
		case CF_SHADER_INFO_TYPE_UINT4:
			return GRAIN_BLUEPRINT_BASE_UINT;
		default:
			return GRAIN_BLUEPRINT_BASE_FLOAT;
	}
}

/**
 * The double whose shortest decimal form is also the shortest decimal that
 * round-trips the float.
 *
 * yyjson prints the shortest representation of a double; the double closest to
 * a float prints with a noisy tail (0.1f -> "0.10000000149011612").
 * Casting the result back to float always recovers `value` exactly.
 */
static double
grain_blueprint_json_double(float value) {
	char buf[32];
	snprintf(buf, sizeof(buf), "%.9g", value);
	for (int precision = 1; precision <= 8; ++precision) {
		char attempt[32];
		snprintf(attempt, sizeof(attempt), "%.*g", precision, value);
		if (strtof(attempt, NULL) == value) {
			memcpy(buf, attempt, sizeof(attempt));
			break;
		}
	}
	return strtod(buf, NULL);
}

static char*
grain_blueprint_strdup(const char* str) {
	if (str == NULL) { return NULL; }
	size_t len = strlen(str);
	char* copy = cf_alloc(len + 1);
	memcpy(copy, str, len + 1);
	return copy;
}

static void
grain_blueprint_free_slots(CK_DYNA grain_blueprint_slot_t** slots) {
	for (int i = 0; i < asize(*slots); ++i) {
		afree((*slots)[i].params);
	}
	afree(*slots);
	*slots = NULL;
}

void
grain_blueprint_cleanup(grain_blueprint_t* blueprint) {
	for (int i = 0; i < asize(blueprint->modules); ++i) {
		cf_free((char*)blueprint->modules[i].source);
		cf_free((char*)blueprint->modules[i].path);
	}
	afree(blueprint->modules);

	grain_blueprint_free_slots(&blueprint->emitter_slots);
	grain_blueprint_free_slots(&blueprint->affector_slots);
	afree(blueprint->renderer_slot.params);

	cf_free(blueprint->spec_emitters);
	cf_free(blueprint->spec_affectors);

	memset(blueprint, 0, sizeof(*blueprint));
}

// Missing keys come back as a zero handle; JSON null is treated as absent too
static bool
grain_blueprint_jval_present(CF_JVal val) {
	return val.id != 0 && !cf_json_is_null(val);
}

static bool
grain_blueprint_jval_is_number(CF_JVal val) {
	CF_JType type = cf_json_type(val);
	return type == CF_JTYPE_INT || type == CF_JTYPE_FLOAT;
}

// cf_json_get_double narrows integers through a 32-bit int; large uints
// (e.g. packed colors) need the 64-bit read
static double
grain_blueprint_jval_to_double(CF_JVal val) {
	if (cf_json_is_int(val)) {
		return (double)cf_json_get_i64(val);
	}
	return cf_json_get_double(val);
}

static const char*
grain_blueprint_get_string(CF_JVal obj, const char* key) {
	CF_JVal val = cf_json_get(obj, key);
	if (!grain_blueprint_jval_present(val) || !cf_json_is_string(val)) { return NULL; }
	return cf_json_get_string(val);
}

static bool
grain_blueprint_get_number(CF_JVal obj, const char* key, double* out) {
	CF_JVal val = cf_json_get(obj, key);
	if (!grain_blueprint_jval_present(val) || !grain_blueprint_jval_is_number(val)) {
		return false;
	}
	*out = grain_blueprint_jval_to_double(val);
	return true;
}

static bool
grain_blueprint_parse_slot(
	grain_t* grain,
	CF_JVal jslot,
	const char* kind_name,
	int slot_index,
	grain_blueprint_slot_t* out
) {
	if (!cf_json_is_object(jslot)) {
		grain_set_last_error(grain, grain_sprintf(
			grain, "%s #%d must be an object", kind_name, slot_index
		));
		return false;
	}

	const char* module_name = grain_blueprint_get_string(jslot, "module");
	if (module_name == NULL) {
		grain_set_last_error(grain, grain_sprintf(
			grain, "%s #%d is missing a `module` name", kind_name, slot_index
		));
		return false;
	}
	out->module = sintern(module_name);

	CF_JVal jparams = cf_json_get(jslot, "params");
	if (!grain_blueprint_jval_present(jparams)) { return true; }
	if (!cf_json_is_object(jparams)) {
		grain_set_last_error(grain, grain_sprintf(
			grain, "`%s`: `params` must be an object", module_name
		));
		return false;
	}

	for (
		CF_JIter itr = cf_json_iter(jparams);
		itr.index < itr.count;
		itr = cf_json_iter_next(itr)
	) {
		const char* param_name = cf_json_iter_key(itr);
		CF_JVal jvalue = cf_json_iter_val(itr);

		grain_blueprint_param_t param = {
			.name = sintern(param_name),
			.type = CF_SHADER_INFO_TYPE_UNKNOWN,
		};

		if (grain_blueprint_jval_is_number(jvalue)) {
			param.num_components = 1;
			param.components[0] = grain_blueprint_jval_to_double(jvalue);
		} else if (cf_json_is_array(jvalue)) {
			int len = cf_json_get_len(jvalue);
			if (len < 1 || len > GRAIN_BLUEPRINT_MAX_COMPONENTS) {
				grain_set_last_error(grain, grain_sprintf(
					grain, "`%s.%s` has an invalid number of components: %d",
					module_name, param_name, len
				));
				return false;
			}
			param.num_components = len;
			for (int i = 0; i < len; ++i) {
				CF_JVal element = cf_json_array_get(jvalue, i);
				if (!grain_blueprint_jval_is_number(element)) {
					grain_set_last_error(grain, grain_sprintf(
						grain, "`%s.%s` must contain only numbers", module_name, param_name
					));
					return false;
				}
				param.components[i] = grain_blueprint_jval_to_double(element);
			}
		} else {
			grain_set_last_error(grain, grain_sprintf(
				grain, "`%s.%s` must be a number or an array of numbers",
				module_name, param_name
			));
			return false;
		}

		apush(out->params, param);
	}

	return true;
}

static bool
grain_blueprint_has_module(
	const grain_blueprint_t* blueprint,
	grain_module_kind_t kind,
	const char* interned_name
) {
	for (int i = 0; i < asize(blueprint->modules); ++i) {
		if (
			blueprint->modules[i].ref.kind == kind
			&&
			blueprint->modules[i].name == interned_name
		) {
			return true;
		}
	}
	return false;
}

static bool
grain_blueprint_check_slots(
	grain_t* grain,
	const grain_blueprint_t* blueprint,
	const CK_DYNA grain_blueprint_slot_t* slots,
	int num_slots,
	grain_module_kind_t kind,
	const char* kind_name
) {
	for (int i = 0; i < num_slots; ++i) {
		if (!grain_blueprint_has_module(blueprint, kind, slots[i].module)) {
			grain_set_last_error(grain, grain_sprintf(
				grain, "Archetype references %s `%s` which is not embedded",
				kind_name, slots[i].module
			));
			return false;
		}
	}
	return true;
}

bool
grain_blueprint_parse(grain_t* grain, CF_JVal root, grain_blueprint_t* blueprint) {
	if (!cf_json_is_object(root)) {
		grain_set_last_error(grain, "Blueprint must be a JSON object");
		return false;
	}

	double version;
	if (!grain_blueprint_get_number(root, "grain_version", &version)) {
		grain_set_last_error(grain, "Blueprint is missing `grain_version`");
		return false;
	}
	if (version < 1 || version > GRAIN_BLUEPRINT_VERSION) {
		grain_set_last_error(grain, grain_sprintf(
			grain, "Unsupported blueprint version: %d", (int)version
		));
		return false;
	}

	const char* name = grain_blueprint_get_string(root, "name");
	blueprint->name = sintern(name != NULL ? name : "Effect");

	double emission_rate = 0.0;
	grain_blueprint_get_number(root, "emission_rate", &emission_rate);
	blueprint->emission_rate = (float)emission_rate;

	CF_JVal jpool = cf_json_get(root, "pool");
	if (!grain_blueprint_jval_present(jpool) || !cf_json_is_object(jpool)) {
		grain_set_last_error(grain, "Blueprint is missing a `pool` object");
		return false;
	}
	double max_emission_rate, lifetime_budget;
	if (
		!grain_blueprint_get_number(jpool, "max_emission_rate", &max_emission_rate)
		||
		!grain_blueprint_get_number(jpool, "lifetime_budget", &lifetime_budget)
	) {
		grain_set_last_error(
			grain, "`pool` must contain `max_emission_rate` and `lifetime_budget`"
		);
		return false;
	}
	blueprint->max_emission_rate = (float)max_emission_rate;
	blueprint->lifetime_budget = (float)lifetime_budget;
	double max_systems = 1.0;
	grain_blueprint_get_number(jpool, "max_systems", &max_systems);
	blueprint->max_systems = (int)max_systems;

	CF_JVal jmodules = cf_json_get(root, "modules");
	if (!grain_blueprint_jval_present(jmodules) || !cf_json_is_array(jmodules)) {
		grain_set_last_error(grain, "Blueprint is missing a `modules` array");
		return false;
	}
	int num_modules = cf_json_get_len(jmodules);
	for (int i = 0; i < num_modules; ++i) {
		CF_JVal jmodule = cf_json_array_get(jmodules, i);
		if (!cf_json_is_object(jmodule)) {
			grain_set_last_error(grain, grain_sprintf(
				grain, "Module #%d must be an object", i
			));
			return false;
		}

		const char* kind_name = grain_blueprint_get_string(jmodule, "kind");
		grain_module_kind_t kind = kind_name != NULL
			? grain_blueprint_kind_from_name(kind_name)
			: GRAIN_MODULE_INVALID;
		if (kind == GRAIN_MODULE_INVALID) {
			grain_set_last_error(grain, grain_sprintf(
				grain, "Module #%d has an invalid `kind`", i
			));
			return false;
		}

		const char* module_name = grain_blueprint_get_string(jmodule, "name");
		const char* source = grain_blueprint_get_string(jmodule, "source");
		if (module_name == NULL || source == NULL) {
			grain_set_last_error(grain, grain_sprintf(
				grain, "Module #%d must contain a `name` and a `source`", i
			));
			return false;
		}

		grain_blueprint_module_t module = {
			.ref = { .kind = kind },
			.name = sintern(module_name),
			.source = grain_blueprint_strdup(source),
			.path = grain_blueprint_strdup(grain_blueprint_get_string(jmodule, "path")),
		};
		apush(blueprint->modules, module);
	}

	CF_JVal jarchetype = cf_json_get(root, "archetype");
	if (!grain_blueprint_jval_present(jarchetype) || !cf_json_is_object(jarchetype)) {
		grain_set_last_error(grain, "Blueprint is missing an `archetype` object");
		return false;
	}

	CF_JVal jemitters = cf_json_get(jarchetype, "emitters");
	if (grain_blueprint_jval_present(jemitters)) {
		if (!cf_json_is_array(jemitters)) {
			grain_set_last_error(grain, "`archetype.emitters` must be an array");
			return false;
		}
		int num_emitters = cf_json_get_len(jemitters);
		for (int i = 0; i < num_emitters; ++i) {
			grain_blueprint_slot_t slot = { 0 };
			if (!grain_blueprint_parse_slot(
				grain, cf_json_array_get(jemitters, i), "emitter", i, &slot
			)) {
				afree(slot.params);
				return false;
			}
			apush(blueprint->emitter_slots, slot);
		}
	}

	CF_JVal jaffectors = cf_json_get(jarchetype, "affectors");
	if (grain_blueprint_jval_present(jaffectors)) {
		if (!cf_json_is_array(jaffectors)) {
			grain_set_last_error(grain, "`archetype.affectors` must be an array");
			return false;
		}
		int num_affectors = cf_json_get_len(jaffectors);
		for (int i = 0; i < num_affectors; ++i) {
			grain_blueprint_slot_t slot = { 0 };
			if (!grain_blueprint_parse_slot(
				grain, cf_json_array_get(jaffectors, i), "affector", i, &slot
			)) {
				afree(slot.params);
				return false;
			}
			apush(blueprint->affector_slots, slot);
		}
	}

	CF_JVal jrenderer = cf_json_get(jarchetype, "renderer");
	if (!grain_blueprint_jval_present(jrenderer)) {
		grain_set_last_error(grain, "`archetype` is missing a `renderer`");
		return false;
	}
	if (!grain_blueprint_parse_slot(
		grain, jrenderer, "renderer", 0, &blueprint->renderer_slot
	)) {
		return false;
	}

	return grain_blueprint_check_slots(
			grain, blueprint,
			blueprint->emitter_slots, asize(blueprint->emitter_slots),
			GRAIN_MODULE_EMITTER, "emitter"
		)
		&& grain_blueprint_check_slots(
			grain, blueprint,
			blueprint->affector_slots, asize(blueprint->affector_slots),
			GRAIN_MODULE_AFFECTOR, "affector"
		)
		&& grain_blueprint_check_slots(
			grain, blueprint,
			&blueprint->renderer_slot, 1,
			GRAIN_MODULE_RENDERER, "renderer"
		);
}

static CF_JVal
grain_blueprint_emit_param_value(const grain_blueprint_param_t* param, CF_JDoc doc) {
	grain_blueprint_base_t base = grain_blueprint_type_base(param->type);

	if (param->num_components == 1) {
		switch (base) {
			case GRAIN_BLUEPRINT_BASE_SINT:
				return cf_json_from_int(doc, (int)param->components[0]);
			case GRAIN_BLUEPRINT_BASE_UINT:
				return cf_json_from_u64(doc, (uint64_t)param->components[0]);
			default:
				return cf_json_from_double(doc, param->components[0]);
		}
	}

	CF_JVal array = cf_json_array(doc);
	for (int i = 0; i < param->num_components; ++i) {
		switch (base) {
			case GRAIN_BLUEPRINT_BASE_SINT:
				cf_json_array_add_int(doc, array, (int)param->components[i]);
				break;
			case GRAIN_BLUEPRINT_BASE_UINT:
				cf_json_array_add_u64(doc, array, (uint64_t)param->components[i]);
				break;
			default:
				cf_json_array_add_double(doc, array, param->components[i]);
				break;
		}
	}
	return array;
}

static CF_JVal
grain_blueprint_emit_slot(const grain_blueprint_slot_t* slot, CF_JDoc doc) {
	CF_JVal jslot = cf_json_object(doc);
	cf_json_object_add_string(doc, jslot, "module", slot->module);

	if (asize(slot->params) > 0) {
		CF_JVal jparams = cf_json_object(doc);
		for (int i = 0; i < asize(slot->params); ++i) {
			const grain_blueprint_param_t* param = &slot->params[i];
			cf_json_object_add(
				doc, jparams, param->name,
				grain_blueprint_emit_param_value(param, doc)
			);
		}
		cf_json_object_add(doc, jslot, "params", jparams);
	}

	return jslot;
}

CF_JVal
grain_blueprint_emit(const grain_blueprint_t* blueprint, CF_JDoc doc) {
	CF_JVal root = cf_json_object(doc);
	cf_json_object_add_int(doc, root, "grain_version", GRAIN_BLUEPRINT_VERSION);
	cf_json_object_add_string(doc, root, "name", blueprint->name);
	cf_json_object_add_double(
		doc, root, "emission_rate",
		grain_blueprint_json_double(blueprint->emission_rate)
	);

	CF_JVal jpool = cf_json_object(doc);
	cf_json_object_add_int(doc, jpool, "max_systems", blueprint->max_systems);
	cf_json_object_add_double(
		doc, jpool, "max_emission_rate",
		grain_blueprint_json_double(blueprint->max_emission_rate)
	);
	cf_json_object_add_double(
		doc, jpool, "lifetime_budget",
		grain_blueprint_json_double(blueprint->lifetime_budget)
	);
	cf_json_object_add(doc, root, "pool", jpool);

	CF_JVal jmodules = cf_json_array(doc);
	for (int i = 0; i < asize(blueprint->modules); ++i) {
		const grain_blueprint_module_t* module = &blueprint->modules[i];
		CF_JVal jmodule = cf_json_object(doc);
		cf_json_object_add_string(
			doc, jmodule, "kind", grain_blueprint_kind_to_name(module->ref.kind)
		);
		cf_json_object_add_string(doc, jmodule, "name", module->name);
		if (module->path != NULL) {
			cf_json_object_add_string(doc, jmodule, "path", module->path);
		}
		cf_json_object_add_string(doc, jmodule, "source", module->source);
		cf_json_array_add(jmodules, jmodule);
	}
	cf_json_object_add(doc, root, "modules", jmodules);

	CF_JVal jarchetype = cf_json_object(doc);
	CF_JVal jemitters = cf_json_array(doc);
	for (int i = 0; i < asize(blueprint->emitter_slots); ++i) {
		cf_json_array_add(
			jemitters, grain_blueprint_emit_slot(&blueprint->emitter_slots[i], doc)
		);
	}
	cf_json_object_add(doc, jarchetype, "emitters", jemitters);
	CF_JVal jaffectors = cf_json_array(doc);
	for (int i = 0; i < asize(blueprint->affector_slots); ++i) {
		cf_json_array_add(
			jaffectors, grain_blueprint_emit_slot(&blueprint->affector_slots[i], doc)
		);
	}
	cf_json_object_add(doc, jarchetype, "affectors", jaffectors);
	cf_json_object_add(
		doc, jarchetype, "renderer",
		grain_blueprint_emit_slot(&blueprint->renderer_slot, doc)
	);
	cf_json_object_add(doc, root, "archetype", jarchetype);

	return root;
}

static CK_MAP(grain_module_t*)*
grain_blueprint_module_store(grain_t* grain, grain_module_kind_t kind) {
	switch (kind) {
		case GRAIN_MODULE_EMITTER: return &grain->emitters;
		case GRAIN_MODULE_AFFECTOR: return &grain->affectors;
		case GRAIN_MODULE_RENDERER: return &grain->renderers;
		default: return NULL;
	}
}

static bool
grain_blueprint_collect_module(
	grain_t* grain,
	grain_blueprint_t* blueprint,
	grain_module_kind_t kind,
	const char* interned_name,
	grain_save_opts_t opts
) {
	if (grain_blueprint_has_module(blueprint, kind, interned_name)) { return true; }

	grain_module_t* module = map_get(
		*grain_blueprint_module_store(grain, kind), interned_name
	);
	if (module == NULL) {
		grain_set_last_error(grain, grain_sprintf(
			grain, "Archetype references undefined %s `%s`",
			grain_blueprint_kind_to_name(kind), interned_name
		));
		return false;
	}

	const char* path = opts.module_path != NULL
		? opts.module_path(opts.userdata, kind, interned_name)
		: NULL;

	grain_blueprint_module_t record = {
		.ref = { .kind = kind, .module = module },
		.name = interned_name,
		.source = grain_blueprint_strdup(module->original_source),
		.path = grain_blueprint_strdup(path),
	};
	apush(blueprint->modules, record);
	return true;
}

static grain_blueprint_slot_t
grain_blueprint_collect_slot(
	grain_system_t* system,
	const grain_archetype_info_t* info,
	const grain_module_info_t* module_info
) {
	grain_blueprint_slot_t slot = { .module = module_info->name };

	for (int i = 0; i < module_info->num_params; ++i) {
		int param_index = module_info->first_param + i;
		const grain_param_info_t* param_info = &info->params[param_index];

		int num_components = grain_blueprint_type_components(param_info->type);
		void* value = grain_get_parameter(system, param_index);
		if (num_components == 0 || value == NULL) { continue; }

		grain_blueprint_param_t param = {
			.name = param_info->name,
			.type = param_info->type,
			.num_components = num_components,
		};
		switch (grain_blueprint_type_base(param_info->type)) {
			case GRAIN_BLUEPRINT_BASE_SINT:
				for (int c = 0; c < num_components; ++c) {
					param.components[c] = (double)((int32_t*)value)[c];
				}
				break;
			case GRAIN_BLUEPRINT_BASE_UINT:
				for (int c = 0; c < num_components; ++c) {
					param.components[c] = (double)((uint32_t*)value)[c];
				}
				break;
			default:
				for (int c = 0; c < num_components; ++c) {
					param.components[c] = grain_blueprint_json_double(((float*)value)[c]);
				}
				break;
		}
		apush(slot.params, param);
	}

	return slot;
}

CF_JVal
grain_save_system(
	grain_t* grain,
	grain_system_t* system,
	grain_save_opts_t opts,
	CF_JDoc doc
) {
	grain_archetype_t* archetype = grain_get_archetype(system);
	grain_archetype_info_t info = grain_inspect_archetype(archetype);
	grain_pool_opts_t pool_opts = grain_get_pool_opts(grain_get_pool(system));

	grain_blueprint_t blueprint = {
		.name = sintern(opts.name != NULL ? opts.name : "Effect"),
		.emission_rate = opts.emission_rate,
		.max_systems = pool_opts.max_systems,
		.max_emission_rate = pool_opts.max_emission_rate,
		.lifetime_budget = pool_opts.lifetime_budget,
	};

	bool ok = true;
	for (int i = 0; ok && i < info.num_emitters; ++i) {
		ok = grain_blueprint_collect_module(
			grain, &blueprint, GRAIN_MODULE_EMITTER, info.emitters[i].name, opts
		);
	}
	for (int i = 0; ok && i < info.num_affectors; ++i) {
		ok = grain_blueprint_collect_module(
			grain, &blueprint, GRAIN_MODULE_AFFECTOR, info.affectors[i].name, opts
		);
	}
	ok = ok && grain_blueprint_collect_module(
		grain, &blueprint, GRAIN_MODULE_RENDERER, info.renderer.name, opts
	);

	CF_JVal result = { 0 };
	if (ok) {
		for (int i = 0; i < info.num_emitters; ++i) {
			apush(
				blueprint.emitter_slots,
				grain_blueprint_collect_slot(system, &info, &info.emitters[i])
			);
		}
		for (int i = 0; i < info.num_affectors; ++i) {
			apush(
				blueprint.affector_slots,
				grain_blueprint_collect_slot(system, &info, &info.affectors[i])
			);
		}
		blueprint.renderer_slot = grain_blueprint_collect_slot(
			system, &info, &info.renderer
		);

		result = grain_blueprint_emit(&blueprint, doc);
	}

	grain_blueprint_cleanup(&blueprint);
	return result;
}

grain_blueprint_t*
grain_load_blueprint(grain_t* grain, CF_JVal val) {
	grain_blueprint_t* blueprint = cf_alloc(sizeof(grain_blueprint_t));
	memset(blueprint, 0, sizeof(*blueprint));

	if (!grain_blueprint_parse(grain, val, blueprint)) { goto fail; }

	// Define embedded modules; redefinition of a live module is the normal
	// live-reload path
	for (int i = 0; i < asize(blueprint->modules); ++i) {
		grain_blueprint_module_t* module = &blueprint->modules[i];
		grain_module_kind_t declared_kind = module->ref.kind;

		grain_module_ref_t ref = grain_define_module(grain, module->source);
		if (ref.kind == GRAIN_MODULE_INVALID) {
			grain_set_last_error(grain, grain_sprintf(
				grain, "While defining `%s`: %s",
				module->name, grain_get_last_error(grain)
			));
			goto fail;
		}
		if (ref.kind != declared_kind) {
			grain_set_last_error(grain, grain_sprintf(
				grain, "Module `%s` is saved as %s but its source declares %s",
				module->name,
				grain_blueprint_kind_to_name(declared_kind),
				grain_blueprint_kind_to_name(ref.kind)
			));
			goto fail;
		}
		module->ref = ref;
	}

	int num_emitters = asize(blueprint->emitter_slots);
	int num_affectors = asize(blueprint->affector_slots);
	if (num_emitters > 0) {
		blueprint->spec_emitters = cf_alloc(sizeof(void*) * num_emitters);
		for (int i = 0; i < num_emitters; ++i) {
			blueprint->spec_emitters[i] = map_get(
				grain->emitters, blueprint->emitter_slots[i].module
			);
		}
	}
	if (num_affectors > 0) {
		blueprint->spec_affectors = cf_alloc(sizeof(void*) * num_affectors);
		for (int i = 0; i < num_affectors; ++i) {
			blueprint->spec_affectors[i] = map_get(
				grain->affectors, blueprint->affector_slots[i].module
			);
		}
	}

	blueprint->archetype = grain_define_archetype(
		grain,
		blueprint->name,
		(grain_archetype_spec_t){
			.emitters = (grain_emitter_t**)blueprint->spec_emitters,
			.num_emitters = num_emitters,
			.affectors = (grain_affector_t**)blueprint->spec_affectors,
			.num_affectors = num_affectors,
			.renderer = (grain_renderer_t*)map_get(
				grain->renderers, blueprint->renderer_slot.module
			),
		}
	);
	if (blueprint->archetype == NULL) { goto fail; }

	return blueprint;

fail:
	grain_blueprint_cleanup(blueprint);
	cf_free(blueprint);
	return NULL;
}

void
grain_destroy_blueprint(grain_blueprint_t* blueprint) {
	if (blueprint == NULL) { return; }

	grain_blueprint_cleanup(blueprint);
	cf_free(blueprint);
}

const char*
grain_blueprint_name(grain_blueprint_t* blueprint) {
	return blueprint->name;
}

float
grain_blueprint_emission_rate(grain_blueprint_t* blueprint) {
	return blueprint->emission_rate;
}

grain_archetype_t*
grain_blueprint_archetype(grain_blueprint_t* blueprint) {
	return blueprint->archetype;
}

grain_pool_opts_t
grain_blueprint_pool_opts(grain_blueprint_t* blueprint) {
	return (grain_pool_opts_t){
		.archetype = blueprint->archetype,
		.max_systems = blueprint->max_systems,
		.max_emission_rate = blueprint->max_emission_rate,
		.lifetime_budget = blueprint->lifetime_budget,
	};
}

int
grain_blueprint_num_modules(grain_blueprint_t* blueprint) {
	return asize(blueprint->modules);
}

grain_blueprint_module_t
grain_blueprint_get_module(grain_blueprint_t* blueprint, int index) {
	if (index < 0 || index >= asize(blueprint->modules)) {
		return (grain_blueprint_module_t){ 0 };
	}
	return blueprint->modules[index];
}

static const grain_module_info_t*
grain_blueprint_find_target_slot(
	const grain_module_info_t* modules,
	int num_modules,
	int preferred_index,
	const char* module_name
) {
	if (
		preferred_index < num_modules
		&&
		modules[preferred_index].name == module_name  // interned
	) {
		return &modules[preferred_index];
	}

	for (int i = 0; i < num_modules; ++i) {
		if (modules[i].name == module_name) { return &modules[i]; }
	}
	return NULL;
}

static void
grain_blueprint_apply_slot(
	grain_system_t* system,
	const grain_archetype_info_t* info,
	const grain_blueprint_slot_t* slot,
	const grain_module_info_t* target
) {
	if (target == NULL) { return; }

	for (int i = 0; i < asize(slot->params); ++i) {
		const grain_blueprint_param_t* param = &slot->params[i];

		for (int j = 0; j < target->num_params; ++j) {
			int param_index = target->first_param + j;
			const grain_param_info_t* param_info = &info->params[param_index];
			if (param_info->name != param->name) { continue; }  // interned
			if (grain_blueprint_type_components(param_info->type) != param->num_components) {
				continue;
			}

			void* value = grain_get_parameter(system, param_index);
			if (value == NULL) { break; }

			switch (grain_blueprint_type_base(param_info->type)) {
				case GRAIN_BLUEPRINT_BASE_SINT:
					for (int c = 0; c < param->num_components; ++c) {
						((int32_t*)value)[c] = (int32_t)param->components[c];
					}
					break;
				case GRAIN_BLUEPRINT_BASE_UINT:
					for (int c = 0; c < param->num_components; ++c) {
						((uint32_t*)value)[c] = (uint32_t)param->components[c];
					}
					break;
				default:
					for (int c = 0; c < param->num_components; ++c) {
						((float*)value)[c] = (float)param->components[c];
					}
					break;
			}
			grain_parameter_modified(system, param_index);
			break;
		}
	}
}

void
grain_blueprint_apply(grain_blueprint_t* blueprint, grain_system_t* system) {
	grain_archetype_info_t info = grain_inspect_archetype(grain_get_archetype(system));

	for (int i = 0; i < asize(blueprint->emitter_slots); ++i) {
		const grain_blueprint_slot_t* slot = &blueprint->emitter_slots[i];
		grain_blueprint_apply_slot(
			system, &info, slot,
			grain_blueprint_find_target_slot(
				info.emitters, info.num_emitters, i, slot->module
			)
		);
	}
	for (int i = 0; i < asize(blueprint->affector_slots); ++i) {
		const grain_blueprint_slot_t* slot = &blueprint->affector_slots[i];
		grain_blueprint_apply_slot(
			system, &info, slot,
			grain_blueprint_find_target_slot(
				info.affectors, info.num_affectors, i, slot->module
			)
		);
	}
	grain_blueprint_apply_slot(
		system, &info, &blueprint->renderer_slot,
		grain_blueprint_find_target_slot(
			&info.renderer, 1, 0, blueprint->renderer_slot.module
		)
	);

	grain_set_emission_rate(system, blueprint->emission_rate);
}
