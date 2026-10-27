#define _POSIX_C_SOURCE 200809L
#include "helpers.h"
#include <stdlib.h>
#include <string.h>

ot_state* new_runtime(size_t semispace_bytes, size_t stack_limit) {
  ot_state* rt = ot_create(semispace_bytes, stack_limit);
  CHECK(rt);
  return rt;
}

ot_state* new_core_runtime(void) {
  ot_state* rt = new_runtime(1024 * 1024, 128 * 1024);
  if (!ot_load_core(rt, TEST_LIBRARY_DIR))
    test_fail(__FILE__, __LINE__, "cannot load core: %s", ot_error(rt));
  return rt;
}

ot_value eval(ot_state* rt, const char* source) {
  ot_value result;
  if (!ot_eval(rt, source, strlen(source), &result))
    test_fail(__FILE__, __LINE__, "%s\n  error: %s", source, ot_error(rt));
  return result;
}

void eval_file(ot_state* rt, const char* path) {
  size_t length;
  char* source = read_file(path, &length);
  ot_value result;
  if (!ot_eval(rt, source, length, &result))
    test_fail(__FILE__, __LINE__, "%s: %s", path, ot_error(rt));
  free(source);
}

void check_true(ot_state* rt, const char* source) {
  ot_value result = eval(rt, source);
  if (result != OT_TRUE)
    test_fail(__FILE__, __LINE__, "%s\n  expected #t, got %s", source, printed(rt, result));
}

void check_int(ot_state* rt, const char* source, intptr_t expected) {
  ot_value result = eval(rt, source);
  if (result != ot_int(expected))
    test_fail(__FILE__, __LINE__, "%s\n  expected %ld, got %s", source, (long)expected,
              printed(rt, result));
}

void check_printed(ot_state* rt, const char* source, const char* expected) {
  char* text = printed(rt, eval(rt, source));
  if (strcmp(text, expected))
    test_fail(__FILE__, __LINE__, "%s\n  expected %s, got %s", source, expected, text);
  free(text);
}

void check_error(ot_state* rt, const char* source, const char* message) {
  ot_value result;
  if (ot_eval(rt, source, strlen(source), &result))
    test_fail(__FILE__, __LINE__, "%s\n  expected error containing \"%s\", got %s", source, message,
              printed(rt, result));
  if (!strstr(ot_error(rt), message))
    test_fail(__FILE__, __LINE__, "%s\n  expected error containing \"%s\", got \"%s\"", source,
              message, ot_error(rt));
  check_int(rt, "(+ 20 22)", 42);
}

char* printed(ot_state* rt, ot_value value) {
  char* text = NULL;
  size_t size = 0;
  FILE* out = open_memstream(&text, &size);
  CHECK(out);
  ot_print(rt, out, value);
  CHECK(!fclose(out));
  return text;
}

char* read_file(const char* path, size_t* length) {
  FILE* file = fopen(path, "rb");
  if (!file) test_fail(__FILE__, __LINE__, "cannot open %s", path);
  size_t capacity = 4096, used = 0;
  char* data = malloc(capacity + 1);
  CHECK(data);
  for (;;) {
    used += fread(data + used, 1, capacity - used, file);
    CHECK(!ferror(file));
    if (feof(file)) break;
    capacity *= 2;
    data = realloc(data, capacity + 1);
    CHECK(data);
  }
  fclose(file);
  data[used] = 0;
  *length = used;
  return data;
}
