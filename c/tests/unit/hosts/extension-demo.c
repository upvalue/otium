/* An embedding host with a native demo namespace, for c/tests/ext-demo.ot.
 * Usage: extension-demo [-L DIR]... FILE
 *
 * demo/make-inline-counter keeps its count in the host object itself.
 * demo/make-owned-counter keeps a pointer to host memory, which
 * demo/release-counter! frees; using a released counter is an error. */
#include "otium.h"
#include <stdlib.h>
#include <string.h>

static const ot_host_type inline_counter = {"demo-counter"};
static const ot_host_type owned_counter = {"demo-owned-counter"};

static ot_value make_inline(ot_state* rt, void* userdata, const ot_value* args, size_t count) {
  (void)userdata;
  (void)count;
  if (!ot_is_int(args[0])) return ot_native_error(rt, "counter: expected an integer");
  intptr_t value = ot_to_int(args[0]);
  return ot_make_host_object(rt, &inline_counter, &value, sizeof(value));
}

static ot_value make_owned(ot_state* rt, void* userdata, const ot_value* args, size_t count) {
  (void)userdata;
  (void)count;
  if (!ot_is_int(args[0])) return ot_native_error(rt, "owned-counter: expected an integer");
  intptr_t* cell = malloc(sizeof(*cell));
  if (!cell) return ot_native_error(rt, "owned-counter: out of memory");
  *cell = ot_to_int(args[0]);
  ot_value result = ot_make_host_object(rt, &owned_counter, &cell, sizeof(cell));
  if (!result) free(cell);
  return result;
}

/* The counter's cell, or NULL after recording an error. */
static intptr_t* counter(ot_state* rt, ot_value value, const char* who) {
  intptr_t* cell = ot_host_object_data(value, &inline_counter);
  if (cell) return cell;
  intptr_t** owned = ot_host_object_data(value, &owned_counter);
  if (!owned) {
    ot_native_error(rt, "%s: expected a demo counter", who);
    return NULL;
  }
  if (!*owned) ot_native_error(rt, "%s: counter was released", who);
  return *owned;
}

static ot_value increment(ot_state* rt, void* userdata, const ot_value* args, size_t count) {
  (void)userdata;
  (void)count;
  intptr_t* cell = counter(rt, args[0], "counter-inc!");
  if (!cell) return 0;
  (*cell)++;
  return OT_NIL;
}

static ot_value value(ot_state* rt, void* userdata, const ot_value* args, size_t count) {
  (void)userdata;
  (void)count;
  intptr_t* cell = counter(rt, args[0], "counter-value");
  return cell ? ot_int(*cell) : 0;
}

static ot_value release(ot_state* rt, void* userdata, const ot_value* args, size_t count) {
  (void)userdata;
  (void)count;
  intptr_t** owned = ot_host_object_data(args[0], &owned_counter);
  if (!owned) return ot_native_error(rt, "release-counter!: expected an owned counter");
  free(*owned);
  *owned = NULL;
  return OT_NIL;
}

static const ot_native demo[] = {
    {"make-inline-counter", make_inline, 1, 1, NULL},
    {"make-owned-counter", make_owned, 1, 1, NULL},
    {"counter-inc!", increment, 1, 1, NULL},
    {"counter-value", value, 1, 1, NULL},
    {"release-counter!", release, 1, 1, NULL},
};

static char* read_all(const char* path, size_t* length) {
  FILE* file = fopen(path, "rb");
  if (!file) return NULL;
  size_t capacity = 4096, used = 0;
  char* buffer = malloc(capacity);
  while (buffer) {
    used += fread(buffer + used, 1, capacity - used, file);
    if (used < capacity) break;
    char* grown = realloc(buffer, capacity *= 2);
    if (!grown) free(buffer);
    buffer = grown;
  }
  fclose(file);
  *length = used;
  return buffer;
}

int main(int argc, char** argv) {
  ot_state* rt = ot_create(1024 * 1024, 128 * 1024);
  if (!rt || !ot_load_expander(rt, OTIUM_LIBRARY_DIR)) return 1;
  int i = 1;
  for (; i + 1 < argc && !strcmp(argv[i], "-L"); i += 2)
    if (!ot_add_load_path(rt, argv[i + 1])) return 1;
  if (i + 1 != argc) {
    fprintf(stderr, "usage: %s [-L DIR]... FILE\n", argv[0]);
    return 2;
  }
  size_t length;
  char* source = read_all(argv[i], &length);
  bool ok = source && ot_load_core(rt, OTIUM_LIBRARY_DIR) &&
            ot_define_natives(rt, "demo", demo, sizeof(demo) / sizeof(demo[0]));
  ot_value result;
  if (ok) ok = ot_eval(rt, source, length, &result);
  if (!ok) fprintf(stderr, "%s: %s\n", argv[i], source ? ot_error(rt) : "cannot read file");
  free(source);
  ot_destroy(rt);
  return ok ? 0 : 1;
}
