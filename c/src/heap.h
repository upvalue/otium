#ifndef OTIUM_HEAP_H
#define OTIUM_HEAP_H

#include "otium.h"
#include <assert.h>

/* These operations require integer operands. False reports fixnum overflow. */
static inline bool value_int_add(ot_value a, ot_value b, ot_value* out) {
  intptr_t n;
#if OTIUM_FAST_INT_REPR
  if (__builtin_add_overflow(ot_int_bits(a), ot_int_bits(b) - 1, &n)) return false;
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
  if (__builtin_sub_overflow(ot_int_bits(a), ot_int_bits(b) - 1, &n)) return false;
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
  if (__builtin_mul_overflow(ot_int_bits(a) - 1, ot_to_int(b), &n)) return false;
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

/* Roots that don't fit the LIFO list, such as global cells and native stack
 * frames, are visited by a hook the runtime installs: it calls visit on every
 * slot holding a managed value. */
typedef void (*oti_root_visitor)(void* context, ot_value* slot);
typedef void (*oti_root_hook)(void* data, oti_root_visitor visit, void* context);
/* Called once per collection, after everything live has been found and before
 * any memory is reused. It may call oti_heap_survivor and nothing else. */
typedef void (*oti_weak_hook)(void* data);

/* Sizes come from a profile: large for 64-bit hosts, small for 32-bit and
 * embedded targets, unless the build picks one. Objects never cross a block;
 * lines are what marking and reuse track. Chunks are aligned to their size,
 * hold their own metadata, and are handed out a block at a time. Objects of
 * OTI_LARGE_MIN bytes or more are allocated one by one and never move. */
#if !defined(OTIUM_GC_SMALL) && !defined(OTIUM_GC_LARGE)
#if SIZE_MAX == UINT32_MAX
#define OTIUM_GC_SMALL
#else
#define OTIUM_GC_LARGE
#endif
#endif
#ifdef OTIUM_GC_SMALL
#define OTI_LINE_BITS 6
#define OTI_BLOCK_BITS 12
#define OTI_CHUNK_BITS 19
#define OTI_LARGE_MIN 2048
#else
#define OTI_LINE_BITS 7
#define OTI_BLOCK_BITS 15
#define OTI_CHUNK_BITS 22
#define OTI_LARGE_MIN 8192
#endif
#define OTI_LINE ((size_t)1 << OTI_LINE_BITS)
#define OTI_BLOCK ((size_t)1 << OTI_BLOCK_BITS)
#define OTI_CHUNK ((size_t)1 << OTI_CHUNK_BITS)
#define OTI_LINES_PER_BLOCK (OTI_BLOCK / OTI_LINE)
#define OTI_BLOCKS_PER_CHUNK (OTI_CHUNK / OTI_BLOCK)

typedef struct oti_chunk oti_chunk;
typedef struct oti_large oti_large;
typedef struct oti_mark_segment oti_mark_segment;

typedef struct {
  unsigned char** items;
  size_t count, capacity;
} oti_block_list;

typedef struct {
  /* The allocation cursor is start + used, and inline allocation may bump it
   * to end, which is start + ready. Native code keeps the cursor in a
   * register and compares with end. Lowering ready to used sends every
   * allocation to the slow path. start is the beginning of the current hole,
   * which runs to hole_end; it holds nil up to filled. */
  unsigned char* start;
  size_t used, ready;
  unsigned char* end;
  unsigned char *hole_end, *filled;
  /* The block holding the hole, and the line to look for the next hole from. */
  unsigned char* block;
  size_t line;
  /* Objects bigger than a line that don't fit the current hole go here, in a
   * block of their own, instead of making the cursor give up its hole. */
  unsigned char *overflow, *overflow_end;
  /* Collection copies objects out of sparse blocks into these, while
   * evacuating; defragment says the next full collection should. */
  unsigned char *evacuation, *evacuation_end;
  bool evacuating, defragment;
  oti_chunk* chunks;
  oti_large* large;
  oti_block_list free_blocks, recyclable;
  /* Blocks not on the free list, bytes in large objects, and lines marked by
   * the last collection. Capacity limits object bytes, not these. */
  size_t blocks_used, large_bytes, live_lines;
  size_t capacity, limit;
  /* The current mark: 1, 2 or 3. An object or line is marked when it holds
   * the current value. */
  uint32_t epoch;
  /* The mark stack: one segment kept from initialization, and more borrowed
   * from free blocks while marking, so collection doesn't need malloc. */
  oti_mark_segment* marks;
  /* Check every collection against a separate trace (OTIUM_GC_VERIFY). */
  bool verify;
  /* Time spent collecting, and the longest collection, in nanoseconds. */
  uint64_t pause_nanoseconds, longest_pause;
  /* Statistics. allocated_bytes counts finished holes, overflow and large
   * objects; the current hole's share is start + used - hole_start. */
  unsigned char* hole_start;
  size_t allocated_at_collection;
  size_t collections, copied_bytes, allocated_bytes, reclaimed_bytes, peak_used, live_bytes,
      marked_bytes;
  oti_roots* roots;
  oti_root_hook hook;
  oti_weak_hook weak;
  void* hook_data;
#ifdef OTIUM_CHECK_ROOTS
#define OTI_QUARANTINE 16
  oti_block_list quarantine[OTI_QUARANTINE];
  size_t quarantine_next;
#endif
} oti_heap;

/* Capacity and limit cover the whole heap: blocks in use and large objects. */
bool oti_heap_init(oti_heap* h, size_t capacity);
void oti_heap_destroy(oti_heap* h);
bool oti_heap_set_limit(oti_heap* h, size_t limit);
/* Bytes the heap holds: live after the last collection plus allocated since. */
size_t oti_heap_used(const oti_heap* h);

static inline void oti_roots_push(oti_heap* h, oti_roots* r, ot_value* slots, size_t count) {
  *r = (oti_roots){h->roots, slots, count};
  h->roots = r;
}

static inline void oti_roots_pop(oti_heap* h, oti_roots* r) {
  assert(h->roots == r);
  h->roots = r->previous;
}

static inline void oti_heap_set_ready(oti_heap* h, size_t ready) {
  h->ready = ready;
  h->end = h->start + ready;
}

void oti_heap_collect(oti_heap* h);
/* Inside the weak hook: where v lives now, or 0 if it died. */
ot_value oti_heap_survivor(oti_heap* h, ot_value v);
/* Whether v is the address of memory the heap manages. For assertions. */
bool oti_heap_contains(const oti_heap* h, ot_value v);
/* Zero means exhaustion. Every live managed value must be rooted across this call.
 * Fields are traced; trailing bytes are opaque. Returned fields start as nil. */
ot_value oti_heap_alloc(oti_heap* h, uint32_t kind, uint32_t fields, size_t bytes);
#define OTI_FILL_CHUNK (32 * 1024)
void oti_heap_fill(oti_heap* h);

/* Inline field access for runtime object operations. */
/* The same 16 bytes on every word size. Native code writes size and gc as one
 * 64-bit store, so allocation leaves gc zero. */
typedef struct {
  uint32_t size;
  uint32_t gc;
  uint32_t fields, kind;
} oti_object_header;

/* Bits of the gc word. The low two hold the mark, zero for an object
 * allocated since the last collection. Forwarded objects hold their new
 * address over fields and kind. Reclaim marks what oti_reclaim reached; the
 * collection that follows clears it on every object it keeps. */
#define OTI_GC_EPOCH 3u
#define OTI_GC_PINNED 4u
#define OTI_GC_FORWARDED 8u
#define OTI_GC_RECLAIM 16u

/* Closures keep their code, a word the collector skips, ahead of their
 * captured values. internal.h checks that K_CLOSURE has this number. */
#define OTI_CODE_PREFIX_KIND 3

static inline ot_value* object_fields(ot_value object) {
  oti_object_header* o = (oti_object_header*)object;
  return (ot_value*)(o + 1) + ((o->kind & 255) == OTI_CODE_PREFIX_KIND);
}

static inline void* object_bytes(ot_value object) {
  return object_fields(object) + ((oti_object_header*)object)->fields;
}

static inline uint32_t object_kind(ot_value object) {
  return ((oti_object_header*)object)->kind & 255;
}

/* Objects get an identity the first time it's asked for, taken from their
 * address then. It's kept in the header, so it survives being moved. */
static inline uint32_t object_identity(ot_value object) {
  oti_object_header* o = (oti_object_header*)object;
  uint32_t identity = (o->kind >> 8) & 0x7fffff;
  if (!identity) {
    identity = (uint32_t)(object >> 3) & 0x7fffff;
    if (!identity) identity = 1;
    o->kind |= identity << 8;
  }
  return identity;
}

/* A pinned object stays where it is. Large objects never move anyway. */
static inline void object_pin(ot_value object) {
  ((oti_object_header*)object)->gc |= OTI_GC_PINNED;
}

static inline bool object_frozen(ot_value object) {
  return (((oti_object_header*)object)->kind & 0x80000000u) != 0;
}

static inline void object_freeze(ot_value object) {
  ((oti_object_header*)object)->kind |= 0x80000000u;
}

#endif
