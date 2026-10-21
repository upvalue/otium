#define OT_INTERNAL
#include "otium.h"
#ifndef OT_WITH_FFI
static void unavailable(ots* s) {
  ot_raise(s, "otium.ffi unavailable: rebuild with libffi installed (-Dffi=enabled)");
}

void ot_register_ffi(ots* s) { ot_register_module(s, "otium.ffi", unavailable); }

void ot_destroy_ffi(ots* s) { (void)s; }
#else
#include "../../native/ffi/native.h"
static const char ot_ffi_src[] = {
#embed "../../lib/otium/ffi.ot"
};
static const unsigned ot_ffi_src_len = sizeof ot_ffi_src;
#include <ffi.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <pthread.h>

/* The initial ABI profile: AAPCS64 on macOS arm64 and SysV on Linux x86-64.
 * Code for either convention compiles on every target. */
#if defined(__APPLE__) && defined(__aarch64__)
static const bool aapcs64 = true, sysv_x86_64 = false;
#elif defined(__linux__) && defined(__x86_64__)
static const bool aapcs64 = false, sysv_x86_64 = true;
#else
static const bool aapcs64 = false, sysv_x86_64 = false;
#endif

typedef struct item {
  struct item* next;
  void (*destroy)(void*);
} item;

typedef struct owner {
  item item;
  void* data;
  size_t size;
  bool released;
} owner;
typedef struct callback callback;

/* code marks a native function address: a callback or a decoded
 * callback-pointer value. */
typedef struct pointer {
  void* address;
  owner* owner;
  callback* callback;
  size_t size;
  bool bounded, owning, code;
} pointer;

typedef struct library {
  item item;
  void* handle;
  char* name;
} library;

typedef struct binding {
  item item;
  library* lib;
  char* name;
  ffi_cif cif;
  ffi_type** types;
  void* address;
  int* kinds;
  int result;
  ffi_type* result_type;
  size_t count;
} binding;

/* The procedure is a global root until the callback is freed. The record
 * itself lives until the state is destroyed, so pointer values can still see
 * that it was released. */
struct callback {
  item item;
  ots* state;
  ffi_closure* closure;
  void* code;
  ffi_cif cif;
  ffi_type** types;
  int* kinds;
  int result;
  size_t count;
  otv procedure;
  bool released;
};

typedef enum pending_kind {
  PENDING_NONE,
  PENDING_CONDITION,
  PENDING_QUIT,
  PENDING_ESCAPE
} pending_kind;

typedef struct error_scope {
  ots* state;
  struct error_scope* previous;
  library* library;
  const char* symbol;
  /* -1 when there is no current argument or expected type. */
  intptr_t argument;
  int expected;
} error_scope;

typedef struct context {
  error_scope* error;
  item* items;
  unsigned pointer_type, library_type, binding_type;
  size_t allocations, bytes;
  /* Callbacks run only on thread while depth foreign calls are active. The
   * first error or exit contained in a callback waits in pending until the
   * outermost call returns. */
  pthread_t thread;
  size_t depth, callbacks;
  pending_kind pending;
  otv pending_condition;
  bool pending_quit_requested;
  int pending_quit_status;
} context;

static context* ctx(ots* s) { return s->ffi_state; }

static void pop_error(error_scope* e) { ctx(e->state)->error = e->previous; }

#define ERROR_SCOPE()                                                                              \
  error_scope scope [[gnu::cleanup(pop_error)]] = {                                                \
      .state = s, .previous = ctx(s)->error, .argument = -1, .expected = -1};                      \
  ctx(s)->error = &scope

static void track(ots* s, item* p, void (*destroy)(void*)) {
  p->next = ctx(s)->items;
  p->destroy = destroy;
  ctx(s)->items = p;
}

static void owner_destroy(void* p) {
  owner* o = p;
  ot_host_free(o->data);
  ot_host_free(o);
}

static void release_owner(context* c, owner* o) {
  if (o->released) return;
  ot_host_free(o->data);
  o->data = NULL;
  o->released = true;
  c->allocations--;
  c->bytes -= o->size;
}

static void callback_release(callback* cb) {
  if (cb->released) return;
  ffi_closure_free(cb->closure);
  cb->closure = NULL;
  ot_global_remove(cb->state, &cb->procedure);
  cb->released = true;
  ctx(cb->state)->callbacks--;
}

/* Free a callback that was never registered, or one already released. */
static void callback_free(callback* cb) {
  if (cb->closure) ffi_closure_free(cb->closure);
  ot_host_free(cb->types);
  ot_host_free(cb->kinds);
  ot_host_free(cb);
}

static void callback_destroy(void* p) {
  callback_release(p);
  callback_free(p);
}

static void library_destroy(void* p) {
  library* l = p;
  ot_host_free(l->name);
  ot_host_free(l);
}

static void free_type(ffi_type* t) {
  if (!t || t->type != FFI_TYPE_STRUCT) return;
  for (size_t i = 0; t->elements[i]; i++) free_type(t->elements[i]);
  ot_host_free(t->elements);
  ot_host_free(t);
}

static void binding_destroy(void* p) {
  binding* b = p;
  if (b->types)
    for (size_t i = 0; i < b->count; i++) free_type(b->types[i]);
  free_type(b->result_type);
  ot_host_free(b->name);
  ot_host_free(b->types);
  ot_host_free(b->kinds);
  ot_host_free(b);
}

static void put(ots* s, otv table, const char* key, otv value) {
  OT_FRAME_SCOPED(s, &table, &value);
  otv k = ot_make_keyword(s, key, strlen(key));
  ot_table_put(s, table, k, value);
}

static otv fail(ots* s, const char* op, const char* reason, const char* message) {
  otv c = ot_table_new(s, 8), v = ot_false;
  OT_FRAME_SCOPED(s, &c, &v);
  v = ot_make_symbol(s, "ffi-error", 9);
  put(s, c, "type", v);
  v = ot_make_string(s, message, strlen(message));
  put(s, c, "message", v);
  v = ot_make_symbol(s, op, strlen(op));
  put(s, c, "operation", v);
  v = ot_make_symbol(s, reason, strlen(reason));
  put(s, c, "reason", v);
  error_scope* e = ctx(s)->error;
  if (e) {
    if (e->library) {
      v = ot_make_string(s, e->library->name, strlen(e->library->name));
      put(s, c, "library", v);
    }
    if (e->symbol) {
      v = ot_make_string(s, e->symbol, strlen(e->symbol));
      put(s, c, "symbol", v);
    }
    if (e->argument >= 0) put(s, c, "argument", ot_make_int(e->argument));
    if (e->expected >= 0 && e->expected < F_COUNT) {
      const char* name = offi_names[e->expected];
      v = ot_make_symbol(s, name, strlen(name));
      put(s, c, "expected-type", v);
    }
  }
  return ot_raise_value(s, c);
}

