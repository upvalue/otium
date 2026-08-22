/* Generation scavenging collector transplanted from GSGC 1.0.
 * Copyright (c) 2011 Ian Piumarta. See LICENSES/GSGC.txt.
 * Otium supplies exact roots, object tracing, finalization, and host allocation. */
#define OT_INTERNAL
#include "otium.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "ot-gc-internal.inc"

#ifndef OT_GSGC_MAX_AGE
#define OT_GSGC_MAX_AGE 4
#endif
#ifndef OT_GSGC_MAX_REMEMBERED
#define OT_GSGC_MAX_REMEMBERED 1024
#endif
#ifndef OT_GSGC_MIN_NEW_SPACE
#define OT_GSGC_MIN_NEW_SPACE (1024u * 1024u)
#endif
#ifndef OT_GC_TIMING
#define OT_GC_TIMING 1
#endif

static_assert(OT_GSGC_MAX_AGE >= 0 && OT_GSGC_MAX_AGE <= UINT8_MAX);
static_assert(OT_GSGC_MAX_REMEMBERED > 0);

typedef union gsgc_header {
  struct {
    size_t block_bytes;
    size_t object_bytes;
    otv forwarding;
    uint8_t age;
    bool remembered;
    bool old;
  } fields;
  max_align_t alignment;
} gsgc_header;

typedef struct gsgc_space {
  const char* name;
  unsigned char* base;
  unsigned char* next;
  unsigned char* limit;
} gsgc_space;

typedef struct gsgc_remembered {
  ot_obj** values;
  size_t count;
  size_t capacity;
} gsgc_remembered;

struct ot_gc_heap {
  gsgc_space new_space;
  gsgc_space survivor_space;
  gsgc_space old_space;
  gsgc_space old_survivor_space;
  gsgc_remembered remembered;
  size_t used_bytes;
  size_t young_bytes;
  size_t collection_copied_bytes;
  size_t collection_promoted_bytes;
  bool collecting_full;
  bool found_young_referent;
#ifdef OT_GC_VALIDATE
  ot_obj* validating_object;
#endif
};

static size_t align_down(size_t value, size_t alignment) { return value - value % alignment; }

static bool align_up(size_t value, size_t alignment, size_t* result) {
  size_t remainder = value % alignment;
  size_t extra = remainder == 0 ? 0 : alignment - remainder;
  if (value > SIZE_MAX - extra) return false;
  *result = value + extra;
  return true;
}

static size_t space_size(const gsgc_space* space) { return (size_t)(space->limit - space->base); }

static size_t space_used(const gsgc_space* space) { return (size_t)(space->next - space->base); }

static size_t space_free(const gsgc_space* space) { return (size_t)(space->limit - space->next); }

static bool space_contains_object(const gsgc_space* space, const ot_obj* object) {
  uintptr_t pointer = (uintptr_t)object;
  return pointer > (uintptr_t)space->base && pointer < (uintptr_t)space->next;
}

static bool space_init(gsgc_space* space, size_t size, const char* name) {
  space->name = name;
  space->base = ot_host_alloc(size);
  if (space->base == NULL) return false;
  space->next = space->base;
  space->limit = space->base + size;
  return true;
}

static void space_destroy(gsgc_space* space) {
  ot_host_free(space->base);
  *space = (gsgc_space){0};
}

static void space_grow(gsgc_space* space, size_t size) {
  unsigned char* grown = ot_host_alloc(size);
  if (grown == NULL) {
    fprintf(stderr, "otium: could not grow GSGC %s to %zu bytes\n", space->name, size);
    abort();
  }
  ot_host_free(space->base);
  space->base = grown;
  space->next = grown;
  space->limit = grown + size;
}

static void space_reset(gsgc_space* space) { space->next = space->base; }

static gsgc_header* object_header(const ot_obj* object) {
  return ((gsgc_header*)(void*)object) - 1;
}

static ot_obj* header_object(gsgc_header* header) { return (ot_obj*)(void*)(header + 1); }

static gsgc_header* next_header(gsgc_header* header) {
  return (gsgc_header*)((unsigned char*)header + header->fields.block_bytes);
}

static void refresh_memory_stats(ots* state) {
  struct ot_gc_heap* heap = state->gc;
  state->stats.reserved_bytes = space_size(&heap->new_space) + space_size(&heap->survivor_space) +
                                space_size(&heap->old_space) +
                                space_size(&heap->old_survivor_space);
  state->stats.metadata_bytes =
      sizeof(*heap) + heap->remembered.capacity * sizeof(*heap->remembered.values);
}

