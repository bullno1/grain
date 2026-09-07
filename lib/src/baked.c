#include "baked.h"
#include "internal.h"
#include "blueprint.h"
#include "dsl.h"
#include <string.h>

static char*
grain_baked_strdup(const char* str) {
	if (str == NULL) { return NULL; }
	size_t len = strlen(str);
	char* copy = cf_alloc(len + 1);
	memcpy(copy, str, len + 1);
	return copy;
}

static grain_module_info_t
grain_baked_module_info(const grain_baked_module_t* baked) {
	return (grain_module_info_t){
		.name = sintern(baked->name),
		.first_param = baked->first_param,
		.num_params = baked->num_params,
		.first_sampler = baked->first_sampler,
		.num_samplers = baked->num_samplers,
	};
}

// The slot's canonical index: [emitters..][affectors..][renderer]
static grain_blueprint_slot_t*
grain_baked_find_slot(grain_blueprint_t* blueprint, int slot) {
	int num_emitters = asize(blueprint->emitter_slots);
	int num_affectors = asize(blueprint->affector_slots);
	if (slot < num_emitters) {
		return &blueprint->emitter_slots[slot];
	} else if (slot < num_emitters + num_affectors) {
		return &blueprint->affector_slots[slot - num_emitters];
	} else if (slot == num_emitters + num_affectors) {
		return &blueprint->renderer_slot;
	} else {
		return NULL;
	}
}