#define ARITY(N, OP)                                                                               \
  ERROR_SCOPE();                                                                                   \
  if (n != (N)) return fail(s, OP, "arity", "wrong number of arguments")

static void* ext(otv v, unsigned type) {
  if (ot_value_type(v) != OT_TYPE_EXT) return NULL;
  ot_ext_obj* e = (ot_ext_obj*)ot_as_obj(v);
  if (e->type != type || e->released) return NULL;
  return e->pointer_payload ? e->payload.pointer : e->payload.bytes;
}

static bool ptr(ots* s, otv v, pointer* out, const char* op) {
  pointer* p = ext(v, ctx(s)->pointer_type);
  if (!p) {
    fail(s, op, "type", "expected foreign pointer");
    return false;
  }
  *out = *p;
  return true;
}

static bool live(ots* s, pointer p, const char* op) {
  if (p.owner && p.owner->released) {
    fail(s, op, "released", "allocation released");
    return false;
  }
  if (p.callback && p.callback->released) {
    fail(s, op, "released", "callback released");
    return false;
  }
  return true;
}

static bool span(ots* s, pointer p, size_t offset, size_t bytes, size_t align, const char* op) {
  if (!live(s, p, op)) return false;
  if (!p.address) {
    fail(s, op, "null", "null memory access");
    return false;
  }
  if (offset > SIZE_MAX - bytes || (p.bounded && (offset > p.size || bytes > p.size - offset)) ||
      (uintptr_t)p.address > UINTPTR_MAX - offset) {
    fail(s, op, "bounds", "memory span out of bounds");
    return false;
  }
  if (((uintptr_t)p.address + offset) % align) {
    fail(s, op, "alignment", "unaligned access");
    return false;
  }
  return true;
}

static bool count(ots* s, otv v, size_t* out, const char* op) {
  if (!ot_is_int(v)) {
    fail(s, op, "type", "expected exact integer");
    return false;
  }
  if (ot_get_int(v) < 0) {
    fail(s, op, "range", "expected nonnegative integer");
    return false;
  }
  *out = (size_t)ot_get_int(v);
  return true;
}

static otv wrap(ots* s, pointer p) { return ot_ext_inline(s, ctx(s)->pointer_type, &p, sizeof(p)); }

static char* string(ots* s, otv v, const char* op) {
  const char* p;
  size_t n;
  if (!ot_string_bytes(v, &p, &n)) {
    fail(s, op, "type", "expected string");
    return NULL;
  }
  if (memchr(p, 0, n) || !offi_utf8((const unsigned char*)p, n)) {
    fail(s, op, "encoding", "invalid UTF-8 or embedded NUL");
    return NULL;
  }
  char* copy = ot_host_alloc(n + 1);
  if (!copy) {
    fail(s, op, "allocation", "string allocation failed");
    return NULL;
  }
  memcpy(copy, p, n);
  copy[n] = 0;
  return copy;
}

static int kind(otv v) {
  return ot_is_int(v) && ot_get_int(v) >= 0 && ot_get_int(v) < F_COUNT ? (int)ot_get_int(v) : -1;
}

static ffi_type* native_type(int k) {
  static ffi_type* types[] = {
      &ffi_type_void,    &ffi_type_uint8,   &ffi_type_sint8,  &ffi_type_uint8,  &ffi_type_sint16,
      &ffi_type_uint16,  &ffi_type_sint32,  &ffi_type_uint32, &ffi_type_sint64, &ffi_type_uint64,
      &ffi_type_sshort,  &ffi_type_ushort,  &ffi_type_sint,   &ffi_type_uint,   &ffi_type_slong,
      &ffi_type_ulong,   &ffi_type_uint64,  &ffi_type_sint64, &ffi_type_float,  &ffi_type_double,
      &ffi_type_pointer, &ffi_type_pointer, &ffi_type_pointer};
  return k >= 0 && k < F_COUNT ? types[k] : NULL;
}

static otv nth(otv list, size_t index) {
  otv head = ot_false;
  for (size_t i = 0; i <= index; i++)
    if (!ot_pair_values(list, &head, &list)) return ot_false;
  return head;
}

/* Layout descriptors from lib/otium/ffi.ot are (kind size alignment members).
 * Struct and union members are (name descriptor offset); an array's members
 * are (element count). */
static otv layout_kind(otv d) { return nth(d, 0); }

static size_t layout_size(otv d) { return (size_t)ot_get_int(nth(d, 1)); }

static size_t layout_align(otv d) { return (size_t)ot_get_int(nth(d, 2)); }

static otv layout_members(otv d) { return nth(d, 3); }

static otv member_layout(otv m) { return nth(m, 1); }

static size_t member_offset(otv m) { return (size_t)ot_get_int(nth(m, 2)); }

static otv array_element(otv d) { return nth(layout_members(d), 0); }

static otv array_count(otv d) { return nth(layout_members(d), 1); }

static int call_kind(otv value) {
  if (ot_is_int(value)) return kind(value);
  otv k = layout_kind(value);
  return ot_is_int(k) ? (int)ot_get_int(k) : -1;
}

enum {
  MAX_AGGREGATE_DEPTH = 64,
  /* SysV passes aggregates of up to two eightbytes in registers. */
  SYSV_REGISTER_BYTES = 16,
  /* AAPCS64 homogeneous floating aggregates have one to four members. */
  AAPCS64_HFA_MEMBERS = 4,
};

/* SysV byte classes; INTEGER wins when classes merge. */
typedef enum sysv_class { CLASS_NONE, CLASS_SSE, CLASS_INTEGER } sysv_class;

/* The scalar leaves of a union, for AAPCS64: none seen yet, all one float
 * kind (F_FLOAT or F_DOUBLE), or mixed. */
enum { LEAVES_NONE = -2, LEAVES_MIXED = -1 };

