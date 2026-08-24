#include "shared.h"

static btest_suite_t find = {
	.name = "decorator/find",
};

// @range(0, max=100) as it would come out of grain_inspect_archetype
static grain_param_decorator_t
make_range(grain_decorator_arg_t args[2]) {
	args[0] = (grain_decorator_arg_t){
		.index = 0,
		.type = GRAIN_DECORATOR_ARG_NUMBER,
		.value.number = 0.f,
	};
	args[1] = (grain_decorator_arg_t){
		.index = -1,
		.name = sintern("max"),
		.type = GRAIN_DECORATOR_ARG_NUMBER,
		.value.number = 100.f,
	};
	return (grain_param_decorator_t){
		.name = sintern("range"),
		.args = args,
		.num_args = 2,
	};
}

BTEST(find, resolves_positional_or_named) {
	grain_decorator_arg_t args[2];
	grain_param_decorator_t range = make_range(args);

	grain_decorator_arg_t out;
	// `min` was passed positionally, `max` by name; both lookups hand in both
	BTEST_EXPECT(grain_find_decorator_arg(&range, 0, "min", &out));
	BTEST_EXPECT_EQUAL("%f", out.value.number, 0.f);
	BTEST_EXPECT(grain_find_decorator_arg(&range, 1, "max", &out));
	BTEST_EXPECT_EQUAL("%f", out.value.number, 100.f);
}

BTEST(find, missing_arg_returns_false) {
	grain_decorator_arg_t args[2];
	grain_param_decorator_t range = make_range(args);

	grain_decorator_arg_t out;
	BTEST_EXPECT(!grain_find_decorator_arg(&range, 2, "step", &out));
}

BTEST(find, null_decorator_returns_false) {
	grain_decorator_arg_t out;
	BTEST_EXPECT(!grain_find_decorator_arg(NULL, 0, "min", &out));
}

BTEST(find, name_only_lookup) {
	grain_decorator_arg_t args[2];
	grain_param_decorator_t range = make_range(args);

	grain_decorator_arg_t out;
	// index -1 matches no positional slot, so only the named arg can resolve
	BTEST_EXPECT(grain_find_decorator_arg(&range, -1, "max", &out));
	BTEST_EXPECT_EQUAL("%f", out.value.number, 100.f);
	BTEST_EXPECT(!grain_find_decorator_arg(&range, -1, NULL, &out));
}

BTEST(find, finds_decorator_by_name) {
	grain_decorator_arg_t args[2];
	grain_param_decorator_t decorators[] = {
		{ .name = sintern("color") },
		make_range(args),
	};
	grain_param_info_t param = {
		.name = sintern("gravity"),
		.decorators = decorators,
		.num_decorators = 2,
	};

	BTEST_EXPECT(grain_find_decorator(&param, "range") == &decorators[1]);
	BTEST_EXPECT(grain_find_decorator(&param, "color") == &decorators[0]);
	BTEST_EXPECT(grain_find_decorator(&param, "step") == NULL);
}
