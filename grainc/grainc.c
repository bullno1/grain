// grainc: bakes a grain effect .json into a single-header C module.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <barg.h>
#include "internal.h"
#include "baked.h"
#include "dsl.h"
#include "emit.h"

typedef struct {
	const char* input;
	const char* output;   // NULL: stdout
	const char* name;     // NULL: derive from the blueprint's name
	const char* prefix;   // NULL: grain_<name>
	const char* depfile;  // NULL: none
	grainc_target_mask_t targets;
} grainc_args_t;

// Module sources read from disk for modules that reference a `path` without
// embedding a `source`. The JSON doc borrows them, so they live until the
// blueprint has been loaded; the paths feed the depfile.
typedef struct {
	char** contents;
	char** paths;
	int count;
	int capacity;
} grainc_resolved_t;

static const char*
grainc_parse_target(void* userdata, const char* value) {
	static const struct {
		const char* name;
		grainc_target_mask_t mask;
	} targets[] = {
		{ "gles3", GRAINC_TARGET_GLES3 },
		{ "vulkan", GRAINC_TARGET_VULKAN },
		{ "d3d12", GRAINC_TARGET_D3D12 },
		{ "metal", GRAINC_TARGET_METAL },
		// Aliases
		{ "web", GRAINC_TARGET_GLES3 },
		{ "desktop", GRAINC_TARGET_DESKTOP },
		{ "all", GRAINC_TARGET_ALL },
	};

	grainc_target_mask_t* out = userdata;
	for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); ++i) {
		if (strcmp(value, targets[i].name) == 0) {
			*out |= targets[i].mask;
			return NULL;
		}
	}
	return "Unknown target";
}

static bool
grainc_parse_args(int argc, char** argv, grainc_args_t* args) {
	*args = (grainc_args_t){ 0 };

	barg_opt_t opts[] = {
		{
			.name = "output",
			.short_name = 'o',
			.value_name = "file",
			.summary = "Output path",
			.description = "Default: stdout",
			.parser = barg_str(&args->output),
		},
		{
			.name = "name",
			.value_name = "name",
			.summary = "Effect name for C identifiers",
			.description = "Default: derived from the blueprint's name",
			.parser = barg_str(&args->name),
		},
		{
			.name = "prefix",
			.value_name = "prefix",
			.summary = "Symbol prefix",
			.description = "Default: grain_<name>",
			.parser = barg_str(&args->prefix),
		},
		{
			.name = "depfile",
			.value_name = "file",
			.summary = "Write a Makefile-style dependency file",
			.description =
				"Lists the input and every module source resolved from a `path`,\n"
				"so build systems rebuild the header when any of them changes",
			.parser = barg_str(&args->depfile),
		},
		{
			.name = "target",
			.value_name = "api",
			.summary = "Graphics API to embed shaders for, can be repeated",
			.description =
				"Default: all\n\n"
				"Available values:\n\n"
				"* gles3\n"
				"* vulkan\n"
				"* d3d12\n"
				"* metal\n"
				"\n"
				"Or one of the aliases:\n\n"
				"* web: webgl2\n"
				"* desktop: all desktop APIs\n"
				"* all: all graphics APIs",
			.repeatable = true,
			.parser = {
				.userdata = &args->targets,
				.parse = grainc_parse_target,
			},
		},
		barg_opt_help(),
	};
	barg_t barg = {
		.usage = "grainc [options] <effect.json>",
		.summary =
			"Bakes a grain effect into a single-header C module: precompiled\n"
			"shader bytecode, archetype reflection, blueprint values and a typed\n"
			"parameter API. Include the header anywhere; define\n"
			"GRAIN_<NAME>_IMPLEMENTATION (or GRAIN_EFFECT_IMPLEMENTATION) in\n"
			"exactly one translation unit.\n\n"
			"A module that has a `path` but no `source` is read from that path,\n"
			"relative to the effect file, so an effect can reference loose .glsl\n"
			"files instead of embedding them.",
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
		fputs("grainc: missing input file (see --help)\n", stderr);
		return false;
	}
	if (result.arg_index != argc - 1) {
		fputs("grainc: only one input file is supported\n", stderr);
		return false;
	}
	args->input = argv[result.arg_index];

	// No --target: embed shaders for every API
	if (args->targets == 0) {
		args->targets = GRAINC_TARGET_ALL;
	}
	return true;
}

static char*
grainc_read_file(const char* path, size_t* out_size) {
	FILE* file = fopen(path, "rb");
	if (file == NULL) {
		fprintf(stderr, "grainc: cannot open `%s`\n", path);
		return NULL;
	}
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	fseek(file, 0, SEEK_SET);
	if (size < 0) {
		fclose(file);
		fprintf(stderr, "grainc: cannot read `%s`\n", path);
		return NULL;
	}
	char* content = malloc((size_t)size + 1);
	if (fread(content, 1, (size_t)size, file) != (size_t)size) {
		fclose(file);
		free(content);
		fprintf(stderr, "grainc: cannot read `%s`\n", path);
		return NULL;
	}
	fclose(file);
	content[size] = '\0';
	*out_size = (size_t)size;
	return content;
}

