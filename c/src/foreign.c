#include "internal.h"
#ifndef OTIUM_FREESTANDING
#include "foreign_types.h"
#include <dlfcn.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

typedef struct oti_library oti_library;

struct oti_library {
  oti_library* next;
  void* handle;
};

typedef struct oti_foreign_binding oti_foreign_binding;

struct oti_foreign_binding {
  oti_foreign_binding* next;
  oti_signature* signature;
  void* target;
  bool collect_safe, marked;
};
typedef struct oti_callback oti_callback;

struct oti_callback {
  oti_callback* next;
  ot_state* runtime;
  oti_signature* signature;
  ffi_closure* closure;
  void* code;
  pthread_t thread;
  size_t root;
  bool released, marked;
};

static _Thread_local ot_state* foreign_runtime;

typedef struct oti_memory oti_memory;

struct oti_memory {
  oti_memory* next;
  void* allocation;
  uintptr_t address;
  size_t size;
  bool released, marked, external;
  oti_memory* parent;
  size_t root;
};

typedef struct {
  uintptr_t address;
  oti_memory* owner;
  size_t length;
  bool bounded, view, callback_pointer;
  oti_callback* callback;
} oti_pointer;

typedef struct {
  oti_object_header header;
  ot_value name;
  oti_library* library;
} oti_library_obj;

OTI_LAYOUT(oti_library_obj, 1, library);

/* arguments and result are private copies of the layout descriptions. */
typedef struct {
  oti_object_header header;
  ot_value library, symbol, arguments, result;
  oti_foreign_binding* binding;
} oti_function_obj;

OTI_LAYOUT(oti_function_obj, 4, binding);

typedef struct {
  oti_object_header header;
  ot_value description;
} oti_foreign_layout_obj;

OTI_FIELDS_ONLY(oti_foreign_layout_obj, 1);

typedef struct {
  oti_object_header header;
  oti_pointer pointer;
} oti_pointer_obj;

OTI_LAYOUT(oti_pointer_obj, 0, pointer);

static inline oti_library_obj* library_obj(ot_value v) {
  return OTI_CAST(oti_library_obj, K_FOREIGN_LIBRARY, v);
}

static inline oti_function_obj* function_obj(ot_value v) {
  return OTI_CAST(oti_function_obj, K_FOREIGN_FUNCTION, v);
}

static inline oti_foreign_layout_obj* foreign_layout_obj(ot_value v) {
  return OTI_CAST(oti_foreign_layout_obj, K_FOREIGN_LAYOUT, v);
}

static inline oti_pointer_obj* pointer_obj(ot_value v) {
  return OTI_CAST(oti_pointer_obj, K_POINTER, v);
}

typedef struct {
  size_t argument;
  const char* expected;
} oti_foreign_error;

struct oti_foreign_state {
  oti_library* libraries;
  oti_signature* signatures;
  oti_foreign_binding* bindings;
  oti_memory* memory;
  size_t allocations, bytes;
  uint32_t null_constant;
  char loader_error[512];
  oti_callback* callbacks;
  /* Every K_POINTER and K_FOREIGN_FUNCTION object, held weakly: collections
   * drop the dead ones and update moved ones (oti_foreign_weak). */
  ot_value* tracked;
  size_t tracked_count, tracked_capacity;
  ot_value* values;
  oti_roots roots;
  size_t value_capacity, callback_count, call_depth;
  bool pending_exit, pending_interrupt;
  int pending_status;
  oti_foreign_error* error_context;
};

static void argument_context(ot_state* rt, size_t index, const char* expected) {
  if (rt->foreign->error_context)
    *rt->foreign->error_context = (oti_foreign_error){index, expected};
}

static ot_value ffi_failure(ot_state* rt, const char* reason, const char* message) {
  oti_ffi_errorf(rt, reason, "%s", message);
  return 0;
}

static const char* c_string(ot_state* rt, ot_value value) {
  if (!oti_kind(value, K_STRING)) {
    oti_ffi_errorf(rt, "type", "expected string");
    return NULL;
  }
  size_t length;
  const char* s = oti_text_bytes(value, &length);
  if (memchr(s, 0, length)) {
    oti_ffi_errorf(rt, "encoding", "embedded NUL");
    return NULL;
  }
  return s;
}

static bool utf8(const unsigned char* s, size_t n) {
  for (size_t i = 0; i < n;) {
    unsigned char c = s[i++];
    if (c < 0x80) continue;
    unsigned more, minimum;
    uint32_t code;
    if (c >= 0xc2 && c <= 0xdf) {
      more = 1;
      minimum = 0x80;
      code = c & 31;
    } else if (c >= 0xe0 && c <= 0xef) {
      more = 2;
      minimum = 0x800;
      code = c & 15;
    } else if (c >= 0xf0 && c <= 0xf4) {
      more = 3;
      minimum = 0x10000;
      code = c & 7;
    } else return false;
    if (more > n - i) return false;
    while (more--) {
      c = s[i++];
      if ((c & 0xc0) != 0x80) return false;
      code = (code << 6) | (c & 63);
    }
    if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
  }
  return true;
}

static bool pointer(ot_state* rt, ot_value value, oti_pointer* out) {
  *out = (oti_pointer){0};
  if (!oti_kind(value, K_POINTER)) return oti_ffi_errorf(rt, "type", "expected pointer");
  *out = pointer_obj(value)->pointer;
  if (out->callback && out->callback->released)
    return oti_ffi_errorf(rt, "released", "released callback");
  for (oti_memory* m = out->owner; m; m = m->parent)
    if (m->released) return oti_ffi_errorf(rt, "released", "released pointer");
  return true;
}

/* Record a new foreign object, or return 0 if there is no room to. */
static ot_value track(ot_state* rt, ot_value value) {
  oti_foreign_state* state = rt->foreign;
  if (!value || !oti_grow(rt, (void**)&state->tracked, &state->tracked_capacity,
                          state->tracked_count + 1, sizeof(ot_value)))
    return 0;
  state->tracked[state->tracked_count++] = value;
  return value;
}

void oti_foreign_weak(ot_state* rt) {
  oti_foreign_state* state = rt->foreign;
  if (!state) return;
  size_t kept = 0;
  for (size_t i = 0; i < state->tracked_count; i++) {
    ot_value value = oti_heap_survivor(&rt->heap, state->tracked[i]);
    if (value) state->tracked[kept++] = value;
  }
  state->tracked_count = kept;
}

static ot_value pointer_object(ot_state* rt, oti_pointer p) {
  if (!p.address && !p.owner) return rt->constants[rt->foreign->null_constant];
  ot_value value = track(rt, oti_alloc(rt, K_POINTER, 0, OTI_BYTES(oti_pointer_obj, 0)));
  if (value) pointer_obj(value)->pointer = p;
  return value;
}

static ot_value wrap_pointer(ot_state* rt, void* address, bool callback_pointer) {
  oti_pointer p = {.address = (uintptr_t)address, .callback_pointer = callback_pointer};
  for (oti_callback* cb = rt->foreign->callbacks; address && cb; cb = cb->next)
    if (!cb->released && cb->code == address) {
      p.callback = cb;
      p.callback_pointer = true;
      return pointer_object(rt, p);
    }
  return pointer_object(rt, p);
}

static ot_value library_object(ot_state* rt, oti_library* library, ot_value name) {
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, &name, 1);
  ot_value value = oti_alloc(rt, K_FOREIGN_LIBRARY, 1, OTI_BYTES(oti_library_obj, 1));
  if (value) {
    library_obj(value)->library = library;
    library_obj(value)->name = name;
  }
  oti_roots_pop(&rt->heap, &roots);
  return value;
}

void oti_foreign_stats(const ot_state* rt, ot_stats* stats) {
  const oti_foreign_state* state = rt->foreign;
  if (!state) return;
  for (oti_library* p = state->libraries; p; p = p->next) stats->libraries++;
  for (oti_foreign_binding* p = state->bindings; p; p = p->next) stats->bindings++;
  for (oti_signature* p = state->signatures; p; p = p->next) stats->signatures++;
  for (oti_memory* p = state->memory; p; p = p->next) stats->memory_records++;
  for (oti_callback* p = state->callbacks; p; p = p->next) stats->callback_records++;
  stats->callbacks = state->callback_count;
  stats->allocations = state->allocations;
  stats->bytes = state->bytes;
}

