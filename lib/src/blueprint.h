#ifndef GRAIN_BLUEPRINT_H
#define GRAIN_BLUEPRINT_H

#include <grain.h>
#include <cute.h>

#define GRAIN_BLUEPRINT_VERSION 1
// Enough for a mat4
#define GRAIN_BLUEPRINT_MAX_COMPONENTS 16

typedef struct {
	const char* name;  // interned
	//! CF_SHADER_INFO_TYPE_UNKNOWN when parsed from JSON; the live type wins at apply
	CF_ShaderInfoDataType type;
	int num_components;
	// double round-trips every float, int32 and uint32 component exactly
	double components[GRAIN_BLUEPRINT_MAX_COMPONENTS];
} grain_blueprint_param_t;

//! One position in the archetype's module list along with its saved param values
typedef struct {
	const char* module;  // interned module name
	CK_DYNA grain_blueprint_param_t* params;
} grain_blueprint_slot_t;

struct grain_blueprint_s {
	const char* name;  // interned
	float emission_rate;

	int max_systems;
	float max_emission_rate;
	float lifetime_budget;

	// source/path are cf_alloc copies owned by the blueprint; names are interned
	CK_DYNA grain_blueprint_module_t* modules;

	CK_DYNA grain_blueprint_slot_t* emitter_slots;
	CK_DYNA grain_blueprint_slot_t* affector_slots;
	grain_blueprint_slot_t renderer_slot;

	// Set by materialization in grain_load_blueprint; NULL after a bare parse
	grain_archetype_t* archetype;
	// The arrays handed to grain_define_archetype. An archetype only reads its
	// spec during definition, so the blueprint owns and frees them.
	void** spec_emitters;
	void** spec_affectors;
};

/**
 * Fill `blueprint` from a JSON value: pure record parsing, no GPU and no
 * module definition, so it works headlessly.
 *
 * On failure the error is reported through grain's last error; `blueprint` may
 * hold partial state and must still be cleaned up.
 */
bool
grain_blueprint_parse(grain_t* grain, CF_JVal val, grain_blueprint_t* blueprint);

//! The pure inverse of grain_blueprint_parse: records to a JSON object in `doc`
CF_JVal
grain_blueprint_emit(const grain_blueprint_t* blueprint, CF_JDoc doc);

//! Free everything a blueprint owns, tolerating partial state; not the struct itself
void
grain_blueprint_cleanup(grain_blueprint_t* blueprint);

#endif