grain_blueprint_t*
grain_load_blueprint_baked(grain_t* grain, const grain_baked_effect_t* baked) {
	if (baked->baked_version != GRAIN_BAKED_VERSION) {
		grain_set_last_error(grain, grain_sprintf(
			grain,
			"Baked effect `%s` has version %u, this runtime expects %d; regenerate it with grainc",
			baked->name, baked->baked_version, GRAIN_BAKED_VERSION
		));
		return NULL;
	}

	const char* interned_name = sintern(baked->name);
	uint32_t revision;
	grain_archetype_t* archetype = grain_upsert_archetype(grain, interned_name, &revision);
	*archetype = (grain_archetype_t){
		// spec stays zeroed: it is never read after definition
		.own_bytecode = false,
		.num_textures = baked->num_textures,
		.update_size = baked->update_size,
		.render_size = baked->render_size,
		.birth_texture = baked->birth_texture,
		.birth_channel = baked->birth_channel,
		.revision = revision,
		.attr_layout_hash = baked->attr_layout_hash,
	};
	archetype->shaders.update_frag_bytecode = baked->update_frag_bytecode;
	archetype->shaders.render_vert_bytecode = baked->render_vert_bytecode;
	archetype->shaders.render_frag_bytecode = baked->render_frag_bytecode;

	// Same ownership conventions as grain_define_archetype so
	// grain_cleanup_archetype and pool reconciliation work unchanged: dynamic
	// arrays and exact-size decorator storage, every name interned (all
	// lookups are pointer comparisons on interned strings)
	archetype->param_decorator_args = baked->num_decorator_args > 0
		? cf_alloc(sizeof(grain_decorator_arg_t) * baked->num_decorator_args)
		: NULL;
	for (int i = 0; i < baked->num_decorator_args; ++i) {
		grain_decorator_arg_t arg = baked->decorator_args[i];
		if (arg.name != NULL) { arg.name = sintern(arg.name); }
		if (
			arg.type == GRAIN_DECORATOR_ARG_STRING
			|| arg.type == GRAIN_DECORATOR_ARG_IDENT
		) {
			arg.value.string = sintern(arg.value.string);
		}
		archetype->param_decorator_args[i] = arg;
	}
	archetype->param_decorators = baked->num_decorators > 0
		? cf_alloc(sizeof(grain_param_decorator_t) * baked->num_decorators)
		: NULL;
	for (int i = 0; i < baked->num_decorators; ++i) {
		const grain_baked_decorator_t* decorator = &baked->decorators[i];
		archetype->param_decorators[i] = (grain_param_decorator_t){
			.name = sintern(decorator->name),
			.args = decorator->num_args > 0
				? &archetype->param_decorator_args[decorator->first_arg]
				: NULL,
			.num_args = decorator->num_args,
		};
	}

	for (int i = 0; i < baked->num_params; ++i) {
		const grain_baked_param_t* param = &baked->params[i];
		grain_param_info_t param_info = {
			.name = sintern(param->name),
			.type = param->type,
			.decorators = param->num_decorators > 0
				? &archetype->param_decorators[param->first_decorator]
				: NULL,
			.num_decorators = param->num_decorators,
		};
		apush(archetype->params, param_info);
		apush(archetype->params_offsets, param->offset);
	}

	for (int i = 0; i < baked->num_samplers; ++i) {
		const grain_baked_sampler_t* sampler = &baked->samplers[i];
		grain_sampler_info_t sampler_info = {
			.name = sintern(sampler->name),
			.decorators = sampler->num_decorators > 0
				? &archetype->param_decorators[sampler->first_decorator]
				: NULL,
			.num_decorators = sampler->num_decorators,
		};
		apush(archetype->samplers, sampler_info);
		apush(archetype->sampler_module_names, sintern(sampler->module_name));
	}

	for (int i = 0; i < baked->num_emitters; ++i) {
		apush(archetype->emitters, grain_baked_module_info(&baked->emitters[i]));
	}
	for (int i = 0; i < baked->num_affectors; ++i) {
		apush(archetype->affectors, grain_baked_module_info(&baked->affectors[i]));
	}
	archetype->renderer = grain_baked_module_info(&baked->renderer);

	if (!grain->headless) {
		archetype->shaders.update_shader = cf_make_shader_from_bytecode(
			grain_dsl_builtin_update_vert(), baked->update_frag_bytecode
		);
		archetype->shaders.render_shader = cf_make_shader_from_bytecode(
			baked->render_vert_bytecode, baked->render_frag_bytecode
		);
	}

	grain_blueprint_t* blueprint = cf_alloc(sizeof(grain_blueprint_t));
	memset(blueprint, 0, sizeof(*blueprint));
	blueprint->name = interned_name;
	blueprint->emission_rate = baked->emission_rate;
	blueprint->max_systems = baked->max_systems;
	blueprint->max_emission_rate = baked->max_emission_rate;
	blueprint->lifetime_budget = baked->lifetime_budget;
	blueprint->max_burst_size = baked->max_burst_size;
	blueprint->has_bounds = baked->has_bounds;
	blueprint->bounds = baked->has_bounds ? baked->bounds : grain_bounds_empty();
	blueprint->archetype = archetype;

	// Module records without sources: keeps counts coherent for the module
	// accessors, but baked effects are invisible to snapshot/save
	for (int i = 0; i < baked->num_emitters; ++i) {
		grain_blueprint_slot_t slot = { .module = sintern(baked->emitters[i].name) };
		apush(blueprint->emitter_slots, slot);
		grain_blueprint_module_t module = {
			.ref = { .kind = GRAIN_MODULE_EMITTER },
			.name = slot.module,
		};
		apush(blueprint->modules, module);
	}
	for (int i = 0; i < baked->num_affectors; ++i) {
		grain_blueprint_slot_t slot = { .module = sintern(baked->affectors[i].name) };
		apush(blueprint->affector_slots, slot);
		grain_blueprint_module_t module = {
			.ref = { .kind = GRAIN_MODULE_AFFECTOR },
			.name = slot.module,
		};
		apush(blueprint->modules, module);
	}
	blueprint->renderer_slot.module = sintern(baked->renderer.name);
	grain_blueprint_module_t renderer_module = {
		.ref = { .kind = GRAIN_MODULE_RENDERER },
		.name = blueprint->renderer_slot.module,
	};
	apush(blueprint->modules, renderer_module);

	for (int i = 0; i < baked->num_param_values; ++i) {
		const grain_baked_param_value_t* value = &baked->param_values[i];
		grain_blueprint_slot_t* slot = grain_baked_find_slot(blueprint, value->slot);
		if (slot == NULL) { continue; }

		grain_blueprint_param_t param = {
			.name = sintern(value->param),
			// The live shader type wins at apply
			.type = CF_SHADER_INFO_TYPE_UNKNOWN,
			.num_components = value->num_components,
		};
		for (int c = 0; c < value->num_components && c < GRAIN_BLUEPRINT_MAX_COMPONENTS; ++c) {
			param.components[c] = baked->values[value->first_value + c];
		}
		apush(slot->params, param);
	}

	for (int i = 0; i < baked->num_texture_paths; ++i) {
		const grain_baked_texture_t* texture = &baked->textures[i];
		grain_blueprint_slot_t* slot = grain_baked_find_slot(blueprint, texture->slot);
		if (slot == NULL) { continue; }

		grain_blueprint_texture_t record = {
			.sampler_name = sintern(texture->sampler),
			.path = grain_baked_strdup(texture->path),
		};
		apush(slot->textures, record);
	}

	return blueprint;
}