void oti_foreign_collect(ot_state* rt) {
  oti_foreign_state* state = rt->foreign;
  if (!state || (!state->bindings && !state->signatures && !state->memory)) return;
  for (oti_foreign_binding* p = state->bindings; p; p = p->next) p->marked = false;
  for (oti_signature* p = state->signatures; p; p = p->next) p->marked = false;
  for (oti_memory* p = state->memory; p; p = p->next) p->marked = false;
  for (oti_callback* p = state->callbacks; p; p = p->next) p->marked = false;
  for (size_t i = 0; i < state->tracked_count; i++) {
    ot_value value = state->tracked[i];
    if (oti_kind(value, K_FOREIGN_FUNCTION)) {
      oti_foreign_binding* binding = function_obj(value)->binding;
      if (binding) {
        binding->marked = true;
        binding->signature->marked = true;
      }
    } else if (oti_kind(value, K_POINTER)) {
      oti_pointer* pointer = &pointer_obj(value)->pointer;
      if (pointer->owner) pointer->owner->marked = true;
      if (pointer->callback) pointer->callback->marked = true;
    }
  }
  oti_callback** callback = &state->callbacks;
  while (*callback) {
    oti_callback* cb = *callback;
    if (!cb->released || cb->closure) cb->signature->marked = true;
    if (!cb->marked && cb->released && !cb->closure) {
      *callback = cb->next;
      free(cb);
    } else callback = &cb->next;
  }
  oti_foreign_binding** binding = &state->bindings;
  while (*binding) {
    oti_foreign_binding* p = *binding;
    if (p->marked) binding = &p->next;
    else {
      *binding = p->next;
      free(p);
    }
  }
  /* Keep four recently bound, unused call interfaces to avoid preparing common
     * signatures during reload. Live bindings and in-progress wrappers pin theirs. */
  size_t spare = 0;
  oti_signature** signature = &state->signatures;
  while (*signature) {
    oti_signature* p = *signature;
    if (p->marked || p->pending || spare++ < 4) signature = &p->next;
    else {
      *signature = p->next;
      oti_foreign_free_abi_nodes(p->nodes);
      free(p);
    }
  }
  oti_memory** memory = &state->memory;
  while (*memory) {
    oti_memory* p = *memory;
    /* C may retain an allocation after all Otium wrappers disappear. */
    if (p->marked || !p->released) memory = &p->next;
    else {
      *memory = p->next;
      free(p);
    }
  }
}

bool oti_foreign_init(ot_state* rt) {
  rt->foreign = calloc(1, sizeof(*rt->foreign));
  if (!rt->foreign) return oti_ffi_errorf(rt, "allocation", "out of host memory");
  oti_roots_push(&rt->heap, &rt->foreign->roots, NULL, 0);
  if (!oti_grow(rt, (void**)&rt->foreign->values, &rt->foreign->value_capacity, 1,
                sizeof(ot_value)))
    return false;
  rt->foreign->values[0] = OT_NIL;
  rt->foreign->roots.slots = rt->foreign->values;
  rt->foreign->roots.count = 1;
  ot_value null = track(rt, oti_alloc(rt, K_POINTER, 0, OTI_BYTES(oti_pointer_obj, 0)));
  if (!null) return false;
  uint32_t index = oti_constant(rt, null);
  if (index == UINT32_MAX) return false;
  rt->foreign->null_constant = index;
  uint32_t global = oti_resolve_global(rt, "ffi/null", 8);
  if (global == UINT32_MAX) return false;
  oti_set_global(rt, global, rt->constants[index]);
  return true;
}

void oti_foreign_destroy(ot_state* rt) {
  oti_foreign_state* state = rt->foreign;
  if (!state) return;
  while (state->callbacks) {
    oti_callback* next = state->callbacks->next;
    if (state->callbacks->closure) ffi_closure_free(state->callbacks->closure);
    free(state->callbacks);
    state->callbacks = next;
  }
  free(state->values);
  free(state->tracked);
  while (state->bindings) {
    oti_foreign_binding* next = state->bindings->next;
    free(state->bindings);
    state->bindings = next;
  }
  while (state->signatures) {
    oti_signature* next = state->signatures->next;
    oti_foreign_free_abi_nodes(state->signatures->nodes);
    free(state->signatures);
    state->signatures = next;
  }
  while (state->libraries) {
    oti_library* next = state->libraries->next;
    dlclose(state->libraries->handle);
    free(state->libraries);
    state->libraries = next;
  }
  while (state->memory) {
    oti_memory* next = state->memory->next;
    free(state->memory->allocation);
    free(state->memory);
    state->memory = next;
  }
  free(state);
  rt->foreign = NULL;
}

static ot_value open_library(ot_state* rt, ot_value names) {
  size_t count;
  if (!oti_foreign_list_length(rt, names, 1024, &count)) return 0;
  if (!count) return ffi_failure(rt, "load", "expected nonempty library list");
  for (ot_value p = names; p != OT_EMPTY; p = oti_cdr(p)) {
    const char* name = c_string(rt, oti_car(p));
    if (!name) return 0;
    void* handle = dlopen(name, RTLD_NOW | RTLD_LOCAL);
    if (!handle) continue;
    for (oti_library* existing = rt->foreign->libraries; existing; existing = existing->next) {
      if (existing->handle != handle) continue;
      dlclose(handle);
      return library_object(rt, existing, oti_car(p));
    }
    oti_library* library = calloc(1, sizeof(*library));
    if (!library) {
      dlclose(handle);
      return ffi_failure(rt, "allocation", "out of host memory");
    }
    library->handle = handle;
    ot_value v = library_object(rt, library, oti_car(p));
    if (!v) {
      dlclose(handle);
      free(library);
      return 0;
    }
    library->next = rt->foreign->libraries;
    rt->foreign->libraries = library;
    return v;
  }
  return ffi_failure(rt, "load", "could not open a candidate library");
}

static void memory_scalar(unsigned type, void* address, oti_foreign_slot* value, bool write);

static ot_value layout_copy(ot_state* rt, ot_value value, unsigned depth, size_t* budget);

static ot_value bind_function(ot_state* rt, size_t first, bool codes) {
  ot_value* args = rt->stack + first;
  if (!oti_kind(args[0], K_FOREIGN_LIBRARY)) return ffi_failure(rt, "type", "expected library");
  const char* name = c_string(rt, args[1]);
  if (!name) return 0;
  if (args[4] != OT_TRUE && args[4] != OT_FALSE)
    return ffi_failure(rt, "type", "expected collection safety boolean");
  oti_library* library = library_obj(args[0])->library;
  dlerror();
  void* target = dlsym(library->handle, name);
  if (dlerror() || !target) return ffi_failure(rt, "symbol", "symbol not found");
  oti_signature* s = oti_foreign_signature(rt, &rt->foreign->signatures, args[2], args[3], codes);
  if (!s) return 0;
  oti_foreign_binding* b = malloc(sizeof(*b));
  if (!b) return ffi_failure(rt, "allocation", "out of host memory");
  *b = (oti_foreign_binding){rt->foreign->bindings, s, target, args[4] == OT_TRUE, false};
  /* Wrapper construction allocates; keep its host signature alive until publication. */
  s->pending = true;
  ot_value live[] = {OT_NIL, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 2);
  size_t budget = 65536;
  live[0] = layout_copy(rt, rt->stack[first + 2], 0, &budget);
  budget = 65536;
  if (live[0]) live[1] = layout_copy(rt, rt->stack[first + 3], 0, &budget);
  ot_value value =
      live[0] && live[1]
          ? track(rt, oti_alloc(rt, K_FOREIGN_FUNCTION, 4, OTI_BYTES(oti_function_obj, 4)))
          : 0;
  if (value) {
    oti_function_obj* f = function_obj(value);
    f->binding = b;
    f->library = rt->stack[first];
    f->symbol = rt->stack[first + 1];
    f->arguments = live[0];
    f->result = live[1];
  }
  oti_roots_pop(&rt->heap, &roots);
  s->pending = false;
  if (!value) {
    free(b);
    return 0;
  }
  /* Collection may have removed the old head while allocating the wrapper. */
  b->next = rt->foreign->bindings;
  rt->foreign->bindings = b;
  return value;
}

static bool to_foreign(ot_state* rt, const oti_scalar* type, ot_value value,
                       oti_foreign_slot* slot) {
  switch (type->kind) {
    case T_BOOL:
      if (value != OT_TRUE && value != OT_FALSE)
        return oti_ffi_errorf(rt, "type", "expected boolean");
      slot->boolean = value == OT_TRUE;
      return true;
    case T_SIGNED:
    case T_UNSIGNED: {
      if (!ot_is_int(value)) return oti_ffi_errorf(rt, "type", "expected exact integer");
      int64_t n = ot_to_int(value);
      if (n < type->minimum || (n >= 0 && (uint64_t)n > type->maximum))
        return oti_ffi_errorf(rt, "range", "integer argument out of range");
      if (type->kind == T_SIGNED) slot->signed_integer = n;
      else slot->unsigned_integer = (uint64_t)n;
      return true;
    }
    case T_FLOAT:
    case T_DOUBLE: {
      double d;
      if (ot_is_int(value)) d = (double)ot_to_int(value);
      else if (oti_kind(value, K_FLOAT)) d = oti_as_float(value)->value;
      else return oti_ffi_errorf(rt, "type", "expected number");
      if (type->kind == T_FLOAT) {
        if (isfinite(d) && fabs(d) > FLT_MAX)
          return oti_ffi_errorf(rt, "range", "float argument out of range");
        slot->single = (float)d;
      } else slot->real = d;
      return true;
    }
    case T_POINTER:
    case T_CALLBACK: {
      oti_pointer p;
      if (!pointer(rt, value, &p)) return false;
      if (type->kind == T_CALLBACK) {
        if (p.address && !p.callback_pointer)
          return oti_ffi_errorf(rt, "type", "expected callback pointer");
        memcpy(&slot->callback, &p.address, sizeof(slot->callback));
      } else slot->pointer = (void*)p.address;
      return true;
    }
    default: return oti_ffi_errorf(rt, "type", "unsupported conversion");
  }
}