static bool
grainc_jval_present(CF_JVal val) {
	return val.id != 0 && !cf_json_is_null(val);
}

static bool
grainc_is_absolute_path(const char* path) {
	if (path[0] == '/') { return true; }
#ifdef _WIN32
	if (path[0] == '\\') { return true; }
	if (isalpha((unsigned char)path[0]) && path[1] == ':') { return true; }
#endif
	return false;
}

// Length of `path` up to and including its last separator; 0 for a bare name
static size_t
grainc_dir_len(const char* path) {
	size_t len = 0;
	for (const char* ch = path; *ch != '\0'; ++ch) {
		if (*ch == '/') { len = (size_t)(ch - path) + 1; }
#ifdef _WIN32
		if (*ch == '\\') { len = (size_t)(ch - path) + 1; }
#endif
	}
	return len;
}

// Path of a module's `path` field, relative to the effect file
static char*
grainc_resolve_path(const char* input, const char* path) {
	size_t dir_len = grainc_is_absolute_path(path) ? 0 : grainc_dir_len(input);
	size_t path_len = strlen(path);
	char* resolved = malloc(dir_len + path_len + 1);
	memcpy(resolved, input, dir_len);
	memcpy(resolved + dir_len, path, path_len + 1);
	return resolved;
}

static void
grainc_resolved_push(grainc_resolved_t* resolved, char* path, char* content) {
	if (resolved->count == resolved->capacity) {
		resolved->capacity = resolved->capacity > 0 ? resolved->capacity * 2 : 8;
		resolved->paths = realloc(resolved->paths, sizeof(char*) * (size_t)resolved->capacity);
		resolved->contents = realloc(resolved->contents, sizeof(char*) * (size_t)resolved->capacity);
	}
	resolved->paths[resolved->count] = path;
	resolved->contents[resolved->count] = content;
	resolved->count += 1;
}

static void
grainc_resolved_free(grainc_resolved_t* resolved) {
	for (int i = 0; i < resolved->count; ++i) {
		free(resolved->paths[i]);
		free(resolved->contents[i]);
	}
	free(resolved->paths);
	free(resolved->contents);
	*resolved = (grainc_resolved_t){ 0 };
}

// Fills in `source` for every module that only names a `path`. The library
// never resolves paths itself, so the effect file is completed here, before
// it is handed over as a blueprint. Modules carrying a `source` are left
// alone: an embedded snapshot wins over the file it was saved from
static bool
grainc_resolve_module_sources(
	CF_JDoc doc,
	const char* input,
	grainc_resolved_t* resolved
) {
	CF_JVal jmodules = cf_json_get(cf_json_get_root(doc), "modules");
	if (!grainc_jval_present(jmodules) || !cf_json_is_array(jmodules)) {
		return true;  // The blueprint loader reports the malformed file
	}

	int num_modules = cf_json_get_len(jmodules);
	for (int i = 0; i < num_modules; ++i) {
		CF_JVal jmodule = cf_json_array_get(jmodules, i);
		if (!cf_json_is_object(jmodule)) { continue; }
		if (grainc_jval_present(cf_json_get(jmodule, "source"))) { continue; }

		CF_JVal jpath = cf_json_get(jmodule, "path");
		if (!grainc_jval_present(jpath) || !cf_json_is_string(jpath)) {
			fprintf(stderr, "grainc: %s: module #%d has neither `source` nor `path`\n", input, i);
			return false;
		}

		char* path = grainc_resolve_path(input, cf_json_get_string(jpath));
		size_t size;
		char* content = grainc_read_file(path, &size);
		if (content == NULL) {
			free(path);
			return false;
		}
		// Zero-copy: the doc references `content`, kept alive by `resolved`
		cf_json_object_add_string(doc, jmodule, "source", content);
		grainc_resolved_push(resolved, path, content);
	}
	return true;
}

// Escapes spaces the way Make and Ninja expect in a depfile
static void
grainc_write_dep_path(FILE* file, const char* path) {
	for (const char* ch = path; *ch != '\0'; ++ch) {
		if (*ch == ' ') { fputc('\\', file); }
		fputc(*ch, file);
	}
}

static bool
grainc_write_depfile(const grainc_args_t* args, const grainc_resolved_t* resolved) {
	if (args->depfile == NULL) { return true; }
	if (args->output == NULL) {
		fputs("grainc: --depfile needs an --output\n", stderr);
		return false;
	}

	FILE* file = fopen(args->depfile, "wb");
	if (file == NULL) {
		fprintf(stderr, "grainc: cannot open `%s` for writing\n", args->depfile);
		return false;
	}
	grainc_write_dep_path(file, args->output);
	fputs(":", file);
	fputc(' ', file);
	grainc_write_dep_path(file, args->input);
	for (int i = 0; i < resolved->count; ++i) {
		fputs(" \\\n ", file);
		grainc_write_dep_path(file, resolved->paths[i]);
	}
	fputc('\n', file);
	fclose(file);
	return true;
}