/* A union's overlapping members contribute to its calling classification.
 * Preserve byte classes as well as size: a 4-aligned union can straddle two
 * SysV eightbytes when nested in a struct. Bytes past the register bytes go
 * unrecorded. See docs/native.md. */
static bool classify_union_member(ots* s, otv d, size_t offset,
                                  sysv_class classes[SYSV_REGISTER_BYTES], int* leaves,
                                  unsigned depth) {
  if (depth > MAX_AGGREGATE_DEPTH) {
    fail(s, "bind", "abi", "aggregate nesting exceeds backend limit");
    return false;
  }
  int k = call_kind(d);
  if (k >= 0 && k < F_COUNT) {
    bool floating = k == F_FLOAT || k == F_DOUBLE;
    int leaf = floating ? k : LEAVES_MIXED;
    if (*leaves == LEAVES_NONE) *leaves = leaf;
    else if (*leaves != leaf) *leaves = LEAVES_MIXED;
    sysv_class cls = floating ? CLASS_SSE : CLASS_INTEGER;
    for (size_t i = 0; i < offi_size(k) && offset + i < SYSV_REGISTER_BYTES; i++)
      if (classes[offset + i] < cls) classes[offset + i] = cls;
    return true;
  }
  if (k == F_ARRAY) {
    size_t count_elements = (size_t)ot_get_int(array_count(d));
    if (!count_elements) {
      fail(s, "bind", "signature", "empty by-value array field");
      return false;
    }
    otv element = array_element(d);
    size_t stride = layout_size(element);
    /* One element establishes the leaf kind even past the register bytes. */
    for (size_t i = 0; i < count_elements; i++) {
      if (!classify_union_member(s, element, offset, classes, leaves, depth + 1)) return false;
      if (offset >= SYSV_REGISTER_BYTES || stride > SYSV_REGISTER_BYTES - offset) break;
      offset += stride;
    }
    return true;
  }
  if (k == F_STRUCT || k == F_UNION) {
    otv members = layout_members(d), member;
    while (ot_pair_values(members, &member, &members)) {
      size_t relative = member_offset(member);
      size_t at = offset >= SYSV_REGISTER_BYTES || relative >= SYSV_REGISTER_BYTES
                      ? SYSV_REGISTER_BYTES
                      : offset + relative;
      if (!classify_union_member(s, member_layout(member), at, classes, leaves, depth + 1))
        return false;
    }
    return true;
  }
  fail(s, "bind", "signature", "invalid aggregate member");
  return false;
}

static ffi_type* carrier_type(size_t unit, bool floating) {
  if (floating) return unit == sizeof(float) ? &ffi_type_float : &ffi_type_double;
  switch (unit) {
    case 1: return &ffi_type_uint8;
    case 2: return &ffi_type_uint16;
    case 4: return &ffi_type_uint32;
    default: return &ffi_type_uint64;
  }
}

static ffi_type* prepare_union(ots* s, otv description, unsigned depth) {
  size_t size = layout_size(description);
  size_t align = layout_align(description);
  sysv_class classes[SYSV_REGISTER_BYTES] = {CLASS_NONE};
  int leaves = LEAVES_NONE;
  if (!classify_union_member(s, description, 0, classes, &leaves, depth)) return NULL;
  if (!size || (align != 1 && align != 2 && align != 4 && align != 8) || size % align) {
    fail(s, "bind", "abi", "unsupported union size or alignment");
    return NULL;
  }
  size_t unit = align;
  bool homogeneous = aapcs64 && (leaves == F_FLOAT || leaves == F_DOUBLE) &&
                     size / offi_size(leaves) <= AAPCS64_HFA_MEMBERS;
  if (homogeneous) unit = offi_size(leaves);
  size_t count_fields = size / unit;
  if (count_fields > SIZE_MAX / sizeof(ffi_type*) - 1) {
    fail(s, "bind", "allocation", "union descriptor too large");
    return NULL;
  }
  ffi_type* t = ot_host_alloc(sizeof(*t));
  if (!t) {
    fail(s, "bind", "allocation", "union type allocation failed");
    return NULL;
  }
  t->type = FFI_TYPE_STRUCT;
  t->elements = ot_host_alloc((count_fields + 1) * sizeof(*t->elements));
  if (!t->elements) {
    ot_host_free(t);
    fail(s, "bind", "allocation", "union fields allocation failed");
    return NULL;
  }
  for (size_t i = 0; i < count_fields; i++) {
    bool floating = homogeneous;
    if (sysv_x86_64 && size <= SYSV_REGISTER_BYTES && unit >= sizeof(float)) {
      sysv_class cls = CLASS_NONE;
      for (size_t j = 0; j < unit; j++)
        if (classes[i * unit + j] > cls) cls = classes[i * unit + j];
      floating = cls == CLASS_SSE;
    }
    t->elements[i] = carrier_type(unit, floating);
  }
  if (ffi_get_struct_offsets(FFI_DEFAULT_ABI, t, NULL) != FFI_OK || t->size != size ||
      t->alignment != align) {
    free_type(t);
    fail(s, "bind", "abi", "native union carrier layout mismatch");
    return NULL;
  }
  return t;
}