bool
grain_baked_system_matches(grain_system_t* system, const grain_baked_effect_t* baked) {
	grain_archetype_t* archetype = grain_get_archetype(system);
	return archetype != NULL
		&& archetype->attr_layout_hash == baked->attr_layout_hash
		&& archetype->update_size == baked->update_size
		&& archetype->render_size == baked->render_size
		&& asize(archetype->params) == baked->num_params;
}

bool
grain_baked_pool_matches(grain_pool_t* pool, const grain_baked_effect_t* baked) {
	grain_archetype_t* archetype = grain_get_pool_opts(pool).archetype;
	return archetype != NULL
		&& archetype->attr_layout_hash == baked->attr_layout_hash
		&& archetype->update_size == baked->update_size
		&& archetype->render_size == baked->render_size
		&& asize(archetype->params) == baked->num_params;
}

// ---------------------------------------------------------------------------
// Exporter
// ---------------------------------------------------------------------------

// Appends one owner's decorators (and their args) to the scratch flat tables.
// The rebuilt order is this visit order, not the archetype's storage order;
// only the (first, num) ranges matter.
static void
grain_bake_decorators(
	grain_bake_scratch_t* scratch,
	const grain_param_decorator_t* decorators,
	int num_decorators,
	int* out_first,
	int* out_num
) {
	*out_first = asize(scratch->decorators);
	*out_num = num_decorators;
	for (int i = 0; i < num_decorators; ++i) {
		const grain_param_decorator_t* decorator = &decorators[i];
		grain_baked_decorator_t baked = {
			.name = decorator->name,
			.first_arg = asize(scratch->decorator_args),
			.num_args = decorator->num_args,
		};
		for (int j = 0; j < decorator->num_args; ++j) {
			apush(scratch->decorator_args, decorator->args[j]);
		}
		apush(scratch->decorators, baked);
	}
}

static grain_baked_module_t
grain_bake_module_info(const grain_module_info_t* info) {
	return (grain_baked_module_t){
		.name = info->name,
		.first_param = info->first_param,
		.num_params = info->num_params,
		.first_sampler = info->first_sampler,
		.num_samplers = info->num_samplers,
	};
}

static void
grain_bake_slot_values(
	grain_bake_scratch_t* scratch,
	const grain_blueprint_slot_t* slot,
	int slot_index
) {
	for (int i = 0; i < asize(slot->params); ++i) {
		const grain_blueprint_param_t* param = &slot->params[i];
		grain_baked_param_value_t value = {
			.slot = slot_index,
			.param = param->name,
			.num_components = param->num_components,
			.first_value = asize(scratch->values),
		};
		for (int c = 0; c < param->num_components; ++c) {
			apush(scratch->values, param->components[c]);
		}
		apush(scratch->param_values, value);
	}
	for (int i = 0; i < asize(slot->textures); ++i) {
		const grain_blueprint_texture_t* texture = &slot->textures[i];
		grain_baked_texture_t record = {
			.slot = slot_index,
			.sampler = texture->sampler_name,
			.path = texture->path,
		};
		apush(scratch->textures, record);
	}
}

