// grainc: bakes a grain effect .json into a single-header C module.
//
// Runs the exact runtime pipeline headlessly -- grain_load_blueprint compiles
// every shader CPU-side through cute-spirv, skipping only GPU objects -- then
// flattens the result with grain_bake and prints it as C.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#define BARG_IMPLEMENTATION
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
	grainc_target_mask_t targets;
} grainc_args_t;

static const char*
grainc_parse_target(void* userdata, const char* value) {
	static const struct {
		const char* name;
		grainc_target_mask_t mask;
	} targets[] = {
		{ "gles3", GRAINC_TARGET_GLES3 },
		{ "webgl2", GRAINC_TARGET_WEBGL2 },
		{ "vulkan", GRAINC_TARGET_VULKAN },
		{ "d3d12", GRAINC_TARGET_D3D12 },
		{ "metal", GRAINC_TARGET_METAL },
		// Aliases
		{ "web", GRAINC_TARGET_WEB },
		{ "desktop", GRAINC_TARGET_DESKTOP },
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
			.summary = "Output path (default: stdout)",
			.parser = barg_str(&args->output),
		},
		{
			.name = "name",
			.value_name = "name",
			.summary = "Effect name for C identifiers "
				"(default: derived from the blueprint's name)",
			.parser = barg_str(&args->name),
		},
		{
			.name = "prefix",
			.value_name = "prefix",
			.summary = "Symbol prefix (default: grain_<name>)",
			.parser = barg_str(&args->prefix),
		},
		{
			.name = "target",
			.value_name = "api",
			.summary = "Graphics API to embed shaders for: gles3, webgl2, "
				"vulkan, d3d12, metal, or the aliases web (webgl2) and "
				"desktop (all desktop APIs); may be repeated (default: all)",
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
			"exactly one translation unit.",
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

	blueprint = grain_load_blueprint(&grain, cf_json_get_root(doc));
	if (blueprint == NULL) {
		fprintf(stderr, "grainc: %s: %s\n", args.input, grain_get_last_error(&grain));
		goto cleanup;
	}

	grain_baked_effect_t effect;
	grain_bake_scratch_t scratch = { 0 };
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
	exit_code = 0;

cleanup:
	if (out != NULL && out != stdout) {
		fclose(out);
		if (exit_code != 0) { remove(args.output); }
	}
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
