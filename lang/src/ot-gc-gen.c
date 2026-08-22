#define OT_INTERNAL
#include "otium.h"

#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "ot-gc-internal.inc"

#ifndef OT_GC_NURSERY_BYTES
#define OT_GC_NURSERY_BYTES (2u * 1024u * 1024u)
#endif
#ifndef OT_GC_OLD_CHUNK_BYTES
#define OT_GC_OLD_CHUNK_BYTES (1024u * 1024u)
#endif
#ifndef OT_GC_LARGE_OBJECT_BYTES
#define OT_GC_LARGE_OBJECT_BYTES (256u * 1024u)
#endif
#ifndef OT_GC_MARK_STACK_ENTRIES
#define OT_GC_MARK_STACK_ENTRIES 16384u
#endif
#ifndef OT_GC_TIMING
#define OT_GC_TIMING 1
#endif

#define GC_CARD_WORDS ((size_t)32)
#define GC_CARD_BYTES (GC_CARD_WORDS * sizeof(uintptr_t))
#define GC_START_NONE UINT32_MAX

typedef struct ot_free_obj {
  uintptr_t header;
  struct ot_free_obj* next;
} ot_free_obj;

struct ot_gc_heap {
  unsigned char* nursery_reservation;
  unsigned char* nursery_from;
  unsigned char* nursery_to;
  unsigned char* nursery_alloc;
  unsigned char* nursery_limit;
  unsigned char* nursery_watermark;
  size_t nursery_bytes;
  size_t large_object_bytes;

  unsigned char* old_reservation;
  unsigned char* old_base;
  size_t old_capacity;
  size_t old_max;
  size_t old_floor;
  size_t old_chunk_bytes;
  size_t old_used;

  ot_free_obj* free_list;
  unsigned char* region_alloc;
  unsigned char* region_limit;
  ot_obj* region_last_object;

  uint8_t* remembered;
  uint32_t* starts;
  uint32_t* marks;
  size_t* cumulative;
  uint8_t* overflow;
  size_t card_count;

  ot_obj** mark_stack;
  size_t mark_stack_capacity;
  size_t mark_stack_top;

  unsigned char* minor_from;
  unsigned char* minor_end;
  unsigned char* minor_to_start;
  unsigned char* minor_old_watermark;
  unsigned char* minor_promotion_start;
  unsigned char* minor_promotion_limit;
  ot_obj* minor_owner;
  bool minor_promote;
  size_t pause_depth;
  uint64_t outer_pause_start;
};

static size_t align_down(size_t value, size_t alignment) { return value - value % alignment; }

static size_t align_up(size_t value, size_t alignment) {
  size_t remainder = value % alignment;
  return remainder == 0 ? value : value + alignment - remainder;
}

static bool address_in(const void* pointer, const unsigned char* base, size_t size) {
  uintptr_t address = (uintptr_t)pointer;
  uintptr_t start = (uintptr_t)base;
  return address >= start && address - start < size;
}

static bool in_old(const struct ot_gc_heap* heap, const void* pointer) {
  return address_in(pointer, heap->old_base, heap->old_capacity);
}

static bool in_current_nursery(const struct ot_gc_heap* heap, const void* pointer) {
  return address_in(pointer, heap->nursery_from,
                    (size_t)(heap->nursery_alloc - heap->nursery_from));
}

static bool in_minor_source(const struct ot_gc_heap* heap, const void* pointer) {
  return address_in(pointer, heap->minor_from, (size_t)(heap->minor_end - heap->minor_from));
}

static bool in_minor_destination(const struct ot_gc_heap* heap, const void* pointer) {
  return address_in(pointer, heap->minor_to_start,
                    (size_t)(heap->nursery_alloc - heap->minor_to_start));
}

static size_t object_size(const ot_obj* object) { return (size_t)(object->header >> 8u); }

static ot_obj_type object_type(const ot_obj* object) {
  return (ot_obj_type)((object->header >> 1u) & 0x7fu);
}

static size_t card_for(const struct ot_gc_heap* heap, const void* pointer) {
  return ((uintptr_t)pointer - (uintptr_t)heap->old_base) / GC_CARD_BYTES;
}

static size_t cards_for_bytes(size_t bytes) {
  return bytes == 0 ? 0 : (bytes + GC_CARD_BYTES - 1) / GC_CARD_BYTES;
}

static void remember_object(struct ot_gc_heap* heap, const ot_obj* object) {
  if (in_old(heap, object)) heap->remembered[card_for(heap, object)] = 1;
}

static void record_block_start(struct ot_gc_heap* heap, const void* pointer, size_t size) {
  if (size == 0) return;
  size_t offset = (size_t)((uintptr_t)pointer - (uintptr_t)heap->old_base);
  size_t first = offset / GC_CARD_BYTES;
  size_t last = (offset + size - 1) / GC_CARD_BYTES;
  uint32_t start_word = (uint32_t)(offset / sizeof(uintptr_t));
  for (size_t card = first; card <= last; card++) {
    size_t card_offset = card * GC_CARD_BYTES;
    if (heap->starts[card] == GC_START_NONE || card_offset >= offset)
      heap->starts[card] = start_word;
  }
}

static void set_free_header(ot_free_obj* block, size_t size) {
  block->header = ot_gc_make_header(OBJ_FREE, size);
  block->next = NULL;
}

static void add_free_block(struct ot_gc_heap* heap, unsigned char* start, size_t size,
                           ot_obj** last_live) {
  if (size == 0) return;
  if (size < sizeof(ot_free_obj)) {
    if (*last_live == NULL) {
      fputs("otium: unusable leading old-space fragment\n", stderr);
      abort();
    }
    size_t previous = object_size(*last_live);
    (*last_live)->header = ot_gc_make_header(object_type(*last_live), previous + size);
    heap->old_used += size;
    record_block_start(heap, *last_live, previous + size);
    return;
  }
  ot_free_obj* block = (ot_free_obj*)start;
  set_free_header(block, size);
  block->next = heap->free_list;
  heap->free_list = block;
  record_block_start(heap, block, size);
}