static ffi_type* prepare_type(ots* s, otv description, unsigned depth) {
  int k = call_kind(description);
  if (k >= 0 && k < F_COUNT) return native_type(k);
  if (k == F_UNION) return prepare_union(s, description, depth);
  if (depth > MAX_AGGREGATE_DEPTH || (k != F_STRUCT && k != F_ARRAY)) {
    fail(s, "bind", "abi", "unsupported aggregate shape");
    return NULL;
  }
  otv members = layout_members(description);
  size_t count_fields = 0;
  if (k == F_ARRAY) {
    otv number = array_count(description);
    if (!ot_is_int(number) || ot_get_int(number) <= 0) {
      fail(s, "bind", "signature", "empty by-value array field");
      return NULL;
    }
    count_fields = (size_t)ot_get_int(number);
  } else {
    otv cursor = members, member;
    while (ot_pair_values(cursor, &member, &cursor)) count_fields++;
    if (cursor != ot_null || !count_fields) {
      fail(s, "bind", "signature", "invalid aggregate fields");
      return NULL;
    }
  }
  if (count_fields > SIZE_MAX / sizeof(ffi_type*) - 1) {
    fail(s, "bind", "allocation", "aggregate descriptor too large");
    return NULL;
  }
  ffi_type* t = ot_host_alloc(sizeof(*t));
  if (!t) {
    fail(s, "bind", "allocation", "aggregate type allocation failed");
    return NULL;
  }
  t->type = FFI_TYPE_STRUCT;
  t->elements = ot_host_alloc((count_fields + 1) * sizeof(*t->elements));
  if (!t->elements) {
    ot_host_free(t);
    fail(s, "bind", "allocation", "aggregate fields allocation failed");
    return NULL;
  }
  for (size_t i = 0; i < count_fields; i++) {
    otv child = k == F_ARRAY ? array_element(description) : member_layout(nth(members, i));
    t->elements[i] = prepare_type(s, child, depth + 1);
    if (!t->elements[i]) {
      free_type(t);
      return NULL;
    }
  }
  size_t* offsets = ot_host_alloc(count_fields * sizeof(*offsets));
  if (!offsets) {
    free_type(t);
    fail(s, "bind", "allocation", "aggregate offsets allocation failed");
    return NULL;
  }
  bool matches = ffi_get_struct_offsets(FFI_DEFAULT_ABI, t, offsets) == FFI_OK;
  matches =
      matches && t->size == layout_size(description) && t->alignment == layout_align(description);
  if (k == F_STRUCT)
    for (size_t i = 0; i < count_fields; i++)
      matches = matches && offsets[i] == member_offset(nth(members, i));
  ot_host_free(offsets);
  if (!matches) {
    free_type(t);
    fail(s, "bind", "abi", "native aggregate layout mismatch");
    return NULL;
  }
  return t;
}

typedef union cell {
  ffi_arg word;
  int64_t i;
  uint64_t u;
  float f;
  double d;
  void* p;
} cell;

static bool encode(ots* s, int k, otv v, cell* out, char** temporary, const char* op) {
  memset(out, 0, sizeof(*out));
  if (k == F_BOOL) {
    if (v != ot_false && v != ot_true) {
      fail(s, op, "type", "expected boolean");
      return false;
    }
    out->u = v == ot_true;
    return true;
  }
  if (k >= F_I8 && k <= F_DIFF) {
    if (!ot_is_int(v)) {
      fail(s, op, "type", "expected exact integer");
      return false;
    }
    int64_t x = ot_get_int(v);
    unsigned bits = (unsigned)offi_size(k) * 8;
    bool signed_type = offi_signed(k);
    if ((!signed_type && (x < 0 || (bits < 64 && (uint64_t)x > ((UINT64_C(1) << bits) - 1)))) ||
        (signed_type && bits < 64 &&
         (x < -(INT64_C(1) << (bits - 1)) || x > ((INT64_C(1) << (bits - 1)) - 1)))) {
      fail(s, op, "range", "integer outside native range");
      return false;
    }
    out->i = x;
    return true;
  }
  if (k == F_FLOAT || k == F_DOUBLE) {
    double d;
    if (ot_is_int(v)) d = (double)ot_get_int(v);
    else if (!ot_float_value(v, &d)) {
      fail(s, op, "type", "expected real number");
      return false;
    }
    if (k == F_FLOAT) {
      out->f = (float)d;
      if (isfinite(d) && !isfinite(out->f)) {
        fail(s, op, "range", "float overflow");
        return false;
      }
    } else out->d = d;
    return true;
  }
  if (k == F_UTF8 && ot_value_type(v) == OT_TYPE_STRING) {
    *temporary = string(s, v, op);
    out->p = *temporary;
    return *temporary != NULL;
  }
  if (k == F_POINTER || k == F_CALLBACK || k == F_UTF8) {
    pointer p;
    if (!ptr(s, v, &p, op) || !live(s, p, op)) return false;
    if (k == F_CALLBACK && p.address && !p.code) {
      fail(s, op, "type", "expected null or callback pointer");
      return false;
    }
    if (k == F_UTF8 && p.address) {
      fail(s, op, "type", "expected null pointer");
      return false;
    }
    out->p = p.address;
    return true;
  }
  fail(s, op, "signature", "invalid scalar type");
  return false;
}

static otv decode(ots* s, int k, cell v, const char* op) {
  if (k == F_VOID) return ot_false;
  if (k == F_BOOL) return (uint8_t)v.u ? ot_true : ot_false;
  if (k >= F_I8 && k <= F_DIFF) {
    unsigned bits = (unsigned)offi_size(k) * 8;
    if (bits < 64) {
      uint64_t mask = (UINT64_C(1) << bits) - 1;
      v.u &= mask;
      if (offi_signed(k) && (v.u & (UINT64_C(1) << (bits - 1)))) v.u |= ~mask;
    }
    if (offi_signed(k)) {
      if (v.i < OT_INT_MIN || v.i > OT_INT_MAX)
        return fail(s, op, "range", "native result exceeds Otium integer range");
      return ot_make_int(v.i);
    }
    if (v.u > (uint64_t)OT_INT_MAX)
      return fail(s, op, "range", "native result exceeds Otium integer range");
    return ot_make_int((intptr_t)v.u);
  }
  if (k == F_FLOAT || k == F_DOUBLE) return ot_make_float(s, k == F_FLOAT ? v.f : v.d);
  if (k == F_UTF8 && v.p) {
    size_t len = strlen(v.p);
    if (!offi_utf8(v.p, len)) return fail(s, op, "encoding", "invalid UTF-8 result");
    return ot_make_string(s, v.p, len);
  }
  return wrap(s, (pointer){.address = v.p, .code = k == F_CALLBACK && v.p});
}

static otv n_null(ots* s, otv* a, int n) {
  (void)a;
  ARITY(0, "null");
  return wrap(s, (pointer){0});
}

static otv n_nullp(ots* s, otv* a, int n) {
  ARITY(1, "null?");
  pointer* p = ext(a[0], ctx(s)->pointer_type);
  return p && !p->address ? ot_true : ot_false;
}

static otv n_pointerp(ots* s, otv* a, int n) {
  ARITY(1, "pointer?");
  if (ot_value_type(a[0]) != OT_TYPE_EXT) return ot_false;
  return ((ot_ext_obj*)ot_as_obj(a[0]))->type == ctx(s)->pointer_type ? ot_true : ot_false;
}

