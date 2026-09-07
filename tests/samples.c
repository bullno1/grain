#include "shared.h"
#include <stdio.h>
#include <stdlib.h>

// Every shipped sample loads headlessly: the embedded module sources parse,
// their decorators scan, and the composed archetype compiles for both the
// desktop and the GLES shader variants. Loading never touches the module
// paths a sample records, so the samples are self-contained here.

static btest_suite_t samples = {
	.name = "samples",
	.init_per_test = test_grain_init,
	.cleanup_per_test = test_grain_cleanup,
};

static char*
read_file(const char* path, size_t* size_out) {
	FILE* file = fopen(path, "rb");
	if (file == NULL) { return NULL; }
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	fseek(file, 0, SEEK_SET);
	char* content = malloc((size_t)size + 1);
	size_t read = fread(content, 1, (size_t)size, file);
	fclose(file);
	content[read] = '\0';
	*size_out = read;
	return content;
}

static void
check_sample(const char* name) {
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", GRAIN_SAMPLES_DIR, name);
	size_t size;
	char* json = read_file(path, &size);
	BTEST_ASSERT_EX(json != NULL, "Could not read %s", path);

	CF_JDoc doc = cf_make_json(json, size);
	grain_blueprint_t* blueprint = grain_load_blueprint(test_grain(), cf_json_get_root(doc));
	BTEST_ASSERT_EX(blueprint != NULL, "%s: %s", name, grain_get_last_error(test_grain()));
	BTEST_EXPECT(grain_blueprint_archetype(blueprint) != NULL);

	grain_destroy_blueprint(blueprint);
	cf_destroy_json(doc);
	free(json);
}

BTEST(samples, fire) { check_sample("fire.json"); }
BTEST(samples, snow) { check_sample("snow.json"); }
// The 3D sample: vec3 attributes, @direction/@cone decorators, billboard()
BTEST(samples, fountain) { check_sample("fountain.json"); }
