#ifndef GRAIN_EDITOR_PREF_H
#define GRAIN_EDITOR_PREF_H

#include <cute_json.h>

typedef enum {
	GRAIN_PREF_VALUE_BOOL,
	GRAIN_PREF_VALUE_INT,
	GRAIN_PREF_VALUE_FLOAT,
	GRAIN_PREF_VALUE_STRING,
} grain_pref_type_t;

typedef struct {
	grain_pref_type_t type;
	const char* name;
	void* address;
} grain_pref_key_t;

typedef struct {
	const char* name;
	grain_pref_key_t* keys;
} grain_pref_section_t;

void
grain_load_prefs(const grain_pref_section_t* sections, CF_JVal pref);

CF_JVal
grain_save_prefs(const grain_pref_section_t* sections, CF_JDoc doc);

#define grain_pref_key(VAR) grain_pref_key_ex(VAR, #VAR)
#define grain_pref_key_ex(VAR, NAME) \
	{ .name = NAME, .address = &(VAR), .type = grain_pref_type(VAR) }
#define grain_pref_list(...) { __VA_ARGS__, { 0 } }
#define grain_pref_type(VAR) _Generic(VAR, \
	bool: GRAIN_PREF_VALUE_BOOL, \
	int: GRAIN_PREF_VALUE_INT, \
	float: GRAIN_PREF_VALUE_FLOAT, \
	char*: GRAIN_PREF_VALUE_STRING \
)

#endif