static ot_value from_foreign(ot_state* rt, const oti_scalar* type, oti_foreign_slot value) {
  switch (type->kind) {
    case T_VOID: return OT_FALSE;
    case T_BOOL: return value.boolean ? OT_TRUE : OT_FALSE;
    case T_SIGNED:
      if (value.signed_integer < INTPTR_MIN / 2 || value.signed_integer > INTPTR_MAX / 2)
        return ffi_failure(rt, "range", "result exceeds integer range");
      return ot_int((intptr_t)value.signed_integer);
    case T_UNSIGNED:
      if (value.unsigned_integer > (uint64_t)(INTPTR_MAX / 2))
        return ffi_failure(rt, "range", "result exceeds integer range");
      return ot_int((intptr_t)value.unsigned_integer);
    case T_FLOAT: return oti_float(rt, value.single);
    case T_DOUBLE: return oti_float(rt, value.real);
    case T_CALLBACK: {
      void* address;
      memcpy(&address, &value.callback, sizeof(address));
      return wrap_pointer(rt, address, true);
    }
    case T_POINTER: return wrap_pointer(rt, value.pointer, false);
    case T_UTF8: {
      if (!value.pointer) return rt->constants[rt->foreign->null_constant];
      size_t n = strlen(value.pointer);
      if (!utf8(value.pointer, n)) return ffi_failure(rt, "encoding", "invalid UTF-8 result");
      return oti_text(rt, K_STRING, value.pointer, n);
    }
  }
  return 0;
}

static ot_value foreign_result(ot_state* rt, ot_value result, const char* op);

static void clear_callback_error(ot_state* rt) {
  oti_clear_error(rt);
  rt->raised = rt->signaled = rt->exit_pending = rt->interrupted = rt->restart_pending = false;
  rt->condition_values[0] = OT_NIL;
  rt->restart_transfer[0] = rt->restart_transfer[1] = OT_NIL;
}

static void callback_entry(ffi_cif* cif, void* result, void** arguments, void* context) {
  oti_callback* cb = context;
  if (!pthread_equal(cb->thread, pthread_self())) {
    fputs("FFI callback on a thread other than the Otium thread\n", stderr);
    abort();
  }
  ot_state* rt = cb->runtime;
  if (foreign_runtime != rt || !rt->foreign->call_depth) {
    fputs("FFI callback outside an Otium foreign call\n", stderr);
    abort();
  }
  oti_signature* sig = cb->signature;
  if (sig->result)
    memset(result, 0, cif->rtype->size < sizeof(ffi_arg) ? sizeof(ffi_arg) : cif->rtype->size);
  ot_value live[] = {OT_EMPTY, OT_NIL, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 3);
  bool ok = true;
  for (size_t i = sig->count; ok && i; i--) {
    oti_foreign_slot value = {0};
    memory_scalar(sig->arguments[i - 1], arguments[i - 1], &value, false);
    live[1] = from_foreign(rt, &oti_foreign_scalars[sig->arguments[i - 1]], value);
    if (!live[1]) {
      ok = false;
      break;
    }
    ot_value pair = oti_alloc(rt, K_PAIR, 2, 0);
    if (!pair) {
      ok = false;
      break;
    }
    oti_as_pair(pair)->car = live[1];
    oti_as_pair(pair)->cdr = live[0];
    live[0] = pair;
  }
  if (ok) {
    live[2] = oti_call_list(rt, rt->foreign->values[cb->root], live[0]);
    ok = live[2] != 0;
  }
  if (ok && sig->result) {
    oti_foreign_slot converted = {0};
    const oti_scalar* type = &oti_foreign_scalars[sig->result];
    ok = to_foreign(rt, type, live[2], &converted);
    if (ok) {
      if (type->kind == T_SIGNED && type->size < sizeof(ffi_arg))
        *(ffi_sarg*)result = (ffi_sarg)converted.signed_integer;
      else if (type->kind == T_UNSIGNED && type->size < sizeof(ffi_arg))
        *(ffi_arg*)result = (ffi_arg)converted.unsigned_integer;
      else if (type->kind == T_BOOL) *(ffi_arg*)result = converted.boolean;
      else memory_scalar(sig->result, result, &converted, true);
    }
  }
  if (!ok) {
    if (rt->restart_pending) {
      clear_callback_error(rt);
      oti_ffi_error(rt, "callback", "escape", "restart escapes a foreign callback");
    } else foreign_result(rt, 0, "callback");
    if (!rt->exit_pending && !rt->interrupted && !rt->signaled) oti_signal_error(rt);
    if (rt->restart_pending) {
      clear_callback_error(rt);
      oti_ffi_error(rt, "callback", "escape", "handler restart escapes a foreign callback");
      rt->signaled = true;
    }
    oti_foreign_state* state = rt->foreign;
    if (state->values[0] == OT_NIL && !state->pending_exit && !state->pending_interrupt) {
      if (rt->exit_pending) {
        state->pending_exit = true;
        state->pending_status = rt->exit_status;
      } else if (rt->interrupted) state->pending_interrupt = true;
      else state->values[0] = oti_error_condition(rt);
    }
    clear_callback_error(rt);
  }
  oti_roots_pop(&rt->heap, &roots);
}

static ot_value foreign_callback_count(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (count) return ffi_failure(rt, "arity", "wrong number of callback arguments");
  return ot_int((intptr_t)rt->foreign->callback_count);
}

static ot_value foreign_free_callback(ot_state* rt, size_t first, size_t count) {
  if (count != 1) return ffi_failure(rt, "arity", "wrong number of callback arguments");
  oti_foreign_state* state = rt->foreign;
  argument_context(rt, 0, "callback-pointer");
  if (!oti_kind(rt->stack[first], K_POINTER)) return ffi_failure(rt, "type", "expected callback");
  oti_pointer p = pointer_obj(rt->stack[first])->pointer;
  if (!p.callback || p.view) return ffi_failure(rt, "type", "expected callback");
  if (!p.callback->released) {
    p.callback->released = true;
    if (!state->call_depth) {
      ffi_closure_free(p.callback->closure);
      p.callback->closure = NULL;
    }
    state->values[p.callback->root] = OT_NIL;
    state->callback_count--;
  }
  return OT_FALSE;
}

static ot_value foreign_callback(ot_state* rt, size_t first, size_t count) {
  if (count != 3) return ffi_failure(rt, "arity", "wrong number of callback arguments");
  oti_foreign_state* state = rt->foreign;
  argument_context(rt, 2, NULL);
  if (!oti_procedure(rt->stack[first + 2]))
    return ffi_failure(rt, "type", "expected callback procedure");
  oti_signature* sig =
      oti_foreign_signature(rt, &state->signatures, rt->stack[first], rt->stack[first + 1], true);
  if (!sig) return 0;
  if (sig->result >= 22) return ffi_failure(rt, "signature", "invalid callback result");
  for (size_t i = 0; i < sig->count; i++)
    if (!sig->arguments[i] || sig->arguments[i] >= 22)
      return ffi_failure(rt, "signature", "invalid callback argument");
  size_t slot = 1;
  while (slot < state->roots.count && state->values[slot] != OT_NIL) slot++;
  if (!oti_grow(rt, (void**)&state->values, &state->value_capacity, slot + 1, sizeof(ot_value)))
    return 0;
  state->roots.slots = state->values;
  if (slot == state->roots.count) state->values[state->roots.count++] = OT_NIL;
  oti_callback* cb = calloc(1, sizeof(*cb));
  if (!cb) return ffi_failure(rt, "allocation", "out of host memory");
  cb->runtime = rt;
  cb->signature = sig;
  cb->root = slot;
  cb->thread = pthread_self();
  cb->closure = ffi_closure_alloc(sizeof(ffi_closure), &cb->code);
  if (!cb->closure ||
      ffi_prep_closure_loc(cb->closure, &sig->cif, callback_entry, cb, cb->code) != FFI_OK) {
    if (cb->closure) ffi_closure_free(cb->closure);
    free(cb);
    return ffi_failure(rt, "abi", "cannot prepare callback closure");
  }
  sig->pending = true;
  ot_value value = pointer_object(
      rt, (oti_pointer){.address = (uintptr_t)cb->code, .callback_pointer = true, .callback = cb});
  sig->pending = false;
  if (!value) {
    ffi_closure_free(cb->closure);
    free(cb);
    return 0;
  }
  state->values[slot] = rt->stack[first + 2];
  cb->next = state->callbacks;
  state->callbacks = cb;
  state->callback_count++;
  return value;
}

static ot_value allocate_memory(ot_state* rt, size_t size, const void* initial);
static bool access_memory(ot_state* rt, oti_pointer p, size_t offset, size_t bytes,
                          size_t alignment);