bool
grain_bake(
	grain_t* grain,
	grain_blueprint_t* blueprint,
	grain_baked_effect_t* out,
	grain_bake_scratch_t* scratch
) {
	grain_archetype_t* archetype = blueprint->archetype;
	if (archetype == NULL) {
		grain_set_last_error(
			grain, "Cannot bake a bare blueprint: it has no archetype"
		);
		return false;
	}

	memset(out, 0, sizeof(*out));
	out->baked_version = GRAIN_BAKED_VERSION;

	out->name = blueprint->name;
	out->emission_rate = blueprint->emission_rate;
	out->max_systems = blueprint->max_systems;
	out->max_emission_rate = blueprint->max_emission_rate;
	out->lifetime_budget = blueprint->lifetime_budget;
	out->max_burst_size = blueprint->max_burst_size;
	out->has_bounds = blueprint->has_bounds;
	out->bounds = blueprint->bounds;

	for (int i = 0; i < asize(archetype->emitters); ++i) {
		apush(scratch->emitters, grain_bake_module_info(&archetype->emitters[i]));
	}
	for (int i = 0; i < asize(archetype->affectors); ++i) {
		apush(scratch->affectors, grain_bake_module_info(&archetype->affectors[i]));
	}
	out->renderer = grain_bake_module_info(&archetype->renderer);

	for (int i = 0; i < asize(archetype->params); ++i) {
		const grain_param_info_t* param = &archetype->params[i];
		grain_baked_param_t baked = {
			.name = param->name,
			.type = param->type,
			.offset = archetype->params_offsets[i],
		};
		grain_bake_decorators(
			scratch, param->decorators, param->num_decorators,
			&baked.first_decorator, &baked.num_decorators
		);
		apush(scratch->params, baked);
	}

	for (int i = 0; i < asize(archetype->samplers); ++i) {
		const grain_sampler_info_t* sampler = &archetype->samplers[i];
		grain_baked_sampler_t baked = {
			.name = sampler->name,
			.module_name = archetype->sampler_module_names[i],
		};
		grain_bake_decorators(
			scratch, sampler->decorators, sampler->num_decorators,
			&baked.first_decorator, &baked.num_decorators
		);
		apush(scratch->samplers, baked);
	}

	int slot_index = 0;
	for (int i = 0; i < asize(blueprint->emitter_slots); ++i) {
		grain_bake_slot_values(scratch, &blueprint->emitter_slots[i], slot_index++);
	}
	for (int i = 0; i < asize(blueprint->affector_slots); ++i) {
		grain_bake_slot_values(scratch, &blueprint->affector_slots[i], slot_index++);
	}
	grain_bake_slot_values(scratch, &blueprint->renderer_slot, slot_index);

	out->emitters = scratch->emitters;
	out->num_emitters = asize(scratch->emitters);
	out->affectors = scratch->affectors;
	out->num_affectors = asize(scratch->affectors);
	out->params = scratch->params;
	out->num_params = asize(scratch->params);
	out->samplers = scratch->samplers;
	out->num_samplers = asize(scratch->samplers);
	out->decorators = scratch->decorators;
	out->num_decorators = asize(scratch->decorators);
	out->decorator_args = scratch->decorator_args;
	out->num_decorator_args = asize(scratch->decorator_args);
	out->param_values = scratch->param_values;
	out->num_param_values = asize(scratch->param_values);
	out->values = scratch->values;
	out->num_values = asize(scratch->values);
	out->textures = scratch->textures;
	out->num_texture_paths = asize(scratch->textures);

	out->num_textures = archetype->num_textures;
	out->update_size = archetype->update_size;
	out->render_size = archetype->render_size;
	out->birth_texture = archetype->birth_texture;
	out->birth_channel = archetype->birth_channel;
	out->attr_layout_hash = archetype->attr_layout_hash;

	out->update_frag_bytecode = archetype->shaders.update_frag_bytecode;
	out->render_vert_bytecode = archetype->shaders.render_vert_bytecode;
	out->render_frag_bytecode = archetype->shaders.render_frag_bytecode;

	return true;
}

void
grain_bake_scratch_free(grain_bake_scratch_t* scratch) {
	afree(scratch->emitters);
	afree(scratch->affectors);
	afree(scratch->params);
	afree(scratch->samplers);
	afree(scratch->decorators);
	afree(scratch->decorator_args);
	afree(scratch->param_values);
	afree(scratch->values);
	afree(scratch->textures);
	memset(scratch, 0, sizeof(*scratch));
}
