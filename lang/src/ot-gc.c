#define OT_INTERNAL
#include "otium.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "ot-gc-internal.inc"

struct ot_gc_heap {
  unsigned char* reservation;
  unsigned char* from_space;
  unsigned char* to_space;
  unsigned char* alloc;
  unsigned char* limit;
  size_t capacity;
  size_t semispace_bytes;
};

static bool in_from_space(const ots* state, const void* pointer) {
  const struct ot_gc_heap* heap = state->gc;
  const unsigned char* byte = pointer;
  return byte >= heap->from_space && byte < heap->from_space + heap->capacity;
}

static void semi_trace_slot(ots* state, otv* slot) {
  struct ot_gc_heap* heap = state->gc;
  otv value = *slot;
  if (!ot_is_ptr(value)) return;
  ot_obj* old = ot_as_obj(value);
  if (!in_from_space(state, old)) return;
  if ((old->header & 1u) != 0) {
    *slot = old->header & ~(uintptr_t)1u;
    return;
  }

  size_t size = (size_t)(old->header >> 8u);
  if (heap->alloc + size > heap->to_space + heap->capacity) {
    fprintf(stderr,
            "otium: live heap exceeds semispace during collection %" PRIu64
            " (copy %zu bytes, header %#" PRIxPTR ")\n",
            state->stats.collections + 1, size, old->header);
    abort();
  }
  ot_obj* copy = (ot_obj*)heap->alloc;
  memcpy(copy, old, size);
  heap->alloc += size;
  old->header = (uintptr_t)copy | 1u;
  *slot = ot_from_obj(copy);
  state->stats.copied_bytes += size;
}

#define OT_GC_TRACE_OBJECT semi_trace_object
#define OT_GC_TRACE_ROOTS semi_trace_roots
#define OT_GC_TRACE_VM semi_trace_vm
#define OT_GC_TRACE_SLOT(state, slot) semi_trace_slot((state), (slot))
#include "ot-gc-trace.inc"

static void finish_exts(ots* state, otv old_exts) {
  otv live = ot_nil;
  while (ot_is_ptr(old_exts)) {
    ot_ext_obj* ext = (ot_ext_obj*)ot_as_obj(old_exts);
    otv next = ext->next;
    if ((ext->header & 1u) != 0) {
      ot_ext_obj* copy = (ot_ext_obj*)(ext->header & ~(uintptr_t)1u);
      copy->next = live;
      live = ot_from_obj(copy);
    } else {
      ot_gc_finalize_ext(state, ext);
    }
    old_exts = next;
  }
  state->exts = live;
}

#ifdef OT_GC_VALIDATE
static void semi_validate_slot(ots* state, otv* slot) {
  if (!ot_is_ptr(*slot)) return;
  const struct ot_gc_heap* heap = state->gc;
  const unsigned char* pointer = (const unsigned char*)ot_as_obj(*slot);
  if (pointer < heap->from_space || pointer >= heap->alloc) {
    fprintf(stderr, "otium: stale GC value after collection %" PRIu64 "\n",
            state->stats.collections + 1);
    abort();
  }
}

#define OT_GC_TRACE_OBJECT semi_validate_object
#define OT_GC_TRACE_ROOTS semi_validate_roots
#define OT_GC_TRACE_VM semi_validate_vm
#define OT_GC_TRACE_SLOT(state, slot) semi_validate_slot((state), (slot))
#include "ot-gc-trace.inc"

static void validate_heap(ots* state) {
  if (!state->config.gc_stress) return;
  struct ot_gc_heap* heap = state->gc;
  semi_validate_roots(state);
  semi_validate_slot(state, &state->exts);
  unsigned char* scan = heap->from_space;
  while (scan < heap->alloc) {
    ot_obj* object = (ot_obj*)scan;
    size_t size = (size_t)(object->header >> 8u);
    if (size < sizeof(*object) || (size & 7u) != 0 || scan + size > heap->alloc) {
      fputs("otium: malformed object after collection\n", stderr);
      abort();
    }
    semi_validate_object(state, object);
    scan += size;
  }
}
#endif

static uint64_t pause_start(void) {
#if OT_GC_TIMING
  return ot_platform_monotonic_ns();
#else
  return 0;
#endif
}

static uint64_t pause_elapsed(uint64_t start) {
#if OT_GC_TIMING
  return ot_platform_monotonic_ns() - start;
#else
  (void)start;
  return 0;
#endif
}