static ot_value foreign_call(ot_state* rt, size_t callable, size_t first, size_t count,
                             size_t* failed_argument, unsigned* expected_type) {
  oti_foreign_binding* binding = function_obj(rt->stack[callable])->binding;
  oti_signature* s = binding->signature;
  if (count != s->count) return ffi_failure(rt, "arity", "wrong number of arguments");
  oti_foreign_slot args[MAX_ARGUMENTS], result = {0};
  char* copies[MAX_ARGUMENTS];
  size_t lengths[MAX_ARGUMENTS];
  for (size_t i = 0; i < count; i++) copies[i] = NULL;
  bool ok = true;
  for (size_t i = 0; i < count; i++) {
    *failed_argument = i;
    *expected_type = s->arguments[i];
    ot_value value = rt->stack[first + i];
    if (s->arguments[i] == AGGREGATE) {
      oti_pointer p;
      ffi_type* type = s->argument_types[i];
      if (!pointer(rt, value, &p) || !access_memory(rt, p, 0, type->size, type->alignment)) {
        ok = false;
        break;
      }
      copies[i] = malloc(type->size);
      lengths[i] = type->size;
      if (!copies[i]) {
        ok = oti_ffi_errorf(rt, "allocation", "out of host memory");
        break;
      }
      memcpy(copies[i], (void*)p.address, type->size);
      continue;
    }
    const oti_scalar* type = &oti_foreign_scalars[s->arguments[i]];
    if (type->kind == T_UTF8) {
      if (oti_kind(value, K_POINTER)) {
        oti_pointer p;
        if (!pointer(rt, value, &p)) {
          ok = false;
          break;
        }
        if (p.address) {
          ok = oti_ffi_errorf(rt, "type", "utf8 needs string or null pointer");
          break;
        }
        args[i].pointer = NULL;
      } else {
        const char* str = c_string(rt, value);
        if (!str) {
          ok = false;
          break;
        }
        size_t n;
        oti_text_bytes(value, &n);
        copies[i] = malloc(n + 1);
        lengths[i] = n + 1;
        if (!copies[i]) {
          ok = oti_ffi_errorf(rt, "allocation", "out of host memory");
          break;
        }
        memcpy(copies[i], str, n + 1);
        args[i].pointer = copies[i];
      }
    } else if (!to_foreign(rt, type, value, &args[i])) {
      ok = false;
      break;
    }
  }
  if (ok) {
    *failed_argument = SIZE_MAX;
    *expected_type = UINT_MAX;
  }
  ot_value value = 0, aggregate_result = OT_NIL;
  oti_roots result_roots;
  oti_roots_push(&rt->heap, &result_roots, &aggregate_result, 1);
  void* aggregate_bytes = NULL;
  if (ok && s->result == AGGREGATE) {
    size_t bytes = s->result_type->size;
    aggregate_bytes = calloc(1, bytes < sizeof(ffi_arg) ? sizeof(ffi_arg) : bytes);
    if (!aggregate_bytes) ok = oti_ffi_errorf(rt, "allocation", "out of host memory");
    else {
      aggregate_result = allocate_memory(rt, bytes, NULL);
      ok = aggregate_result != 0;
    }
  }
  if (ok) {
    /* Native temporaries remain stable while callbacks allocate in the VM. */
    union {
      oti_foreign_slot slot;
      ffi_arg word;
      ffi_sarg signed_word;
    } storage[MAX_ARGUMENTS], returned = {0};

    void* addresses[MAX_ARGUMENTS];
    for (size_t i = 0; i < count; i++) {
      if (s->arguments[i] == AGGREGATE) addresses[i] = copies[i];
      else {
        memory_scalar(s->arguments[i], &storage[i], &args[i], true);
        addresses[i] = &storage[i];
      }
    }
    ot_state* previous_runtime = foreign_runtime;
    foreign_runtime = rt;
    rt->foreign->call_depth++;
    ffi_call(&s->cif, FFI_FN(binding->target), aggregate_bytes ? aggregate_bytes : &returned,
             addresses);
    rt->foreign->call_depth--;
    foreign_runtime = previous_runtime;
    if (!rt->foreign->call_depth)
      for (oti_callback* cb = rt->foreign->callbacks; cb; cb = cb->next)
        if (cb->released && cb->closure) {
          ffi_closure_free(cb->closure);
          cb->closure = NULL;
        }
    if (s->result == AGGREGATE) {
      oti_pointer p = pointer_obj(aggregate_result)->pointer;
      memcpy((void*)p.address, aggregate_bytes, s->result_type->size);
      value = aggregate_result;
    } else {
      const oti_scalar* type = &oti_foreign_scalars[s->result];
      if (type->kind == T_SIGNED && type->size < sizeof(ffi_arg))
        result.signed_integer = returned.signed_word;
      else if (type->kind == T_UNSIGNED && type->size < sizeof(ffi_arg))
        result.unsigned_integer = returned.word;
      else if (type->kind == T_BOOL) result.boolean = returned.word != 0;
      else memory_scalar(s->result, &returned, &result, false);
      if (oti_foreign_scalars[s->result].kind == T_POINTER && result.pointer)
        for (size_t i = 0; i < count; i++) {
          uintptr_t address = (uintptr_t)result.pointer, begin = (uintptr_t)copies[i];
          if (copies[i] && address >= begin && address - begin <= lengths[i]) {
            ok = oti_ffi_errorf(rt, "type", "pointer escapes temporary UTF-8 argument");
            break;
          }
        }
      if (ok) {
        *expected_type = s->result;
        value = from_foreign(rt, &oti_foreign_scalars[s->result], result);
      }
    }
  }
  oti_foreign_state* state = rt->foreign;
  if (!state->call_depth &&
      (state->values[0] != OT_NIL || state->pending_exit || state->pending_interrupt)) {
    if (aggregate_result != OT_NIL && aggregate_result) {
      oti_pointer p = pointer_obj(aggregate_result)->pointer;
      free(p.owner->allocation);
      p.owner->allocation = NULL;
      p.owner->released = true;
      state->allocations--;
      state->bytes -= p.owner->size;
    }
    clear_callback_error(rt);
    if (state->pending_exit) {
      oti_error(rt, "exit requested");
      rt->exit_pending = true;
      rt->exit_status = state->pending_status;
    } else if (state->pending_interrupt) {
      oti_error(rt, "interrupted");
      rt->interrupted = true;
    } else {
      oti_error(rt, "uncaught callback condition");
      rt->condition_values[0] = state->values[0];
      rt->raised = rt->signaled = true;
    }
    state->values[0] = OT_NIL;
    state->pending_exit = state->pending_interrupt = false;
    value = 0;
  }
  oti_roots_pop(&rt->heap, &result_roots);
  free(aggregate_bytes);
  for (size_t i = 0; i < count; i++) free(copies[i]);
  return value;
}

static bool nonnegative(ot_state* rt, ot_value value, size_t* out) {
  *out = 0;
  if (!ot_is_int(value)) return oti_ffi_errorf(rt, "type", "expected exact integer");
  intptr_t n = ot_to_int(value);
  if (n < 0) return oti_ffi_errorf(rt, "range", "expected nonnegative integer");
  *out = (size_t)n;
  return true;
}

static ot_value allocate_memory(ot_state* rt, size_t size, const void* initial) {
  oti_foreign_state* state = rt->foreign;
  if (size > (size_t)(INTPTR_MAX / 2) - state->bytes)
    return ffi_failure(rt, "range", "allocation size overflow");
  oti_memory* m = calloc(1, sizeof(*m));
  if (!m) return ffi_failure(rt, "allocation", "out of host memory");
  m->allocation = calloc(1, size ? size : 1);
  if (!m->allocation) {
    free(m);
    return ffi_failure(rt, "allocation", "allocation failed");
  }
  if (initial && size) memcpy(m->allocation, initial, size);
  m->address = (uintptr_t)m->allocation;
  m->size = size;
  ot_value p = pointer_object(
      rt, (oti_pointer){.address = m->address, .owner = m, .length = size, .bounded = true});
  if (!p) {
    free(m->allocation);
    free(m);
    return 0;
  }
  m->next = state->memory;
  state->memory = m;
  state->allocations++;
  state->bytes += size;
  return p;
}

static bool access_memory(ot_state* rt, oti_pointer p, size_t offset, size_t bytes,
                          size_t alignment) {
  if (!p.address) return oti_ffi_errorf(rt, "null", "null pointer");
  if ((p.bounded && (offset > p.length || bytes > p.length - offset)) ||
      offset > UINTPTR_MAX - p.address || bytes > UINTPTR_MAX - (p.address + offset))
    return oti_ffi_errorf(rt, "bounds", "memory access out of bounds");
  if ((p.address + offset) % alignment) return oti_ffi_errorf(rt, "alignment", "unaligned access");
  return true;
}

/* Use the declared C type's representation, not a prefix of ForeignSlot.
 * This also avoids assumptions about byte order for narrow integers. */
#define MEMORY_CASE(i, t, member)                                                                  \
  case i: {                                                                                        \
    t v;                                                                                           \
    if (write) {                                                                                   \
      v = (t)value->member;                                                                        \
      memcpy(address, &v, sizeof(v));                                                              \
    } else {                                                                                       \
      memcpy(&v, address, sizeof(v));                                                              \
      value->member = v;                                                                           \
    }                                                                                              \
    break;                                                                                         \
  }

