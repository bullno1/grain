#ifndef GRAIN_DECORATOR_H
#define GRAIN_DECORATOR_H

#include <grain.h>
#include <cute.h>

typedef struct {
	const char* param;   // interned member name it binds to
	const char* name;    // interned decorator name
	int first_arg;
	int num_args;
} grain_decorator_t;

// Parses `@name(args)` decorators out of the Params block of `source`,
// blanking them with spaces in place so the source stays a valid module with
// unchanged line numbers. Appends to the two arrays; args of one decorator are
// contiguous at [first_arg, first_arg + num_args).
bool
grain_decorator_extract(
	grain_t* grain,
	char* source,
	CK_DYNA grain_decorator_t** decorators,
	CK_DYNA grain_decorator_arg_t** args
);

#endif