static void remembered_grow(ots* state) {
  struct ot_gc_heap* heap = state->gc;
  size_t capacity = heap->remembered.capacity * 2;
  if (capacity < heap->remembered.capacity ||
      capacity > SIZE_MAX / sizeof(*heap->remembered.values))
    abort();
  void* grown =
      ot_host_realloc(heap->remembered.values, capacity * sizeof(*heap->remembered.values));
  if (grown == NULL) abort();
  heap->remembered.values = grown;
  heap->remembered.capacity = capacity;
  refresh_memory_stats(state);
}

static void remember_object(ots* state, ot_obj* object) {
  struct ot_gc_heap* heap = state->gc;
  gsgc_header* header = object_header(object);
  if (header->fields.remembered) return;
  if (heap->remembered.count == heap->remembered.capacity) remembered_grow(state);
  header->fields.remembered = true;
  heap->remembered.values[heap->remembered.count++] = object;
}

static void copy_event(ots* state, size_t bytes, bool promoted) {
  struct ot_gc_heap* heap = state->gc;
  heap->collection_copied_bytes += bytes;
  state->stats.copied_bytes += bytes;
  if (promoted) {
    heap->collection_promoted_bytes += bytes;
    state->stats.promoted_bytes += bytes;
  }
}

static gsgc_header* copy_to_space(gsgc_header* source, gsgc_space* destination, bool old) {
  size_t block_bytes = source->fields.block_bytes;
  if (block_bytes > space_free(destination)) {
    fprintf(stderr, "otium: GSGC %s is full while copying %zu bytes\n", destination->name,
            block_bytes);
    abort();
  }
  gsgc_header* copy = (gsgc_header*)(void*)destination->next;
  memcpy(copy, source, block_bytes);
  destination->next += block_bytes;
  copy->fields.forwarding = 0;
  copy->fields.remembered = false;
  copy->fields.old = old;
  source->fields.forwarding = ot_from_obj(header_object(copy));
  return copy;
}

static void full_copy_object(ots* state, gsgc_header* source) {
  struct ot_gc_heap* heap = state->gc;
  bool promoted = !source->fields.old;
  (void)copy_to_space(source, &heap->old_survivor_space, true);
  copy_event(state, source->fields.object_bytes, promoted);
}

static void minor_copy_object(ots* state, gsgc_header* source) {
  struct ot_gc_heap* heap = state->gc;
  bool promote = source->fields.age >= OT_GSGC_MAX_AGE;
  gsgc_header* copy =
      copy_to_space(source, promote ? &heap->old_space : &heap->survivor_space, promote);
  if (promote) remember_object(state, header_object(copy));
  else copy->fields.age++;
  copy_event(state, source->fields.object_bytes, promote);
}

static void gsgc_trace_slot(ots* state, otv* slot) {
  if (!ot_is_ptr(*slot)) return;
  struct ot_gc_heap* heap = state->gc;
  ot_obj* object = ot_as_obj(*slot);
  if (heap->collecting_full) {
    if (space_contains_object(&heap->old_survivor_space, object)) return;
  } else if (space_contains_object(&heap->survivor_space, object)) {
    return;
  }
  gsgc_header* source = object_header(object);
  if (heap->collecting_full) {
    if (source->fields.forwarding == 0) full_copy_object(state, source);
    *slot = source->fields.forwarding;
    return;
  }
  if (source->fields.old) return;
  heap->found_young_referent = true;
  if (source->fields.forwarding == 0) minor_copy_object(state, source);
  *slot = source->fields.forwarding;
}

#define OT_GC_TRACE_OBJECT gsgc_trace_object
#define OT_GC_TRACE_ROOTS gsgc_trace_roots
#define OT_GC_TRACE_VM gsgc_trace_vm
#define OT_GC_TRACE_SLOT(state, slot) gsgc_trace_slot((state), (slot))
#include "ot-gc-trace.inc"

static void process_extension_weaks(ots* state) {
  struct ot_gc_heap* heap = state->gc;
  otv old_exts = state->exts;
  otv live = ot_nil;
  while (ot_is_ptr(old_exts)) {
    ot_ext_obj* ext = (ot_ext_obj*)ot_as_obj(old_exts);
    otv next = ext->next;
    gsgc_header* header = object_header((ot_obj*)ext);
    otv resolved;
    if (!heap->collecting_full && header->fields.old) resolved = old_exts;
    else resolved = header->fields.forwarding;
    if (resolved == 0) {
      ot_gc_finalize_ext(state, ext);
    } else {
      ot_ext_obj* retained = (ot_ext_obj*)ot_as_obj(resolved);
      retained->next = live;
      live = resolved;
    }
    old_exts = next;
  }
  state->exts = live;
}

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