static otv n_equal(ots* s, otv* a, int n) {
  ARITY(2, "pointer=?");
  pointer p, q;
  if (!ptr(s, a[0], &p, "pointer=?") || !ptr(s, a[1], &q, "pointer=?")) return OT_UNWIND;
  return p.address == q.address ? ot_true : ot_false;
}

static otv n_size(ots* s, otv* a, int n) {
  ARITY(1, "sizeof");
  int k = kind(a[0]);
  if (k <= F_VOID) return fail(s, "sizeof", "type", "invalid type");
  return ot_make_int((intptr_t)offi_size(k));
}

static otv n_align(ots* s, otv* a, int n) {
  ARITY(1, "alignof");
  int k = kind(a[0]);
  if (k <= F_VOID) return fail(s, "alignof", "type", "invalid type");
  return ot_make_int((intptr_t)offi_align(k));
}

static otv n_alloc(ots* s, otv* a, int n) {
  ARITY(2, "alloc");
  size_t bytes, align;
  if (!count(s, a[0], &bytes, "alloc") || !count(s, a[1], &align, "alloc")) return OT_UNWIND;
  if (!align || align > _Alignof(max_align_t))
    return fail(s, "alloc", "alignment", "unsupported allocation alignment");
  owner* o = ot_host_alloc(sizeof(*o));
  if (!o) return fail(s, "alloc", "allocation", "allocation failed");
  o->data = ot_host_alloc(bytes ? bytes : 1);
  o->size = bytes;
  if (!o->data) {
    ot_host_free(o);
    return fail(s, "alloc", "allocation", "allocation failed");
  }
  track(s, &o->item, owner_destroy);
  ctx(s)->allocations++;
  ctx(s)->bytes += bytes;
  return wrap(
      s, (pointer){.address = o->data, .owner = o, .size = bytes, .bounded = true, .owning = true});
}

static otv n_free(ots* s, otv* a, int n) {
  ARITY(1, "free!");
  pointer p;
  if (!ptr(s, a[0], &p, "free!")) return OT_UNWIND;
  if (!p.address) return ot_false;
  if (!p.owning) return fail(s, "free!", "type", "only owning allocations can be freed");
  release_owner(ctx(s), p.owner);
  return ot_false;
}

static otv n_view(ots* s, otv* a, int n) {
  ARITY(3, "view");
  pointer p;
  size_t offset, bytes;
  if (!ptr(s, a[0], &p, "view") || !count(s, a[1], &offset, "view") ||
      !count(s, a[2], &bytes, "view") || !span(s, p, offset, bytes, 1, "view"))
    return OT_UNWIND;
  p.address = (char*)p.address + offset;
  p.size = bytes;
  p.bounded = true;
  p.owning = false;
  p.code = false;
  return wrap(s, p);
}

static otv n_ref(ots* s, otv* a, int n) {
  ARITY(3, "ref");
  pointer p;
  size_t offset;
  int k = kind(a[1]);
  if (k <= F_VOID || k == F_UTF8) return fail(s, "ref", "type", "expected memory scalar");
  if (!ptr(s, a[0], &p, "ref") || !count(s, a[2], &offset, "ref") ||
      !span(s, p, offset, offi_size(k), offi_align(k), "ref"))
    return OT_UNWIND;
  cell v = {0};
  memcpy(&v, (char*)p.address + offset, offi_size(k));
  return decode(s, k, v, "ref");
}

static otv n_set(ots* s, otv* a, int n) {
  ARITY(4, "set!");
  pointer p;
  size_t offset;
  int k = kind(a[1]);
  if (k <= F_VOID || k == F_UTF8) return fail(s, "set!", "type", "expected memory scalar");
  if (!ptr(s, a[0], &p, "set!") || !count(s, a[2], &offset, "set!") ||
      !span(s, p, offset, offi_size(k), offi_align(k), "set!"))
    return OT_UNWIND;
  cell v;
  char* tmp = NULL;
  if (!encode(s, k, a[3], &v, &tmp, "set!")) return OT_UNWIND;
  memcpy((char*)p.address + offset, &v, offi_size(k));
  return ot_false;
}

static otv n_copy(ots* s, otv* a, int n) {
  ARITY(3, "copy!");
  pointer p, q;
  size_t bytes;
  if (!ptr(s, a[0], &p, "copy!") || !ptr(s, a[1], &q, "copy!") ||
      !count(s, a[2], &bytes, "copy!") || !span(s, p, 0, bytes, 1, "copy!") ||
      !span(s, q, 0, bytes, 1, "copy!"))
    return OT_UNWIND;
  memmove(p.address, q.address, bytes);
  return ot_false;
}

static otv n_read(ots* s, otv* a, int n) {
  ARITY(2, "read-utf8");
  pointer p;
  size_t bytes;
  if (!ptr(s, a[0], &p, "read-utf8") || !count(s, a[1], &bytes, "read-utf8") ||
      !span(s, p, 0, bytes, 1, "read-utf8"))
    return OT_UNWIND;
  char* end = memchr(p.address, 0, bytes);
  if (!end) return fail(s, "read-utf8", "bounds", "no NUL within bound");
  size_t len = (size_t)(end - (char*)p.address);
  if (!offi_utf8(p.address, len)) return fail(s, "read-utf8", "encoding", "invalid UTF-8");
  return ot_make_string(s, p.address, len);
}

static otv n_alloc_string(ots* s, otv* a, int n) {
  ARITY(1, "alloc-utf8");
  char* p = string(s, a[0], "alloc-utf8");
  if (!p) return OT_UNWIND;
  size_t len = strlen(p) + 1;
  otv args[] = {ot_make_int((intptr_t)len), ot_make_int(1)};
  otv result = n_alloc(s, args, 2);
  if (result != OT_UNWIND) {
    pointer* q = ext(result, ctx(s)->pointer_type);
    memcpy(q->address, p, len);
  }
  ot_host_free(p);
  return result;
}

static otv n_stats(ots* s, otv* a, int n) {
  (void)a;
  ARITY(0, "allocation-count");
  return ot_make_int((intptr_t)ctx(s)->allocations);
}

/* Nothing unwinds through the native frames between a callback and the call
 * that led to it. The unwind in progress is stopped and recorded instead, and
 * signal-site handlers have already run for a condition. */
