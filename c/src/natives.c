#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* Host procedures and host objects. A native procedure is a managed object
 * holding an index into the runtime's table of registered natives. */
typedef struct {
  ot_native_fn function;
  void* userdata;
  size_t min_args, max_args;
} oti_native;

struct oti_native_state {
  oti_native* items;
  size_t count, capacity;
  uint32_t* spaces;
  size_t space_count, space_capacity;
};

typedef struct {
  oti_object_header header;
  uint32_t index;
} oti_native_obj;

OTI_LAYOUT(oti_native_obj, 0, index);

typedef struct {
  oti_object_header header;
  const ot_host_type* type;
  size_t size;
  unsigned char data[];
} oti_host_obj;

OTI_LAYOUT(oti_host_obj, 0, type);
_Static_assert(offsetof(oti_host_obj, data) % 8 == 0, "host data alignment");

static oti_native_state* state(ot_state* rt) {
  if (!rt->natives) {
    rt->natives = calloc(1, sizeof(oti_native_state));
    if (!rt->natives) oti_error(rt, "out of host memory");
  }
  return rt->natives;
}

void oti_natives_destroy(ot_state* rt) {
  if (!rt->natives) return;
  free(rt->natives->items);
  free(rt->natives->spaces);
  free(rt->natives);
  rt->natives = NULL;
}

bool oti_native_namespace(const ot_state* rt, uint32_t space) {
  if (!rt->natives) return false;
  for (size_t i = 0; i < rt->natives->space_count; i++)
    if (rt->natives->spaces[i] == space) return true;
  return false;
}

static bool remember_space(ot_state* rt, oti_native_state* n, uint32_t space) {
  if (oti_native_namespace(rt, space)) return true;
  if (!oti_grow(rt, (void**)&n->spaces, &n->space_capacity, n->space_count + 1, sizeof(uint32_t)))
    return false;
  n->spaces[n->space_count++] = space;
  return true;
}

bool ot_define_natives(ot_state* rt, const char* space, const ot_native* natives, size_t count) {
  if (!oti_host_entry(rt)) return false;
  oti_clear_error(rt);
  if (!space || (count && !natives)) return oti_error(rt, "missing native definitions");
  oti_native_state* n = state(rt);
  if (!n) return false;
  uint32_t id = oti_namespace(rt, space, strlen(space));
  if (id == UINT32_MAX || !remember_space(rt, n, id)) return false;
  uint32_t saved = rt->current_namespace;
  bool ok = true;
  for (size_t i = 0; ok && i < count; i++) {
    const ot_native* d = &natives[i];
    if (!d->name || !d->function || d->min_args > d->max_args || n->count >= UINT32_MAX) {
      ok = oti_error(rt, "invalid native definition");
      break;
    }
    if (!oti_grow(rt, (void**)&n->items, &n->capacity, n->count + 1, sizeof(oti_native))) {
      ok = false;
      break;
    }
    ot_value object = oti_alloc(rt, K_NATIVE, 0, OTI_BYTES(oti_native_obj, 0));
    if (!object) {
      ok = false;
      break;
    }
    oti_roots roots;
    oti_roots_push(&rt->heap, &roots, &object, 1);
    rt->current_namespace = id;
    uint32_t global = oti_global(rt, d->name, strlen(d->name));
    rt->current_namespace = saved;
    oti_roots_pop(&rt->heap, &roots);
    if (global == UINT32_MAX) {
      ok = false;
      break;
    }
    ((oti_native_obj*)object)->index = (uint32_t)n->count;
    n->items[n->count++] = (oti_native){d->function, d->userdata, d->min_args, d->max_args};
    rt->globals[global] = object;
  }
  rt->current_namespace = saved;
  return ok;
}

ot_value oti_native_call(ot_state* rt, ot_value native, size_t first, size_t count) {
  oti_native* d = &rt->natives->items[((oti_native_obj*)native)->index];
  if (count < d->min_args || count > d->max_args) {
    oti_coded_error(rt, "E0301", "wrong number of arguments");
    return 0;
  }
  ot_value result = d->function(rt, d->userdata, rt->stack + first, count);
  if (!result && !oti_failed(rt)) oti_error(rt, "native procedure failed without an error");
  return result;
}

ot_value ot_make_string(ot_state* rt, const char* bytes, size_t length) {
  return oti_text(rt, K_STRING, bytes, length);
}

ot_value ot_make_float(ot_state* rt, double number) { return oti_float(rt, number); }

ot_value ot_make_list(ot_state* rt, const ot_value* items, size_t count) {
  ot_value* live = malloc((count + 1) * sizeof(ot_value));
  if (!live) return oti_fail(rt, "out of host memory");
  if (count) memcpy(live, items, count * sizeof(ot_value));
  live[count] = OT_EMPTY;
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, count + 1);
  ot_value result = live[count];
  for (size_t i = count; i-- > 0;) {
    result = oti_cons(rt, live[i], live[count]);
    if (!result) break;
    live[count] = result;
  }
  oti_roots_pop(&rt->heap, &roots);
  free(live);
  return result;
}

bool ot_number_value(ot_value v, double* out) {
  if (ot_is_int(v)) {
    *out = (double)ot_to_int(v);
    return true;
  }
  if (!oti_kind(v, K_FLOAT)) return false;
  memcpy(out, object_bytes(v), sizeof(*out));
  return true;
}

ot_value ot_make_host_object(ot_state* rt, const ot_host_type* type, const void* data,
                             size_t size) {
  if (!type || (size && !data)) return oti_fail(rt, "missing host object type or data");
  if (size > SIZE_MAX - OTI_BYTES(oti_host_obj, 0)) return oti_fail(rt, "host object too large");
  ot_value object = oti_alloc(rt, K_HOST_OBJECT, 0, OTI_BYTES(oti_host_obj, 0) + size);
  if (!object) return 0;
  oti_host_obj* o = (oti_host_obj*)object;
  o->type = type;
  o->size = size;
  if (size) memcpy(o->data, data, size);
  return object;
}

void* ot_host_object_data(ot_value v, const ot_host_type* type) {
  if (!oti_kind(v, K_HOST_OBJECT) || ((oti_host_obj*)v)->type != type) return NULL;
  return ((oti_host_obj*)v)->data;
}

const char* oti_host_object_name(ot_value v) {
  const char* name = ((oti_host_obj*)v)->type->name;
  return name ? name : "host-object";
}