static void collect_for(ots* state, size_t requested) {
  struct ot_gc_heap* heap = state->gc;
  uint64_t start = pause_start();

  /* Cheney scan: copy roots into the other semispace, then scan copied
   * objects until every reachable edge has been forwarded. */
  size_t before = (size_t)(heap->alloc - heap->from_space);
  otv old_exts = state->exts;
  unsigned char* old_from = heap->from_space;
  heap->alloc = heap->to_space;
  semi_trace_roots(state);
  unsigned char* scan = heap->to_space;
  while (scan < heap->alloc) {
    ot_obj* object = (ot_obj*)scan;
    size_t size = (size_t)(object->header >> 8u);
    semi_trace_object(state, object);
    scan += size;
  }
  finish_exts(state, old_exts);

  size_t after = (size_t)(heap->alloc - heap->to_space);
  heap->from_space = heap->to_space;
  heap->to_space = old_from;
  heap->limit = heap->from_space + heap->capacity;
  state->stats.collections++;
  state->stats.reclaimed_bytes += before > after ? before - after : 0;

  if (after + requested > heap->capacity) {
    size_t grown = heap->capacity;
    while (grown < after + requested && grown < heap->semispace_bytes) {
      size_t next = grown > heap->semispace_bytes / 2 ? heap->semispace_bytes : grown * 2;
      if (next <= grown) break;
      grown = next;
    }
    heap->capacity = grown;
    heap->limit = heap->from_space + heap->capacity;
  }

#ifdef OT_GC_VALIDATE
  validate_heap(state);
#endif
  uint64_t elapsed = pause_elapsed(start);
  ot_gc_record_phase(&state->stats.full_copy, elapsed);
  ot_gc_record_phase(&state->stats.mutator_pause, elapsed);
}

bool ot_gc_heap_init(ots* state) {
  struct ot_gc_heap* heap = ot_host_alloc(sizeof(*heap));
  if (heap == NULL) return false;
  memset(heap, 0, sizeof(*heap));

  heap->semispace_bytes = state->config.heap_max & ~(size_t)7u;
  size_t reservation_bytes = heap->semispace_bytes * 2 + 7;
  heap->reservation = ot_host_alloc(reservation_bytes);
  if (heap->reservation == NULL) {
    ot_host_free(heap);
    return false;
  }
  uintptr_t aligned = ((uintptr_t)heap->reservation + 7u) & ~(uintptr_t)7u;
  heap->from_space = (unsigned char*)aligned;
  heap->to_space = heap->from_space + heap->semispace_bytes;
  heap->alloc = heap->from_space;
  heap->capacity = state->config.heap_init & ~(size_t)7u;
  heap->limit = heap->from_space + heap->capacity;
  state->gc = heap;
  state->stats.reserved_bytes = reservation_bytes;
  state->stats.metadata_bytes = sizeof(*heap);
  return true;
}

void ot_gc_heap_destroy(ots* state) {
  if (state->gc == NULL) return;
  ot_host_free(state->gc->reservation);
  ot_host_free(state->gc);
  state->gc = NULL;
}

void* ot_alloc(ots* state, size_t size, ot_obj_type type) {
  struct ot_gc_heap* heap = state->gc;
  size = ot_gc_align_object(size);
  if (size > heap->semispace_bytes) return NULL;
  if (state->config.gc_stress && heap->alloc != heap->from_space) collect_for(state, size);
  if (heap->alloc + size > heap->limit) collect_for(state, size);
  if (heap->alloc + size > heap->limit) return NULL;

  ot_obj* object = (ot_obj*)heap->alloc;
  heap->alloc += size;
  memset(object, 0, size);
  object->header = ot_gc_make_header(type, size);
  state->stats.allocations++;
  state->stats.allocated_bytes += size;
  size_t used = (size_t)(heap->alloc - heap->from_space);
  if (used > state->stats.peak_used_bytes) state->stats.peak_used_bytes = used;
  return object;
}

void ot_collect(ots* state) { collect_for(state, 0); }

ot_gc_stats ot_get_gc_stats(const ots* state) {
  const struct ot_gc_heap* heap = state->gc;
  ot_gc_stats stats = state->stats;
  stats.used_bytes = (size_t)(heap->alloc - heap->from_space);
  stats.capacity_bytes = heap->capacity;
  stats.largest_free_region_bytes = heap->capacity - stats.used_bytes;
  return stats;
}