static void close_region(struct ot_gc_heap* heap) {
  if (heap->region_alloc == NULL) return;
  size_t remaining = (size_t)(heap->region_limit - heap->region_alloc);
  add_free_block(heap, heap->region_alloc, remaining, &heap->region_last_object);
  heap->region_alloc = NULL;
  heap->region_limit = NULL;
  heap->region_last_object = NULL;
}

static void rebuild_layout(struct ot_gc_heap* heap) {
  close_region(heap);
  memset(heap->starts, 0xff, heap->card_count * sizeof(*heap->starts));
  heap->free_list = NULL;
  heap->old_used = 0;

  unsigned char* scan = heap->old_base;
  unsigned char* end = heap->old_base + heap->old_capacity;
  unsigned char* free_start = NULL;
  ot_obj* last_live = NULL;
  while (scan < end) {
    ot_obj* object = (ot_obj*)scan;
    size_t size = object_size(object);
    if (size < sizeof(ot_obj) || (size & 7u) != 0 || size > (size_t)(end - scan)) {
      fprintf(stderr, "otium: malformed old-space block at offset %zu\n",
              (size_t)(scan - heap->old_base));
      abort();
    }
    if (object_type(object) == OBJ_FREE) {
      if (free_start == NULL) free_start = scan;
    } else {
      if (free_start != NULL) {
        add_free_block(heap, free_start, (size_t)(scan - free_start), &last_live);
        free_start = NULL;
      }
      record_block_start(heap, object, size);
      heap->old_used += size;
      last_live = object;
    }
    scan += size;
  }
  if (free_start != NULL) add_free_block(heap, free_start, (size_t)(end - free_start), &last_live);
}

static size_t largest_free_region(const struct ot_gc_heap* heap) {
  size_t largest =
      heap->region_alloc == NULL ? 0 : (size_t)(heap->region_limit - heap->region_alloc);
  for (const ot_free_obj* block = heap->free_list; block != NULL; block = block->next) {
    size_t size = object_size((const ot_obj*)block);
    if (size > largest) largest = size;
  }
  return largest;
}

static bool acquire_region(struct ot_gc_heap* heap, size_t requested) {
  close_region(heap);
  ot_free_obj** best_link = NULL;
  size_t best_size = 0;
  for (ot_free_obj** link = &heap->free_list; *link != NULL; link = &(*link)->next) {
    size_t size = object_size((ot_obj*)*link);
    if (size >= requested && size > best_size) {
      best_link = link;
      best_size = size;
    }
  }
  if (best_link == NULL) return false;
  ot_free_obj* best = *best_link;
  *best_link = best->next;
  heap->region_alloc = (unsigned char*)best;
  heap->region_limit = heap->region_alloc + best_size;
  heap->region_last_object = NULL;
  return true;
}

static bool grow_old_space(struct ot_gc_heap* heap, size_t requested) {
  close_region(heap);
  if (requested > heap->old_max - heap->old_capacity) return false;
  size_t wanted = heap->old_capacity + requested;
  size_t target = align_up(wanted, heap->old_chunk_bytes);
  if (target > heap->old_max || target < wanted) target = heap->old_max;
  if (target <= heap->old_capacity) return false;

  unsigned char* appended = heap->old_base + heap->old_capacity;
  size_t added = target - heap->old_capacity;
  set_free_header((ot_free_obj*)appended, added);
  size_t old_cards = cards_for_bytes(heap->old_capacity);
  size_t new_cards = cards_for_bytes(target);
  memset(heap->remembered + old_cards, 0, new_cards - old_cards);
  memset(heap->starts + old_cards, 0xff, (new_cards - old_cards) * sizeof(*heap->starts));
  memset(heap->marks + old_cards, 0, (new_cards - old_cards) * sizeof(*heap->marks));
  memset(heap->cumulative + old_cards, 0, (new_cards - old_cards) * sizeof(*heap->cumulative));
  memset(heap->overflow + old_cards, 0, new_cards - old_cards);
  heap->old_capacity = target;
  rebuild_layout(heap);
  return true;
}

