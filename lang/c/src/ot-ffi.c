#define OT_INTERNAL
#include "otium.h"
#ifndef OT_WITH_FFI
static void unavailable(ots* s) { ot_raise(s, "otium.ffi unavailable: rebuild with WITH_FFI=1"); }
void ot_register_ffi(ots* s) { ot_register_module(s, "otium.ffi", unavailable); }
void ot_destroy_ffi(ots* s) { (void)s; }
#else
#include "../../shared/ffi/native.h"
#include "ot-ffi-source.h"
#include <ffi.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>

static void* zero_alloc(size_t count, size_t size) {
  if (size && count > SIZE_MAX / size) return NULL;
  size_t bytes = count * size;
  void* p = ot_host_alloc(bytes ? bytes : 1);
  if (p) memset(p, 0, bytes);
  return p;
}
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
typedef struct pointer {
  void* address;
  owner* owner;
  size_t size;
  bool bounded, owning;
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
typedef struct error_scope {
  ots* state;
  struct error_scope* previous;
  library* library;
  const char* symbol;
  intptr_t argument;
  int expected;
} error_scope;
typedef struct context {
  error_scope* error;
  item* items;
  unsigned pointer_type, library_type, binding_type;
  size_t allocations, bytes;
} context;
static context* ctx(ots* s) { return s->ffi_state; }
static void pop_error(error_scope* e) { ctx(e->state)->error = e->previous; }
#define ERROR_SCOPE()                                                                              \
  error_scope scope [[gnu::cleanup(pop_error)]] = {s, ctx(s)->error, NULL, NULL, -1, -1};          \
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
static int call_kind(otv value) {
  return ot_is_int(value) ? kind(value)
                          : (ot_is_int(nth(value, 0)) ? (int)ot_get_int(nth(value, 0)) : -1);
}
/* A union's overlapping members contribute to its calling classification.
 * Preserve byte classes as well as size: a 4-aligned union can straddle two
 * SysV eightbytes when nested in a struct. See shared/ffi/ABI.md. */
static bool classify_union_member(ots* s, otv d, size_t offset, unsigned char classes[32],
                                  int* floating, unsigned depth) {
  if (depth > 64) {
    fail(s, "bind", "abi", "aggregate nesting exceeds backend limit");
    return false;
  }
  int k = call_kind(d);
  if (k >= 0 && k < F_COUNT) {
    int base = (k == F_FLOAT || k == F_DOUBLE) ? k : -1;
    if (*floating == 0) *floating = base;
    else if (*floating != base) *floating = -1;
    unsigned char cls = base > 0 ? 1 : 2;
    for (size_t i = 0; i < offi_size(k) && offset + i < 32; i++)
      if (classes[offset + i] < cls) classes[offset + i] = cls;
    return true;
  }
  otv fields = nth(d, 3);
  if (k == 25) {
    size_t count_fields = (size_t)ot_get_int(nth(fields, 1));
    if (!count_fields) {
      fail(s, "bind", "signature", "empty by-value array field");
      return false;
    }
    otv element = nth(fields, 0);
    size_t stride = (size_t)ot_get_int(nth(element, 1));
    /* One element establishes the homogeneous base even past our byte map. */
    for (size_t i = 0; i < count_fields; i++) {
      if (!classify_union_member(s, element, offset, classes, floating, depth + 1)) return false;
      if (offset >= 32 || stride > 32 - offset) break;
      offset += stride;
    }
    return true;
  }
  if (k == 23 || k == 24) {
    otv field;
    while (ot_pair_values(fields, &field, &fields)) {
      size_t relative = (size_t)ot_get_int(nth(field, 2));
      size_t at = offset >= 32 || relative >= 32 ? 32 : offset + relative;
      if (!classify_union_member(s, nth(field, 1), at, classes, floating, depth + 1)) return false;
    }
    return true;
  }
  fail(s, "bind", "signature", "invalid aggregate member");
  return false;
}
static ffi_type* prepare_union(ots* s, otv description, unsigned depth) {
  size_t size = (size_t)ot_get_int(nth(description, 1));
  size_t align = (size_t)ot_get_int(nth(description, 2));
  unsigned char classes[32] = {0};
  int floating = 0;
  if (!classify_union_member(s, description, 0, classes, &floating, depth)) return NULL;
  if (!size || (align != 1 && align != 2 && align != 4 && align != 8) || size % align) {
    fail(s, "bind", "abi", "unsupported union size or alignment");
    return NULL;
  }
  size_t unit = align;
  bool homogeneous = false;
#if defined(__aarch64__)
  homogeneous = floating > 0 && size / offi_size(floating) <= 4;
  if (homogeneous) unit = offi_size(floating);
#endif
  size_t count_fields = size / unit;
  if (count_fields > SIZE_MAX / sizeof(ffi_type*) - 1) {
    fail(s, "bind", "allocation", "union descriptor too large");
    return NULL;
  }
  ffi_type* t = zero_alloc(1, sizeof(*t));
  if (!t) {
    fail(s, "bind", "allocation", "union type allocation failed");
    return NULL;
  }
  t->type = FFI_TYPE_STRUCT;
  t->elements = zero_alloc(count_fields + 1, sizeof(*t->elements));
  if (!t->elements) {
    ot_host_free(t);
    fail(s, "bind", "allocation", "union fields allocation failed");
    return NULL;
  }
  for (size_t i = 0; i < count_fields; i++) {
    bool fp = homogeneous;
#if defined(__x86_64__)
    if (size <= 16 && unit >= 4) {
      unsigned char cls = 0;
      for (size_t j = 0; j < unit; j++)
        if (classes[i * unit + j] > cls) cls = classes[i * unit + j];
      fp = cls == 1;
    }
#endif
    t->elements[i] = fp ? (unit == 4 ? &ffi_type_float : &ffi_type_double)
                        : (unit == 1   ? &ffi_type_uint8
                           : unit == 2 ? &ffi_type_uint16
                           : unit == 4 ? &ffi_type_uint32
                                       : &ffi_type_uint64);
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
  if (k == 24) return prepare_union(s, description, depth);
  if (depth > 64 || (k != 23 && k != 25)) {
    fail(s, "bind", "abi", "unsupported aggregate shape");
    return NULL;
  }
  otv fields = nth(description, 3);
  size_t count_fields = 0;
  if (k == 25) {
    otv number = nth(fields, 1);
    if (!ot_is_int(number) || ot_get_int(number) <= 0) {
      fail(s, "bind", "signature", "empty by-value array field");
      return NULL;
    }
    count_fields = (size_t)ot_get_int(number);
  } else {
    otv cursor = fields, field;
    while (ot_pair_values(cursor, &field, &cursor)) count_fields++;
    if (cursor != ot_null || !count_fields) {
      fail(s, "bind", "signature", "invalid aggregate fields");
      return NULL;
    }
  }
  if (count_fields > SIZE_MAX / sizeof(ffi_type*) - 1) {
    fail(s, "bind", "allocation", "aggregate descriptor too large");
    return NULL;
  }
  ffi_type* t = zero_alloc(1, sizeof(*t));
  if (!t) {
    fail(s, "bind", "allocation", "aggregate type allocation failed");
    return NULL;
  }
  t->type = FFI_TYPE_STRUCT;
  t->elements = zero_alloc(count_fields + 1, sizeof(*t->elements));
  if (!t->elements) {
    ot_host_free(t);
    fail(s, "bind", "allocation", "aggregate fields allocation failed");
    return NULL;
  }
  for (size_t i = 0; i < count_fields; i++) {
    otv child = k == 25 ? nth(fields, 0) : nth(nth(fields, i), 1);
    t->elements[i] = prepare_type(s, child, depth + 1);
    if (!t->elements[i]) {
      free_type(t);
      return NULL;
    }
  }
  size_t* offsets = zero_alloc(count_fields, sizeof(*offsets));
  if (!offsets) {
    free_type(t);
    fail(s, "bind", "allocation", "aggregate offsets allocation failed");
    return NULL;
  }
  bool matches = ffi_get_struct_offsets(FFI_DEFAULT_ABI, t, offsets) == FFI_OK;
  matches = matches && t->size == (size_t)ot_get_int(nth(description, 1)) &&
            t->alignment == (size_t)ot_get_int(nth(description, 2));
  if (k == 23)
    for (size_t i = 0; i < count_fields; i++)
      matches = matches && offsets[i] == (size_t)ot_get_int(nth(nth(fields, i), 2));
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
    if (k != F_POINTER && p.address) {
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
  return wrap(s, (pointer){.address = v.p});
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
  owner* o = zero_alloc(1, sizeof(*o));
  if (!o) return fail(s, "alloc", "allocation", "allocation failed");
  o->data = zero_alloc(1, bytes ? bytes : 1);
  o->size = bytes;
  if (!o->data) {
    ot_host_free(o);
    return fail(s, "alloc", "allocation", "allocation failed");
  }
  track(s, &o->item, owner_destroy);
  ctx(s)->allocations++;
  ctx(s)->bytes += bytes;
  return wrap(s, (pointer){o->data, o, bytes, true, true});
}
static otv n_free(ots* s, otv* a, int n) {
  ARITY(1, "free!");
  pointer p;
  if (!ptr(s, a[0], &p, "free!")) return OT_UNWIND;
  if (!p.address) return ot_false;
  if (!p.owning) return fail(s, "free!", "type", "only owning allocations can be freed");
  if (!p.owner->released) {
    ot_host_free(p.owner->data);
    p.owner->data = NULL;
    p.owner->released = true;
    ctx(s)->allocations--;
    ctx(s)->bytes -= p.owner->size;
  }
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
  library* l = zero_alloc(1, sizeof(*l));
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
  cell* values = zero_alloc(count_args, sizeof(*values));
  void** addresses = zero_alloc(count_args, sizeof(*addresses));
  char** temporaries = zero_alloc(count_args, sizeof(*temporaries));
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
    if (b->kinds[i] >= F_COUNT) {
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
  if (b->result >= F_COUNT) {
    otv args[] = {ot_make_int((intptr_t)b->result_type->size),
                  ot_make_int(b->result_type->alignment)};
    aggregate = n_alloc(s, args, 2);
    if (aggregate == OT_UNWIND) goto done;
    pointer* p = ext(aggregate, ctx(s)->pointer_type);
    destination = p->address;
  }
  ffi_call(&b->cif, FFI_FN(b->address), destination, addresses);
  result = b->result >= F_COUNT ? aggregate : decode(s, b->result, returned, "call");
done:
  for (int i = 0; i < n; i++) ot_host_free(temporaries[i]);
  ot_host_free(values);
  ot_host_free(addresses);
  ot_host_free(temporaries);
  return result;
}
static otv n_bind(ots* s, otv* a, int n) {
  ARITY(4, "bind");
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
  binding* b = zero_alloc(1, sizeof(*b));
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
  b->types = zero_alloc(len ? len : 1, sizeof(*b->types));
  b->kinds = zero_alloc(len ? len : 1, sizeof(*b->kinds));
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
#if !(defined(__APPLE__) && defined(__aarch64__)) && !(defined(__linux__) && defined(__x86_64__))
  fail(s, "import", "abi", "FFI target is outside the initial ABI profile");
  return;
#endif
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
                   {"%fail", n_fail},
                   {"%error-field", n_error_field}};
  for (size_t i = 0; i < sizeof(functions) / sizeof(*functions); i++)
    ot_def_nat(s, functions[i].name, functions[i].function);
  otv ignored;
  if (!ot_eval_src(s, ot_ffi_src, ot_ffi_src_len, "<ffi>", &ignored)) return;
  for (size_t i = 0; i < sizeof(functions) / sizeof(*functions); i++) {
    if (functions[i].name[0] == '%') {
      char source[128];
      int len =
          snprintf(source, sizeof(source), "(define- %s %s)", functions[i].name, functions[i].name);
      if (!ot_eval_src(s, source, (size_t)len, "<ffi-private>", &ignored)) return;
    }
  }
}
void ot_register_ffi(ots* s) {
  context* c = zero_alloc(1, sizeof(*c));
  if (!c) abort();
  s->ffi_state = c;
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
  ot_host_free(c);
  s->ffi_state = NULL;
}
#endif