static void memory_scalar(unsigned type, void* address, oti_foreign_slot* value, bool write) {
  switch (type) {
    case 1:
      if (write) memcpy(address, &value->boolean, sizeof(bool));
      else value->boolean = *(unsigned char*)address != 0;
      break;
      MEMORY_CASE(2, int8_t, signed_integer)
      MEMORY_CASE(3, uint8_t, unsigned_integer)
      MEMORY_CASE(4, int16_t, signed_integer)
      MEMORY_CASE(5, uint16_t, unsigned_integer)
      MEMORY_CASE(6, int32_t, signed_integer)
      MEMORY_CASE(7, uint32_t, unsigned_integer)
      MEMORY_CASE(8, int64_t, signed_integer)
      MEMORY_CASE(9, uint64_t, unsigned_integer)
      MEMORY_CASE(10, short, signed_integer)
      MEMORY_CASE(11, unsigned short, unsigned_integer)
      MEMORY_CASE(12, int, signed_integer)
      MEMORY_CASE(13, unsigned int, unsigned_integer)
      MEMORY_CASE(14, long, signed_integer)
      MEMORY_CASE(15, unsigned long, unsigned_integer)
      MEMORY_CASE(16, size_t, unsigned_integer)
      MEMORY_CASE(17, ptrdiff_t, signed_integer)
      MEMORY_CASE(18, float, single)
      MEMORY_CASE(19, double, real)
      MEMORY_CASE(20, void*, pointer)
      MEMORY_CASE(21, oti_foreign_callback, callback) MEMORY_CASE(22, void*, pointer)
  }
}

#undef MEMORY_CASE

/* Descriptions are private snapshots; callers only receive detached trees. */
static ot_value layout_copy(ot_state* rt, ot_value value, unsigned depth, size_t* budget) {
  if (!*budget || depth > 256)
    return ffi_failure(rt, "type", "layout description too deep or cyclic");
  --*budget;
  if (value == OT_EMPTY || ot_is_int(value) || oti_kind(value, K_SYMBOL)) return value;
  if (!oti_kind(value, K_PAIR)) return ffi_failure(rt, "type", "invalid layout description");
  ot_value live[] = {value, OT_NIL, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 3);
  live[1] = layout_copy(rt, oti_car(live[0]), depth + 1, budget);
  if (live[1]) live[2] = layout_copy(rt, oti_cdr(live[0]), depth + 1, budget);
  ot_value result = 0;
  if (live[1] && live[2]) {
    result = oti_alloc(rt, K_PAIR, 2, 0);
    if (result) {
      oti_as_pair(result)->car = live[1];
      oti_as_pair(result)->cdr = live[2];
    }
  }
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

static bool foreign_arity(ot_state* rt, size_t count, size_t wanted) {
  if (count != wanted) return oti_ffi_errorf(rt, "arity", "wrong number of arguments");
  return true;
}

/* ffi/%layout, and ffi/%layout-data when data is set. */
static ot_value foreign_layout(ot_state* rt, size_t first, size_t count, bool data) {
  if (count != 1) return ffi_failure(rt, "arity", "layout operation needs one argument");
  ot_value input = rt->stack[first];
  if (data) {
    if (!oti_kind(input, K_FOREIGN_LAYOUT)) return OT_FALSE;
    input = foreign_layout_obj(input)->description;
  }
  size_t budget = 65536;
  ot_value description = layout_copy(rt, input, 0, &budget);
  if (!description || data) return description;
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, &description, 1);
  ot_value result = oti_alloc(rt, K_FOREIGN_LAYOUT, 1, 0);
  if (result) foreign_layout_obj(result)->description = description;
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

static ot_value foreign_fail(ot_state* rt, size_t first, size_t count) {
  if (count != 3) return ffi_failure(rt, "arity", "fail requires operation, reason and message");
  const char *op = c_string(rt, rt->stack[first]), *reason = c_string(rt, rt->stack[first + 1]),
             *message = c_string(rt, rt->stack[first + 2]);
  return op && reason && message ? oti_ffi_error(rt, op, reason, message) : 0;
}

static ot_value foreign_error_field(ot_state* rt, size_t first, size_t count) {
  if (count != 2) return ffi_failure(rt, "arity", "error-field needs two arguments");
  const char* field = c_string(rt, rt->stack[first + 1]);
  if (!field) return 0;
  if (!oti_kind(rt->stack[first], K_TABLE)) return OT_FALSE;
  size_t n = strlen(field);
  char* name = malloc(n + 2);
  if (!name) return ffi_failure(rt, "allocation", "out of host memory");
  name[0] = ':';
  memcpy(name + 1, field, n + 1);
  ot_value key = oti_intern(rt, K_KEYWORD, name, n + 1);
  free(name);
  return key ? oti_table_get(rt, rt->stack[first], key, OT_FALSE) : 0;
}

static ot_value foreign_raw_null(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!foreign_arity(rt, count, 0)) return 0;
  return rt->constants[rt->foreign->null_constant];
}

static ot_value foreign_loader_error(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!foreign_arity(rt, count, 0)) return 0;
  return oti_text(rt, K_STRING, rt->foreign->loader_error, strlen(rt->foreign->loader_error));
}

static ot_value foreign_raw_bind(ot_state* rt, size_t first, size_t count) {
  if (!foreign_arity(rt, count, 5)) return 0;
  return bind_function(rt, first, true);
}

static ot_value foreign_raw_open(ot_state* rt, size_t first, size_t count) {
  if (!foreign_arity(rt, count, 1)) return 0;
  const char* name = c_string(rt, rt->stack[first]);
  if (!name) return 0;
  void* handle = dlopen(name, RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    const char* error = dlerror();
    snprintf(rt->foreign->loader_error, sizeof(rt->foreign->loader_error), "%s",
             error ? error : "load failed");
    return OT_FALSE;
  }
  for (oti_library* p = rt->foreign->libraries; p; p = p->next)
    if (p->handle == handle) {
      dlclose(handle);
      return library_object(rt, p, rt->stack[first]);
    }
  oti_library* library = calloc(1, sizeof(*library));
  if (!library) {
    dlclose(handle);
    return ffi_failure(rt, "allocation", "out of host memory");
  }
  library->handle = handle;
  ot_value result = library_object(rt, library, rt->stack[first]);
  if (!result) {
    dlclose(handle);
    free(library);
    return 0;
  }
  library->next = rt->foreign->libraries;
  rt->foreign->libraries = library;
  return result;
}

static ot_value foreign_raw_alloc(ot_state* rt, size_t first, size_t count) {
  if (!foreign_arity(rt, count, 2)) return 0;
  size_t size, alignment;
  if (!nonnegative(rt, rt->stack[first], &size) ||
      !nonnegative(rt, rt->stack[first + 1], &alignment))
    return 0;
  if (!alignment || (alignment & (alignment - 1)) || alignment > _Alignof(max_align_t))
    return ffi_failure(rt, "type", "invalid allocation alignment");
  return allocate_memory(rt, size, NULL);
}

/* ffi/%sizeof, and ffi/%alignof when align is set. */
static ot_value foreign_raw_sizeof(ot_state* rt, size_t first, size_t count, bool align) {
  if (!foreign_arity(rt, count, 1)) return 0;
  int type = oti_foreign_scalar_code(rt, rt->stack[first], false);
  if (type < 0) return 0;
  return ot_int(
      (intptr_t)(align ? oti_foreign_scalars[type].alignment : oti_foreign_scalars[type].size));
}

/* ffi/%ref, and ffi/%set! when set is set. The offset is in bytes. */
static ot_value foreign_raw_ref(ot_state* rt, size_t first, size_t count, bool set) {
  if (!foreign_arity(rt, count, set ? 4 : 3)) return 0;
  oti_pointer p;
  size_t offset;
  argument_context(rt, 1, NULL);
  int type = oti_foreign_scalar_code(rt, rt->stack[first + 1], true);
  if (type < 0) return 0;
  argument_context(rt, 0, "pointer");
  if (!pointer(rt, rt->stack[first], &p)) return 0;
  argument_context(rt, 2, "size-t");
  if (!nonnegative(rt, rt->stack[first + 2], &offset)) return 0;
  const oti_scalar* s = &oti_foreign_scalars[type];
  argument_context(rt, 0, "pointer");
  if (!access_memory(rt, p, offset, s->size, s->alignment)) return 0;
  oti_foreign_slot slot = {0};
  argument_context(rt, 3, s->name);
  if (set && !to_foreign(rt, s, rt->stack[first + 3], &slot)) return 0;
  memory_scalar((unsigned)type, (void*)(p.address + offset), &slot, set);
  argument_context(rt, SIZE_MAX, s->name);
  return set ? OT_FALSE : from_foreign(rt, s, slot);
}