static void* old_allocate_raw(struct ot_gc_heap* heap, size_t size, bool allow_growth) {
  if (size > heap->old_max) return NULL;
  if (heap->region_alloc == NULL || (size_t)(heap->region_limit - heap->region_alloc) < size) {
    if (!acquire_region(heap, size)) {
      if (!allow_growth || !grow_old_space(heap, size) || !acquire_region(heap, size)) return NULL;
    }
  }
  ot_obj* object = (ot_obj*)heap->region_alloc;
  heap->region_alloc += size;
  heap->region_last_object = object;
  heap->old_used += size;
  record_block_start(heap, object, size);
  remember_object(heap, object);
  return object;
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

static void enter_mutator_pause(ots* state) {
  struct ot_gc_heap* heap = state->gc;
  if (heap->pause_depth++ == 0) heap->outer_pause_start = pause_start();
}

static void leave_mutator_pause(ots* state) {
  struct ot_gc_heap* heap = state->gc;
  if (heap->pause_depth == 0) {
    fputs("otium: unbalanced GC pause boundary\n", stderr);
    abort();
  }
  if (--heap->pause_depth == 0)
    ot_gc_record_phase(&state->stats.mutator_pause, pause_elapsed(heap->outer_pause_start));
}

static void major_collect(ots* state, bool force_compact);
#ifdef OT_GC_VALIDATE
static void validate_heap(ots* state);
#endif

static bool prepare_promotion_region(ots* state, size_t upper_bound) {
  struct ot_gc_heap* heap = state->gc;
  close_region(heap);
  if (upper_bound == 0) return false;

  while (largest_free_region(heap) < upper_bound && heap->old_capacity < heap->old_max) {
    size_t largest = largest_free_region(heap);
    size_t missing = upper_bound - largest;
    if (!grow_old_space(heap, missing)) break;
  }
  if (largest_free_region(heap) < upper_bound) {
    major_collect(state, true);
    while (largest_free_region(heap) < upper_bound && heap->old_capacity < heap->old_max) {
      size_t largest = largest_free_region(heap);
      size_t missing = upper_bound - largest;
      if (!grow_old_space(heap, missing)) break;
    }
  }
  return largest_free_region(heap) >= upper_bound && acquire_region(heap, upper_bound);
}

static void minor_trace_slot(ots* state, otv* slot) {
  struct ot_gc_heap* heap = state->gc;
  otv value = *slot;
  if (ot_is_ptr(value)) {
    ot_obj* old = ot_as_obj(value);
    if (in_minor_source(heap, old)) {
      if ((old->header & 1u) != 0) {
        *slot = old->header & ~(uintptr_t)1u;
      } else {
        size_t size = object_size(old);
        bool promote = heap->minor_promote && (unsigned char*)old < heap->minor_old_watermark;
        ot_obj* copy;
        if (promote) {
          copy = old_allocate_raw(heap, size, false);
          if (copy == NULL) {
            fputs("otium: promotion reservation invariant failed\n", stderr);
            abort();
          }
          memcpy(copy, old, size);
          state->stats.promoted_bytes += size;
        } else {
          if (heap->nursery_alloc + size > heap->minor_to_start + heap->nursery_bytes) {
            fputs("otium: live nursery exceeds to-space\n", stderr);
            abort();
          }
          copy = (ot_obj*)heap->nursery_alloc;
          memcpy(copy, old, size);
          heap->nursery_alloc += size;
          state->stats.copied_bytes += size;
        }
        old->header = (uintptr_t)copy | 1u;
        *slot = ot_from_obj(copy);
      }
    }
  }

  if (heap->minor_owner != NULL && ot_is_ptr(*slot) && in_minor_destination(heap, ot_as_obj(*slot)))
    remember_object(heap, heap->minor_owner);
}

#define OT_GC_TRACE_OBJECT minor_visit_object
#define OT_GC_TRACE_ROOTS minor_visit_roots
#define OT_GC_TRACE_VM minor_visit_vm
#define OT_GC_TRACE_SLOT(state, slot) minor_trace_slot((state), (slot))
#include "ot-gc-trace.inc"

static void minor_scan_object(ots* state, ot_obj* object) {
  struct ot_gc_heap* heap = state->gc;
  ot_obj* previous_owner = heap->minor_owner;
  heap->minor_owner = in_old(heap, object) ? object : NULL;
  minor_visit_object(state, object);
  heap->minor_owner = previous_owner;
}

static void minor_scan_remembered(ots* state) {
  struct ot_gc_heap* heap = state->gc;
  size_t active_cards = cards_for_bytes(heap->old_capacity);
  for (size_t card = 0; card < active_cards; card++) {
    if (heap->overflow[card] == 0) continue;
    heap->overflow[card] = 0;
    if (heap->starts[card] == GC_START_NONE) continue;
    unsigned char* scan = heap->old_base + (size_t)heap->starts[card] * sizeof(uintptr_t);
    unsigned char* card_end = heap->old_base + (card + 1) * GC_CARD_BYTES;
    if (card_end > heap->old_base + heap->old_capacity)
      card_end = heap->old_base + heap->old_capacity;
    while (scan < card_end) {
      if (heap->minor_promotion_start != NULL && scan >= heap->minor_promotion_start &&
          scan < heap->minor_promotion_limit) {
        scan = heap->minor_promotion_limit;
        continue;
      }
      ot_obj* object = (ot_obj*)scan;
      size_t size = object_size(object);
      if (size < sizeof(ot_obj) || (size & 7u) != 0 ||
          size > (size_t)(heap->old_base + heap->old_capacity - scan)) {
        fputs("otium: malformed block while scanning remembered set\n", stderr);
        abort();
      }
      if (object_type(object) != OBJ_FREE) minor_scan_object(state, object);
      scan += size;
    }
  }
}

static void minor_finish_exts(ots* state, otv old_exts) {
  struct ot_gc_heap* heap = state->gc;
  otv live = ot_nil;
  while (ot_is_ptr(old_exts)) {
    ot_ext_obj* ext = (ot_ext_obj*)ot_as_obj(old_exts);
    otv next = ext->next;
    if (in_minor_source(heap, ext)) {
      if ((ext->header & 1u) != 0) {
        ot_ext_obj* copy = (ot_ext_obj*)(ext->header & ~(uintptr_t)1u);
        copy->next = live;
        live = ot_from_obj(copy);
      } else {
        ot_gc_finalize_ext(state, ext);
      }
    } else {
      ext->next = live;
      live = ot_from_obj(ext);
    }
    old_exts = next;
  }
  state->exts = live;
}

static void minor_collect(ots* state) {
  struct ot_gc_heap* heap = state->gc;
  if (heap->nursery_alloc == heap->nursery_from) return;
  enter_mutator_pause(state);
  size_t source_used = (size_t)(heap->nursery_alloc - heap->nursery_from);
  size_t promotion_upper = (size_t)(heap->nursery_watermark - heap->nursery_from);

  heap->minor_promote = prepare_promotion_region(state, promotion_upper);
  uint64_t start = pause_start();
  size_t before = heap->old_used + source_used;
  otv old_exts = state->exts;
  heap->minor_from = heap->nursery_from;
  heap->minor_end = heap->nursery_alloc;
  heap->minor_to_start = heap->nursery_to;
  heap->minor_old_watermark = heap->nursery_watermark;
  heap->minor_promotion_start = heap->region_alloc;
  heap->minor_promotion_limit = heap->region_limit;

  size_t active_cards = cards_for_bytes(heap->old_capacity);
  memcpy(heap->overflow, heap->remembered, active_cards);
  memset(heap->remembered, 0, active_cards);

  unsigned char* promoted_scan = heap->region_alloc;
  unsigned char* nursery_scan = heap->minor_to_start;
  heap->nursery_alloc = heap->minor_to_start;
  minor_visit_roots(state);
  minor_scan_remembered(state);
  while (nursery_scan < heap->nursery_alloc ||
         (promoted_scan != NULL && promoted_scan < heap->region_alloc)) {
    if (nursery_scan < heap->nursery_alloc) {
      ot_obj* object = (ot_obj*)nursery_scan;
      size_t size = object_size(object);
      minor_scan_object(state, object);
      nursery_scan += size;
    } else {
      ot_obj* object = (ot_obj*)promoted_scan;
      size_t size = object_size(object);
      minor_scan_object(state, object);
      promoted_scan += size;
    }
  }
  minor_finish_exts(state, old_exts);
  close_region(heap);

  unsigned char* old_from = heap->nursery_from;
  heap->nursery_from = heap->minor_to_start;
  heap->nursery_to = old_from;
  heap->nursery_limit = heap->nursery_from + heap->nursery_bytes;
  heap->nursery_watermark = heap->nursery_alloc;
  heap->minor_from = NULL;
  heap->minor_end = NULL;
  heap->minor_to_start = NULL;
  heap->minor_old_watermark = NULL;
  heap->minor_promotion_start = NULL;
  heap->minor_promotion_limit = NULL;
  heap->minor_owner = NULL;
  size_t after = heap->old_used + (size_t)(heap->nursery_alloc - heap->nursery_from);
  state->stats.collections++;
  if (before > after) state->stats.reclaimed_bytes += before - after;
  ot_gc_record_phase(&state->stats.minor, pause_elapsed(start));
#ifdef OT_GC_VALIDATE
  validate_heap(state);
#endif
  leave_mutator_pause(state);
}

static bool word_is_marked(const struct ot_gc_heap* heap, const ot_obj* object) {
  size_t word = ((uintptr_t)object - (uintptr_t)heap->old_base) / sizeof(uintptr_t);
  return (heap->marks[word / GC_CARD_WORDS] & (UINT32_C(1) << (word % GC_CARD_WORDS))) != 0;
}

static void mark_object_words(struct ot_gc_heap* heap, const ot_obj* object) {
  size_t first = ((uintptr_t)object - (uintptr_t)heap->old_base) / sizeof(uintptr_t);
  size_t count = object_size(object) / sizeof(uintptr_t);
  while (count != 0) {
    size_t card = first / GC_CARD_WORDS;
    size_t bit = first % GC_CARD_WORDS;
    size_t take = GC_CARD_WORDS - bit;
    if (take > count) take = count;
    uint32_t low = take == 32 ? UINT32_MAX : (UINT32_C(1) << take) - 1u;
    heap->marks[card] |= low << bit;
    first += take;
    count -= take;
  }
}

static void major_mark_slot(ots* state, otv* slot) {
  struct ot_gc_heap* heap = state->gc;
  if (!ot_is_ptr(*slot)) return;
  ot_obj* object = ot_as_obj(*slot);
  if (!in_old(heap, object)) return;
  if (object_type(object) == OBJ_FREE) {
    fputs("otium: strong pointer refers to free old-space memory\n", stderr);
    abort();
  }
  if (word_is_marked(heap, object)) return;
  mark_object_words(heap, object);
  if (heap->mark_stack_top < heap->mark_stack_capacity) {
    heap->mark_stack[heap->mark_stack_top++] = object;
  } else {
    heap->overflow[card_for(heap, object)] = 1;
    state->stats.mark_stack_overflows++;
  }
}

#define OT_GC_TRACE_OBJECT major_mark_object
#define OT_GC_TRACE_ROOTS major_mark_roots
#define OT_GC_TRACE_VM major_mark_vm
#define OT_GC_TRACE_SLOT(state, slot) major_mark_slot((state), (slot))
#include "ot-gc-trace.inc"

static void mark_nursery_references(ots* state) {
  struct ot_gc_heap* heap = state->gc;
  unsigned char* scan = heap->nursery_from;
  while (scan < heap->nursery_alloc) {
    ot_obj* object = (ot_obj*)scan;
    size_t size = object_size(object);
    major_mark_object(state, object);
    scan += size;
  }
}

static bool recover_mark_overflow(ots* state) {
  struct ot_gc_heap* heap = state->gc;
  bool found = false;
  size_t active_cards = cards_for_bytes(heap->old_capacity);
  for (size_t card = 0; card < active_cards; card++) {
    if (heap->overflow[card] == 0) continue;
    heap->overflow[card] = 0;
    found = true;
    unsigned char* scan = heap->old_base + (size_t)heap->starts[card] * sizeof(uintptr_t);
    unsigned char* card_end = heap->old_base + (card + 1) * GC_CARD_BYTES;
    if (card_end > heap->old_base + heap->old_capacity)
      card_end = heap->old_base + heap->old_capacity;
    while (scan < card_end) {
      ot_obj* object = (ot_obj*)scan;
      size_t size = object_size(object);
      if (object_type(object) != OBJ_FREE && word_is_marked(heap, object))
        major_mark_object(state, object);
      scan += size;
    }
  }
  return found;
}

static void mark_old_space(ots* state) {
  struct ot_gc_heap* heap = state->gc;
  close_region(heap);
  size_t active_cards = cards_for_bytes(heap->old_capacity);
  memset(heap->marks, 0, active_cards * sizeof(*heap->marks));
  memset(heap->overflow, 0, active_cards);
  heap->mark_stack_top = 0;

  major_mark_roots(state);
  mark_nursery_references(state);
  for (;;) {
    while (heap->mark_stack_top != 0) {
      ot_obj* object = heap->mark_stack[--heap->mark_stack_top];
      major_mark_object(state, object);
    }
    if (!recover_mark_overflow(state)) break;
  }
}

static unsigned popcount32(uint32_t value) {
  unsigned count = 0;
  while (value != 0) {
    value &= value - 1;
    count++;
  }
  return count;
}

static size_t build_cumulative_marks(struct ot_gc_heap* heap) {
  size_t words = 0;
  size_t active_cards = cards_for_bytes(heap->old_capacity);
  for (size_t card = 0; card < active_cards; card++) {
    heap->cumulative[card] = words;
    words += popcount32(heap->marks[card]);
  }
  return words;
}

static ot_obj* compacted_address(const struct ot_gc_heap* heap, const ot_obj* object) {
  size_t word = ((uintptr_t)object - (uintptr_t)heap->old_base) / sizeof(uintptr_t);
  size_t card = word / GC_CARD_WORDS;
  size_t bit = word % GC_CARD_WORDS;
  uint32_t lower = bit == 0 ? 0 : heap->marks[card] & ((UINT32_C(1) << bit) - 1u);
  size_t destination_word = heap->cumulative[card] + popcount32(lower);
  return (ot_obj*)(heap->old_base + destination_word * sizeof(uintptr_t));
}

static void analyze_marks(struct ot_gc_heap* heap, size_t* live_bytes, size_t* highest_live) {
  size_t live = 0;
  size_t highest = 0;
  unsigned char* scan = heap->old_base;
  unsigned char* end = heap->old_base + heap->old_capacity;
  while (scan < end) {
    ot_obj* object = (ot_obj*)scan;
    size_t size = object_size(object);
    if (object_type(object) != OBJ_FREE && word_is_marked(heap, object)) {
      live += size;
      highest = (size_t)(scan + size - heap->old_base);
    }
    scan += size;
  }
  *live_bytes = live;
  *highest_live = highest;
}

static void major_fix_slot(ots* state, otv* slot) {
  struct ot_gc_heap* heap = state->gc;
  if (!ot_is_ptr(*slot)) return;
  ot_obj* object = ot_as_obj(*slot);
  if (!in_old(heap, object)) return;
  if (!word_is_marked(heap, object)) {
    fputs("otium: live object contains pointer to dead old-space object\n", stderr);
    abort();
  }
  *slot = ot_from_obj(compacted_address(heap, object));
}

#define OT_GC_TRACE_OBJECT major_fix_object
#define OT_GC_TRACE_ROOTS major_fix_roots
#define OT_GC_TRACE_VM major_fix_vm
#define OT_GC_TRACE_SLOT(state, slot) major_fix_slot((state), (slot))
#include "ot-gc-trace.inc"

static void relink_major_exts(ots* state, bool compact) {
  struct ot_gc_heap* heap = state->gc;
  otv cursor = state->exts;
  otv live = ot_nil;
  while (ot_is_ptr(cursor)) {
    ot_ext_obj* ext = (ot_ext_obj*)ot_as_obj(cursor);
    otv next = ext->next;
    if (in_old(heap, ext)) {
      if (word_is_marked(heap, (ot_obj*)ext)) {
        ot_ext_obj* destination =
            compact ? (ot_ext_obj*)compacted_address(heap, (ot_obj*)ext) : ext;
        ext->next = live;
        live = ot_from_obj(destination);
      } else {
        ot_gc_finalize_ext(state, ext);
      }
    } else if (in_current_nursery(heap, ext)) {
      ext->next = live;
      live = ot_from_obj(ext);
    } else {
      fputs("otium: extension list contains a pointer outside the heap\n", stderr);
      abort();
    }
    cursor = next;
  }
  state->exts = live;
}

static void fix_compaction_pointers(ots* state) {
  struct ot_gc_heap* heap = state->gc;
  major_fix_roots(state);

  unsigned char* scan = heap->nursery_from;
  while (scan < heap->nursery_alloc) {
    ot_obj* object = (ot_obj*)scan;
    size_t size = object_size(object);
    major_fix_object(state, object);
    scan += size;
  }

  scan = heap->old_base;
  unsigned char* end = heap->old_base + heap->old_capacity;
  while (scan < end) {
    ot_obj* object = (ot_obj*)scan;
    size_t size = object_size(object);
    if (object_type(object) != OBJ_FREE && word_is_marked(heap, object))
      major_fix_object(state, object);
    scan += size;
  }
}

static size_t retained_capacity(const struct ot_gc_heap* heap, size_t highest_live) {
  size_t target =
      highest_live == 0 ? heap->old_floor : align_up(highest_live, heap->old_chunk_bytes);
  if (target < heap->old_floor) target = heap->old_floor;
  if (target > heap->old_capacity) target = heap->old_capacity;
  return target;
}

static void clear_major_metadata(struct ot_gc_heap* heap) {
  size_t active_cards = cards_for_bytes(heap->old_capacity);
  memset(heap->marks, 0, active_cards * sizeof(*heap->marks));
  memset(heap->cumulative, 0, active_cards * sizeof(*heap->cumulative));
  memset(heap->overflow, 0, active_cards);
}

static void sweep_old_space(struct ot_gc_heap* heap, size_t highest_live) {
  unsigned char* original_end = heap->old_base + heap->old_capacity;
  size_t target = retained_capacity(heap, highest_live);
  heap->old_capacity = target;
  heap->free_list = NULL;
  heap->region_alloc = NULL;
  heap->region_limit = NULL;
  heap->region_last_object = NULL;
  heap->old_used = 0;
  memset(heap->starts, 0xff, heap->card_count * sizeof(*heap->starts));
  memset(heap->remembered, 0, heap->card_count);

  unsigned char* scan = heap->old_base;
  unsigned char* free_start = NULL;
  ot_obj* last_live = NULL;
  while (scan < original_end) {
    ot_obj* object = (ot_obj*)scan;
    size_t size = object_size(object);
    bool live = object_type(object) != OBJ_FREE && word_is_marked(heap, object);
    if (live) {
      if (free_start != NULL && free_start < heap->old_base + target) {
        unsigned char* free_end = scan < heap->old_base + target ? scan : heap->old_base + target;
        add_free_block(heap, free_start, (size_t)(free_end - free_start), &last_live);
        free_start = NULL;
      }
      if (scan >= heap->old_base + target || scan + size > heap->old_base + target) {
        fputs("otium: retained old-space capacity excludes a live object\n", stderr);
        abort();
      }
      record_block_start(heap, object, size);
      remember_object(heap, object);
      heap->old_used += size;
      last_live = object;
    } else if (free_start == NULL) {
      free_start = scan;
    }
    scan += size;
  }
  if (free_start != NULL && free_start < heap->old_base + target)
    add_free_block(heap, free_start, (size_t)(heap->old_base + target - free_start), &last_live);
  clear_major_metadata(heap);
}

static void compact_old_space(ots* state, size_t live_bytes) {
  struct ot_gc_heap* heap = state->gc;
  build_cumulative_marks(heap);
  relink_major_exts(state, true);
  fix_compaction_pointers(state);

  unsigned char* scan = heap->old_base;
  unsigned char* original_end = heap->old_base + heap->old_capacity;
  while (scan < original_end) {
    ot_obj* object = (ot_obj*)scan;
    size_t size = object_size(object);
    if (object_type(object) != OBJ_FREE && word_is_marked(heap, object)) {
      ot_obj* destination = compacted_address(heap, object);
      if (destination != object) {
        memmove(destination, object, size);
        state->stats.moved_bytes += size;
      }
    }
    scan += size;
  }

  size_t target = retained_capacity(heap, live_bytes);
  heap->old_capacity = target;
  heap->free_list = NULL;
  heap->region_alloc = NULL;
  heap->region_limit = NULL;
  heap->region_last_object = NULL;
  heap->old_used = 0;
  memset(heap->starts, 0xff, heap->card_count * sizeof(*heap->starts));
  memset(heap->remembered, 0, heap->card_count);

  scan = heap->old_base;
  unsigned char* live_end = heap->old_base + live_bytes;
  ot_obj* last_live = NULL;
  while (scan < live_end) {
    ot_obj* object = (ot_obj*)scan;
    size_t size = object_size(object);
    record_block_start(heap, object, size);
    remember_object(heap, object);
    heap->old_used += size;
    last_live = object;
    scan += size;
  }
  add_free_block(heap, scan, (size_t)(heap->old_base + target - scan), &last_live);
  clear_major_metadata(heap);
}

static bool compaction_policy(const struct ot_gc_heap* heap, size_t live_bytes, size_t highest_live,
                              bool forced) {
  if (forced) return true;
  size_t sweep_chunks =
      highest_live == 0 ? 0 : (highest_live + heap->old_chunk_bytes - 1) / heap->old_chunk_bytes;
  size_t compact_chunks =
      live_bytes == 0 ? 0 : (live_bytes + heap->old_chunk_bytes - 1) / heap->old_chunk_bytes;
  return compact_chunks < sweep_chunks;
}

static void major_collect(ots* state, bool force_compact) {
  struct ot_gc_heap* heap = state->gc;
  enter_mutator_pause(state);
  uint64_t start = pause_start();
  size_t before = heap->old_used;
  mark_old_space(state);

  size_t live_bytes;
  size_t highest_live;
  analyze_marks(heap, &live_bytes, &highest_live);
  bool compact = compaction_policy(heap, live_bytes, highest_live,
                                   force_compact || state->config.gc_force_compact);
  if (compact) {
    compact_old_space(state, live_bytes);
  } else {
    relink_major_exts(state, false);
    sweep_old_space(heap, highest_live);
  }
  state->stats.collections++;
  if (before > heap->old_used) state->stats.reclaimed_bytes += before - heap->old_used;
  if (compact) ot_gc_record_phase(&state->stats.major_compact, pause_elapsed(start));
  else ot_gc_record_phase(&state->stats.major_sweep, pause_elapsed(start));
#ifdef OT_GC_VALIDATE
  validate_heap(state);
#endif
  leave_mutator_pause(state);
}

#ifdef OT_GC_VALIDATE
static bool exact_heap_object(const struct ot_gc_heap* heap, const ot_obj* sought) {
  if (in_current_nursery(heap, sought)) {
    unsigned char* scan = heap->nursery_from;
    while (scan < heap->nursery_alloc) {
      ot_obj* object = (ot_obj*)scan;
      size_t size = object_size(object);
      if (object == sought) return true;
      if ((uintptr_t)sought > (uintptr_t)object && (uintptr_t)sought < (uintptr_t)object + size)
        return false;
      scan += size;
    }
    return false;
  }
  if (!in_old(heap, sought)) return false;
  size_t card = card_for(heap, sought);
  if (heap->starts[card] == GC_START_NONE) return false;
  unsigned char* scan = heap->old_base + (size_t)heap->starts[card] * sizeof(uintptr_t);
  while (scan < heap->old_base + heap->old_capacity) {
    ot_obj* object = (ot_obj*)scan;
    size_t size = object_size(object);
    if (object == sought) return object_type(object) != OBJ_FREE;
    if ((uintptr_t)sought > (uintptr_t)object && (uintptr_t)sought < (uintptr_t)object + size)
      return false;
    if ((uintptr_t)object > (uintptr_t)sought) return false;
    scan += size;
  }
  return false;
}

static void validate_slot(ots* state, otv* slot) {
  if (!ot_is_ptr(*slot)) return;
  if (!exact_heap_object(state->gc, ot_as_obj(*slot))) {
    fprintf(stderr, "otium: invalid heap pointer %#" PRIxPTR " after collection %" PRIu64 "\n",
            (uintptr_t)*slot, state->stats.collections);
    abort();
  }
}

#define OT_GC_TRACE_OBJECT validate_object
#define OT_GC_TRACE_ROOTS validate_roots
#define OT_GC_TRACE_VM validate_vm
#define OT_GC_TRACE_SLOT(state, slot) validate_slot((state), (slot))
#include "ot-gc-trace.inc"

static void validate_heap(ots* state) {
  if (!state->config.gc_stress) return;
  struct ot_gc_heap* heap = state->gc;
  if (heap->region_alloc != NULL) {
    fputs("otium: active allocation region left open after collection\n", stderr);
    abort();
  }
  validate_roots(state);

  size_t ext_count = 0;
  for (otv cursor = state->exts; ot_is_ptr(cursor);) {
    validate_slot(state, &cursor);
    if (!ot_has_type(cursor, OBJ_EXT) || ++ext_count > state->stats.allocations + 1) {
      fputs("otium: malformed extension list after collection\n", stderr);
      abort();
    }
    cursor = ((ot_ext_obj*)ot_as_obj(cursor))->next;
  }

  unsigned char* scan = heap->nursery_from;
  while (scan < heap->nursery_alloc) {
    ot_obj* object = (ot_obj*)scan;
    size_t size = object_size(object);
    if (size < sizeof(ot_obj) || (size & 7u) != 0 || size > (size_t)(heap->nursery_alloc - scan)) {
      fputs("otium: malformed nursery object after collection\n", stderr);
      abort();
    }
    validate_object(state, object);
    scan += size;
  }

  scan = heap->old_base;
  unsigned char* old_end = heap->old_base + heap->old_capacity;
  while (scan < old_end) {
    ot_obj* object = (ot_obj*)scan;
    size_t size = object_size(object);
    if (size < sizeof(ot_obj) || (size & 7u) != 0 || size > (size_t)(old_end - scan)) {
      fputs("otium: malformed old-space object after collection\n", stderr);
      abort();
    }
    if (object_type(object) != OBJ_FREE) validate_object(state, object);
    scan += size;
  }
}
#endif

static void update_peak(ots* state) {
  struct ot_gc_heap* heap = state->gc;
  size_t used = heap->old_used + (size_t)(heap->nursery_alloc - heap->nursery_from);
  if (used > state->stats.peak_used_bytes) state->stats.peak_used_bytes = used;
}

static void* allocate_old_with_collection(ots* state, size_t size) {
  struct ot_gc_heap* heap = state->gc;
  if (state->config.gc_stress) {
    enter_mutator_pause(state);
    minor_collect(state);
    major_collect(state, false);
    void* stressed = old_allocate_raw(heap, size, true);
    leave_mutator_pause(state);
    if (stressed != NULL) return stressed;
  }

  void* object = old_allocate_raw(heap, size, true);
  if (object != NULL) return object;
  enter_mutator_pause(state);
  minor_collect(state);
  object = old_allocate_raw(heap, size, true);
  if (object == NULL) {
    major_collect(state, true);
    object = old_allocate_raw(heap, size, true);
  }
  leave_mutator_pause(state);
  return object;
}

bool ot_gc_heap_init(ots* state) {
  struct ot_gc_heap* heap = ot_host_alloc(sizeof(*heap));
  if (heap == NULL) return false;
  memset(heap, 0, sizeof(*heap));
  state->gc = heap;

  size_t total_max = align_down(state->config.heap_max, GC_CARD_BYTES);
  if (total_max < GC_CARD_BYTES * 2) goto fail;
  size_t nursery = align_down((size_t)OT_GC_NURSERY_BYTES, sizeof(uintptr_t));
  size_t nursery_max = align_down(total_max / 4, sizeof(uintptr_t));
  if (nursery < GC_CARD_BYTES) nursery = GC_CARD_BYTES;
  if (nursery > nursery_max) nursery = nursery_max;
  if (nursery < GC_CARD_BYTES) goto fail;

  heap->nursery_bytes = nursery;
  heap->old_max = align_down(total_max - nursery, GC_CARD_BYTES);
  if (heap->old_max < GC_CARD_BYTES || heap->old_max / sizeof(uintptr_t) > UINT32_MAX) goto fail;
  heap->card_count = cards_for_bytes(heap->old_max);

  size_t chunk = align_up((size_t)OT_GC_OLD_CHUNK_BYTES, GC_CARD_BYTES);
  if (chunk < GC_CARD_BYTES) chunk = GC_CARD_BYTES;
  if (chunk > heap->old_max) chunk = heap->old_max;
  heap->old_chunk_bytes = chunk;

  size_t initial = state->config.heap_init > nursery ? state->config.heap_init - nursery : chunk;
  if (initial < chunk) initial = chunk;
  initial = align_up(initial, chunk);
  if (initial > heap->old_max) initial = heap->old_max;
  heap->old_floor = initial;
  heap->old_capacity = initial;

  size_t large = ot_gc_align_object((size_t)OT_GC_LARGE_OBJECT_BYTES);
  if (large < sizeof(ot_free_obj)) large = sizeof(ot_free_obj);
  heap->large_object_bytes = large;

  size_t nursery_reservation_bytes = nursery * 2 + 7;
  heap->nursery_reservation = ot_host_alloc(nursery_reservation_bytes);
  if (heap->nursery_reservation == NULL) goto fail;
  uintptr_t nursery_aligned = ((uintptr_t)heap->nursery_reservation + 7u) & ~(uintptr_t)7u;
  heap->nursery_from = (unsigned char*)nursery_aligned;
  heap->nursery_to = heap->nursery_from + nursery;
  heap->nursery_alloc = heap->nursery_from;
  heap->nursery_limit = heap->nursery_from + nursery;
  heap->nursery_watermark = heap->nursery_from;

  size_t old_reservation_bytes = heap->old_max + GC_CARD_BYTES - 1;
  heap->old_reservation = ot_host_alloc(old_reservation_bytes);
  if (heap->old_reservation == NULL) goto fail;
  uintptr_t old_aligned = align_up((uintptr_t)heap->old_reservation, GC_CARD_BYTES);
  heap->old_base = (unsigned char*)old_aligned;

  heap->remembered = ot_host_alloc(heap->card_count * sizeof(*heap->remembered));
  heap->starts = ot_host_alloc(heap->card_count * sizeof(*heap->starts));
  heap->marks = ot_host_alloc(heap->card_count * sizeof(*heap->marks));
  heap->cumulative = ot_host_alloc(heap->card_count * sizeof(*heap->cumulative));
  heap->overflow = ot_host_alloc(heap->card_count * sizeof(*heap->overflow));
  heap->mark_stack_capacity = (size_t)OT_GC_MARK_STACK_ENTRIES;
  if (heap->mark_stack_capacity == 0) goto fail;
  heap->mark_stack = ot_host_alloc(heap->mark_stack_capacity * sizeof(*heap->mark_stack));
  if (heap->remembered == NULL || heap->starts == NULL || heap->marks == NULL ||
      heap->cumulative == NULL || heap->overflow == NULL || heap->mark_stack == NULL)
    goto fail;

  memset(heap->remembered, 0, heap->card_count * sizeof(*heap->remembered));
  memset(heap->starts, 0xff, heap->card_count * sizeof(*heap->starts));
  memset(heap->marks, 0, heap->card_count * sizeof(*heap->marks));
  memset(heap->cumulative, 0, heap->card_count * sizeof(*heap->cumulative));
  memset(heap->overflow, 0, heap->card_count * sizeof(*heap->overflow));
  set_free_header((ot_free_obj*)heap->old_base, heap->old_capacity);
  rebuild_layout(heap);

  state->stats.reserved_bytes = nursery_reservation_bytes + old_reservation_bytes;
  state->stats.metadata_bytes =
      sizeof(*heap) +
      heap->card_count * (sizeof(*heap->remembered) + sizeof(*heap->starts) + sizeof(*heap->marks) +
                          sizeof(*heap->cumulative) + sizeof(*heap->overflow)) +
      heap->mark_stack_capacity * sizeof(*heap->mark_stack);
  return true;

fail:
  ot_gc_heap_destroy(state);
  return false;
}

void ot_gc_heap_destroy(ots* state) {
  struct ot_gc_heap* heap = state->gc;
  if (heap == NULL) return;
  ot_host_free(heap->mark_stack);
  ot_host_free(heap->overflow);
  ot_host_free(heap->cumulative);
  ot_host_free(heap->marks);
  ot_host_free(heap->starts);
  ot_host_free(heap->remembered);
  ot_host_free(heap->old_reservation);
  ot_host_free(heap->nursery_reservation);
  ot_host_free(heap);
  state->gc = NULL;
}

void ot_gc_write_barrier(ots* state, const ot_obj* owner, otv value) {
  struct ot_gc_heap* heap = state->gc;
  if (owner == NULL || !in_old(heap, owner) || !ot_is_ptr(value)) return;
  ot_obj* target = ot_as_obj(value);
  if (in_current_nursery(heap, target)) remember_object(heap, owner);
}

void* ot_alloc(ots* state, size_t size, ot_obj_type type) {
  struct ot_gc_heap* heap = state->gc;
  if (size > SIZE_MAX - 7u) return NULL;
  size = ot_gc_align_object(size);
  if (size > heap->old_max) return NULL;

  ot_obj* object;
  if (size >= heap->large_object_bytes || size > heap->nursery_bytes) {
    object = allocate_old_with_collection(state, size);
    if (object == NULL) return NULL;
  } else {
    if (state->config.gc_stress && heap->nursery_alloc != heap->nursery_from) minor_collect(state);
    if (heap->nursery_alloc + size > heap->nursery_limit) minor_collect(state);
    if (heap->nursery_alloc + size > heap->nursery_limit) return NULL;
    object = (ot_obj*)heap->nursery_alloc;
    heap->nursery_alloc += size;
  }

  memset(object, 0, size);
  object->header = ot_gc_make_header(type, size);
  state->stats.allocations++;
  state->stats.allocated_bytes += size;
  update_peak(state);
  return object;
}

void ot_collect(ots* state) {
  enter_mutator_pause(state);
  minor_collect(state);
  major_collect(state, false);
  leave_mutator_pause(state);
}

ot_gc_stats ot_get_gc_stats(const ots* state) {
  const struct ot_gc_heap* heap = state->gc;
  ot_gc_stats stats = state->stats;
  size_t nursery_used = (size_t)(heap->nursery_alloc - heap->nursery_from);
  stats.used_bytes = heap->old_used + nursery_used;
  stats.capacity_bytes = heap->old_capacity + heap->nursery_bytes;
  size_t free_bytes = heap->old_capacity - heap->old_used;
  stats.largest_free_region_bytes = largest_free_region(heap);
  stats.fragmentation_bytes = free_bytes > stats.largest_free_region_bytes
                                  ? free_bytes - stats.largest_free_region_bytes
                                  : 0;
  return stats;
}
