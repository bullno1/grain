// grain-probe: runs a grain effect offline, probes it, and reports or writes
// back the pool sizing and bounds it measured. See --help.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <cute.h>
#include <barg.h>
#include <grain.h>

#define PROBE_TOOL "grain-probe"

typedef struct {
	const char** items;
	int count;
	int capacity;
} str_list_t;

typedef struct {
	const char* input;
	const char* output;     // --write target; NULL: in place
	float rate;             // < 0: the blueprint's
	float duration;         // < 0: twice the lifetime budget
	float dt;
	float margin;           // fraction added on top of the measured rate and lifetime
	float padding;          // fraction of the extent added per side of the bounds
	float min_occupancy;    // --check fails below this peak occupancy; 0 disables
	bool json;
	bool write;
	bool union_bounds;
	bool check;
	bool gles;
	str_list_t sets;        // Module.param=value
	str_list_t textures;    // sampler=path or Module.sampler=path
	str_list_t bursts;      // count@time
} args_t;

typedef struct {
	int count;
	float time;
	bool fired;
} burst_t;

typedef struct {
	grain_bounds_t bounds;
	float max_age;
	float max_age_time;     // sim time of the sample that set max_age
	int peak_live;
	int num_samples;
	float last_sample_time;
	bool recycled;          // some sample drew a particle older than its slot's revisit time
} measurement_t;

// ---- Argument parsing ----

static const char*
parse_float(void* userdata, const char* value) {
	char* end;
	float f = strtof(value, &end);
	if (end == value) { return "Expected a number"; }
	if (*end == '%' && end[1] == '\0') {
		f /= 100.f;
	} else if (*end != '\0') {
		return "Expected a number";
	}
	*(float*)userdata = f;
	return NULL;
}

static const char*
parse_list(void* userdata, const char* value) {
	str_list_t* list = userdata;
	if (list->count == list->capacity) {
		list->capacity = list->capacity > 0 ? list->capacity * 2 : 8;
		list->items = realloc(list->items, sizeof(const char*) * (size_t)list->capacity);
	}
	list->items[list->count++] = value;
	return NULL;
}