static void finish_collection(ots* state, size_t before, size_t after, ot_gc_phase_stats* phase,
                              uint64_t started) {
  if (before > after) state->stats.reclaimed_bytes += before - after;
  state->stats.collections++;
  uint64_t elapsed = pause_elapsed(started);
  ot_gc_record_phase(phase, elapsed);
  ot_gc_record_phase(&state->stats.mutator_pause, elapsed);
}

#ifdef OT_GC_VALIDATE
static bool exact_object_in(const gsgc_space* space, const ot_obj* sought) {
  for (gsgc_header* scan = (gsgc_header*)(void*)space->base; (unsigned char*)scan < space->next;
       scan = next_header(scan)) {
    if (header_object(scan) == sought) return true;
    if ((uintptr_t)sought > (uintptr_t)header_object(scan) &&
        (uintptr_t)sought < (uintptr_t)scan + scan->fields.block_bytes)
      return false;
  }
  return false;
}

static void validate_slot(ots* state, otv* slot) {
  if (!ot_is_ptr(*slot)) return;
  struct ot_gc_heap* heap = state->gc;
  ot_obj* object = ot_as_obj(*slot);
  if (exact_object_in(&heap->new_space, object) || exact_object_in(&heap->old_space, object))
    return;
  if (heap->validating_object == NULL) {
    fprintf(stderr, "otium: invalid GSGC root pointer %#" PRIxPTR " after collection %" PRIu64 "\n",
            (uintptr_t)*slot, state->stats.collections + 1);
  } else {
    fprintf(stderr,
            "otium: invalid GSGC pointer %#" PRIxPTR " in object type %d at byte %zu after "
            "collection %" PRIu64 " (owner age %u old %d; new [%p, %p), old [%p, %p), "
            "discarded-new [%p, %p))\n",
            (uintptr_t)*slot, (int)ot_object_type(ot_from_obj(heap->validating_object)),
            (size_t)((unsigned char*)slot - (unsigned char*)heap->validating_object),
            state->stats.collections + 1,
            (unsigned)object_header(heap->validating_object)->fields.age,
            object_header(heap->validating_object)->fields.old, (void*)heap->new_space.base,
            (void*)heap->new_space.next, (void*)heap->old_space.base, (void*)heap->old_space.next,
            (void*)heap->survivor_space.base, (void*)heap->survivor_space.next);
  }
  abort();
}

#define OT_GC_TRACE_OBJECT validate_object
#define OT_GC_TRACE_ROOTS validate_roots
#define OT_GC_TRACE_VM validate_vm
#define OT_GC_TRACE_SLOT(state, slot) validate_slot((state), (slot))
#include "ot-gc-trace.inc"

static void validate_heap(ots* state) {
  if (!state->config.gc_stress) return;
  struct ot_gc_heap* heap = state->gc;
  heap->validating_object = NULL;
  validate_roots(state);
  validate_slot(state, &state->exts);
  const gsgc_space* spaces[] = {&heap->new_space, &heap->old_space};
  for (size_t i = 0; i < sizeof spaces / sizeof spaces[0]; i++)
    for (gsgc_header* scan = (gsgc_header*)(void*)spaces[i]->base;
         (unsigned char*)scan < spaces[i]->next; scan = next_header(scan)) {
      if (scan->fields.block_bytes < sizeof(*scan) + sizeof(ot_obj) ||
          scan->fields.object_bytes < sizeof(ot_obj) ||
          (unsigned char*)scan + scan->fields.block_bytes > spaces[i]->next) {
        fputs("otium: malformed GSGC block after collection\n", stderr);
        abort();
      }
      heap->validating_object = header_object(scan);
      validate_object(state, heap->validating_object);
    }
  heap->validating_object = NULL;
}
#else
static void validate_heap(ots* state) { (void)state; }
#endif

