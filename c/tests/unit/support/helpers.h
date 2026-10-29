#ifndef OTIUM_TEST_HELPERS_H
#define OTIUM_TEST_HELPERS_H

/* Shared helpers for tests that drive the runtime through the public API.
 * Every helper fails the current test, naming the source, on mismatch. */

#include "otium.h"
#include "test.h"

/* OTIUM_LIBRARY_DIR and OTIUM_TEST_FIXTURE_DIR are set by the Makefile. */
#define TEST_LIBRARY_DIR OTIUM_LIBRARY_DIR
#define TEST_FIXTURE(name) OTIUM_TEST_FIXTURE_DIR "/" name
/* A shared library the build made, named without its platform extension. */
#define TEST_SHARED_LIBRARY(name) TEST_FIXTURE(name "." OTIUM_TEST_LIBRARY_EXTENSION)

ot_state* new_runtime(size_t heap_bytes, size_t stack_limit);
/* A runtime with a 1 MiB heap, 128 KiB stack and the core library. */
ot_state* new_core_runtime(void);

ot_value eval(ot_state* rt, const char* source);
void eval_file(ot_state* rt, const char* path);
void check_true(ot_state* rt, const char* source);
void check_int(ot_state* rt, const char* source, intptr_t expected);
/* Compare ot_print output. */
void check_printed(ot_state* rt, const char* source, const char* expected);
/* Expect evaluation to fail with an error containing message, then check that
 * the runtime still evaluates normally. */
void check_error(ot_state* rt, const char* source, const char* message);

/* The printed form of a value, owned by the caller. */
char* printed(ot_state* rt, ot_value value);
/* A whole file, NUL-terminated and owned by the caller. */
char* read_file(const char* path, size_t* length);

#endif