static void contain(ots* s) {
  context* c = ctx(s);
  if (!c->pending) {
    ot_unwind_kind kind = s->vm.unwind_kind;
    c->pending = kind == UNWIND_CONDITION ? PENDING_CONDITION
                 : kind == UNWIND_QUIT    ? PENDING_QUIT
                                          : PENDING_ESCAPE;
    c->pending_condition = c->pending == PENDING_ESCAPE ? ot_false : s->vm.condition;
    c->pending_quit_requested = s->quit_requested;
    c->pending_quit_status = s->quit_status;
  }
  ot_clear_condition(s);
  s->quit_requested = false;
}

/* Raise the contained condition or repeat the exit, without running handlers
 * again. A restart outside the callback cannot be repeated. */
static otv replay(ots* s) {
  context* c = ctx(s);
  pending_kind kind = c->pending;
  otv condition = c->pending_condition;
  c->pending = PENDING_NONE;
  c->pending_condition = ot_false;
  if (kind == PENDING_ESCAPE)
    return fail(s, "callback", "escape", "non-local exit from foreign callback");
  s->vm.condition = condition;
  s->vm.unwind_kind = kind == PENDING_CONDITION ? UNWIND_CONDITION : UNWIND_QUIT;
  if (kind == PENDING_QUIT) {
    s->quit_requested = c->pending_quit_requested;
    s->quit_status = c->pending_quit_status;
  }
  return OT_UNWIND;
}

[[noreturn]] static void callback_abort(const char* message) {
  fprintf(stderr, "otium: %s\n", message);
  fflush(stderr);
  abort();
}

/* libffi widens integer results to a full register. */
static void store_result(int k, const cell* v, void* ret) {
  if (k == F_VOID) return;
  if (k == F_FLOAT) *(float*)ret = v->f;
  else if (k == F_DOUBLE) *(double*)ret = v->d;
  else if (k == F_POINTER || k == F_CALLBACK) *(void**)ret = v->p;
  else if (k == F_BOOL) *(ffi_arg*)ret = v->u != 0;
  else if (offi_signed(k)) *(ffi_sarg*)ret = (ffi_sarg)v->i;
  else *(ffi_arg*)ret = (ffi_arg)v->u;
}

static void trampoline(ffi_cif* cif, void* ret, void** args, void* data) {
  (void)cif;
  callback* cb = data;
  ots* s = cb->state;
  context* c = ctx(s);
  if (!pthread_equal(pthread_self(), c->thread))
    callback_abort("foreign callback invoked on a thread other than the Otium thread");
  if (!c->depth) callback_abort("foreign callback invoked outside a foreign call from Otium");
  error_scope* outer = c->error;
  c->error = NULL;
  size_t argc = cb->count, slots = argc ? argc : 1;
  otv values[slots];
  otv* roots[slots];
  for (size_t i = 0; i < slots; i++) {
    values[i] = ot_false;
    roots[i] = &values[i];
  }
  ot_frame frame;
  ot_frame_push(s, &frame, roots, slots);
  otv result = ot_nil;
  for (size_t i = 0; i < argc && result != OT_UNWIND; i++) {
    cell v = {0};
    memcpy(&v, args[i], offi_size(cb->kinds[i]));
    result = values[i] = decode(s, cb->kinds[i], v, "callback");
  }
  if (result != OT_UNWIND) result = ot_apply(s, cb->procedure, values, argc);
  ot_frame_pop(s, &frame);
  cell out = {0};
  char* unused = NULL;
  if (result != OT_UNWIND && cb->result != F_VOID &&
      !encode(s, cb->result, result, &out, &unused, "callback"))
    result = OT_UNWIND;
  if (result == OT_UNWIND) {
    contain(s);
    memset(&out, 0, sizeof(out));
  }
  c->error = outer;
  store_result(cb->result, &out, ret);
}

static otv n_callback(ots* s, otv* a, int n) {
  ARITY(3, "callback");
  if (ot_value_type(a[2]) != OT_TYPE_FUNCTION)
    return fail(s, "callback", "type", "expected procedure");
  int result = kind(a[1]);
  size_t len = 0;
  otv tail = a[0], head;
  while (ot_pair_values(tail, &head, &tail)) {
    int k = kind(head);
    if (k <= F_VOID || k >= F_UTF8 || ++len > UINT_MAX)
      return fail(s, "callback", "signature", "invalid callback argument type");
  }
  if (tail != ot_null || result < 0 || result >= F_UTF8)
    return fail(s, "callback", "signature", "invalid callback signature");
  callback* cb = ot_host_alloc(sizeof(*cb));
  if (cb) {
    cb->types = ot_host_alloc((len ? len : 1) * sizeof(*cb->types));
    cb->kinds = ot_host_alloc((len ? len : 1) * sizeof(*cb->kinds));
  }
  if (!cb || !cb->types || !cb->kinds) {
    if (cb) callback_free(cb);
    return fail(s, "callback", "allocation", "callback allocation failed");
  }
  tail = a[0];
  for (size_t i = 0; i < len; i++) {
    ot_pair_values(tail, &head, &tail);
    cb->kinds[i] = kind(head);
    cb->types[i] = native_type(cb->kinds[i]);
  }
  cb->state = s;
  cb->count = len;
  cb->result = result;
  if (ffi_prep_cif(&cb->cif, FFI_DEFAULT_ABI, (unsigned)len, native_type(result), cb->types) !=
      FFI_OK) {
    callback_free(cb);
    return fail(s, "callback", "abi", "unsupported ABI");
  }
  cb->closure = ffi_closure_alloc(sizeof(ffi_closure), &cb->code);
  if (!cb->closure ||
      ffi_prep_closure_loc(cb->closure, &cb->cif, trampoline, cb, cb->code) != FFI_OK) {
    callback_free(cb);
    return fail(s, "callback", "abi", "callback closure preparation failed");
  }
  cb->procedure = a[2];
  ot_global_add(s, &cb->procedure);
  track(s, &cb->item, callback_destroy);
  ctx(s)->callbacks++;
  return wrap(s, (pointer){.address = cb->code, .callback = cb, .code = true});
}

static otv n_free_callback(ots* s, otv* a, int n) {
  ARITY(1, "free-callback!");
  pointer* p = ext(a[0], ctx(s)->pointer_type);
  if (!p || !p->callback || !p->code) return fail(s, "free-callback!", "type", "expected callback");
  callback_release(p->callback);
  return ot_false;
}