static void full_collect(ots* state) {
  struct ot_gc_heap* heap = state->gc;
  uint64_t started = pause_start();
  size_t before = heap->used_bytes;
  size_t live_blocks = space_used(&heap->old_space) + space_used(&heap->new_space);
  if (space_size(&heap->old_survivor_space) <= live_blocks) {
    size_t target;
    if (live_blocks > SIZE_MAX - (size_t)OT_GSGC_MIN_NEW_SPACE) abort();
    if (!align_up(live_blocks + (size_t)OT_GSGC_MIN_NEW_SPACE, _Alignof(max_align_t), &target))
      abort();
    space_grow(&heap->old_survivor_space, target);
    refresh_memory_stats(state);
  }

  heap->collecting_full = true;
  heap->collection_copied_bytes = 0;
  heap->collection_promoted_bytes = 0;
  space_reset(&heap->old_survivor_space);
  for (size_t i = 0; i < heap->remembered.count; i++)
    object_header(heap->remembered.values[i])->fields.remembered = false;
  heap->remembered.count = 0;

  gsgc_trace_roots(state);
  for (gsgc_header* scan = (gsgc_header*)(void*)heap->old_survivor_space.base;
       (unsigned char*)scan < heap->old_survivor_space.next; scan = next_header(scan)) {
    gsgc_trace_object(state, header_object(scan));
  }
  process_extension_weaks(state);

  space_reset(&heap->new_space);
  gsgc_space swap = heap->old_space;
  heap->old_space = heap->old_survivor_space;
  heap->old_survivor_space = swap;
  if (space_size(&heap->old_survivor_space) < space_size(&heap->old_space)) {
    space_grow(&heap->old_survivor_space, space_size(&heap->old_space));
    refresh_memory_stats(state);
  }
  heap->used_bytes = heap->collection_copied_bytes;
  heap->young_bytes = 0;
  heap->collecting_full = false;
  validate_heap(state);
  finish_collection(state, before, heap->used_bytes, &state->stats.full_copy, started);
}

static void minor_collect(ots* state) {
  struct ot_gc_heap* heap = state->gc;
  if (space_free(&heap->old_space) < space_used(&heap->new_space)) {
    full_collect(state);
    return;
  }
  uint64_t started = pause_start();
  size_t before = heap->used_bytes;
  size_t before_young = heap->young_bytes;
  if (space_size(&heap->survivor_space) < space_size(&heap->new_space)) {
    space_grow(&heap->survivor_space, space_size(&heap->new_space));
    refresh_memory_stats(state);
  }

  heap->collecting_full = false;
  heap->collection_copied_bytes = 0;
  heap->collection_promoted_bytes = 0;
  space_reset(&heap->survivor_space);
  gsgc_trace_roots(state);

  size_t retained = 0;
  gsgc_header* scan = (gsgc_header*)(void*)heap->survivor_space.base;
  for (;;) {
    for (size_t source = retained; source < heap->remembered.count; source++) {
      ot_obj* object = heap->remembered.values[source];
      heap->found_young_referent = false;
      gsgc_trace_object(state, object);
      if (heap->found_young_referent) heap->remembered.values[retained++] = object;
      else object_header(object)->fields.remembered = false;
    }
    heap->remembered.count = retained;
    if ((unsigned char*)scan == heap->survivor_space.next) break;
    while ((unsigned char*)scan < heap->survivor_space.next) {
      gsgc_trace_object(state, header_object(scan));
      scan = next_header(scan);
    }
    if (retained == heap->remembered.count) break;
  }
  process_extension_weaks(state);

  gsgc_space swap = heap->new_space;
  heap->new_space = heap->survivor_space;
  heap->survivor_space = swap;
  heap->used_bytes = before - before_young + heap->collection_copied_bytes;
  heap->young_bytes = heap->collection_copied_bytes - heap->collection_promoted_bytes;
  validate_heap(state);
  finish_collection(state, before, heap->used_bytes, &state->stats.minor, started);
}

static bool old_headroom_insufficient(const struct ot_gc_heap* heap) {
  size_t target = space_size(&heap->old_survivor_space);
  size_t nursery = space_size(&heap->new_space);
  return target < nursery || space_used(&heap->old_space) >= target - nursery;
}

static bool allocation_block_size(size_t object_bytes, size_t* block_bytes) {
  if (object_bytes > SIZE_MAX - sizeof(gsgc_header)) return false;
  return align_up(sizeof(gsgc_header) + object_bytes, _Alignof(max_align_t), block_bytes);
}