static ot_value foreign_copy(ot_state* rt, size_t first, size_t count) {
  if (!foreign_arity(rt, count, 3)) return 0;
  oti_pointer dest, src;
  size_t bytes;
  argument_context(rt, 0, "pointer");
  if (!pointer(rt, rt->stack[first], &dest)) return 0;
  argument_context(rt, 1, "pointer");
  if (!pointer(rt, rt->stack[first + 1], &src)) return 0;
  argument_context(rt, 2, "size-t");
  if (!nonnegative(rt, rt->stack[first + 2], &bytes)) return 0;
  argument_context(rt, 0, "pointer");
  if (!access_memory(rt, dest, 0, bytes, 1)) return 0;
  argument_context(rt, 1, "pointer");
  if (!access_memory(rt, src, 0, bytes, 1)) return 0;
  memmove((void*)dest.address, (const void*)src.address, bytes);
  return OT_FALSE;
}

static ot_value foreign_write_bytes(ot_state* rt, size_t first, size_t count) {
  if (count < 2 || count > 4) return ffi_failure(rt, "arity", "wrong number of arguments");
  oti_pointer dest;
  argument_context(rt, 0, "pointer");
  if (!pointer(rt, rt->stack[first], &dest)) return 0;
  argument_context(rt, 1, NULL);
  if (!oti_kind(rt->stack[first + 1], K_BYTEVECTOR))
    return ffi_failure(rt, "type", "expected bytevector");
  size_t length = oti_bytes_length(rt->stack[first + 1]), start = 0, end = length;
  argument_context(rt, 2, "size-t");
  if (count > 2 && !nonnegative(rt, rt->stack[first + 2], &start)) return 0;
  if (start > length) return ffi_failure(rt, "range", "invalid bytevector start");
  argument_context(rt, 3, "size-t");
  if (count > 3 && !nonnegative(rt, rt->stack[first + 3], &end)) return 0;
  if (start > end || end > length) return ffi_failure(rt, "range", "invalid bytevector end");
  argument_context(rt, 0, "pointer");
  if (!access_memory(rt, dest, 0, end - start, 1)) return 0;
  memcpy((void*)dest.address, oti_bytes_data(rt->stack[first + 1]) + start, end - start);
  return OT_FALSE;
}

/* ffi/read-bytes, and ffi/read-utf8-bytes when text is set. */
static ot_value foreign_read_bytes(ot_state* rt, size_t first, size_t count, bool text) {
  if (!foreign_arity(rt, count, 2)) return 0;
  oti_pointer src;
  size_t bytes;
  argument_context(rt, 0, "pointer");
  if (!pointer(rt, rt->stack[first], &src)) return 0;
  argument_context(rt, 1, "size-t");
  if (!nonnegative(rt, rt->stack[first + 1], &bytes)) return 0;
  argument_context(rt, 0, "pointer");
  if (!access_memory(rt, src, 0, bytes, 1)) return 0;
  if (text) {
    if (!utf8((const unsigned char*)src.address, bytes))
      return ffi_failure(rt, "encoding", "invalid UTF-8");
    return oti_text(rt, K_STRING, (const char*)src.address, bytes);
  }
  ot_value result = oti_bytes_new(rt, bytes);
  if (result) memcpy(oti_bytes_data(result), (const void*)src.address, bytes);
  return result;
}

bool oti_foreign_owned(ot_value value) {
  if (!oti_kind(value, K_POINTER)) return false;
  oti_pointer p = pointer_obj(value)->pointer;
  return p.owner && !p.view;
}

static ot_value foreign_own(ot_state* rt, size_t first, size_t count) {
  if (count != 2) return ffi_failure(rt, "arity", "own requires pointer and release procedure");
  oti_pointer p;
  argument_context(rt, 0, "pointer");
  if (!pointer(rt, rt->stack[first], &p)) return 0;
  argument_context(rt, 1, NULL);
  if (!oti_procedure(rt->stack[first + 1]))
    return ffi_failure(rt, "type", "expected release procedure");
  oti_foreign_state* state = rt->foreign;
  size_t slot = 1;
  while (slot + 1 < state->roots.count &&
         (state->values[slot] != OT_NIL || state->values[slot + 1] != OT_NIL))
    slot++;
  size_t needed = slot + 2;
  if (!oti_grow(rt, (void**)&state->values, &state->value_capacity, needed, sizeof(ot_value)))
    return 0;
  state->roots.slots = state->values;
  while (state->roots.count < needed) state->values[state->roots.count++] = OT_NIL;
  oti_memory* m = calloc(1, sizeof(*m));
  if (!m) return ffi_failure(rt, "allocation", "out of host memory");
  m->address = p.address;
  m->size = p.length;
  m->external = true;
  m->parent = p.owner;
  m->root = slot;
  p.owner = m;
  p.view = false;
  ot_value result = pointer_object(rt, p);
  if (!result) {
    free(m);
    return 0;
  }
  state->values[slot] = rt->stack[first];
  state->values[slot + 1] = rt->stack[first + 1];
  m->next = state->memory;
  state->memory = m;
  return result;
}

static ot_value release_pointer(ot_state* rt, ot_value value) {
  if (!oti_kind(value, K_POINTER)) return ffi_failure(rt, "type", "expected pointer");
  oti_pointer p = pointer_obj(value)->pointer;
  if (!p.address && !p.owner) return OT_FALSE;
  if (!p.owner || p.view) return ffi_failure(rt, "type", "free needs owning pointer");
  oti_memory* m = p.owner;
  if (m->released) return OT_FALSE;
  if (!m->external) {
    m->released = true;
    free(m->allocation);
    m->allocation = NULL;
    rt->foreign->allocations--;
    rt->foreign->bytes -= m->size;
    return OT_FALSE;
  }
  ot_value live[] = {rt->foreign->values[m->root], rt->foreign->values[m->root + 1]};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 2);
  ot_value args = oti_alloc(rt, K_PAIR, 2, 0);
  ot_value result = 0;
  if (args) {
    m->released = true;
    rt->foreign->values[m->root] = rt->foreign->values[m->root + 1] = OT_NIL;
    oti_as_pair(args)->car = live[0];
    oti_as_pair(args)->cdr = OT_EMPTY;
    result = oti_call_list(rt, live[1], args);
  }
  oti_roots_pop(&rt->heap, &roots);
  return result ? OT_FALSE : 0;
}

static ot_value foreign_open(ot_state* rt, size_t first, size_t count) {
  if (!foreign_arity(rt, count, 1)) return 0;
  return open_library(rt, rt->stack[first]);
}

static ot_value foreign_bind(ot_state* rt, size_t first, size_t count) {
  if (!foreign_arity(rt, count, 5)) return 0;
  return bind_function(rt, first, false);
}

static ot_value foreign_allocation_count(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!foreign_arity(rt, count, 0)) return 0;
  return ot_int((intptr_t)rt->foreign->allocations);
}

static ot_value foreign_allocation_bytes(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!foreign_arity(rt, count, 0)) return 0;
  return ot_int((intptr_t)rt->foreign->bytes);
}

static ot_value foreign_pointer_p(ot_state* rt, size_t first, size_t count) {
  if (!foreign_arity(rt, count, 1)) return 0;
  return oti_kind(rt->stack[first], K_POINTER) ? OT_TRUE : OT_FALSE;
}

static ot_value foreign_null_p(ot_state* rt, size_t first, size_t count) {
  if (!foreign_arity(rt, count, 1)) return 0;
  ot_value value = rt->stack[first];
  if (!oti_kind(value, K_POINTER)) return OT_FALSE;
  return !pointer_obj(value)->pointer.address ? OT_TRUE : OT_FALSE;
}

static ot_value foreign_has_symbol(ot_state* rt, size_t first, size_t count) {
  if (!foreign_arity(rt, count, 2)) return 0;
  argument_context(rt, 0, NULL);
  if (!oti_kind(rt->stack[first], K_FOREIGN_LIBRARY))
    return ffi_failure(rt, "type", "expected library");
  argument_context(rt, 1, "utf8");
  const char* name = c_string(rt, rt->stack[first + 1]);
  if (!name) return 0;
  oti_library* lib = library_obj(rt->stack[first])->library;
  dlerror();
  void* target = dlsym(lib->handle, name);
  return !dlerror() && target ? OT_TRUE : OT_FALSE;
}

static ot_value foreign_alloc_utf8(ot_state* rt, size_t first, size_t count) {
  if (!foreign_arity(rt, count, 1)) return 0;
  argument_context(rt, 0, "utf8");
  const char* s = c_string(rt, rt->stack[first]);
  if (!s) return 0;
  size_t n;
  oti_text_bytes(rt->stack[first], &n);
  return allocate_memory(rt, n + 1, s);
}

/* ffi/sizeof, and ffi/alignof when align is set. */
static ot_value foreign_sizeof(ot_state* rt, size_t first, size_t count, bool align) {
  if (!foreign_arity(rt, count, 1)) return 0;
  argument_context(rt, 0, NULL);
  int type = oti_foreign_scalar(rt, rt->stack[first], true);
  if (type < 0) return 0;
  return ot_int(
      (intptr_t)(align ? oti_foreign_scalars[type].alignment : oti_foreign_scalars[type].size));
}