static otv n_callback_count(ots* s, otv* a, int n) {
  (void)a;
  ARITY(0, "callback-count");
  return ot_make_int((intptr_t)ctx(s)->callbacks);
}

static otv n_bytes(ots* s, otv* a, int n) {
  (void)a;
  ARITY(0, "allocation-bytes");
  return ot_make_int((intptr_t)ctx(s)->bytes);
}

static otv n_open(ots* s, otv* a, int n) {
  ARITY(1, "open-library");
  char* name = string(s, a[0], "open-library");
  if (!name) return OT_UNWIND;
  void* handle = offi_open(name);
  if (!handle) {
    ot_host_free(name);
    return ot_false;
  }
  for (item* it = ctx(s)->items; it; it = it->next)
    if (it->destroy == library_destroy && ((library*)it)->handle == handle) {
      ot_host_free(name);
      return ot_ext_pointer(s, ctx(s)->library_type, it);
    }
  library* l = ot_host_alloc(sizeof(*l));
  if (!l) {
    ot_host_free(name);
    return fail(s, "open-library", "allocation", "library metadata allocation failed");
  }
  l->handle = handle;
  l->name = name;
  track(s, &l->item, library_destroy);
  return ot_ext_pointer(s, ctx(s)->library_type, l);
}

static otv n_loader_error(ots* s, otv* a, int n) {
  (void)a;
  ARITY(0, "open-library");
  const char* e = offi_loader_error();
  return ot_make_string(s, e, strlen(e));
}

static otv n_symbol(ots* s, otv* a, int n) {
  ARITY(2, "has-symbol?");
  library* l = ext(a[0], ctx(s)->library_type);
  if (!l) return fail(s, "has-symbol?", "type", "expected library");
  char* name = string(s, a[1], "has-symbol?");
  if (!name) return OT_UNWIND;
  bool found = offi_symbol(l->handle, name) != NULL;
  ot_host_free(name);
  return found ? ot_true : ot_false;
}

static otv call(ots* s, otv context_value, otv* a, int n) {
  ERROR_SCOPE();
  binding* b = ext(context_value, ctx(s)->binding_type);
  scope.library = b->lib;
  scope.symbol = b->name;
  if ((size_t)n != b->count) return fail(s, "call", "arity", "wrong number of native arguments");
  size_t count_args = n ? (size_t)n : 1;
  cell* values = ot_host_alloc((count_args) * sizeof(*values));
  void** addresses = ot_host_alloc((count_args) * sizeof(*addresses));
  char** temporaries = ot_host_alloc((count_args) * sizeof(*temporaries));
  if (!values || !addresses || !temporaries) {
    ot_host_free(values);
    ot_host_free(addresses);
    ot_host_free(temporaries);
    return fail(s, "call", "allocation", "call storage allocation failed");
  }
  otv result = OT_UNWIND, aggregate = ot_false;
  OT_FRAME_SCOPED(s, &aggregate);
  for (int i = 0; i < n; i++) {
    scope.argument = i;
    scope.expected = b->kinds[i];
    if (b->kinds[i] >= F_STRUCT) {
      pointer p;
      ffi_type* t = b->types[i];
      if (!ptr(s, a[i], &p, "call") || !span(s, p, 0, t->size, t->alignment, "call")) goto done;
      temporaries[i] = ot_host_alloc(t->size);
      if (!temporaries[i]) {
        fail(s, "call", "allocation", "aggregate copy allocation failed");
        goto done;
      }
      memcpy(temporaries[i], p.address, t->size);
      addresses[i] = temporaries[i];
    } else {
      if (!encode(s, b->kinds[i], a[i], &values[i], &temporaries[i], "call")) goto done;
      addresses[i] = &values[i];
    }
  }
  scope.argument = -1;
  scope.expected = b->result;
  cell returned = {0};
  void* destination = &returned;
  if (b->result >= F_STRUCT) {
    otv args[] = {ot_make_int((intptr_t)b->result_type->size),
                  ot_make_int(b->result_type->alignment)};
    aggregate = n_alloc(s, args, 2);
    if (aggregate == OT_UNWIND) goto done;
    pointer* p = ext(aggregate, ctx(s)->pointer_type);
    destination = p->address;
  }
  context* c = ctx(s);
  c->depth++;
  ffi_call(&b->cif, FFI_FN(b->address), destination, addresses);
  c->depth--;
  if (!c->depth && c->pending) {
    if (aggregate != ot_false) release_owner(c, ((pointer*)ext(aggregate, c->pointer_type))->owner);
    result = replay(s);
    goto done;
  }
  result = b->result >= F_STRUCT ? aggregate : decode(s, b->result, returned, "call");
done:
  for (int i = 0; i < n; i++) ot_host_free(temporaries[i]);
  ot_host_free(values);
  ot_host_free(addresses);
  ot_host_free(temporaries);
  return result;
}

static otv n_bind(ots* s, otv* a, int n) {
  ARITY(5, "bind");
  if (a[4] != ot_true && a[4] != ot_false)
    return fail(s, "bind", "type", "expected collection safety boolean");
  library* l = ext(a[0], ctx(s)->library_type);
  if (!l) return fail(s, "bind", "type", "expected library");
  int result = call_kind(a[3]);
  if (result < 0) return fail(s, "bind", "signature", "invalid result type");
  char* name = string(s, a[1], "bind");
  if (!name) return OT_UNWIND;
  if (!offi_symbol(l->handle, name)) {
    ot_host_free(name);
    return fail(s, "bind", "symbol", "native symbol missing");
  }
  size_t len = 0;
  otv tail = a[2], head;
  while (ot_pair_values(tail, &head, &tail)) {
    if (call_kind(head) <= F_VOID) {
      ot_host_free(name);
      return fail(s, "bind", "signature", "invalid argument type");
    }
    if (++len > UINT_MAX) break;
  }
  if (tail != ot_null || len > UINT_MAX) {
    ot_host_free(name);
    return fail(s, "bind", "signature", "invalid argument list");
  }
  binding* b = ot_host_alloc(sizeof(*b));
  if (!b) {
    ot_host_free(name);
    return fail(s, "bind", "allocation", "binding allocation failed");
  }
  b->name = name;
  b->lib = l;
  scope.library = l;
  b->address = offi_symbol(l->handle, name);
  b->count = len;
  b->result = result;
  b->types = ot_host_alloc((len ? len : 1) * sizeof(*b->types));
  b->kinds = ot_host_alloc((len ? len : 1) * sizeof(*b->kinds));
  if (!b->types || !b->kinds) {
    binding_destroy(b);
    return fail(s, "bind", "allocation", "signature allocation failed");
  }
  tail = a[2];
  for (size_t i = 0; i < len; i++) {
    ot_pair_values(tail, &head, &tail);
    b->kinds[i] = call_kind(head);
    b->types[i] = prepare_type(s, head, 0);
    if (!b->types[i]) {
      binding_destroy(b);
      return OT_UNWIND;
    }
  }
  b->result_type = prepare_type(s, a[3], 0);
  if (!b->result_type) {
    binding_destroy(b);
    return OT_UNWIND;
  }
  if (ffi_prep_cif(&b->cif, FFI_DEFAULT_ABI, (unsigned)len, b->result_type, b->types) != FFI_OK) {
    binding_destroy(b);
    return fail(s, "bind", "abi", "unsupported ABI");
  }
  track(s, &b->item, binding_destroy);
  otv context_value = ot_ext_pointer(s, ctx(s)->binding_type, b);
  return ot_make_native(s, b->name, call, context_value);
}

