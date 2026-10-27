#include "internal.h"
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

static ot_value failure(ot_state* rt, const char* message) {
  oti_error(rt, message);
  return 0;
}

static const char* c_string(ot_state* rt, ot_value value) {
  if (!oti_kind(value, K_STRING)) {
    oti_error(rt, "FFI type: expected string");
    return NULL;
  }
  size_t length;
  const char* s = oti_text_bytes(value, &length);
  if (memchr(s, 0, length)) {
    oti_error(rt, "FFI encoding: embedded NUL");
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
  if (!oti_kind(value, K_POINTER)) return oti_error(rt, "FFI type: expected pointer");
  memcpy(out, object_bytes(value), sizeof(*out));
  if (out->callback && out->callback->released) return oti_error(rt, "FFI released callback");
  for (oti_memory* m = out->owner; m; m = m->parent)
    if (m->released) return oti_error(rt, "FFI released pointer");
  return true;
}

static ot_value pointer_object(ot_state* rt, oti_pointer p) {
  if (!p.address && !p.owner) return rt->constants[rt->foreign->null_constant];
  ot_value value = oti_alloc(rt, K_POINTER, 0, sizeof(p));
  if (value) memcpy(object_bytes(value), &p, sizeof(p));
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
  ot_value value = oti_alloc(rt, K_FOREIGN_LIBRARY, 1, sizeof(library));
  if (value) {
    memcpy(object_bytes(value), &library, sizeof(library));
    object_fields(value)[0] = name;
  }
  oti_roots_pop(&rt->heap, &roots);
  return value;
}

static void* object_data(ot_value value) {
  void* data;
  memcpy(&data, object_bytes(value), sizeof(data));
  return data;
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
  unsigned char* space = rt->heap.space[rt->heap.active];
  for (size_t offset = 0; offset < rt->heap.used;) {
    ot_value value = (ot_value)(space + offset);
    if (oti_kind(value, K_FOREIGN_FUNCTION)) {
      oti_foreign_binding* binding = object_data(value);
      if (binding) {
        binding->marked = true;
        binding->signature->marked = true;
      }
    } else if (oti_kind(value, K_POINTER)) {
      oti_pointer pointer;
      memcpy(&pointer, object_bytes(value), sizeof(pointer));
      if (pointer.owner) pointer.owner->marked = true;
      if (pointer.callback) pointer.callback->marked = true;
    }
    offset += ((oti_object_header*)value)->size;
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
  if (!rt->foreign) return oti_error(rt, "FFI allocation: out of host memory");
  oti_roots_push(&rt->heap, &rt->foreign->roots, NULL, 0);
  if (!oti_grow(rt, (void**)&rt->foreign->values, &rt->foreign->value_capacity, 1,
                sizeof(ot_value)))
    return false;
  rt->foreign->values[0] = OT_NIL;
  rt->foreign->roots.slots = rt->foreign->values;
  rt->foreign->roots.count = 1;
  ot_value null = oti_alloc(rt, K_POINTER, 0, sizeof(oti_pointer));
  if (!null) return false;
  uint32_t index = oti_constant(rt, null);
  if (index == UINT32_MAX) return false;
  rt->foreign->null_constant = index;
  uint32_t global = oti_resolve_global(rt, "ffi/null", 8);
  if (global == UINT32_MAX) return false;
  rt->globals[global] = rt->constants[index];
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
  if (!count) return failure(rt, "FFI load: expected nonempty library list");
  for (ot_value p = names; p != OT_EMPTY; p = object_fields(p)[1]) {
    const char* name = c_string(rt, object_fields(p)[0]);
    if (!name) return 0;
    void* handle = dlopen(name, RTLD_NOW | RTLD_LOCAL);
    if (!handle) continue;
    for (oti_library* existing = rt->foreign->libraries; existing; existing = existing->next) {
      if (existing->handle != handle) continue;
      dlclose(handle);
      return library_object(rt, existing, object_fields(p)[0]);
    }
    oti_library* library = calloc(1, sizeof(*library));
    if (!library) {
      dlclose(handle);
      return failure(rt, "FFI allocation: out of host memory");
    }
    library->handle = handle;
    ot_value v = library_object(rt, library, object_fields(p)[0]);
    if (!v) {
      dlclose(handle);
      free(library);
      return 0;
    }
    library->next = rt->foreign->libraries;
    rt->foreign->libraries = library;
    return v;
  }
  return failure(rt, "FFI load: could not open a candidate library");
}

static void memory_scalar(unsigned type, void* address, oti_foreign_slot* value, bool write);

static ot_value layout_copy(ot_state* rt, ot_value value, unsigned depth, size_t* budget);

static ot_value bind_function(ot_state* rt, size_t first, bool codes) {
  ot_value* args = rt->stack + first;
  if (!oti_kind(args[0], K_FOREIGN_LIBRARY)) return failure(rt, "FFI type: expected library");
  const char* name = c_string(rt, args[1]);
  if (!name) return 0;
  if (args[4] != OT_TRUE && args[4] != OT_FALSE)
    return failure(rt, "FFI type: expected collection safety boolean");
  oti_library* library = object_data(args[0]);
  dlerror();
  void* target = dlsym(library->handle, name);
  if (dlerror() || !target) return failure(rt, "FFI symbol: symbol not found");
  oti_signature* s = oti_foreign_signature(rt, &rt->foreign->signatures, args[2], args[3], codes);
  if (!s) return 0;
  oti_foreign_binding* b = malloc(sizeof(*b));
  if (!b) return failure(rt, "FFI allocation: out of host memory");
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
  ot_value value = live[0] && live[1] ? oti_alloc(rt, K_FOREIGN_FUNCTION, 4, sizeof(b)) : 0;
  if (value) {
    memcpy(object_bytes(value), &b, sizeof(b));
    object_fields(value)[0] = rt->stack[first];
    object_fields(value)[1] = rt->stack[first + 1];
    object_fields(value)[2] = live[0];
    object_fields(value)[3] = live[1];
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
      if (value != OT_TRUE && value != OT_FALSE) return oti_error(rt, "FFI type: expected boolean");
      slot->boolean = value == OT_TRUE;
      return true;
    case T_SIGNED:
    case T_UNSIGNED: {
      if (!ot_is_int(value)) return oti_error(rt, "FFI type: expected exact integer");
      int64_t n = ot_to_int(value);
      if (n < type->minimum || (n >= 0 && (uint64_t)n > type->maximum))
        return oti_error(rt, "FFI range: integer argument out of range");
      if (type->kind == T_SIGNED) slot->signed_integer = n;
      else slot->unsigned_integer = (uint64_t)n;
      return true;
    }
    case T_FLOAT:
    case T_DOUBLE: {
      double d;
      if (ot_is_int(value)) d = (double)ot_to_int(value);
      else if (oti_kind(value, K_FLOAT)) memcpy(&d, object_bytes(value), sizeof(d));
      else return oti_error(rt, "FFI type: expected number");
      if (type->kind == T_FLOAT) {
        if (isfinite(d) && fabs(d) > FLT_MAX)
          return oti_error(rt, "FFI range: float argument out of range");
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
          return oti_error(rt, "FFI type: expected callback pointer");
        memcpy(&slot->callback, &p.address, sizeof(slot->callback));
      } else slot->pointer = (void*)p.address;
      return true;
    }
    default: return oti_error(rt, "FFI type: unsupported conversion");
  }
}

static ot_value from_foreign(ot_state* rt, const oti_scalar* type, oti_foreign_slot value) {
  switch (type->kind) {
    case T_VOID: return OT_FALSE;
    case T_BOOL: return value.boolean ? OT_TRUE : OT_FALSE;
    case T_SIGNED:
      if (value.signed_integer < INTPTR_MIN / 2 || value.signed_integer > INTPTR_MAX / 2)
        return failure(rt, "FFI range: result exceeds integer range");
      return ot_int((intptr_t)value.signed_integer);
    case T_UNSIGNED:
      if (value.unsigned_integer > (uint64_t)(INTPTR_MAX / 2))
        return failure(rt, "FFI range: result exceeds integer range");
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
      if (!utf8(value.pointer, n)) return failure(rt, "FFI encoding: invalid UTF-8 result");
      return oti_text(rt, K_STRING, value.pointer, n);
    }
  }
  return 0;
}

static ot_value foreign_result(ot_state* rt, ot_value result, const char* op);

static void clear_callback_error(ot_state* rt) {
  rt->error[0] = 0;
  rt->error_code = NULL;
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
    object_fields(pair)[0] = live[1];
    object_fields(pair)[1] = live[0];
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

static ot_value callback_builtin(ot_state* rt, unsigned id, size_t first, size_t count) {
  oti_foreign_state* state = rt->foreign;
  size_t wanted = id == B_FFI_CALLBACK ? 3 : id == B_FFI_CALLBACK_COUNT ? 0 : 1;
  if (count != wanted) return failure(rt, "FFI arity: wrong number of callback arguments");
  if (id == B_FFI_CALLBACK_COUNT) return ot_int((intptr_t)state->callback_count);
  if (id == B_FFI_FREE_CALLBACK) {
    argument_context(rt, 0, "callback-pointer");
    if (!oti_kind(rt->stack[first], K_POINTER)) return failure(rt, "FFI type: expected callback");
    oti_pointer p;
    memcpy(&p, object_bytes(rt->stack[first]), sizeof(p));
    if (!p.callback || p.view) return failure(rt, "FFI type: expected callback");
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
  argument_context(rt, 2, NULL);
  if (oti_support_builtin(rt, B_PROCEDURE_P, first + 2, 1) != OT_TRUE)
    return failure(rt, "FFI type: expected callback procedure");
  oti_signature* sig =
      oti_foreign_signature(rt, &state->signatures, rt->stack[first], rt->stack[first + 1], true);
  if (!sig) return 0;
  if (sig->result >= 22) return failure(rt, "FFI signature: invalid callback result");
  for (size_t i = 0; i < sig->count; i++)
    if (!sig->arguments[i] || sig->arguments[i] >= 22)
      return failure(rt, "FFI signature: invalid callback argument");
  size_t slot = 1;
  while (slot < state->roots.count && state->values[slot] != OT_NIL) slot++;
  if (!oti_grow(rt, (void**)&state->values, &state->value_capacity, slot + 1, sizeof(ot_value)))
    return 0;
  state->roots.slots = state->values;
  if (slot == state->roots.count) state->values[state->roots.count++] = OT_NIL;
  oti_callback* cb = calloc(1, sizeof(*cb));
  if (!cb) return failure(rt, "FFI allocation: out of host memory");
  cb->runtime = rt;
  cb->signature = sig;
  cb->root = slot;
  cb->thread = pthread_self();
  cb->closure = ffi_closure_alloc(sizeof(ffi_closure), &cb->code);
  if (!cb->closure ||
      ffi_prep_closure_loc(cb->closure, &sig->cif, callback_entry, cb, cb->code) != FFI_OK) {
    if (cb->closure) ffi_closure_free(cb->closure);
    free(cb);
    return failure(rt, "FFI abi: cannot prepare callback closure");
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
  oti_foreign_binding* binding = object_data(rt->stack[callable]);
  oti_signature* s = binding->signature;
  if (count != s->count) return failure(rt, "FFI arity: wrong number of arguments");
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
        ok = oti_error(rt, "FFI allocation: out of host memory");
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
          ok = oti_error(rt, "FFI type: utf8 needs string or null pointer");
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
          ok = oti_error(rt, "FFI allocation: out of host memory");
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
    if (!aggregate_bytes) ok = oti_error(rt, "FFI allocation: out of host memory");
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
      oti_pointer p;
      memcpy(&p, object_bytes(aggregate_result), sizeof(p));
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
            ok = oti_error(rt, "FFI type: pointer escapes temporary UTF-8 argument");
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
      oti_pointer p;
      memcpy(&p, object_bytes(aggregate_result), sizeof(p));
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
  if (!ot_is_int(value)) return oti_error(rt, "FFI type: expected exact integer");
  intptr_t n = ot_to_int(value);
  if (n < 0) return oti_error(rt, "FFI range: expected nonnegative integer");
  *out = (size_t)n;
  return true;
}

static ot_value allocate_memory(ot_state* rt, size_t size, const void* initial) {
  oti_foreign_state* state = rt->foreign;
  if (size > (size_t)(INTPTR_MAX / 2) - state->bytes)
    return failure(rt, "FFI range: allocation size overflow");
  oti_memory* m = calloc(1, sizeof(*m));
  if (!m) return failure(rt, "FFI allocation: out of host memory");
  m->allocation = calloc(1, size ? size : 1);
  if (!m->allocation) {
    free(m);
    return failure(rt, "FFI allocation failed");
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
  if (!p.address) return oti_error(rt, "FFI null pointer");
  if ((p.bounded && (offset > p.length || bytes > p.length - offset)) ||
      offset > UINTPTR_MAX - p.address || bytes > UINTPTR_MAX - (p.address + offset))
    return oti_error(rt, "FFI bounds: memory access out of bounds");
  if ((p.address + offset) % alignment) return oti_error(rt, "FFI alignment: unaligned access");
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
    return failure(rt, "FFI type: layout description too deep or cyclic");
  --*budget;
  if (value == OT_EMPTY || ot_is_int(value) || oti_kind(value, K_SYMBOL)) return value;
  if (!oti_kind(value, K_PAIR)) return failure(rt, "FFI type: invalid layout description");
  ot_value live[] = {value, OT_NIL, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 3);
  live[1] = layout_copy(rt, object_fields(live[0])[0], depth + 1, budget);
  if (live[1]) live[2] = layout_copy(rt, object_fields(live[0])[1], depth + 1, budget);
  ot_value result = 0;
  if (live[1] && live[2]) {
    result = oti_alloc(rt, K_PAIR, 2, 0);
    if (result) {
      object_fields(result)[0] = live[1];
      object_fields(result)[1] = live[2];
    }
  }
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

static ot_value shared_builtin(ot_state* rt, unsigned id, size_t first, size_t count) {
  if (id == B_FFI_LAYOUT || id == B_FFI_LAYOUT_DATA) {
    if (count != 1) return failure(rt, "FFI arity: layout operation needs one argument");
    ot_value input = rt->stack[first];
    if (id == B_FFI_LAYOUT_DATA) {
      if (!oti_kind(input, K_FOREIGN_LAYOUT)) return OT_FALSE;
      input = object_fields(input)[0];
    }
    size_t budget = 65536;
    ot_value description = layout_copy(rt, input, 0, &budget);
    if (!description || id == B_FFI_LAYOUT_DATA) return description;
    oti_roots roots;
    oti_roots_push(&rt->heap, &roots, &description, 1);
    ot_value result = oti_alloc(rt, K_FOREIGN_LAYOUT, 1, 0);
    if (result) object_fields(result)[0] = description;
    oti_roots_pop(&rt->heap, &roots);
    return result;
  }

  if (id == B_FFI_FAIL) {
    if (count != 3) return failure(rt, "FFI arity: fail requires operation, reason and message");
    const char *op = c_string(rt, rt->stack[first]), *reason = c_string(rt, rt->stack[first + 1]),
               *message = c_string(rt, rt->stack[first + 2]);
    return op && reason && message ? oti_ffi_error(rt, op, reason, message) : 0;
  }
  if (id == B_FFI_ERROR_FIELD) {
    if (count != 2) return failure(rt, "FFI arity: error-field needs two arguments");
    const char* field = c_string(rt, rt->stack[first + 1]);
    if (!field) return 0;
    if (!oti_kind(rt->stack[first], K_TABLE)) return OT_FALSE;
    size_t n = strlen(field);
    char* name = malloc(n + 2);
    if (!name) return failure(rt, "FFI allocation: out of host memory");
    name[0] = ':';
    memcpy(name + 1, field, n + 1);
    ot_value key = oti_intern(rt, K_KEYWORD, name, n + 1);
    free(name);
    return key ? oti_table_get(rt, rt->stack[first], key, OT_FALSE) : 0;
  }
  size_t wanted = id == B_FFI_RAW_NULL || id == B_FFI_LOADER_ERROR ? 0
                  : id == B_FFI_RAW_BIND                           ? 5
                  : id == B_FFI_RAW_ALLOC                          ? 2
                  : id == B_FFI_RAW_REF                            ? 3
                  : id == B_FFI_RAW_SET                            ? 4
                                                                   : 1;
  if (count != wanted) return failure(rt, "FFI arity: wrong number of arguments");
  if (id == B_FFI_RAW_NULL) return rt->constants[rt->foreign->null_constant];
  if (id == B_FFI_LOADER_ERROR)
    return oti_text(rt, K_STRING, rt->foreign->loader_error, strlen(rt->foreign->loader_error));
  if (id == B_FFI_RAW_BIND) return bind_function(rt, first, true);
  if (id == B_FFI_RAW_OPEN) {
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
      return failure(rt, "FFI allocation: out of host memory");
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
  if (id == B_FFI_RAW_ALLOC) {
    size_t size, alignment;
    if (!nonnegative(rt, rt->stack[first], &size) ||
        !nonnegative(rt, rt->stack[first + 1], &alignment))
      return 0;
    if (!alignment || (alignment & (alignment - 1)) || alignment > _Alignof(max_align_t))
      return failure(rt, "FFI type: invalid allocation alignment");
    return allocate_memory(rt, size, NULL);
  }
  if (id == B_FFI_RAW_SIZE || id == B_FFI_RAW_ALIGN) {
    int type = oti_foreign_scalar_code(rt, rt->stack[first], false);
    if (type < 0) return 0;
    return ot_int((intptr_t)(id == B_FFI_RAW_SIZE ? oti_foreign_scalars[type].size
                                                  : oti_foreign_scalars[type].alignment));
  }
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
  if (id == B_FFI_RAW_SET && !to_foreign(rt, s, rt->stack[first + 3], &slot)) return 0;
  memory_scalar((unsigned)type, (void*)(p.address + offset), &slot, id == B_FFI_RAW_SET);
  argument_context(rt, SIZE_MAX, s->name);
  return id == B_FFI_RAW_SET ? OT_FALSE : from_foreign(rt, s, slot);
}

static ot_value bulk_memory(ot_state* rt, unsigned id, size_t first, size_t count) {
  size_t min = id == B_FFI_COPY ? 3 : 2;
  size_t max = id == B_FFI_WRITE_BYTES ? 4 : min;
  if (count < min || count > max) return failure(rt, "FFI arity: wrong number of arguments");
  ot_value* args = rt->stack + first;
  oti_pointer dest;
  argument_context(rt, 0, "pointer");
  if (!pointer(rt, args[0], &dest)) return 0;
  if (id == B_FFI_COPY) {
    oti_pointer src;
    size_t bytes;
    argument_context(rt, 1, "pointer");
    if (!pointer(rt, args[1], &src)) return 0;
    argument_context(rt, 2, "size-t");
    if (!nonnegative(rt, args[2], &bytes)) return 0;
    argument_context(rt, 0, "pointer");
    if (!access_memory(rt, dest, 0, bytes, 1)) return 0;
    argument_context(rt, 1, "pointer");
    if (!access_memory(rt, src, 0, bytes, 1)) return 0;
    memmove((void*)dest.address, (const void*)src.address, bytes);
    return OT_FALSE;
  }
  if (id == B_FFI_WRITE_BYTES) {
    argument_context(rt, 1, NULL);
    if (!oti_kind(args[1], K_BYTEVECTOR)) return failure(rt, "FFI type: expected bytevector");
    size_t length = oti_bytes_length(args[1]), start = 0, end = length;
    argument_context(rt, 2, "size-t");
    if (count > 2 && !nonnegative(rt, args[2], &start)) return 0;
    if (start > length) return failure(rt, "FFI range: invalid bytevector start");
    argument_context(rt, 3, "size-t");
    if (count > 3 && !nonnegative(rt, args[3], &end)) return 0;
    if (start > end || end > length) return failure(rt, "FFI range: invalid bytevector end");
    argument_context(rt, 0, "pointer");
    if (!access_memory(rt, dest, 0, end - start, 1)) return 0;
    memcpy((void*)dest.address, oti_bytes_data(args[1]) + start, end - start);
    return OT_FALSE;
  }
  size_t bytes;
  argument_context(rt, 1, "size-t");
  if (!nonnegative(rt, args[1], &bytes)) return 0;
  argument_context(rt, 0, "pointer");
  if (!access_memory(rt, dest, 0, bytes, 1)) return 0;
  if (id == B_FFI_READ_UTF8_BYTES) {
    if (!utf8((const unsigned char*)dest.address, bytes))
      return failure(rt, "FFI encoding: invalid UTF-8");
    return oti_text(rt, K_STRING, (const char*)dest.address, bytes);
  }
  ot_value result = oti_bytes_new(rt, bytes);
  if (result) memcpy(oti_bytes_data(result), (const void*)dest.address, bytes);
  return result;
}

bool oti_foreign_owned(ot_value value) {
  if (!oti_kind(value, K_POINTER)) return false;
  oti_pointer p;
  memcpy(&p, object_bytes(value), sizeof(p));
  return p.owner && !p.view;
}

static ot_value own_pointer(ot_state* rt, size_t first, size_t count) {
  if (count != 2) return failure(rt, "FFI arity: own requires pointer and release procedure");
  oti_pointer p;
  argument_context(rt, 0, "pointer");
  if (!pointer(rt, rt->stack[first], &p)) return 0;
  argument_context(rt, 1, NULL);
  if (oti_support_builtin(rt, B_PROCEDURE_P, first + 1, 1) != OT_TRUE)
    return failure(rt, "FFI type: expected release procedure");
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
  if (!m) return failure(rt, "FFI allocation: out of host memory");
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
  if (!oti_kind(value, K_POINTER)) return failure(rt, "FFI type: expected pointer");
  oti_pointer p;
  memcpy(&p, object_bytes(value), sizeof(p));
  if (!p.address && !p.owner) return OT_FALSE;
  if (!p.owner || p.view) return failure(rt, "FFI type: free needs owning pointer");
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
    object_fields(args)[0] = live[0];
    object_fields(args)[1] = OT_EMPTY;
    result = oti_call_list(rt, live[1], args);
  }
  oti_roots_pop(&rt->heap, &roots);
  return result ? OT_FALSE : 0;
}

static ot_value foreign_builtin(ot_state* rt, unsigned id, size_t first, size_t count) {
  if (id == B_FFI_OWN) return own_pointer(rt, first, count);
  if (id >= B_FFI_CALLBACK && id <= B_FFI_CALLBACK_COUNT)
    return callback_builtin(rt, id, first, count);
  if (id >= B_FFI_COPY && id <= B_FFI_READ_UTF8_BYTES) return bulk_memory(rt, id, first, count);
  if (id >= B_FFI_RAW_NULL) return shared_builtin(rt, id, first, count);
  size_t arity = id == B_FFI_BIND                      ? 5
                 : id == B_FFI_SET                     ? 4
                 : id == B_FFI_REF || id == B_FFI_VIEW ? 3
                 : id == B_FFI_HAS_SYMBOL || id == B_FFI_ALLOC || id == B_FFI_POINTER_EQ ||
                         id == B_FFI_READ_UTF8
                     ? 2
                 : id == B_FFI_COUNT || id == B_FFI_BYTES ? 0
                                                          : 1;
  if (count != arity) return failure(rt, "FFI arity: wrong number of arguments");
  ot_value* args = rt->stack + first;
  if (id == B_FFI_OPEN) return open_library(rt, args[0]);
  if (id == B_FFI_BIND) return bind_function(rt, first, false);
  if (id == B_FFI_COUNT) return ot_int((intptr_t)rt->foreign->allocations);
  if (id == B_FFI_BYTES) return ot_int((intptr_t)rt->foreign->bytes);
  if (id == B_FFI_POINTER) return oti_kind(args[0], K_POINTER) ? OT_TRUE : OT_FALSE;
  if (id == B_FFI_NULL) {
    if (!oti_kind(args[0], K_POINTER)) return OT_FALSE;
    oti_pointer p;
    memcpy(&p, object_bytes(args[0]), sizeof(p));
    return !p.address ? OT_TRUE : OT_FALSE;
  }
  if (id == B_FFI_HAS_SYMBOL) {
    argument_context(rt, 0, NULL);
    if (!oti_kind(args[0], K_FOREIGN_LIBRARY)) return failure(rt, "FFI type: expected library");
    argument_context(rt, 1, "utf8");
    const char* name = c_string(rt, args[1]);
    if (!name) return 0;
    oti_library* lib = object_data(args[0]);
    dlerror();
    void* target = dlsym(lib->handle, name);
    return !dlerror() && target ? OT_TRUE : OT_FALSE;
  }
  if (id == B_FFI_ALLOC_UTF8) {
    argument_context(rt, 0, "utf8");
    const char* s = c_string(rt, args[0]);
    if (!s) return 0;
    size_t n;
    oti_text_bytes(args[0], &n);
    return allocate_memory(rt, n + 1, s);
  }
  if (id == B_FFI_SIZEOF || id == B_FFI_ALIGNOF || id == B_FFI_ALLOC) {
    argument_context(rt, 0, NULL);
    int type = oti_foreign_scalar(rt, args[0], true);
    if (type < 0) return 0;
    if (id != B_FFI_ALLOC)
      return ot_int((intptr_t)(id == B_FFI_SIZEOF ? oti_foreign_scalars[type].size
                                                  : oti_foreign_scalars[type].alignment));
    size_t n;
    argument_context(rt, 1, "size-t");
    if (!nonnegative(rt, args[1], &n)) return 0;
    if (n > SIZE_MAX / oti_foreign_scalars[type].size)
      return failure(rt, "FFI range: allocation size overflow");
    return allocate_memory(rt, n * oti_foreign_scalars[type].size, NULL);
  }
  if (id == B_FFI_FREE) {
    argument_context(rt, 0, "pointer");
    return release_pointer(rt, args[0]);
  }
  oti_pointer p;
  if (id == B_FFI_POINTER_EQ) {
    argument_context(rt, 0, "pointer");
    if (!oti_kind(args[0], K_POINTER)) return failure(rt, "FFI type: expected pointer");
    argument_context(rt, 1, "pointer");
    if (!oti_kind(args[1], K_POINTER)) return failure(rt, "FFI type: expected pointer");
    oti_pointer other;
    memcpy(&p, object_bytes(args[0]), sizeof(p));
    memcpy(&other, object_bytes(args[1]), sizeof(other));
    return p.address == other.address ? OT_TRUE : OT_FALSE;
  }
  argument_context(rt, 0, "pointer");
  if (!pointer(rt, args[0], &p)) return 0;
  if (id == B_FFI_VIEW) {
    size_t offset, bytes;
    argument_context(rt, 1, "size-t");
    if (!nonnegative(rt, args[1], &offset)) return 0;
    argument_context(rt, 2, "size-t");
    if (!nonnegative(rt, args[2], &bytes)) return 0;
    argument_context(rt, 0, "pointer");
    if (!access_memory(rt, p, offset, bytes, 1)) return 0;
    p.address += offset;
    p.length = bytes;
    p.bounded = true;
    p.view = true;
    return pointer_object(rt, p);
  }
  if (id == B_FFI_READ_UTF8) {
    size_t limit;
    argument_context(rt, 1, "size-t");
    if (!nonnegative(rt, args[1], &limit)) return 0;
    argument_context(rt, 0, "pointer");
    if (!access_memory(rt, p, 0, limit, 1)) return 0;
    const char* s = (const char*)p.address;
    const char* end = memchr(s, 0, limit);
    if (!end) return failure(rt, "FFI bounds: missing string terminator");
    if (!utf8((const unsigned char*)s, (size_t)(end - s)))
      return failure(rt, "FFI encoding: invalid UTF-8");
    return oti_text(rt, K_STRING, s, (size_t)(end - s));
  }
  if (id == B_FFI_REF || id == B_FFI_SET) {
    argument_context(rt, 1, NULL);
    int type = oti_foreign_scalar(rt, args[1], true);
    if (type < 0) return 0;
    size_t index;
    argument_context(rt, 2, "size-t");
    if (!nonnegative(rt, args[2], &index)) return 0;
    const oti_scalar* s = &oti_foreign_scalars[type];
    if (index > SIZE_MAX / s->size) return failure(rt, "FFI range: offset overflow");
    size_t offset = index * s->size;
    argument_context(rt, 0, "pointer");
    if (!access_memory(rt, p, offset, s->size, s->alignment)) return 0;
    oti_foreign_slot slot = {0};
    argument_context(rt, 3, s->name);
    if (id == B_FFI_SET && !to_foreign(rt, s, args[3], &slot)) return 0;
    memory_scalar((unsigned)type, (void*)(p.address + offset), &slot, id == B_FFI_SET);
    argument_context(rt, SIZE_MAX, s->name);
    return id == B_FFI_SET ? OT_FALSE : from_foreign(rt, s, slot);
  }
  return failure(rt, "unknown FFI operation");
}

static ot_value foreign_result(ot_state* rt, ot_value result, const char* op) {
  if (result || rt->raised || rt->exit_pending || rt->interrupted || rt->restart_pending ||
      strncmp(rt->error, "FFI ", 4))
    return result;
  char reason[64];
  size_t n = strcspn(rt->error + 4, ": ");
  if (n >= sizeof(reason)) n = sizeof(reason) - 1;
  memcpy(reason, rt->error + 4, n);
  reason[n] = 0;
  return oti_ffi_error(rt, op, reason, rt->error);
}

static bool new_ffi_error(ot_state* rt) {
  return !rt->raised && !rt->exit_pending && !rt->interrupted && !rt->restart_pending &&
         !strncmp(rt->error, "FFI ", 4);
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
    ok = condition_field(rt, ":library", object_fields(live[0])[0]);
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
  if (!library_context(rt, object_fields(function)[0], object_fields(function)[1])) return 0;
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
      object_fields(type)[0] = argument == SIZE_MAX
                                   ? object_fields(function)[3]
                                   : list_item(object_fields(function)[2], argument);
    }
  }
  if (type) condition_field(rt, ":expected-type", type);
  return 0;
}

static const char* operation(unsigned id) {
  switch (id) {
    case B_FFI_OPEN:
    case B_FFI_RAW_OPEN: return "open-library";
    case B_FFI_BIND:
    case B_FFI_RAW_BIND: return "bind";
    case B_FFI_REF:
    case B_FFI_RAW_REF: return "ref";
    case B_FFI_SET:
    case B_FFI_RAW_SET: return "set!";
    case B_FFI_ALLOC:
    case B_FFI_RAW_ALLOC: return "alloc";
    case B_FFI_SIZEOF:
    case B_FFI_RAW_SIZE: return "sizeof";
    case B_FFI_ALIGNOF:
    case B_FFI_RAW_ALIGN: return "alignof";
    case B_FFI_HAS_SYMBOL: return "has-symbol?";
    case B_FFI_POINTER: return "pointer?";
    case B_FFI_NULL:
    case B_FFI_RAW_NULL: return "null?";
    case B_FFI_POINTER_EQ: return "pointer=?";
    case B_FFI_FREE: return "free!";
    case B_FFI_VIEW: return "view";
    case B_FFI_ALLOC_UTF8: return "alloc-utf8";
    case B_FFI_READ_UTF8: return "read-utf8";
    case B_FFI_COUNT: return "allocation-count";
    case B_FFI_BYTES: return "allocation-bytes";
    case B_FFI_COPY: return "copy!";
    case B_FFI_READ_BYTES: return "read-bytes";
    case B_FFI_WRITE_BYTES: return "write-bytes!";
    case B_FFI_READ_UTF8_BYTES: return "read-utf8-bytes";
    case B_FFI_OWN: return "own";
    case B_FFI_CALLBACK: return "callback";
    case B_FFI_FREE_CALLBACK: return "free-callback!";
    case B_FFI_CALLBACK_COUNT: return "callback-count";
    case B_FFI_LAYOUT:
    case B_FFI_LAYOUT_DATA: return "layout";
    case B_FFI_ERROR_FIELD: return "error-field";
    case B_FFI_LOADER_ERROR: return "loader-error";
    default: return "foreign";
  }
}

ot_value oti_foreign_builtin(ot_state* rt, unsigned id, size_t first, size_t count) {
  oti_foreign_error context = {SIZE_MAX, NULL};
  oti_foreign_error* previous = rt->foreign->error_context;
  rt->foreign->error_context = &context;
  ot_value result = foreign_builtin(rt, id, first, count);
  rt->foreign->error_context = previous;
  bool annotate = !result && new_ffi_error(rt);
  result = foreign_result(rt, result, operation(id));
  if (annotate && rt->raised) {
    if ((id == B_FFI_BIND || id == B_FFI_RAW_BIND) && count >= 2)
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