static bool
parse_args(int argc, char** argv, args_t* args) {
	*args = (args_t){
		.rate = -1.f,
		.duration = -1.f,
		.dt = 1.f / 60.f,
		.margin = 0.25f,
		.padding = 0.25f,
	};

	barg_opt_t opts[] = {
		{
			.name = "set",
			.value_name = "Module.param=value",
			.summary = "Override a parameter, can be repeated",
			.description =
				"Vectors take comma-separated components: --set Point.position=10,20\n"
				"A module used more than once is indexed: --set Point[1].spread=0.5",
			.repeatable = true,
			.parser = { .userdata = &args->sets, .parse = parse_list },
		},
		{
			.name = "texture",
			.value_name = "sampler=path.png",
			.summary = "Bind a texture to a sampler, can be repeated",
			.description =
				"Default: the paths saved in the effect, relative to it. Unbound\n"
				"samplers read opaque white. Qualify with the module to disambiguate:\n"
				"--texture VectorField.field=flow.png",
			.repeatable = true,
			.parser = { .userdata = &args->textures, .parse = parse_list },
		},
		{
			.name = "rate",
			.value_name = "particles/s",
			.summary = "Emission rate to run at",
			.description = "Default: the effect's saved rate. Written back by --write.",
			.parser = { .userdata = &args->rate, .parse = parse_float },
		},
		{
			.name = "burst",
			.value_name = "count@seconds",
			.summary = "Fire a burst at a time in the run, can be repeated",
			.repeatable = true,
			.parser = { .userdata = &args->bursts, .parse = parse_list },
		},
		{
			.name = "duration",
			.value_name = "seconds",
			.summary = "Simulated time to run for",
			.description = "Default: twice the lifetime budget, so the oldest particle shows up",
			.parser = { .userdata = &args->duration, .parse = parse_float },
		},
		{
			.name = "dt",
			.value_name = "seconds",
			.summary = "Fixed time step",
			.description = "Default: 1/60. Fixed steps make a run deterministic.",
			.parser = { .userdata = &args->dt, .parse = parse_float },
		},
		{
			.name = "margin",
			.value_name = "fraction",
			.summary = "Headroom on the recommended rate and lifetime",
			.description = "Default: 0.25. Accepts a percentage: --margin 25%",
			.parser = { .userdata = &args->margin, .parse = parse_float },
		},
		{
			.name = "padding",
			.value_name = "fraction",
			.summary = "Growth of each side of the bounds, relative to its extent",
			.description = "Default: 0.25. Accepts a percentage: --padding 25%",
			.parser = { .userdata = &args->padding, .parse = parse_float },
		},
		{
			.name = "json",
			.summary = "Print the report as JSON",
			.boolean = true,
			.parser = barg_boolean(&args->json),
		},
		{
			.name = "write",
			.summary = "Write the recommended pool config and bounds into the effect",
			.description =
				"Rewrites the file in place (or --output) through the blueprint\n"
				"writer, the same one the editor saves with. Max burst size is kept.",
			.boolean = true,
			.parser = barg_boolean(&args->write),
		},
		{
			.name = "output",
			.value_name = "path",
			.summary = "Where --write puts the effect",
			.description = "Default: the input file",
			.parser = barg_str(&args->output),
		},
		{
			.name = "union",
			.summary = "Grow the effect's saved bounds instead of replacing them",
			.description = "For sweeps: run once per parameter set with --write --union",
			.boolean = true,
			.parser = barg_boolean(&args->union_bounds),
		},
		{
			.name = "check",
			.summary = "Exit 2 if the current pool recycles live particles",
			.description = "Combine with --min-occupancy to also fail on wasteful pools",
			.boolean = true,
			.parser = barg_boolean(&args->check),
		},
		{
			.name = "min-occupancy",
			.value_name = "fraction",
			.summary = "With --check: fail when peak occupancy is below this",
			.parser = { .userdata = &args->min_occupancy, .parse = parse_float },
		},
		{
			.name = "gles",
			.summary = "Use the OpenGL ES backend",
			.description =
				"The web build's path. Needed on display-less machines together with\n"
				"SDL_VIDEO_DRIVER=offscreen, where Mesa's llvmpipe provides the context.",
			.boolean = true,
			.parser = barg_boolean(&args->gles),
		},
		barg_opt_help(),
	};
	barg_t barg = {
		.usage = PROBE_TOOL " [options] <effect.json>",
		.summary =
			"Runs an effect with a fixed time step, probes every frame, and reports\n"
			"the pool sizing and bounds it measured. The system runs at the identity\n"
			"transform, so the bounds are in the effect's local space.",
		.opts = opts,
		.num_opts = sizeof(opts) / sizeof(opts[0]),
		.allow_positional = true,
	};

	barg_result_t result = barg_parse(&barg, argc, (const char**)argv);
	switch (result.status) {
		case BARG_SHOW_HELP:
			barg_print_result(&barg, result, stdout);
			exit(0);
		case BARG_PARSE_ERROR:
			barg_print_result(&barg, result, stderr);
			return false;
		case BARG_OK:
			break;
	}

	if (result.arg_index >= argc) {
		fputs(PROBE_TOOL ": missing input file (see --help)\n", stderr);
		return false;
	}
	if (result.arg_index != argc - 1) {
		fputs(PROBE_TOOL ": only one input file is supported\n", stderr);
		return false;
	}
	args->input = argv[result.arg_index];

	if (args->dt <= 0.f) {
		fputs(PROBE_TOOL ": --dt must be positive\n", stderr);
		return false;
	}
	if (args->output != NULL && !args->write) {
		fputs(PROBE_TOOL ": --output needs --write\n", stderr);
		return false;
	}
	return true;
}

// ---- Files ----

static char*
read_file(const char* path, size_t* out_size) {
	FILE* file = fopen(path, "rb");
	if (file == NULL) {
		fprintf(stderr, PROBE_TOOL ": cannot open `%s`\n", path);
		return NULL;
	}
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	fseek(file, 0, SEEK_SET);
	if (size < 0) {
		fclose(file);
		fprintf(stderr, PROBE_TOOL ": cannot read `%s`\n", path);
		return NULL;
	}
	char* content = malloc((size_t)size + 1);
	if (fread(content, 1, (size_t)size, file) != (size_t)size) {
		fclose(file);
		free(content);
		fprintf(stderr, PROBE_TOOL ": cannot read `%s`\n", path);
		return NULL;
	}
	fclose(file);
	content[size] = '\0';
	if (out_size != NULL) { *out_size = (size_t)size; }
	return content;
}

static bool
write_file(const char* path, const char* content) {
	FILE* file = fopen(path, "wb");
	if (file == NULL) {
		fprintf(stderr, PROBE_TOOL ": cannot open `%s` for writing\n", path);
		return false;
	}
	size_t len = strlen(content);
	bool ok = fwrite(content, 1, len, file) == len && fputc('\n', file) != EOF;
	fclose(file);
	if (!ok) { fprintf(stderr, PROBE_TOOL ": cannot write `%s`\n", path); }
	return ok;
}

static bool
is_absolute_path(const char* path) {
	if (path[0] == '/') { return true; }
#ifdef _WIN32
	if (path[0] == '\\') { return true; }
	if (isalpha((unsigned char)path[0]) && path[1] == ':') { return true; }
#endif
	return false;
}

// Length of `path` up to and including its last separator; 0 for a bare name
static size_t
dir_len(const char* path) {
	size_t len = 0;
	for (const char* ch = path; *ch != '\0'; ++ch) {
		if (*ch == '/') { len = (size_t)(ch - path) + 1; }
#ifdef _WIN32
		if (*ch == '\\') { len = (size_t)(ch - path) + 1; }
#endif
	}
	return len;
}