static ot_value foreign_alloc(ot_state* rt, size_t first, size_t count) {
  if (!foreign_arity(rt, count, 2)) return 0;
  argument_context(rt, 0, NULL);
  int type = oti_foreign_scalar(rt, rt->stack[first], true);
  if (type < 0) return 0;
  size_t n;
  argument_context(rt, 1, "size-t");
  if (!nonnegative(rt, rt->stack[first + 1], &n)) return 0;
  if (n > SIZE_MAX / oti_foreign_scalars[type].size)
    return ffi_failure(rt, "range", "allocation size overflow");
  return allocate_memory(rt, n * oti_foreign_scalars[type].size, NULL);
}

static ot_value foreign_free(ot_state* rt, size_t first, size_t count) {
  if (!foreign_arity(rt, count, 1)) return 0;
  argument_context(rt, 0, "pointer");
  return release_pointer(rt, rt->stack[first]);
}

static ot_value foreign_pointer_eq(ot_state* rt, size_t first, size_t count) {
  if (!foreign_arity(rt, count, 2)) return 0;
  ot_value a = rt->stack[first], b = rt->stack[first + 1];
  argument_context(rt, 0, "pointer");
  if (!oti_kind(a, K_POINTER)) return ffi_failure(rt, "type", "expected pointer");
  argument_context(rt, 1, "pointer");
  if (!oti_kind(b, K_POINTER)) return ffi_failure(rt, "type", "expected pointer");
  return pointer_obj(a)->pointer.address == pointer_obj(b)->pointer.address ? OT_TRUE : OT_FALSE;
}

static ot_value foreign_view(ot_state* rt, size_t first, size_t count) {
  if (!foreign_arity(rt, count, 3)) return 0;
  oti_pointer p;
  size_t offset, bytes;
  argument_context(rt, 0, "pointer");
  if (!pointer(rt, rt->stack[first], &p)) return 0;
  argument_context(rt, 1, "size-t");
  if (!nonnegative(rt, rt->stack[first + 1], &offset)) return 0;
  argument_context(rt, 2, "size-t");
  if (!nonnegative(rt, rt->stack[first + 2], &bytes)) return 0;
  argument_context(rt, 0, "pointer");
  if (!access_memory(rt, p, offset, bytes, 1)) return 0;
  p.address += offset;
  p.length = bytes;
  p.bounded = true;
  p.view = true;
  return pointer_object(rt, p);
}

static ot_value foreign_read_utf8(ot_state* rt, size_t first, size_t count) {
  if (!foreign_arity(rt, count, 2)) return 0;
  oti_pointer p;
  size_t limit;
  argument_context(rt, 0, "pointer");
  if (!pointer(rt, rt->stack[first], &p)) return 0;
  argument_context(rt, 1, "size-t");
  if (!nonnegative(rt, rt->stack[first + 1], &limit)) return 0;
  argument_context(rt, 0, "pointer");
  if (!access_memory(rt, p, 0, limit, 1)) return 0;
  const char* s = (const char*)p.address;
  const char* end = memchr(s, 0, limit);
  if (!end) return ffi_failure(rt, "bounds", "missing string terminator");
  if (!utf8((const unsigned char*)s, (size_t)(end - s)))
    return ffi_failure(rt, "encoding", "invalid UTF-8");
  return oti_text(rt, K_STRING, s, (size_t)(end - s));
}

/* ffi/ref, and ffi/set! when set is set. The index counts elements. */
static ot_value foreign_ref(ot_state* rt, size_t first, size_t count, bool set) {
  if (!foreign_arity(rt, count, set ? 4 : 3)) return 0;
  oti_pointer p;
  argument_context(rt, 0, "pointer");
  if (!pointer(rt, rt->stack[first], &p)) return 0;
  argument_context(rt, 1, NULL);
  int type = oti_foreign_scalar(rt, rt->stack[first + 1], true);
  if (type < 0) return 0;
  size_t index;
  argument_context(rt, 2, "size-t");
  if (!nonnegative(rt, rt->stack[first + 2], &index)) return 0;
  const oti_scalar* s = &oti_foreign_scalars[type];
  if (index > SIZE_MAX / s->size) return ffi_failure(rt, "range", "offset overflow");
  size_t offset = index * s->size;
  argument_context(rt, 0, "pointer");
  if (!access_memory(rt, p, offset, s->size, s->alignment)) return 0;
  oti_foreign_slot slot = {0};
  argument_context(rt, 3, s->name);
  if (set && !to_foreign(rt, s, rt->stack[first + 3], &slot)) return 0;
  memory_scalar((unsigned)type, (void*)(p.address + offset), &slot, set);
  argument_context(rt, SIZE_MAX, s->name);
  return set ? OT_FALSE : from_foreign(rt, s, slot);
}

static bool new_ffi_error(ot_state* rt) {
  return !rt->raised && !rt->exit_pending && !rt->interrupted && !rt->restart_pending &&
         rt->error.set && rt->error.ffi_reason;
}

static ot_value foreign_result(ot_state* rt, ot_value result, const char* op) {
  if (result || !new_ffi_error(rt)) return result;
  oti_error_record e;
  oti_roots roots;
  oti_take_error(rt, &e, &roots);
  ot_value condition = oti_ffi_error(rt, op, e.ffi_reason, e.message);
  oti_drop_error(rt, &e, &roots);
  return condition;
}

static bool condition_field(ot_state* rt, const char* name, ot_value value) {
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, &value, 1);
  ot_value key = oti_intern(rt, K_KEYWORD, name, strlen(name));
  bool ok = key && oti_table_set(rt, rt->condition_values[0], key, value);
  oti_roots_pop(&rt->heap, &roots);
  return ok;
}

static bool library_context(ot_state* rt, ot_value library, ot_value symbol) {
  ot_value live[] = {library, symbol};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 2);
  bool ok = true;
  if (oti_kind(live[0], K_FOREIGN_LIBRARY))
    ok = condition_field(rt, ":library", library_obj(live[0])->name);
  if (ok && oti_kind(live[1], K_STRING)) ok = condition_field(rt, ":symbol", live[1]);
  oti_roots_pop(&rt->heap, &roots);
  return ok;
}

ot_value oti_foreign_call(ot_state* rt, size_t callable, size_t first, size_t count) {
  size_t argument = SIZE_MAX;
  unsigned expected = UINT_MAX;
  ot_value result = foreign_call(rt, callable, first, count, &argument, &expected);
  bool context = !result && new_ffi_error(rt);
  result = foreign_result(rt, result, "call");
  if (!context || !rt->raised) return result;
  ot_value function = rt->stack[callable];
  if (!library_context(rt, function_obj(function)->library, function_obj(function)->symbol))
    return 0;
  if (argument != SIZE_MAX && !condition_field(rt, ":argument", ot_int((intptr_t)argument)))
    return 0;
  ot_value type = OT_FALSE;
  if (expected < AGGREGATE)
    type = oti_intern(rt, K_SYMBOL, oti_foreign_scalars[expected].name,
                      strlen(oti_foreign_scalars[expected].name));
  else if (expected == AGGREGATE) {
    type = oti_alloc(rt, K_FOREIGN_LAYOUT, 1, 0);
    if (type) {
      function = rt->stack[callable];
      foreign_layout_obj(type)->description =
          argument == SIZE_MAX ? function_obj(function)->result
                               : list_item(function_obj(function)->arguments, argument);
    }
  }
  if (type) condition_field(rt, ":expected-type", type);
  return 0;
}

static ot_value foreign_get(ot_state* rt, size_t first, size_t count) {
  return foreign_ref(rt, first, count, false);
}

static ot_value foreign_set(ot_state* rt, size_t first, size_t count) {
  return foreign_ref(rt, first, count, true);
}

static ot_value foreign_size(ot_state* rt, size_t first, size_t count) {
  return foreign_sizeof(rt, first, count, false);
}

static ot_value foreign_align(ot_state* rt, size_t first, size_t count) {
  return foreign_sizeof(rt, first, count, true);
}

static ot_value foreign_bytes_read(ot_state* rt, size_t first, size_t count) {
  return foreign_read_bytes(rt, first, count, false);
}

static ot_value foreign_utf8_bytes_read(ot_state* rt, size_t first, size_t count) {
  return foreign_read_bytes(rt, first, count, true);
}

static ot_value foreign_raw_size(ot_state* rt, size_t first, size_t count) {
  return foreign_raw_sizeof(rt, first, count, false);
}

static ot_value foreign_raw_align(ot_state* rt, size_t first, size_t count) {
  return foreign_raw_sizeof(rt, first, count, true);
}

static ot_value foreign_raw_get(ot_state* rt, size_t first, size_t count) {
  return foreign_raw_ref(rt, first, count, false);
}

static ot_value foreign_raw_set(ot_state* rt, size_t first, size_t count) {
  return foreign_raw_ref(rt, first, count, true);
}

static ot_value foreign_layout_type(ot_state* rt, size_t first, size_t count) {
  return foreign_layout(rt, first, count, false);
}

static ot_value foreign_layout_data(ot_state* rt, size_t first, size_t count) {
  return foreign_layout(rt, first, count, true);
}

/* Run an FFI builtin, turning an FFI error into an ffi-error condition
 * named after op and annotated with the argument context the builtin left. */