bool ot_gc_heap_init(ots* state) {
  struct ot_gc_heap* heap = ot_host_alloc(sizeof(*heap));
  if (heap == NULL) return false;
  memset(heap, 0, sizeof(*heap));
  state->gc = heap;

  size_t alignment = _Alignof(max_align_t);
  size_t new_size = align_down(state->config.heap_init, alignment);
  size_t minimum = align_down((size_t)OT_GSGC_MIN_NEW_SPACE, alignment);
  if (new_size < minimum) new_size = minimum;
  if (new_size < sizeof(gsgc_header) * 2) goto fail;
  if (new_size > SIZE_MAX / 2) goto fail;
  size_t old_size = new_size * 2;

  if (!space_init(&heap->new_space, new_size, "new space") ||
      !space_init(&heap->survivor_space, new_size, "new survivor space") ||
      !space_init(&heap->old_space, old_size, "old space") ||
      !space_init(&heap->old_survivor_space, old_size, "old survivor space"))
    goto fail;

  heap->remembered.capacity = OT_GSGC_MAX_REMEMBERED;
  if (heap->remembered.capacity < 32) heap->remembered.capacity = 32;
  if (heap->remembered.capacity > SIZE_MAX / sizeof(*heap->remembered.values)) goto fail;
  heap->remembered.values =
      ot_host_alloc(heap->remembered.capacity * sizeof(*heap->remembered.values));
  if (heap->remembered.values == NULL) goto fail;
  refresh_memory_stats(state);
  return true;

fail:
  ot_gc_heap_destroy(state);
  return false;
}

void ot_gc_heap_destroy(ots* state) {
  struct ot_gc_heap* heap = state->gc;
  if (heap == NULL) return;
  ot_host_free(heap->remembered.values);
  space_destroy(&heap->old_survivor_space);
  space_destroy(&heap->old_space);
  space_destroy(&heap->survivor_space);
  space_destroy(&heap->new_space);
  ot_host_free(heap);
  state->gc = NULL;
}

void ot_gc_write_barrier(ots* state, const ot_obj* owner, otv value) {
  if (!ot_is_ptr(value)) return;
  gsgc_header* owner_header = object_header(owner);
  gsgc_header* value_header = object_header(ot_as_obj(value));
  if (owner_header->fields.old && !value_header->fields.old)
    remember_object(state, (ot_obj*)(void*)owner);
}

void* ot_alloc(ots* state, size_t size, ot_obj_type type) {
  struct ot_gc_heap* heap = state->gc;
  size = ot_gc_align_object(size);
  if (size > state->config.heap_max) return NULL;
  size_t block_bytes;
  if (!allocation_block_size(size, &block_bytes)) return NULL;

  if (state->config.gc_stress && heap->young_bytes != 0) minor_collect(state);
  bool needs_collection =
      block_bytes > space_free(&heap->new_space) || heap->remembered.count > OT_GSGC_MAX_REMEMBERED;
  if (needs_collection) {
    do {
      minor_collect(state);
    } while (heap->remembered.count >= OT_GSGC_MAX_REMEMBERED);

    if (block_bytes > space_free(&heap->new_space) || old_headroom_insufficient(heap)) {
      full_collect(state);
      if (block_bytes > space_free(&heap->new_space)) {
        size_t target;
        if (block_bytes > SIZE_MAX / 2 ||
            !align_up(block_bytes * 2, _Alignof(max_align_t), &target))
          return NULL;
        space_grow(&heap->new_space, target);
        refresh_memory_stats(state);
      }
    }
    if (old_headroom_insufficient(heap)) {
      size_t target;
      if (!align_up(space_used(&heap->old_space) + space_size(&heap->new_space),
                    _Alignof(max_align_t), &target))
        return NULL;
      space_grow(&heap->old_survivor_space, target);
      refresh_memory_stats(state);
      full_collect(state);
    }
  }
  if (block_bytes > space_free(&heap->new_space)) return NULL;

  gsgc_header* header = (gsgc_header*)(void*)heap->new_space.next;
  heap->new_space.next += block_bytes;
  memset(header, 0, block_bytes);
  header->fields.block_bytes = block_bytes;
  header->fields.object_bytes = size;
  ot_obj* object = header_object(header);
  object->header = ot_gc_make_header(type, size);

  heap->used_bytes += size;
  heap->young_bytes += size;
  state->stats.allocations++;
  state->stats.allocated_bytes += size;
  if (heap->used_bytes > state->stats.peak_used_bytes)
    state->stats.peak_used_bytes = heap->used_bytes;
  return object;
}

void ot_collect(ots* state) { full_collect(state); }

ot_gc_stats ot_get_gc_stats(const ots* state) {
  const struct ot_gc_heap* heap = state->gc;
  ot_gc_stats stats = state->stats;
  stats.used_bytes = heap->used_bytes;
  stats.capacity_bytes = space_size(&heap->new_space) + space_size(&heap->old_space);
  size_t new_free = space_free(&heap->new_space);
  size_t old_free = space_free(&heap->old_space);
  stats.largest_free_region_bytes = new_free > old_free ? new_free : old_free;
  return stats;
}
