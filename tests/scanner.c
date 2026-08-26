#include "shared.h"
#include "decorator.h"
#include <stdio.h>

static void init_per_test(void);
static void cleanup_per_test(void);

static btest_suite_t scanner = {
	.name = "decorator/scanner",
	.init_per_test = init_per_test,
	.cleanup_per_test = cleanup_per_test,
};

static struct {
	char* stripped;
	CK_DYNA grain_decorator_t* decorators;
	CK_DYNA grain_decorator_arg_t* args;
	CK_DYNA const char** samplers;
} fixture;

static void
init_per_test(void) {
	test_grain_init();
	fixture.stripped = NULL;
	fixture.decorators = NULL;
	fixture.args = NULL;
	fixture.samplers = NULL;
}

static void
cleanup_per_test(void) {
	cf_free(fixture.stripped);
	afree(fixture.decorators);
	afree(fixture.args);
	afree(fixture.samplers);
	test_grain_cleanup();
}

// Runs the scanner on a private copy of `source`, leaving the stripped copy
// and the extracted decorators in the fixture.
static bool
extract(const char* source) {
	size_t len = strlen(source);
	fixture.stripped = cf_alloc(len + 1);
	memcpy(fixture.stripped, source, len + 1);
	return grain_decorator_extract(
		test_grain(), fixture.stripped, &fixture.decorators, &fixture.args,
		&fixture.samplers
	);
}

static void
expect_scan_error(const char* params_body, const char* error_fragment) {
	char source[1024];
	snprintf(source, sizeof(source), "Affector(T)\nParams(\n%s\n)\n", params_body);
	BTEST_EXPECT(!extract(source));
	GRAIN_EXPECT_ERROR_CONTAINS(error_fragment);
}

static const char* decorated_source =
	"Affector(Gravity)\n"
	"Requires(\n"
	"	vec2 velocity;\n"
	")\n"
	"Params(\n"
	"	@range(0, max=100)\n"
	"	@description(\"How much \\\"push\\\"\")\n"
	"	float gravity;\n"
	"	@color uint color;\n"
	"	@range(min=-1, max=1) float a, b;\n"
	"	vec2 plain;\n"
	")\n"
	"void process() {}\n";

BTEST(scanner, blanks_decorators_preserving_layout) {
	BTEST_ASSERT_EX(
		extract(decorated_source), "%s", grain_get_last_error(test_grain())
	);

	BTEST_EXPECT(strchr(fixture.stripped, '@') == NULL);
	BTEST_EXPECT_EQUAL("%zu", strlen(fixture.stripped), strlen(decorated_source));

	int lines_before = 0;
	int lines_after = 0;
	for (const char* c = decorated_source; *c != '\0'; ++c) { lines_before += *c == '\n'; }
	for (const char* c = fixture.stripped; *c != '\0'; ++c) { lines_after += *c == '\n'; }
	BTEST_EXPECT_EQUAL("%d", lines_after, lines_before);

	BTEST_EXPECT(strstr(fixture.stripped, "float gravity;") != NULL);
	BTEST_EXPECT(strstr(fixture.stripped, "uint color;") != NULL);
	BTEST_EXPECT(strstr(fixture.stripped, "float a, b;") != NULL);
}

BTEST(scanner, binds_stacked_decorators_to_the_next_declaration) {
	BTEST_ASSERT_EX(
		extract(decorated_source), "%s", grain_get_last_error(test_grain())
	);
	BTEST_ASSERT_EQUAL("%d", asize(fixture.decorators), 5);

	BTEST_EXPECT(fixture.decorators[0].name == sintern("range"));
	BTEST_EXPECT(fixture.decorators[0].param == sintern("gravity"));
	BTEST_EXPECT(fixture.decorators[1].name == sintern("description"));
	BTEST_EXPECT(fixture.decorators[1].param == sintern("gravity"));
	BTEST_EXPECT(fixture.decorators[2].name == sintern("color"));
	BTEST_EXPECT(fixture.decorators[2].param == sintern("color"));
	BTEST_EXPECT_EQUAL("%d", fixture.decorators[2].num_args, 0);
}

BTEST(scanner, parses_mixed_positional_and_named_args) {
	BTEST_ASSERT_EX(
		extract(decorated_source), "%s", grain_get_last_error(test_grain())
	);
	BTEST_ASSERT_EQUAL("%d", fixture.decorators[0].num_args, 2);

	// @range(0, max=100)
	grain_decorator_arg_t* range_args = &fixture.args[fixture.decorators[0].first_arg];
	BTEST_EXPECT_EQUAL("%d", range_args[0].index, 0);
	BTEST_EXPECT(range_args[0].name == NULL);
	BTEST_EXPECT_EQUAL("%d", range_args[0].type, GRAIN_DECORATOR_ARG_NUMBER);
	BTEST_EXPECT_EQUAL("%f", range_args[0].value.number, 0.f);
	BTEST_EXPECT_EQUAL("%d", range_args[1].index, -1);
	BTEST_EXPECT(range_args[1].name == sintern("max"));
	BTEST_EXPECT_EQUAL("%f", range_args[1].value.number, 100.f);

	// Negative number in a named arg: @range(min=-1, ...)
	grain_decorator_arg_t* ab_args = &fixture.args[fixture.decorators[3].first_arg];
	BTEST_EXPECT(ab_args[0].name == sintern("min"));
	BTEST_EXPECT_EQUAL("%f", ab_args[0].value.number, -1.f);
}