static otv n_fail(ots* s, otv* a, int n) {
  ARITY(3, "error");
  char* op = string(s, a[0], "error");
  if (!op) return OT_UNWIND;
  char* reason = string(s, a[1], "error");
  if (!reason) {
    ot_host_free(op);
    return OT_UNWIND;
  }
  char* message = string(s, a[2], "error");
  if (!message) {
    ot_host_free(op);
    ot_host_free(reason);
    return OT_UNWIND;
  }
  otv result = fail(s, op, reason, message);
  ot_host_free(op);
  ot_host_free(reason);
  ot_host_free(message);
  return result;
}

static otv n_error_field(ots* s, otv* a, int n) {
  ARITY(2, "error-accessor");
  if (ot_value_type(a[0]) != OT_TYPE_TABLE) return ot_false;
  char* key = string(s, a[1], "error-accessor");
  if (!key) return OT_UNWIND;
  otv k = ot_make_keyword(s, key, strlen(key));
  ot_host_free(key);
  return ot_table_get(s, a[0], k, ot_false);
}

static otv layout_call(ots* s, otv data, otv* a, int n) {
  (void)data;
  (void)a;
  (void)n;
  return fail(s, "layout", "type", "layout is not callable");
}

static otv n_layout(ots* s, otv* a, int n) {
  ARITY(1, "layout");
  return ot_make_native(s, "ffi-layout", layout_call, a[0]);
}

static otv n_layout_data(ots* s, otv* a, int n) {
  ARITY(1, "layout");
  if (!ot_is_ptr(a[0]) || ot_object_type(a[0]) != OBJ_NAT) return ot_false;
  ot_nat_obj* f = (ot_nat_obj*)ot_as_obj(a[0]);
  return f->context_function == layout_call ? f->context : ot_false;
}

static void module(ots* s) {
  if (!aapcs64 && !sysv_x86_64) {
    fail(s, "import", "abi", "FFI target is outside the initial ABI profile");
    return;
  }
  if (sizeof(void*) != 8 || sizeof(size_t) != 8 || sizeof(ptrdiff_t) != 8 || sizeof(bool) != 1 ||
      FLT_RADIX != 2 || FLT_MANT_DIG != 24 || DBL_MANT_DIG != 53) {
    fail(s, "import", "abi", "native scalar representation does not match the FFI ABI profile");
    return;
  }

  static const struct {
    const char* name;
    ot_nat function;
  } functions[] = {{"%layout", n_layout},
                   {"%layout-data", n_layout_data},
                   {"%null", n_null},
                   {"null?", n_nullp},
                   {"pointer?", n_pointerp},
                   {"pointer=?", n_equal},
                   {"%sizeof", n_size},
                   {"%alignof", n_align},
                   {"%alloc", n_alloc},
                   {"free!", n_free},
                   {"view", n_view},
                   {"%ref", n_ref},
                   {"%set!", n_set},
                   {"copy!", n_copy},
                   {"read-utf8", n_read},
                   {"alloc-utf8", n_alloc_string},
                   {"allocation-count", n_stats},
                   {"allocation-bytes", n_bytes},
                   {"%open", n_open},
                   {"%loader-error", n_loader_error},
                   {"has-symbol?", n_symbol},
                   {"%bind", n_bind},
                   {"%callback", n_callback},
                   {"free-callback!", n_free_callback},
                   {"callback-count", n_callback_count},
                   {"%fail", n_fail},
                   {"%error-field", n_error_field}};

  ctx(s)->thread = pthread_self();
  for (size_t i = 0; i < sizeof(functions) / sizeof(*functions); i++)
    ot_def_nat(s, functions[i].name, functions[i].function);
  otv ignored;
  if (!ot_eval_src(s, ot_ffi_src, ot_ffi_src_len, "<ffi>", &ignored)) return;
  for (size_t i = 0; i < sizeof(functions) / sizeof(*functions); i++) {
    if (functions[i].name[0] == '%') {
      char source[128];
      int len =
          snprintf(source, sizeof(source), "(def- %s %s)", functions[i].name, functions[i].name);
      if (!ot_eval_src(s, source, (size_t)len, "<ffi-private>", &ignored)) return;
    }
  }
}

void ot_register_ffi(ots* s) {
  context* c = ot_host_alloc(sizeof(*c));
  if (!c) abort();
  s->ffi_state = c;
  c->pending_condition = ot_false;
  ot_global_add(s, &c->pending_condition);
  c->pointer_type = ot_ext_type(s, "ffi.pointer", NULL);
  c->library_type = ot_ext_type(s, "ffi.library", NULL);
  c->binding_type = ot_ext_type(s, "ffi.binding", NULL);
  ot_register_module(s, "otium.ffi", module);
}

void ot_destroy_ffi(ots* s) {
  context* c = ctx(s);
  if (!c) return;
  for (item* p = c->items; p;) {
    item* next = p->next;
    p->destroy(p);
    p = next;
  }
  ot_global_remove(s, &c->pending_condition);
  ot_host_free(c);
  s->ffi_state = NULL;
}
#endif
