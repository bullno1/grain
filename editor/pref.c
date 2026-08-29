#include "pref.h"

void
grain_load_prefs(const grain_pref_section_t* sections, CF_JVal pref) {
	for (int section_index = 0; sections[section_index].name != NULL; ++section_index) {
		grain_pref_section_t section = sections[section_index];
		CF_JVal jsection = cf_json_get(pref, section.name);
		if (!cf_json_is_object(jsection)) { continue; }

		for (int key_index = 0; section.keys[key_index].address != NULL; ++key_index) {
			grain_pref_key_t key = section.keys[key_index];
			CF_JVal jvalue = cf_json_get(jsection, key.name);

			switch (key.type) {
				case GRAIN_PREF_VALUE_INT:
					if (cf_json_is_int(jvalue)) {
						*(int*)key.address = cf_json_get_int(jvalue);
					}
					break;
				case GRAIN_PREF_VALUE_FLOAT:
					if (cf_json_is_float(jvalue)) {
						*(float*)key.address = cf_json_get_float(jvalue);
					}
					break;
				case GRAIN_PREF_VALUE_BOOL:
					if (cf_json_is_bool(jvalue)) {
						*(bool*)key.address = cf_json_get_bool(jvalue);
					}
					break;
				case GRAIN_PREF_VALUE_STRING:
					if (cf_json_is_string(jvalue)) {
						sset(*(char**)key.address, cf_json_get_string(jvalue));
					}
					break;
			}
		}
	}
}

CF_JVal
grain_save_prefs(const grain_pref_section_t* sections, CF_JDoc doc) {
	CF_JVal jpref = cf_json_object(doc);
	for (int section_index = 0; sections[section_index].name != NULL; ++section_index) {
		grain_pref_section_t section = sections[section_index];

		CF_JVal jsection = cf_json_object(doc);
		cf_json_object_add(doc, jpref, section.name, jsection);

		for (int key_index = 0; section.keys[key_index].address != NULL; ++key_index) {
			grain_pref_key_t key = section.keys[key_index];

			switch (key.type) {
				case GRAIN_PREF_VALUE_INT:
					cf_json_object_add_int(doc, jsection, key.name, *(int*)key.address);
					break;
				case GRAIN_PREF_VALUE_FLOAT:
					cf_json_object_add_float(doc, jsection, key.name, *(float*)key.address);
					break;
				case GRAIN_PREF_VALUE_BOOL:
					cf_json_object_add_bool(doc, jsection, key.name, *(bool*)key.address);
					break;
				case GRAIN_PREF_VALUE_STRING:
					cf_json_object_add_string(doc, jsection, key.name, *(const char**)key.address);
					break;
			}
		}
	}

	return jpref;
}