BTEST(scanner, parses_string_args_with_escapes) {
	BTEST_ASSERT_EX(
		extract(decorated_source), "%s", grain_get_last_error(test_grain())
	);

	grain_decorator_arg_t* desc_args = &fixture.args[fixture.decorators[1].first_arg];
	BTEST_ASSERT_EQUAL("%d", fixture.decorators[1].num_args, 1);
	BTEST_EXPECT_EQUAL("%d", desc_args[0].type, GRAIN_DECORATOR_ARG_STRING);
	BTEST_EXPECT(desc_args[0].value.string == sintern("How much \"push\""));
}

BTEST(scanner, duplicates_decorators_across_declarators) {
	BTEST_ASSERT_EX(
		extract(decorated_source), "%s", grain_get_last_error(test_grain())
	);

	// @range(min=-1, max=1) float a, b;
	BTEST_EXPECT(fixture.decorators[3].param == sintern("a"));
	BTEST_EXPECT(fixture.decorators[4].param == sintern("b"));
	BTEST_EXPECT(fixture.decorators[4].name == sintern("range"));
	BTEST_EXPECT_EQUAL("%d", fixture.decorators[4].num_args, 2);
	// The duplicate shares the arg range instead of re-parsing it
	BTEST_EXPECT_EQUAL("%d", fixture.decorators[4].first_arg, fixture.decorators[3].first_arg);
}

BTEST(scanner, binds_to_the_last_word_ignoring_qualifiers_and_types) {
	BTEST_ASSERT_EX(
		extract(
			"Params(\n"
			"	@range(0, max=100) highp float gravity;\n"
			"	@color lowp uint a, b;\n"
			")\n"
		),
		"%s", grain_get_last_error(test_grain())
	);
	BTEST_ASSERT_EQUAL("%d", asize(fixture.decorators), 3);
	BTEST_EXPECT(fixture.decorators[0].param == sintern("gravity"));
	BTEST_EXPECT(fixture.decorators[1].param == sintern("a"));
	BTEST_EXPECT(fixture.decorators[2].param == sintern("b"));
}

BTEST(scanner, parses_ident_args) {
	BTEST_ASSERT_EX(
		extract(
			"Params(\n"
			"	@widget(SLIDER, type=COLOR_WHEEL) float gravity;\n"
			")\n"
		),
		"%s", grain_get_last_error(test_grain())
	);
	BTEST_ASSERT_EQUAL("%d", asize(fixture.decorators), 1);
	BTEST_ASSERT_EQUAL("%d", fixture.decorators[0].num_args, 2);

	grain_decorator_arg_t* args = &fixture.args[fixture.decorators[0].first_arg];
	BTEST_EXPECT_EQUAL("%d", args[0].index, 0);
	BTEST_EXPECT(args[0].name == NULL);
	BTEST_EXPECT_EQUAL("%d", args[0].type, GRAIN_DECORATOR_ARG_IDENT);
	BTEST_EXPECT(args[0].value.string == sintern("SLIDER"));
	BTEST_EXPECT_EQUAL("%d", args[1].index, -1);
	BTEST_EXPECT(args[1].name == sintern("type"));
	BTEST_EXPECT_EQUAL("%d", args[1].type, GRAIN_DECORATOR_ARG_IDENT);
	BTEST_EXPECT(args[1].value.string == sintern("COLOR_WHEEL"));
}

BTEST(scanner, no_params_block_is_a_noop) {
	BTEST_ASSERT(extract("Affector(T)\nvoid process() {}\n"));
	BTEST_EXPECT_EQUAL("%d", asize(fixture.decorators), 0);
}

BTEST(scanner, rejects_two_values_without_separator) {
	// `min` parses as a positional ident value, so the `0` is the offender
	expect_scan_error(
		"@range(min 0) float gravity;",
		"Expected `,` or `)`"
	);
}

BTEST(scanner, rejects_declaration_inside_unclosed_arg_list) {
	// `float` parses as an ident value, so `gravity` is the offender
	expect_scan_error(
		"@range(min=0, float gravity;",
		"Expected `,` or `)`"
	);
}

BTEST(scanner, rejects_unterminated_string) {
	expect_scan_error(
		"@description(\"oops) float gravity;",
		"Unterminated string"
	);
}

BTEST(scanner, rejects_non_value_argument) {
	expect_scan_error(
		"@range(min=$) float gravity;",
		"Expected an argument value"
	);
}

BTEST(scanner, rejects_missing_decorator_name) {
	expect_scan_error("@ float gravity;", "Expected a decorator name");
}

BTEST(scanner, rejects_decorator_without_declaration) {
	expect_scan_error("float gravity;\n@color", "not attached");
}

BTEST(scanner, rejects_decorator_on_empty_statement) {
	expect_scan_error("@color; float gravity;", "not attached");
}
