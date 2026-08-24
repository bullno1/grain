#include "decorator.h"
#include "internal.h"
#include <string.h>
#include <stdlib.h>

static bool
grain_is_ident_start(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool
grain_is_ident_char(char c) {
	return grain_is_ident_start(c) || (c >= '0' && c <= '9');
}

// Advances past whitespace and comments. Returns false on an unterminated
// block comment.
static bool
grain_skip_space(char** pos_ptr) {
	char* pos = *pos_ptr;
	for (;;) {
		if (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
			++pos;
		} else if (pos[0] == '/' && pos[1] == '/') {
			while (*pos != '\0' && *pos != '\n') { ++pos; }
		} else if (pos[0] == '/' && pos[1] == '*') {
			char* end = strstr(pos + 2, "*/");
			if (end == NULL) { *pos_ptr = pos; return false; }
			pos = end + 2;
		} else {
			break;
		}
	}
	*pos_ptr = pos;
	return true;
}

static char*
grain_find_params_block(char* source) {
	char* pos = source;
	while (*pos != '\0') {
		if (!grain_skip_space(&pos)) { return NULL; }
		if (grain_is_ident_start(*pos)) {
			char* start = pos;
			while (grain_is_ident_char(*pos)) { ++pos; }
			if (pos - start == 6 && memcmp(start, "Params", 6) == 0) {
				char* after = pos;
				if (!grain_skip_space(&after)) { return NULL; }
				if (*after == '(') { return after + 1; }
			}
		} else if (*pos != '\0') {
			++pos;
		}
	}
	return NULL;
}

static void
grain_blank(char* start, char* end) {
	for (char* c = start; c < end; ++c) {
		if (*c != '\n' && *c != '\r') { *c = ' '; }
	}
}

static bool
grain_parse_decorator(
	grain_t* grain,
	char** pos_ptr,
	CK_DYNA grain_decorator_t** decorators,
	CK_DYNA grain_decorator_arg_t** args
) {
	char* pos = *pos_ptr + 1;  // past `@`
	if (!grain_is_ident_start(*pos)) {
		grain_set_last_error(grain, "Expected a decorator name after `@`");
		return false;
	}
	char* name_start = pos;
	while (grain_is_ident_char(*pos)) { ++pos; }
	grain_decorator_t decorator = {
		.name = sintern_range(name_start, pos),
		.first_arg = asize(*args),
	};

	// The argument list is optional; commit to it only if a `(` follows.
	char* peek = pos;
	if (grain_skip_space(&peek) && *peek == '(') {
		pos = peek + 1;
		int positional_index = 0;
		for (;;) {
			if (!grain_skip_space(&pos)) { goto unterminated; }
			if (*pos == ')') { ++pos; break; }
			if (*pos == '\0') { goto unterminated; }

			grain_decorator_arg_t arg = { .index = -1 };
			if (grain_is_ident_start(*pos)) {
				char* arg_name_start = pos;
				while (grain_is_ident_char(*pos)) { ++pos; }
				arg.name = sintern_range(arg_name_start, pos);
				if (!grain_skip_space(&pos) || *pos != '=') {
					grain_set_last_error(grain, grain_sprintf(
						grain, "Expected `=` after argument `%s` of decorator `@%s`",
						arg.name, decorator.name
					));
					return false;
				}
				++pos;
				if (!grain_skip_space(&pos)) { goto unterminated; }
			} else {
				arg.index = positional_index++;
			}

			if (*pos == '"') {
				++pos;
				char buf[1024];
				int len = 0;
				while (*pos != '"') {
					char c = *pos;
					if (c == '\0' || c == '\n') {
						grain_set_last_error(grain, grain_sprintf(
							grain, "Unterminated string in decorator `@%s`", decorator.name
						));
						return false;
					}
					if (c == '\\' && (pos[1] == '"' || pos[1] == '\\')) {
						c = pos[1];
						++pos;
					}
					if (len >= (int)sizeof(buf) - 1) {
						grain_set_last_error(grain, grain_sprintf(
							grain, "String too long in decorator `@%s`", decorator.name
						));
						return false;
					}
					buf[len++] = c;
					++pos;
				}
				++pos;
				buf[len] = '\0';
				arg.type = GRAIN_DECORATOR_ARG_STRING;
				arg.value.string = sintern(buf);
			} else {
				char* end;
				float number = strtof(pos, &end);
				if (end == pos) {
					grain_set_last_error(grain, grain_sprintf(
						grain, "Expected an argument value in decorator `@%s`", decorator.name
					));
					return false;
				}
				arg.type = GRAIN_DECORATOR_ARG_NUMBER;
				arg.value.number = number;
				pos = end;
			}

			apush(*args, arg);
			++decorator.num_args;

			if (!grain_skip_space(&pos)) { goto unterminated; }
			if (*pos == ',') {
				++pos;
			} else if (*pos != ')') {
				grain_set_last_error(grain, grain_sprintf(
					grain, "Expected `,` or `)` in decorator `@%s`", decorator.name
				));
				return false;
			}
		}
	}

	apush(*decorators, decorator);
	*pos_ptr = pos;
	return true;

unterminated:
	grain_set_last_error(grain, grain_sprintf(
		grain, "Unterminated argument list in decorator `@%s`", decorator.name
	));
	return false;
}

static bool
grain_scan_params_block(
	grain_t* grain,
	char* block,
	CK_DYNA grain_decorator_t** decorators,
	CK_DYNA grain_decorator_arg_t** args
) {
	// A declarator is the last identifier of each comma-separated segment:
	// qualifiers and types are skipped naturally, and the compiler validates
	// the actual declaration right after.
	CK_DYNA const char** declarators = NULL;
	const char* last_ident = NULL;
	int pending_start = asize(*decorators);
	bool ok = false;

	char* pos = block;
	for (;;) {
		if (!grain_skip_space(&pos)) {
			grain_set_last_error(grain, "Unterminated comment in `Params` block");
			goto end;
		}
		char c = *pos;
		if (c == '\0') {
			grain_set_last_error(grain, "Unterminated `Params` block");
			goto end;
		} else if (c == ')') {
			if (asize(*decorators) > pending_start) {
				grain_set_last_error(grain, "Decorator is not attached to any declaration");
				goto end;
			}
			ok = true;
			goto end;
		} else if (c == '@') {
			char* start = pos;
			if (!grain_parse_decorator(grain, &pos, decorators, args)) { goto end; }
			grain_blank(start, pos);
		} else if (grain_is_ident_start(c)) {
			char* start = pos;
			while (grain_is_ident_char(*pos)) { ++pos; }
			last_ident = sintern_range(start, pos);
		} else if (c == ',') {
			++pos;
			if (last_ident != NULL) {
				apush(declarators, last_ident);
				last_ident = NULL;
			}
		} else if (c == ';') {
			++pos;
			if (last_ident != NULL) {
				apush(declarators, last_ident);
				last_ident = NULL;
			}
			int num_pending = asize(*decorators) - pending_start;
			if (num_pending > 0) {
				if (asize(declarators) < 1) {
					grain_set_last_error(grain, "Decorator is not attached to a declaration");
					goto end;
				}
				for (int i = 0; i < num_pending; ++i) {
					(*decorators)[pending_start + i].param = declarators[0];
				}
				// Extra declarators (`float a, b;`) get duplicated entries.
				for (int decl = 1; decl < asize(declarators); ++decl) {
					for (int i = 0; i < num_pending; ++i) {
						grain_decorator_t dup = (*decorators)[pending_start + i];
						dup.param = declarators[decl];
						apush(*decorators, dup);
					}
				}
			}
			pending_start = asize(*decorators);
			aclear(declarators);
		} else {
			++pos;
		}
	}

end:
	afree(declarators);
	return ok;
}

bool
grain_decorator_extract(
	grain_t* grain,
	char* source,
	CK_DYNA grain_decorator_t** decorators,
	CK_DYNA grain_decorator_arg_t** args
) {
	char* block = grain_find_params_block(source);
	// No Params block: nothing to strip, leave the source to the compiler.
	if (block == NULL) { return true; }
	return grain_scan_params_block(grain, block, decorators, args);
}

const grain_param_decorator_t*
grain_find_decorator(const grain_param_info_t* param, const char* name) {
	const char* interned = sintern(name);
	for (int i = 0; i < param->num_decorators; ++i) {
		if (param->decorators[i].name == interned) {
			return &param->decorators[i];
		}
	}
	return NULL;
}

bool
grain_find_decorator_arg(
	const grain_param_decorator_t* decorator,
	int index,
	const char* name,
	grain_decorator_arg_t* out
) {
	if (decorator == NULL) { return false; }

	const char* interned = name != NULL ? sintern(name) : NULL;
	for (int i = 0; i < decorator->num_args; ++i) {
		const grain_decorator_arg_t* arg = &decorator->args[i];
		bool matched = arg->name == NULL
			? arg->index == index
			: (interned != NULL && arg->name == interned);
		if (matched) {
			*out = *arg;
			return true;
		}
	}
	return false;
}
