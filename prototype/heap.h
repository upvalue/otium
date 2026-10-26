#ifndef OTIUM_PROTOTYPE_HEAP_H
#define OTIUM_PROTOTYPE_HEAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef uintptr_t ot_value;

enum { OT_NIL = 2, OT_FALSE = 6, OT_TRUE = 10, OT_EMPTY = 14 };

static inline ot_value ot_int(intptr_t n) { return ((uintptr_t)n << 1) | 1; }

static inline bool ot_is_int(ot_value v) { return (v & 1) != 0; }
#if !defined(OTIUM_PORTABLE_INTS) && INTPTR_MIN == (-INTPTR_MAX - 1) && (-1 >> 1) == -1
#define OTIUM_FAST_INT_REPR 1

static inline intptr_t value_int_signed(ot_value v) {
  intptr_t bits;
  _Static_assert(sizeof(bits) == sizeof(v), "matching integer widths");
  memcpy(&bits, &v, sizeof(bits));
  return bits;
}
#else
#define OTIUM_FAST_INT_REPR 0
#endif

static inline intptr_t ot_to_int(ot_value v) {
#if OTIUM_FAST_INT_REPR
  return value_int_signed(v) >> 1;
#else
  return v & ((uintptr_t)1 << (sizeof(v) * 8 - 1)) ? -(intptr_t)((~v >> 1) + 1)
                                                   : (intptr_t)(v >> 1);
#endif
}

/* These operations require integer operands. False reports fixnum overflow. */
static inline bool value_int_add(ot_value a, ot_value b, ot_value* out) {
  intptr_t n;
#if OTIUM_FAST_INT_REPR
  if (__builtin_add_overflow(value_int_signed(a), value_int_signed(b) - 1, &n)) return false;
  *out = (ot_value)n;
#else
  if (__builtin_add_overflow(ot_to_int(a), ot_to_int(b), &n) || n < INTPTR_MIN / 2 ||
      n > INTPTR_MAX / 2)
    return false;
  *out = ot_int(n);
#endif
  return true;
}

static inline bool value_int_sub(ot_value a, ot_value b, ot_value* out) {
  intptr_t n;
#if OTIUM_FAST_INT_REPR
  if (__builtin_sub_overflow(value_int_signed(a), value_int_signed(b) - 1, &n)) return false;
  *out = (ot_value)n;
#else
  if (__builtin_sub_overflow(ot_to_int(a), ot_to_int(b), &n) || n < INTPTR_MIN / 2 ||
      n > INTPTR_MAX / 2)
    return false;
  *out = ot_int(n);
#endif
  return true;
}

static inline bool value_int_mul(ot_value a, ot_value b, ot_value* out) {
  intptr_t n;
#if OTIUM_FAST_INT_REPR
  if (__builtin_mul_overflow(value_int_signed(a) - 1, ot_to_int(b), &n)) return false;
  *out = (ot_value)n | 1;
#else
  if (__builtin_mul_overflow(ot_to_int(a), ot_to_int(b), &n) || n < INTPTR_MIN / 2 ||
      n > INTPTR_MAX / 2)
    return false;
  *out = ot_int(n);
#endif
  return true;
}

static inline bool value_is_ptr(ot_value v) { return v != 0 && (v & 7) == 0; }

typedef struct oti_roots oti_roots;

struct oti_roots {
  oti_roots* previous;
  ot_value* slots;
  size_t count;
};

typedef struct {
  unsigned char* space[2];
  size_t capacity, limit, used, collections, copied_bytes;
  unsigned active;
  uint32_t next_identity;
  oti_roots* roots;
} oti_heap;

bool oti_heap_init(oti_heap* h, size_t semispace_bytes);
void oti_heap_destroy(oti_heap* h);
bool oti_heap_set_limit(oti_heap* h, size_t semispace_bytes);
void oti_roots_push(oti_heap* h, oti_roots* r, ot_value* slots, size_t count);
void oti_roots_pop(oti_heap* h, oti_roots* r);
void oti_heap_collect(oti_heap* h);
/* Zero means exhaustion. Every live managed value must be rooted across this call.
 * Fields are traced; trailing bytes are opaque. Returned fields start as nil. */
ot_value oti_heap_alloc(oti_heap* h, uint32_t kind, uint32_t fields, size_t bytes);

/* Inline field access for runtime object operations. */
typedef struct {
  size_t size;
  uint32_t fields, kind;
} oti_object_header;

static inline ot_value* object_fields(ot_value object) { return (ot_value*)((oti_object_header*)object + 1); }

static inline void* object_bytes(ot_value object) {
  return object_fields(object) + ((oti_object_header*)object)->fields;
}

static inline uint32_t object_kind(ot_value object) { return ((oti_object_header*)object)->kind & 255; }

static inline uint32_t object_identity(ot_value object) {
  return (((oti_object_header*)object)->kind >> 8) & 0x7fffff;
}

static inline bool object_frozen(ot_value object) {
  return (((oti_object_header*)object)->kind & 0x80000000u) != 0;
}

static inline void object_freeze(ot_value object) { ((oti_object_header*)object)->kind |= 0x80000000u; }

#endif