// Only invalid characters are replaced; casing is preserved
static void
grainc_c_name(char* dst, size_t cap, const char* src) {
	size_t len = 0;
	for (const char* ch = src; *ch != '\0' && len + 1 < cap; ++ch) {
		if (isalnum((unsigned char)*ch)) {
			dst[len++] = *ch;
		} else if (len > 0 && dst[len - 1] != '_') {
			dst[len++] = '_';
		}
	}
	while (len > 0 && dst[len - 1] == '_') { --len; }
	if (len == 0) { dst[len++] = 'e'; }
	dst[len] = '\0';
	if (isdigit((unsigned char)dst[0]) && len + 1 < cap) {
		memmove(dst + 1, dst, len + 1);
		dst[0] = 'e';
	}
}

// Mirrors the static grain_free_modules in grain.c (same pattern as
// tests/shared.h)
static void
grainc_free_modules(CK_MAP(grain_module_t*)* module_store) {
	for (int i = 0; i < map_size(*module_store); ++i) {
		grain_module_t* module = (*module_store)[i];
		cf_free(module->source);
		cf_free(module->original_source);
		grain_dsl_free_module_info(module->info);
		cf_free(module);
	}
	map_free(*module_store);
	*module_store = NULL;
}

int
main(int argc, char** argv) {
	grainc_args_t args;
	if (!grainc_parse_args(argc, argv, &args)) { return 1; }

	size_t json_size;
	char* json = grainc_read_file(args.input, &json_size);
	if (json == NULL) { return 1; }

	// The doc references the buffer (zero-copy) and the blueprint references
	// interned strings; both stay alive through emission
	CF_JDoc doc = cf_make_json(json, json_size);

	// Headless: archetype definition compiles all shaders on the CPU through
	// cute-spirv but skips the GPU objects, so no window or GPU is needed
	grain_t grain = {
		.headless = true,
		.arena = cf_make_arena(16, 64 * 1024),
	};

	int exit_code = 1;
	grain_blueprint_t* blueprint = NULL;
	FILE* out = NULL;
	grainc_resolved_t resolved = { 0 };
	// Declared before the first `goto cleanup` so the cleanup can free it
	grain_bake_scratch_t scratch = { 0 };

	if (!grainc_resolve_module_sources(doc, args.input, &resolved)) {
		goto cleanup;
	}

	blueprint = grain_load_blueprint(&grain, cf_json_get_root(doc));
	if (blueprint == NULL) {
		fprintf(stderr, "grainc: %s: %s\n", args.input, grain_get_last_error(&grain));
		goto cleanup;
	}

	grain_baked_effect_t effect;
	if (!grain_bake(&grain, blueprint, &effect, &scratch)) {
		fprintf(stderr, "grainc: %s: %s\n", args.input, grain_get_last_error(&grain));
		goto cleanup;
	}

	char name[64];
	if (args.name != NULL) {
		grainc_c_name(name, sizeof(name), args.name);
	} else {
		grainc_c_name(name, sizeof(name), grain_blueprint_name(blueprint));
	}
	char prefix[96];
	if (args.prefix != NULL) {
		snprintf(prefix, sizeof(prefix), "%s", args.prefix);
	} else {
		snprintf(prefix, sizeof(prefix), "grain_%s", name);
	}

	out = args.output != NULL ? fopen(args.output, "wb") : stdout;
	if (out == NULL) {
		fprintf(stderr, "grainc: cannot open `%s` for writing\n", args.output);
		goto cleanup;
	}

	// The banner names just the file, so the output does not depend on where
	// the build put the source tree
	const char* source_name = strrchr(args.input, '/');
#ifdef _WIN32
	const char* backslash = strrchr(args.input, '\\');
	if (backslash != NULL && (source_name == NULL || backslash > source_name)) {
		source_name = backslash;
	}
#endif
	source_name = source_name != NULL ? source_name + 1 : args.input;

	grainc_emit_header(out, source_name, prefix, &effect, args.targets);
	if (!grainc_write_depfile(&args, &resolved)) { goto cleanup; }
	exit_code = 0;

cleanup:
	if (out != NULL && out != stdout) {
		fclose(out);
		if (exit_code != 0) { remove(args.output); }
	}
	grainc_resolved_free(&resolved);
	grain_bake_scratch_free(&scratch);
	grain_destroy_blueprint(blueprint);
	grainc_free_modules(&grain.emitters);
	grainc_free_modules(&grain.affectors);
	grainc_free_modules(&grain.renderers);
	grain_free_archetypes(&grain);
	cf_destroy_arena(&grain.arena);
	cf_destroy_json(doc);
	free(json);
	return exit_code;
}

#define BARG_IMPLEMENTATION
#include <barg.h>