//! A path saved in the effect, relative to the effect file
static char*
resolve_path(const char* input, const char* path) {
	size_t base_len = is_absolute_path(path) ? 0 : dir_len(input);
	size_t path_len = strlen(path);
	char* resolved = malloc(base_len + path_len + 1);
	memcpy(resolved, input, base_len);
	memcpy(resolved + base_len, path, path_len + 1);
	return resolved;
}

static bool
jval_present(CF_JVal val) {
	return val.id != 0 && !cf_json_is_null(val);
}

// Same completion grainc does: modules that only name a `path` get their
// source read from disk before the blueprint loads. The doc borrows the
// buffers, which `sources` keeps alive.
static bool
resolve_module_sources(CF_JDoc doc, const char* input, str_list_t* sources) {
	CF_JVal jmodules = cf_json_get(cf_json_get_root(doc), "modules");
	if (!jval_present(jmodules) || !cf_json_is_array(jmodules)) {
		return true;  // The blueprint loader reports the malformed file
	}

	int num_modules = cf_json_get_len(jmodules);
	for (int i = 0; i < num_modules; ++i) {
		CF_JVal jmodule = cf_json_array_get(jmodules, i);
		if (!cf_json_is_object(jmodule)) { continue; }
		if (jval_present(cf_json_get(jmodule, "source"))) { continue; }

		CF_JVal jpath = cf_json_get(jmodule, "path");
		if (!jval_present(jpath) || !cf_json_is_string(jpath)) {
			fprintf(stderr, PROBE_TOOL ": %s: module #%d has neither `source` nor `path`\n", input, i);
			return false;
		}

		char* path = resolve_path(input, cf_json_get_string(jpath));
		char* content = read_file(path, NULL);
		free(path);
		if (content == NULL) { return false; }
		cf_json_object_add_string(doc, jmodule, "source", content);
		parse_list(sources, content);
	}
	return true;
}

// ---- Parameters ----