static ot_value foreign_run(ot_state* rt, const char* op, oti_builtin_fn run, size_t first,
                            size_t count) {
  oti_foreign_error context = {SIZE_MAX, NULL};
  oti_foreign_error* previous = rt->foreign->error_context;
  rt->foreign->error_context = &context;
  ot_value result = run(rt, first, count);
  rt->foreign->error_context = previous;
  bool annotate = !result && new_ffi_error(rt);
  result = foreign_result(rt, result, op);
  if (annotate && rt->raised) {
    if ((run == foreign_bind || run == foreign_raw_bind) && count >= 2)
      library_context(rt, rt->stack[first], rt->stack[first + 1]);
    if (context.argument != SIZE_MAX)
      condition_field(rt, ":argument", ot_int((intptr_t)context.argument));
    if (context.expected) {
      ot_value type = oti_intern(rt, K_SYMBOL, context.expected, strlen(context.expected));
      if (type) condition_field(rt, ":expected-type", type);
    }
  }
  return result;
}

#else
/* Without an operating system there is no libffi, dlopen or threads, so every
 * FFI builtin raises an ffi-error with reason unavailable. */
bool oti_foreign_init(ot_state* rt) {
  (void)rt;
  return true;
}

void oti_foreign_destroy(ot_state* rt) { (void)rt; }

void oti_foreign_collect(ot_state* rt) { (void)rt; }

void oti_foreign_weak(ot_state* rt) { (void)rt; }

void oti_foreign_stats(const ot_state* rt, ot_stats* stats) {
  (void)rt;
  (void)stats;
}

bool oti_foreign_owned(ot_value value) {
  (void)value;
  return false;
}

static ot_value foreign_unavailable(ot_state* rt, const char* op) {
  return oti_ffi_error(rt, op, "unavailable", "foreign calls are not available in this build");
}

ot_value oti_foreign_call(ot_state* rt, size_t callable, size_t first, size_t count) {
  (void)callable;
  (void)first;
  (void)count;
  return foreign_unavailable(rt, "call");
}

#define foreign_run(rt, op, run, first, count)                                                     \
  ((void)(first), (void)(count), foreign_unavailable(rt, op))
#endif

static ot_value ffi_open_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "open-library", foreign_open, first, count);
}

static ot_value ffi_bind_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "bind", foreign_bind, first, count);
}

static ot_value ffi_has_symbol_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "has-symbol?", foreign_has_symbol, first, count);
}

static ot_value ffi_pointer_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "pointer?", foreign_pointer_p, first, count);
}

static ot_value ffi_null_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "null?", foreign_null_p, first, count);
}

static ot_value ffi_pointer_eq_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "pointer=?", foreign_pointer_eq, first, count);
}

static ot_value ffi_alloc_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "alloc", foreign_alloc, first, count);
}

static ot_value ffi_free_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "free!", foreign_free, first, count);
}

static ot_value ffi_ref_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "ref", foreign_get, first, count);
}

static ot_value ffi_set_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "set!", foreign_set, first, count);
}

static ot_value ffi_sizeof_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "sizeof", foreign_size, first, count);
}

static ot_value ffi_alignof_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "alignof", foreign_align, first, count);
}

static ot_value ffi_view_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "view", foreign_view, first, count);
}

static ot_value ffi_alloc_utf8_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "alloc-utf8", foreign_alloc_utf8, first, count);
}

static ot_value ffi_read_utf8_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "read-utf8", foreign_read_utf8, first, count);
}

static ot_value ffi_count_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "allocation-count", foreign_allocation_count, first, count);
}

static ot_value ffi_bytes_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "allocation-bytes", foreign_allocation_bytes, first, count);
}

static ot_value ffi_copy_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "copy!", foreign_copy, first, count);
}

static ot_value ffi_read_bytes_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "read-bytes", foreign_bytes_read, first, count);
}

static ot_value ffi_write_bytes_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "write-bytes!", foreign_write_bytes, first, count);
}

static ot_value ffi_read_utf8_bytes_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "read-utf8-bytes", foreign_utf8_bytes_read, first, count);
}

static ot_value ffi_own_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "own", foreign_own, first, count);
}

static ot_value ffi_callback_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "callback", foreign_callback, first, count);
}

static ot_value ffi_free_callback_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "free-callback!", foreign_free_callback, first, count);
}

static ot_value ffi_callback_count_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "callback-count", foreign_callback_count, first, count);
}

static ot_value ffi_raw_null_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "null?", foreign_raw_null, first, count);
}

static ot_value ffi_raw_size_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "sizeof", foreign_raw_size, first, count);
}

static ot_value ffi_raw_align_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "alignof", foreign_raw_align, first, count);
}

static ot_value ffi_raw_bind_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "bind", foreign_raw_bind, first, count);
}

static ot_value ffi_raw_open_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "open-library", foreign_raw_open, first, count);
}

static ot_value ffi_loader_error_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "loader-error", foreign_loader_error, first, count);
}

static ot_value ffi_raw_alloc_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "alloc", foreign_raw_alloc, first, count);
}

static ot_value ffi_raw_ref_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "ref", foreign_raw_get, first, count);
}

static ot_value ffi_raw_set_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "set!", foreign_raw_set, first, count);
}

static ot_value ffi_fail_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "foreign", foreign_fail, first, count);
}

static ot_value ffi_error_field_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "error-field", foreign_error_field, first, count);
}

static ot_value ffi_layout_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "layout", foreign_layout_type, first, count);
}

static ot_value ffi_layout_data_builtin(ot_state* rt, size_t first, size_t count) {
  return foreign_run(rt, "layout", foreign_layout_data, first, count);
}

const oti_builtin_def oti_foreign_builtins[] = {
    {B_FFI_OPEN, "ffi/open-library", ffi_open_builtin},
    {B_FFI_BIND, "ffi/bind", ffi_bind_builtin},
    {B_FFI_HAS_SYMBOL, "ffi/has-symbol?", ffi_has_symbol_builtin},
    {B_FFI_POINTER, "ffi/pointer?", ffi_pointer_builtin},
    {B_FFI_NULL, "ffi/null?", ffi_null_builtin},
    {B_FFI_POINTER_EQ, "ffi/pointer=?", ffi_pointer_eq_builtin},
    {B_FFI_ALLOC, "ffi/alloc", ffi_alloc_builtin},
    {B_FFI_FREE, "ffi/free!", ffi_free_builtin},
    {B_FFI_REF, "ffi/ref", ffi_ref_builtin},
    {B_FFI_SET, "ffi/set!", ffi_set_builtin},
    {B_FFI_SIZEOF, "ffi/sizeof", ffi_sizeof_builtin},
    {B_FFI_ALIGNOF, "ffi/alignof", ffi_alignof_builtin},
    {B_FFI_VIEW, "ffi/view", ffi_view_builtin},
    {B_FFI_ALLOC_UTF8, "ffi/alloc-utf8", ffi_alloc_utf8_builtin},
    {B_FFI_READ_UTF8, "ffi/read-utf8", ffi_read_utf8_builtin},
    {B_FFI_COUNT, "ffi/allocation-count", ffi_count_builtin},
    {B_FFI_BYTES, "ffi/allocation-bytes", ffi_bytes_builtin},
    {B_FFI_COPY, "ffi/copy!", ffi_copy_builtin},
    {B_FFI_READ_BYTES, "ffi/read-bytes", ffi_read_bytes_builtin},
    {B_FFI_WRITE_BYTES, "ffi/write-bytes!", ffi_write_bytes_builtin},
    {B_FFI_READ_UTF8_BYTES, "ffi/read-utf8-bytes", ffi_read_utf8_bytes_builtin},
    {B_FFI_OWN, "ffi/own", ffi_own_builtin},
    {B_FFI_CALLBACK, "ffi/%callback", ffi_callback_builtin},
    {B_FFI_FREE_CALLBACK, "ffi/free-callback!", ffi_free_callback_builtin},
    {B_FFI_CALLBACK_COUNT, "ffi/callback-count", ffi_callback_count_builtin},
    {B_FFI_RAW_NULL, "ffi/%null", ffi_raw_null_builtin},
    {B_FFI_RAW_SIZE, "ffi/%sizeof", ffi_raw_size_builtin},
    {B_FFI_RAW_ALIGN, "ffi/%alignof", ffi_raw_align_builtin},
    {B_FFI_RAW_BIND, "ffi/%bind", ffi_raw_bind_builtin},
    {B_FFI_RAW_OPEN, "ffi/%open", ffi_raw_open_builtin},
    {B_FFI_LOADER_ERROR, "ffi/%loader-error", ffi_loader_error_builtin},
    {B_FFI_RAW_ALLOC, "ffi/%alloc", ffi_raw_alloc_builtin},
    {B_FFI_RAW_REF, "ffi/%ref", ffi_raw_ref_builtin},
    {B_FFI_RAW_SET, "ffi/%set!", ffi_raw_set_builtin},
    {B_FFI_FAIL, "ffi/%fail", ffi_fail_builtin},
    {B_FFI_ERROR_FIELD, "ffi/%error-field", ffi_error_field_builtin},
    {B_FFI_LAYOUT, "ffi/%layout", ffi_layout_builtin},
    {B_FFI_LAYOUT_DATA, "ffi/%layout-data", ffi_layout_data_builtin},
    {0, NULL, NULL},
};