static int
type_components(CF_ShaderInfoDataType type) {
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

static bool
type_is_float(CF_ShaderInfoDataType type) {
	switch (type) {
		case CF_SHADER_INFO_TYPE_FLOAT:
		case CF_SHADER_INFO_TYPE_FLOAT2:
		case CF_SHADER_INFO_TYPE_FLOAT3:
		case CF_SHADER_INFO_TYPE_FLOAT4:
		case CF_SHADER_INFO_TYPE_MAT4:
			return true;
		default:
			return false;
	}
}

static bool
type_is_signed(CF_ShaderInfoDataType type) {
	switch (type) {
		case CF_SHADER_INFO_TYPE_SINT:
		case CF_SHADER_INFO_TYPE_SINT2:
		case CF_SHADER_INFO_TYPE_SINT3:
		case CF_SHADER_INFO_TYPE_SINT4:
			return true;
		default:
			return false;
	}
}

/**
 * `Module.member` or `Module[n].member`: the n-th module of that name across
 * emitters, affectors and the renderer, in canonical order.
 *
 * Returns the module and its kind, or NULL.
 */
static const grain_module_info_t*
find_module(
	const grain_archetype_info_t* info,
	const char* spec,
	char* member, size_t member_cap,
	grain_module_kind_t* out_kind,
	int* out_index
) {
	const char* dot = strchr(spec, '.');
	if (dot == NULL) { return NULL; }

	char module_name[128];
	size_t name_len = (size_t)(dot - spec);
	if (name_len >= sizeof(module_name)) { return NULL; }
	memcpy(module_name, spec, name_len);
	module_name[name_len] = '\0';

	int wanted = 0;
	char* bracket = strchr(module_name, '[');
	if (bracket != NULL) {
		wanted = atoi(bracket + 1);
		*bracket = '\0';
	}

	snprintf(member, member_cap, "%s", dot + 1);
	char* eq = strchr(member, '=');
	if (eq != NULL) { *eq = '\0'; }

	int seen = 0;
	for (int i = 0; i < info->num_emitters; ++i) {
		if (strcmp(info->emitters[i].name, module_name) == 0 && seen++ == wanted) {
			*out_kind = GRAIN_MODULE_EMITTER;
			*out_index = i;
			return &info->emitters[i];
		}
	}
	for (int i = 0; i < info->num_affectors; ++i) {
		if (strcmp(info->affectors[i].name, module_name) == 0 && seen++ == wanted) {
			*out_kind = GRAIN_MODULE_AFFECTOR;
			*out_index = i;
			return &info->affectors[i];
		}
	}
	if (strcmp(info->renderer.name, module_name) == 0 && seen == wanted) {
		*out_kind = GRAIN_MODULE_RENDERER;
		*out_index = 0;
		return &info->renderer;
	}
	return NULL;
}

static bool
apply_override(grain_system_t* system, const grain_archetype_info_t* info, const char* spec) {
	const char* eq = strchr(spec, '=');
	if (eq == NULL) {
		fprintf(stderr, PROBE_TOOL ": --set %s: expected Module.param=value\n", spec);
		return false;
	}

	char param_name[128];
	grain_module_kind_t kind;
	int module_index;
	const grain_module_info_t* module = find_module(
		info, spec, param_name, sizeof(param_name), &kind, &module_index
	);
	if (module == NULL) {
		fprintf(stderr, PROBE_TOOL ": --set %s: no such module\n", spec);
		return false;
	}

	int param_index = -1;
	for (int i = 0; i < module->num_params; ++i) {
		if (strcmp(info->params[module->first_param + i].name, param_name) == 0) {
			param_index = module->first_param + i;
			break;
		}
	}
	if (param_index < 0) {
		fprintf(stderr, PROBE_TOOL ": --set %s: `%s` has no param `%s`\n", spec, module->name, param_name);
		return false;
	}

	CF_ShaderInfoDataType type = info->params[param_index].type;
	int num_components = type_components(type);
	uint32_t storage[16];
	const char* cursor = eq + 1;
	for (int i = 0; i < num_components; ++i) {
		char* end;
		if (type_is_float(type)) {
			float f = strtof(cursor, &end);
			memcpy(&storage[i], &f, sizeof(f));
		} else if (type_is_signed(type)) {
			int32_t v = (int32_t)strtol(cursor, &end, 0);
			memcpy(&storage[i], &v, sizeof(v));
		} else {
			uint32_t v = (uint32_t)strtoul(cursor, &end, 0);
			memcpy(&storage[i], &v, sizeof(v));
		}
		if (end == cursor) {
			fprintf(stderr, PROBE_TOOL ": --set %s: expected %d component(s)\n", spec, num_components);
			return false;
		}
		cursor = end;
		if (*cursor == ',') { ++cursor; }
	}
	if (*cursor != '\0') {
		fprintf(stderr, PROBE_TOOL ": --set %s: expected %d component(s)\n", spec, num_components);
		return false;
	}

	void* target = grain_get_parameter(system, param_index);
	if (target == NULL) {
		fprintf(stderr, PROBE_TOOL ": --set %s: parameter is not writable\n", spec);
		return false;
	}
	memcpy(target, storage, sizeof(uint32_t) * (size_t)num_components);
	grain_parameter_modified(system, param_index);
	return true;
}

// ---- Textures ----

typedef struct {
	CF_Texture* textures;
	int count;
} texture_set_t;

static int
find_sampler_index(
	const grain_archetype_info_t* info,
	const grain_module_info_t* module,
	const char* sampler_name
) {
	for (int i = 0; i < module->num_samplers; ++i) {
		if (strcmp(info->samplers[module->first_sampler + i].name, sampler_name) == 0) {
			return module->first_sampler + i;
		}
	}
	return -1;
}

static const grain_module_info_t*
module_by_kind(const grain_archetype_info_t* info, grain_module_kind_t kind, int index) {
	switch (kind) {
		case GRAIN_MODULE_EMITTER: return &info->emitters[index];
		case GRAIN_MODULE_AFFECTOR: return &info->affectors[index];
		case GRAIN_MODULE_RENDERER: return &info->renderer;
		default: return NULL;
	}
}

static bool
bind_texture_file(
	grain_pool_t* pool,
	texture_set_t* textures,
	int sampler_index,
	const char* path,
	const char* sampler_name
) {
	size_t size;
	char* data = read_file(path, &size);
	if (data == NULL) { return false; }

	CF_Image image = { 0 };
	CF_Result result = cf_image_load_png_from_memory(data, (int)size, &image);
	free(data);
	if (cf_is_error(result)) {
		fprintf(stderr, PROBE_TOOL ": %s: not a PNG (%s)\n", path, sampler_name);
		return false;
	}

	CF_Texture texture = cf_make_texture(cf_texture_defaults(image.w, image.h));
	cf_texture_update(texture, image.pix, image.w * image.h * (int)sizeof(CF_Pixel));
	cf_image_free(&image);

	textures->textures = realloc(textures->textures, sizeof(CF_Texture) * (size_t)(textures->count + 1));
	textures->textures[textures->count++] = texture;
	grain_set_texture(pool, sampler_index, (grain_texture_binding_t){ .texture = texture });
	return true;
}

//! Saved paths first, then --texture overrides on top
static bool
bind_textures(
	const args_t* args,
	grain_pool_t* pool,
	grain_blueprint_t* blueprint,
	const grain_archetype_info_t* info,
	texture_set_t* textures
) {
	int num_saved = grain_blueprint_num_textures(blueprint);
	for (int i = 0; i < num_saved; ++i) {
		grain_blueprint_texture_info_t record = grain_blueprint_get_texture(blueprint, i);
		const grain_module_info_t* module = module_by_kind(info, record.kind, record.module_index);
		if (module == NULL) { continue; }
		int sampler_index = find_sampler_index(info, module, record.sampler_name);
		if (sampler_index < 0) { continue; }

		char* path = resolve_path(args->input, record.path);
		bool ok = bind_texture_file(pool, textures, sampler_index, path, record.sampler_name);
		free(path);
		if (!ok) {
			fprintf(
				stderr, PROBE_TOOL ": warning: `%s.%s` samples opaque white; pass --texture to bind it\n",
				module->name, record.sampler_name
			);
		}
	}

	for (int i = 0; i < args->textures.count; ++i) {
		const char* spec = args->textures.items[i];
		const char* eq = strchr(spec, '=');
		if (eq == NULL) {
			fprintf(stderr, PROBE_TOOL ": --texture %s: expected sampler=path\n", spec);
			return false;
		}

		int sampler_index = -1;
		char sampler_name[128];
		if (memchr(spec, '.', (size_t)(eq - spec)) != NULL) {
			grain_module_kind_t kind;
			int module_index;
			const grain_module_info_t* module = find_module(
				info, spec, sampler_name, sizeof(sampler_name), &kind, &module_index
			);
			if (module == NULL) {
				fprintf(stderr, PROBE_TOOL ": --texture %s: no such module\n", spec);
				return false;
			}
			sampler_index = find_sampler_index(info, module, sampler_name);
		} else {
			snprintf(sampler_name, sizeof(sampler_name), "%.*s", (int)(eq - spec), spec);
			// Any module's sampler of that name; the first match wins
			for (int j = 0; j < info->num_emitters && sampler_index < 0; ++j) {
				sampler_index = find_sampler_index(info, &info->emitters[j], sampler_name);
			}
			for (int j = 0; j < info->num_affectors && sampler_index < 0; ++j) {
				sampler_index = find_sampler_index(info, &info->affectors[j], sampler_name);
			}
			if (sampler_index < 0) {
				sampler_index = find_sampler_index(info, &info->renderer, sampler_name);
			}
		}
		if (sampler_index < 0) {
			fprintf(stderr, PROBE_TOOL ": --texture %s: no such sampler\n", spec);
			return false;
		}
		if (!bind_texture_file(pool, textures, sampler_index, eq + 1, sampler_name)) {
			return false;
		}
	}
	return true;
}

// ---- Bursts ----

static bool
parse_bursts(const str_list_t* specs, burst_t** out) {
	burst_t* bursts = calloc((size_t)(specs->count > 0 ? specs->count : 1), sizeof(burst_t));
	for (int i = 0; i < specs->count; ++i) {
		const char* spec = specs->items[i];
		char* end;
		long count = strtol(spec, &end, 10);
		if (end == spec || *end != '@' || count <= 0) {
			fprintf(stderr, PROBE_TOOL ": --burst %s: expected count@seconds\n", spec);
			free(bursts);
			return false;
		}
		const char* time_str = end + 1;
		float time = strtof(time_str, &end);
		if (end == time_str || *end != '\0' || time < 0.f) {
			fprintf(stderr, PROBE_TOOL ": --burst %s: expected count@seconds\n", spec);
			free(bursts);
			return false;
		}
		bursts[i] = (burst_t){ .count = (int)count, .time = time };
	}
	*out = bursts;
	return true;
}

// ---- The run ----

static void
fold_sample(measurement_t* m, const grain_probe_result_t* result, int pool_size, float rate) {
	m->num_samples += 1;
	m->last_sample_time = result->elapsed;
	grain_bounds_union(&m->bounds, result->bounds);
	if (result->max_age > m->max_age) {
		m->max_age = result->max_age;
		m->max_age_time = result->elapsed;
	}
	if (result->num_live > m->peak_live) { m->peak_live = result->num_live; }

	// A slot is revisited every pool_size / rate seconds; a particle still
	// drawn at that age is overwritten by the next one born into its slot
	if (rate > 0.f && result->max_age >= 0.95f * (float)pool_size / rate) {
		m->recycled = true;
	}
}

static bool
run(
	const args_t* args,
	grain_t* grain,
	grain_system_t* system,
	burst_t* bursts, int num_bursts,
	float rate, float duration, int pool_size,
	measurement_t* out
) {
	measurement_t m = { .bounds = grain_bounds_empty() };
	grain_probe_t* probe = NULL;
	int steps = (int)ceilf(duration / args->dt);
	float time = 0.f;

	for (int step = 0; step < steps; ++step) {
		cf_app_update(NULL);

		grain_begin_update(grain);
		grain_set_emission_rate(system, rate);
		for (int i = 0; i < num_bursts; ++i) {
			if (!bursts[i].fired && bursts[i].time <= time) {
				grain_burst(system, bursts[i].count);
				bursts[i].fired = true;
			}
		}
		grain_tick(system, args->dt);
		grain_end_update(grain);
		time += args->dt;

		if (probe != NULL && grain_probe_ready(probe)) {
			const grain_probe_result_t* result = grain_probe_result(probe);
			if (result == NULL) {
				fputs(PROBE_TOOL ": probe readback failed\n", stderr);
				grain_destroy_probe(probe);
				return false;
			}
			fold_sample(&m, result, pool_size, rate);
			grain_destroy_probe(probe);
			probe = NULL;
		}
		if (probe == NULL) {
			probe = grain_probe_system(system);
			if (probe == NULL) {
				fprintf(stderr, PROBE_TOOL ": cannot probe: %s\n", grain_get_last_error(grain));
				return false;
			}
		}

		// Submits the frame; the hidden window draws nothing
		cf_app_draw_onto_screen(false);
	}

	// Drain the last capture
	for (int spin = 0; probe != NULL && spin < 64; ++spin) {
		if (grain_probe_ready(probe)) {
			const grain_probe_result_t* result = grain_probe_result(probe);
			if (result != NULL) { fold_sample(&m, result, pool_size, rate); }
			grain_destroy_probe(probe);
			probe = NULL;
		} else {
			cf_app_update(NULL);
			cf_app_draw_onto_screen(false);
		}
	}
	if (probe != NULL) { grain_destroy_probe(probe); }

	if (m.num_samples == 0) {
		fputs(PROBE_TOOL ": no probe completed; is the run long enough for one frame?\n", stderr);
		return false;
	}
	*out = m;
	return true;
}

// ---- Report ----

typedef struct {
	float rate;
	float duration;
	grain_pool_opts_t current;
	grain_pool_opts_t recommended;
	int current_size;
	int recommended_size;
	float peak_occupancy;
	grain_bounds_t bounds;   // padded
	bool truncated;          // the oldest age was still climbing when the run ended
	bool outlives_budget;
} report_t;

static int
pool_size_of(grain_pool_opts_t opts) {
	return (int)ceil((double)opts.max_emission_rate * (double)opts.lifetime_budget) + opts.max_burst_size;
}

static grain_bounds_t
pad_bounds(grain_bounds_t b, float fraction) {
	if (grain_bounds_is_empty(b)) { return b; }
	for (int i = 0; i < 3; ++i) {
		float pad = (b.max[i] - b.min[i]) * fraction;
		b.min[i] -= pad;
		b.max[i] += pad;
	}
	return b;
}

static report_t
make_report(const args_t* args, const measurement_t* m, grain_pool_opts_t current, float rate, float duration) {
	report_t r = {
		.rate = rate,
		.duration = duration,
		.current = current,
		.recommended = current,
	};
	float lifetime = m->max_age > 0.1f ? m->max_age : 0.1f;
	float max_rate = rate > 1.f ? rate : 1.f;
	r.recommended.max_emission_rate = ceilf(max_rate * (1.f + args->margin));
	r.recommended.lifetime_budget = ceilf(lifetime * (1.f + args->margin) * 10.f) / 10.f;
	r.current_size = pool_size_of(current);
	r.recommended_size = pool_size_of(r.recommended);
	r.peak_occupancy = r.current_size > 0 ? (float)m->peak_live / (float)r.current_size : 0.f;
	r.bounds = pad_bounds(m->bounds, args->padding);
	r.truncated = m->max_age_time >= m->last_sample_time - 2.f * args->dt;
	r.outlives_budget = m->max_age > current.lifetime_budget;
	return r;
}

static void
print_report(const args_t* args, const measurement_t* m, const report_t* r) {
	printf(
		"%s  rate %.1f/s  duration %.1fs  dt 1/%.0f  samples %d\n\n",
		args->input, r->rate, r->duration, 1.f / args->dt, m->num_samples
	);
	printf("%-16s%-14s%-12s%s\n", "", "measured", "current", "recommended");
	printf("%-16s%-14.1f%-12.1f%.1f\n", "max rate /s", r->rate, r->current.max_emission_rate, r->recommended.max_emission_rate);
	printf("%-16s%-14.2f%-12.1f%.1f\n", "lifetime s", m->max_age, r->current.lifetime_budget, r->recommended.lifetime_budget);
	printf("%-16s%-14s%-12d%d", "pool size", "-", r->current_size, r->recommended_size);
	if (r->recommended_size > 0 && r->current_size > r->recommended_size) {
		printf("   (%.1fx smaller)", (float)r->current_size / (float)r->recommended_size);
	} else if (r->recommended_size > r->current_size) {
		printf("   (%.1fx larger)", (float)r->recommended_size / (float)r->current_size);
	}
	printf("\n%-16s%-14d%.1f%% of current\n", "peak live", m->peak_live, r->peak_occupancy * 100.f);

	if (grain_bounds_is_empty(r->bounds)) {
		printf("\nbounds  none: nothing was ever drawn\n");
	} else {
		printf(
			"\nbounds  [%.1f, %.1f, %.1f] to [%.1f, %.1f, %.1f]  padded %.0f%%\n",
			r->bounds.min[0], r->bounds.min[1], r->bounds.min[2],
			r->bounds.max[0], r->bounds.max[1], r->bounds.max[2],
			args->padding * 100.f
		);
	}

	if (m->recycled) {
		printf("\nwarning: live particles are recycled at the current pool size\n");
	}
	if (r->outlives_budget) {
		printf("warning: particles outlive the current lifetime budget\n");
	}
	if (r->truncated) {
		printf("warning: the oldest age was still climbing at the end; run longer (--duration)\n");
	}
}

// The shortest decimal that reads back as the same float, so the JSON shows
// 3.8 rather than the double nearest to 3.8f (same trick as the blueprint
// writer)
static double
json_float(float value) {
	for (int precision = 1; precision <= 9; ++precision) {
		char attempt[32];
		snprintf(attempt, sizeof(attempt), "%.*g", precision, value);
		if (strtof(attempt, NULL) == value) { return strtod(attempt, NULL); }
	}
	return value;
}

static void
add_pool_json(CF_JDoc doc, CF_JVal parent, const char* key, grain_pool_opts_t opts, int size) {
	CF_JVal obj = cf_json_object(doc);
	cf_json_object_add_double(doc, obj, "max_emission_rate", json_float(opts.max_emission_rate));
	cf_json_object_add_double(doc, obj, "lifetime_budget", json_float(opts.lifetime_budget));
	cf_json_object_add_int(doc, obj, "max_burst_size", opts.max_burst_size);
	cf_json_object_add_int(doc, obj, "pool_size", size);
	cf_json_object_add(doc, parent, key, obj);
}

static CF_JVal
bounds_corner_json(CF_JDoc doc, const float* corner) {
	CF_JVal arr = cf_json_array(doc);
	for (int i = 0; i < 3; ++i) { cf_json_array_add_double(doc, arr, json_float(corner[i])); }
	return arr;
}

static void
print_json(const args_t* args, const measurement_t* m, const report_t* r) {
	CF_JDoc doc = cf_make_json(NULL, 0);
	CF_JVal root = cf_json_object(doc);
	cf_json_object_add_string(doc, root, "input", args->input);
	cf_json_object_add_double(doc, root, "rate", json_float(r->rate));
	cf_json_object_add_double(doc, root, "duration", json_float(r->duration));
	cf_json_object_add_double(doc, root, "dt", json_float(args->dt));
	cf_json_object_add_int(doc, root, "samples", m->num_samples);

	CF_JVal measured = cf_json_object(doc);
	cf_json_object_add_double(doc, measured, "max_age", json_float(m->max_age));
	cf_json_object_add_int(doc, measured, "peak_live", m->peak_live);
	cf_json_object_add_double(doc, measured, "peak_occupancy", json_float(r->peak_occupancy));
	cf_json_object_add_bool(doc, measured, "recycled", m->recycled);
	cf_json_object_add_bool(doc, measured, "outlives_budget", r->outlives_budget);
	cf_json_object_add_bool(doc, measured, "truncated", r->truncated);
	cf_json_object_add(doc, root, "measured", measured);

	add_pool_json(doc, root, "current", r->current, r->current_size);
	add_pool_json(doc, root, "recommended", r->recommended, r->recommended_size);

	if (!grain_bounds_is_empty(r->bounds)) {
		CF_JVal bounds = cf_json_object(doc);
		cf_json_object_add(doc, bounds, "min", bounds_corner_json(doc, r->bounds.min));
		cf_json_object_add(doc, bounds, "max", bounds_corner_json(doc, r->bounds.max));
		cf_json_object_add(doc, root, "bounds", bounds);
	} else {
		cf_json_object_add_null(doc, root, "bounds");
	}

	cf_json_set_root(doc, root);
	char* text = cf_json_to_string(doc);
	puts(text);
	sfree(text);
	cf_destroy_json(doc);
}

static bool
write_effect(const args_t* args, grain_blueprint_t* blueprint, const report_t* r) {
	grain_pool_opts_t opts = grain_blueprint_pool_opts(blueprint);
	opts.max_emission_rate = r->recommended.max_emission_rate;
	opts.lifetime_budget = r->recommended.lifetime_budget;
	grain_blueprint_set_pool_opts(blueprint, opts);
	grain_blueprint_set_emission_rate(blueprint, r->rate);

	grain_bounds_t bounds = r->bounds;
	if (args->union_bounds) {
		grain_bounds_t saved;
		if (grain_blueprint_bounds(blueprint, &saved)) {
			grain_bounds_union(&bounds, saved);
		}
	}
	grain_blueprint_set_bounds(blueprint, bounds);

	// The document borrows the blueprint's strings: printed before anything
	// else is freed
	CF_JDoc doc = cf_make_json(NULL, 0);
	cf_json_set_root(doc, grain_save_blueprint(blueprint, doc));
	char* text = cf_json_to_string(doc);
	cf_destroy_json(doc);

	const char* path = args->output != NULL ? args->output : args->input;
	bool ok = write_file(path, text);
	sfree(text);
	if (ok && !args->json) { printf("\nwrote %s\n", path); }
	return ok;
}

// ---- Main ----

int
main(int argc, char** argv) {
	args_t args;
	if (!parse_args(argc, argv, &args)) { return 1; }

	burst_t* bursts = NULL;
	if (!parse_bursts(&args.bursts, &bursts)) { return 1; }

	size_t json_size;
	char* json = read_file(args.input, &json_size);
	if (json == NULL) { return 1; }
	CF_JDoc doc = cf_make_json(json, json_size);

	str_list_t sources = { 0 };
	if (!resolve_module_sources(doc, args.input, &sources)) { return 1; }

	CF_AppOptionFlags options =
		CF_APP_OPTIONS_HIDDEN_BIT
		| CF_APP_OPTIONS_NO_AUDIO_BIT
		| CF_APP_OPTIONS_FILE_SYSTEM_DONT_DEFAULT_MOUNT_BIT;
	if (args.gles) { options |= CF_APP_OPTIONS_GFX_OPENGL_BIT; }
	CF_Result app = cf_make_app(PROBE_TOOL, 0, 0, 0, 64, 64, options, argv[0]);
	if (cf_is_error(app)) {
		fprintf(stderr, PROBE_TOOL ": cannot create a graphics context: %s\n", app.details);
		return 1;
	}

	int exit_code = 1;
	grain_t* grain = grain_create();
	grain_blueprint_t* blueprint = NULL;
	grain_pool_t* pool = NULL;
	texture_set_t textures = { 0 };

	blueprint = grain_load_blueprint(grain, cf_json_get_root(doc));
	if (blueprint == NULL) {
		fprintf(stderr, PROBE_TOOL ": %s: %s\n", args.input, grain_get_last_error(grain));
		goto cleanup;
	}

	grain_pool_opts_t current = grain_blueprint_pool_opts(blueprint);
	grain_pool_opts_t pool_opts = current;
	pool_opts.max_systems = 1;
	pool = grain_create_pool(grain, pool_opts);
	if (pool == NULL) {
		fprintf(stderr, PROBE_TOOL ": %s\n", grain_get_last_error(grain));
		goto cleanup;
	}
	grain_system_t* system = grain_create_system(pool);
	grain_blueprint_apply(blueprint, system);

	grain_archetype_info_t info = grain_inspect_archetype(grain_blueprint_archetype(blueprint));
	for (int i = 0; i < args.sets.count; ++i) {
		if (!apply_override(system, &info, args.sets.items[i])) { goto cleanup; }
	}
	if (!bind_textures(&args, pool, blueprint, &info, &textures)) { goto cleanup; }

	float rate = args.rate >= 0.f ? args.rate : grain_blueprint_emission_rate(blueprint);
	float duration = args.duration > 0.f ? args.duration : 2.f * current.lifetime_budget;
	int pool_size = pool_size_of(current);

	measurement_t m;
	if (!run(&args, grain, system, bursts, args.bursts.count, rate, duration, pool_size, &m)) {
		goto cleanup;
	}

	report_t r = make_report(&args, &m, current, rate, duration);
	if (args.json) {
		print_json(&args, &m, &r);
	} else {
		print_report(&args, &m, &r);
	}
	if (args.write && !write_effect(&args, blueprint, &r)) { goto cleanup; }

	exit_code = 0;
	if (args.check) {
		if (m.recycled) {
			fputs(PROBE_TOOL ": check failed: live particles are recycled\n", stderr);
			exit_code = 2;
		}
		if (args.min_occupancy > 0.f && r.peak_occupancy < args.min_occupancy) {
			fprintf(
				stderr, PROBE_TOOL ": check failed: peak occupancy %.1f%% is below %.1f%%\n",
				r.peak_occupancy * 100.f, args.min_occupancy * 100.f
			);
			exit_code = 2;
		}
	}

cleanup:
	for (int i = 0; i < textures.count; ++i) { cf_destroy_texture(textures.textures[i]); }
	free(textures.textures);
	if (pool != NULL) { grain_destroy_pool(pool); }
	grain_destroy_blueprint(blueprint);
	grain_destroy(grain);
	cf_destroy_app();
	cf_destroy_json(doc);
	for (int i = 0; i < sources.count; ++i) { free((char*)sources.items[i]); }
	free(sources.items);
	free(args.sets.items);
	free(args.textures.items);
	free(args.bursts.items);
	free(bursts);
	free(json);
	return exit_code;
}

#define BARG_IMPLEMENTATION
#include <barg.h>
