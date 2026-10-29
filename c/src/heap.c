#include "internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef OTIUM_FREESTANDING
#include <sys/mman.h>
#endif

/* A mark-region heap after Immix (Blackburn and McKinley, PLDI 2008); see
 * scratch/immix-gc.md. Allocation bumps through holes, runs of free lines in
 * blocks. A collection marks live objects and the lines they cover, then
 * sweeps the line marks to find free blocks and holes for the next round.
 * Dead objects are never visited. */

_Static_assert(sizeof(oti_object_header) == 16, "object header");
_Static_assert(OTI_LARGE_MIN <= OTI_BLOCK / 2, "medium objects fit a block");

/* The most of the heap that may stay live after a collection before it grows,
 * as a percentage. Growth aims to bring it back to this. Lower means fewer
 * collections and more memory. */
#define OTI_HEAP_LIVE_PERCENT 50

typedef struct {
  uint8_t state, mark;
  uint16_t live_lines;
} oti_block_info;

/* Evacuate marks a block whose objects are copied out as they are found;
 * quarantine holds freed blocks out of use for a while in root-checking
 * builds. */
enum { BLOCK_FREE, BLOCK_USED, BLOCK_EVACUATE, BLOCK_QUARANTINE };

/* Evacuate during a full collection when the last one left blocks in use less
 * than this full, by line. */
#define OTI_DEFRAG_PERCENT 60

/* Metadata sits at the start of each chunk, in blocks of its own, so every
 * other block's payload is whole. */
struct oti_chunk {
  oti_chunk* next;
  oti_block_info blocks[OTI_BLOCKS_PER_CHUNK];
  uint8_t lines[OTI_BLOCKS_PER_CHUNK][OTI_LINES_PER_BLOCK];
};

#define META_BLOCKS ((sizeof(oti_chunk) + OTI_BLOCK - 1) / OTI_BLOCK)
#define USABLE_BLOCKS (OTI_BLOCKS_PER_CHUNK - META_BLOCKS)

/* Large objects follow this link, malloc'd one at a time. */
struct oti_large {
  oti_large *previous, *next;
};

_Static_assert(sizeof(oti_large) % 8 == 0, "large object alignment");

static inline oti_chunk* heap_chunk_of(const void* p) {
  return (oti_chunk*)((uintptr_t)p & ~(uintptr_t)(OTI_CHUNK - 1));
}

static inline size_t heap_block_index(const void* p) {
  return ((uintptr_t)p & (OTI_CHUNK - 1)) >> OTI_BLOCK_BITS;
}

static inline size_t heap_line_index(const void* p) {
  return ((uintptr_t)p & (OTI_BLOCK - 1)) >> OTI_LINE_BITS;
}

static inline oti_block_info* heap_block_info(const void* p) {
  return &heap_chunk_of(p)->blocks[heap_block_index(p)];
}

static inline uint8_t* heap_block_lines(const void* p) { return heap_chunk_of(p)->lines[heap_block_index(p)]; }

static inline unsigned char* heap_block_at(oti_chunk* c, size_t i) {
  return (unsigned char*)c + (i << OTI_BLOCK_BITS);
}

static inline oti_object_header* heap_header(ot_value v) { return (oti_object_header*)v; }

static inline ot_value heap_large_object(oti_large* l) { return (ot_value)(l + 1); }

/* --- Memory ------------------------------------------------------------------ */

static oti_chunk* heap_map_chunk(void) {
#ifndef OTIUM_FREESTANDING
  /* Map twice the size and trim to an aligned chunk. Fresh pages are zero,
   * which is what line marks and block states start as. */
  size_t span = 2 * OTI_CHUNK;
  unsigned char* p = mmap(NULL, span, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED) return NULL;
  uintptr_t start = ((uintptr_t)p + OTI_CHUNK - 1) & ~(uintptr_t)(OTI_CHUNK - 1);
  if (start > (uintptr_t)p) munmap(p, start - (uintptr_t)p);
  uintptr_t end = (uintptr_t)p + span;
  if (end > start + OTI_CHUNK) munmap((void*)(start + OTI_CHUNK), end - (start + OTI_CHUNK));
  return (oti_chunk*)start;
#else
  oti_chunk* c = aligned_alloc(OTI_CHUNK, OTI_CHUNK);
  if (c) memset(c, 0, sizeof(*c));
  return c;
#endif
}

static void heap_unmap_chunk(oti_chunk* c) {
#ifndef OTIUM_FREESTANDING
  munmap(c, OTI_CHUNK);
#else
  free(c);
#endif
}

static bool heap_reserve(oti_block_list* list, size_t count) {
  if (count <= list->capacity) return true;
  size_t capacity = list->capacity ? list->capacity : 64;
  while (capacity < count) capacity *= 2;
  unsigned char** items = realloc(list->items, capacity * sizeof(*items));
  if (!items) return false;
  list->items = items;
  list->capacity = capacity;
  return true;
}

static size_t heap_chunk_count(const oti_heap* h) {
  size_t n = 0;
  for (oti_chunk* c = h->chunks; c; c = c->next) n++;
  return n;
}

/* Map a chunk and put its blocks on the free list, lowest address last so
 * it is taken first. */
static bool heap_add_chunk(oti_heap* h) {
  size_t blocks = (heap_chunk_count(h) + 1) * USABLE_BLOCKS;
  if (!heap_reserve(&h->free_blocks, blocks) || !heap_reserve(&h->recyclable, blocks)) return false;
  oti_chunk* c = heap_map_chunk();
  if (!c) return false;
  c->next = h->chunks;
  h->chunks = c;
  for (size_t i = OTI_BLOCKS_PER_CHUNK; i-- > META_BLOCKS;)
    h->free_blocks.items[h->free_blocks.count++] = heap_block_at(c, i);
  return true;
}

/* Whether the heap may hold bytes more without passing its capacity, which
 * counts object bytes as semispaces did. Fragmentation can make the blocks
 * holding them add up to more. */
static bool heap_affordable(const oti_heap* h, size_t bytes) {
  size_t used = oti_heap_used(h);
  return used <= h->capacity && bytes <= h->capacity - used;
}

/* Blocks may add up to twice the limit, the memory two semispaces took, and a
 * few more so that a tiny heap still has a block for small objects and one for
 * overflow. */
#define OTI_SLACK_BLOCKS 4

/* Root-checking builds evacuate everything, so they may need far more. */
static size_t heap_block_cap(const oti_heap* h) {
#ifdef OTIUM_CHECK_ROOTS
  (void)h;
  return SIZE_MAX;
#else
  return 2 * h->limit / OTI_BLOCK + OTI_SLACK_BLOCKS;
#endif
}

/* Allocation leaves this many blocks under the cap for evacuation to copy
 * into, a fortieth of them and at least two. */
static size_t heap_headroom(const oti_heap* h) {
  size_t cap = heap_block_cap(h);
  return cap == SIZE_MAX ? 0 : cap / 40 > 2 ? cap / 40 : 2;
}

/* A block that has just been taken counts as full, so it isn't picked for
 * evacuation on the strength of a sweep from before it held anything. */
static unsigned char* heap_take_free_block(oti_heap* h) {
  size_t reserve = h->evacuating ? 0 : heap_headroom(h);
  if (h->blocks_used + reserve >= heap_block_cap(h)) return NULL;
  if (!h->free_blocks.count && !heap_add_chunk(h)) return NULL;
  unsigned char* block = h->free_blocks.items[--h->free_blocks.count];
  *heap_block_info(block) = (oti_block_info){BLOCK_USED, 0, OTI_LINES_PER_BLOCK};
  h->blocks_used++;
  return block;
}

/* --- Allocation -------------------------------------------------------------- */

/* Fill the next chunk past used with nil so inline allocation can skip it.
 * Inline allocation stops where the heap would pass its capacity. */
void oti_heap_fill(oti_heap* h) {
  if (!h->start) return;
  unsigned char* cursor = h->start + h->used;
  if (h->filled < cursor) h->filled = cursor;
  size_t left = (size_t)(h->hole_end - h->filled);
  unsigned char* end = h->filled + (left > OTI_FILL_CHUNK ? OTI_FILL_CHUNK : left);
  for (ot_value* slot = (ot_value*)h->filled; slot < (ot_value*)end; slot++) *slot = OT_NIL;
  h->filled = end;
  size_t used = oti_heap_used(h);
  size_t room = used < h->capacity ? (h->capacity - used) & ~(size_t)7 : 0;
  if ((size_t)(end - cursor) > room) end = cursor + room;
  oti_heap_set_ready(h, (size_t)(end - h->start));
}

static void heap_end_hole(oti_heap* h) {
  if (h->start) h->allocated_bytes += h->used;
  h->start = h->hole_start = h->hole_end = h->filled = NULL;
  h->used = 0;
  oti_heap_set_ready(h, 0);
}

static void heap_begin_hole(oti_heap* h, unsigned char* from, unsigned char* to) {
  heap_end_hole(h);
  h->start = h->hole_start = h->filled = from;
  h->hole_end = to;
  oti_heap_fill(h);
}

/* Find the next hole in the current block. A line is free when it isn't
 * marked; small objects mark only their first line, so the line after a
 * marked one may hold the end of one and is skipped. */
static bool heap_next_hole_in_block(oti_heap* h) {
  if (!h->block) return false;
  const uint8_t* lines = heap_block_lines(h->block);
  uint8_t epoch = (uint8_t)h->epoch;
  for (size_t i = h->line; i < OTI_LINES_PER_BLOCK; i++) {
    if (lines[i] == epoch || (i && lines[i - 1] == epoch)) continue;
    size_t j = i + 1;
    while (j < OTI_LINES_PER_BLOCK && lines[j] != epoch) j++;
    h->line = j;
    heap_begin_hole(h, h->block + i * OTI_LINE, h->block + j * OTI_LINE);
    return true;
  }
  h->line = OTI_LINES_PER_BLOCK;
  return false;
}

static bool heap_next_hole(oti_heap* h) {
  if (heap_next_hole_in_block(h)) return true;
  while (h->recyclable.count) {
    h->block = h->recyclable.items[--h->recyclable.count];
    h->line = 0;
    if (heap_next_hole_in_block(h)) return true;
  }
  h->block = heap_take_free_block(h);
  h->line = 0;
  return heap_next_hole_in_block(h);
}

#ifdef OTIUM_CHECK_KINDS
/* Allocation must never land on a line where a marked object starts. The
 * first line may hold the end of the previous object. */
static void heap_check_free(const oti_heap* h, const unsigned char* p, size_t size) {
  const uint8_t* lines = heap_block_lines(p);
  size_t first = heap_line_index(p), last = heap_line_index(p + size - 1);
  if (heap_block_index(p) < META_BLOCKS || heap_block_info(p)->state != BLOCK_USED ||
      heap_block_index(p + size - 1) != heap_block_index(p)) {
    fprintf(stderr, "otium: allocation outside a block in use at %p\n", (void*)p);
    abort();
  }
  for (size_t i = first + 1; i <= last; i++)
    if (lines[i] == h->epoch) {
      fprintf(stderr, "otium: allocation at %p over marked line %zu\n", (void*)p, i);
      abort();
    }
}
#else
#define heap_check_free(h, p, size) ((void)0)
#endif

static unsigned char* heap_bump(oti_heap* h, size_t size) {
  if (!h->start) return NULL;
  unsigned char* cursor = h->start + h->used;
  if (size > (size_t)(h->hole_end - cursor)) return NULL;
  h->used += size;
  heap_check_free(h, cursor, size);
  return cursor;
}

static unsigned char* heap_bump_overflow(oti_heap* h, size_t size) {
  if (!h->overflow || size > (size_t)(h->overflow_end - h->overflow)) {
    unsigned char* block = heap_take_free_block(h);
    if (!block) return NULL;
    h->overflow = block;
    h->overflow_end = block + OTI_BLOCK;
  }
  unsigned char* p = h->overflow;
  heap_check_free(h, p, size);
  h->overflow += size;
  h->allocated_bytes += size;
  return p;
}

static unsigned char* heap_alloc_large(oti_heap* h, size_t size) {
  if (!heap_affordable(h, size)) return NULL;
  oti_large* l = malloc(sizeof(oti_large) + size);
  if (!l) return NULL;
  *l = (oti_large){NULL, h->large};
  if (h->large) h->large->previous = l;
  h->large = l;
  h->large_bytes += size;
  h->allocated_bytes += size;
  return (unsigned char*)heap_large_object(l);
}

static unsigned char* heap_place(oti_heap* h, size_t size) {
  if (!heap_affordable(h, size)) return NULL;
  if (size >= OTI_LARGE_MIN) return heap_alloc_large(h, size);
  unsigned char* p = heap_bump(h, size);
  if (p) return p;
  if (size > OTI_LINE && (p = heap_bump_overflow(h, size))) return p;
  while (heap_next_hole(h))
    if ((p = heap_bump(h, size))) return p;
  return NULL;
}

/* --- Collection -------------------------------------------------------------- */

/* A block's worth of mark stack. The first is malloc'd when the heap is made
 * and kept. The rest are free blocks, borrowed until marking is done, or
 * malloc'd if there are none. */
struct oti_mark_segment {
  oti_mark_segment* previous;
  size_t count;
  bool borrowed;
};

#define SEGMENT_ITEMS ((OTI_BLOCK - sizeof(oti_mark_segment)) / sizeof(ot_value))

static inline ot_value* heap_segment_items(oti_mark_segment* s) { return (ot_value*)(s + 1); }

static oti_mark_segment* heap_new_segment(oti_heap* h) {
  oti_mark_segment* s = NULL;
  bool borrowed = h->free_blocks.count || heap_add_chunk(h);
  if (borrowed) {
    unsigned char* block = h->free_blocks.items[--h->free_blocks.count];
    heap_block_info(block)->state = BLOCK_QUARANTINE;
    s = (oti_mark_segment*)block;
  } else if (!(s = malloc(OTI_BLOCK))) {
    fputs("otium: out of memory while collecting\n", stderr);
    abort();
  }
  *s = (oti_mark_segment){h->marks, 0, borrowed};
  return s;
}

static void heap_push(oti_heap* h, ot_value v) {
  if (h->marks->count == SEGMENT_ITEMS) h->marks = heap_new_segment(h);
  heap_segment_items(h->marks)[h->marks->count++] = v;
}

/* Take the next object to trace, giving back segments as they empty. */
static bool heap_pop(oti_heap* h, ot_value* v) {
  while (!h->marks->count) {
    oti_mark_segment* s = h->marks;
    if (!s->previous) return false;
    h->marks = s->previous;
    if (s->borrowed) {
      heap_block_info(s)->state = BLOCK_FREE;
      h->free_blocks.items[h->free_blocks.count++] = (unsigned char*)s;
    } else free(s);
  }
  *v = heap_segment_items(h->marks)[--h->marks->count];
  return true;
}

/* Mark the object v, the lines it covers and its block, and queue it to have
 * its fields traced. Small objects mark only their first line. */
static void heap_mark(oti_heap* h, ot_value v) {
  oti_object_header* o = heap_header(v);
  uint8_t epoch = (uint8_t)h->epoch;
  o->gc = (o->gc & ~(OTI_GC_EPOCH | OTI_GC_RECLAIM)) | epoch |
          (h->generational ? OTI_GC_UNLOGGED : 0);
  size_t size = o->size;
  h->marked_bytes += size;
  if (size < OTI_LARGE_MIN) {
    oti_chunk* c = heap_chunk_of(o);
    size_t b = heap_block_index(o);
    c->blocks[b].mark = epoch;
    uint8_t* lines = c->lines[b];
    size_t first = heap_line_index(o);
    if (size <= OTI_LINE) lines[first] = epoch;
    else
      for (size_t i = first, last = heap_line_index((unsigned char*)o + size - 1); i <= last; i++)
        lines[i] = epoch;
  }
  if (o->fields) heap_push(h, v);
}

static inline ot_value heap_forwarded(ot_value v) {
  ot_value forwarded;
  memcpy(&forwarded, &heap_header(v)->fields, sizeof(forwarded));
  return forwarded;
}

/* Bump space for evacuated objects, in free blocks. Running out ends
 * evacuation for this collection; what is left is marked in place. */
static unsigned char* heap_evacuation_space(oti_heap* h, size_t size) {
  if (!h->evacuation || size > (size_t)(h->evacuation_end - h->evacuation)) {
    unsigned char* block = heap_take_free_block(h);
    if (!block) {
      h->evacuating = false;
      return NULL;
    }
    h->evacuation = block;
    h->evacuation_end = block + OTI_BLOCK;
  }
  unsigned char* p = h->evacuation;
  h->evacuation += size;
  return p;
}

/* Copy v out of its block and leave its new address behind. */
static ot_value heap_evacuate(oti_heap* h, ot_value v) {
  oti_object_header* o = heap_header(v);
  size_t size = o->size;
  unsigned char* p = heap_evacuation_space(h, size);
  if (!p) return 0;
  /* Most objects are a few words, where calling memcpy costs more than the
   * copy. Fixed 8-byte copies keep clang from turning this back into a call. */
  if (size <= 64)
    for (size_t i = 0; i < size; i += 8) __builtin_memcpy(p + i, (unsigned char*)o + i, 8);
  else memcpy(p, o, size);
  ot_value copy = (ot_value)p;
  o->gc |= OTI_GC_FORWARDED;
  memcpy(&o->fields, &copy, sizeof(copy));
  h->copied_bytes += size;
  return copy;
}

static inline void heap_trace(oti_heap* h, ot_value* slot) {
  ot_value v = *slot;
  if (!value_is_ptr(v)) return;
  uint32_t gc = heap_header(v)->gc;
  if (gc & OTI_GC_FORWARDED) {
    *slot = heap_forwarded(v);
    return;
  }
  if ((gc & OTI_GC_EPOCH) == h->epoch) return;
  if (h->evacuating && !(gc & OTI_GC_PINNED) && heap_header(v)->size < OTI_LARGE_MIN &&
      heap_block_info((void*)v)->state == BLOCK_EVACUATE) {
    ot_value copy = heap_evacuate(h, v);
    if (copy) *slot = v = copy;
  }
  heap_mark(h, v);
}

static void heap_trace_root(void* context, ot_value* slot) { heap_trace(context, slot); }

static void heap_drain(oti_heap* h) {
  ot_value v;
  while (heap_pop(h, &v)) {
    ot_value* fields = object_fields(v);
    for (uint32_t i = 0, n = heap_header(v)->fields; i < n; i++) heap_trace(h, &fields[i]);
  }
}

/* Poison memory that is free after a sweep, so a pointer that missed a
 * collection reads garbage. */
static void heap_poison(unsigned char* p, size_t bytes) {
#ifdef OTIUM_CHECK_ROOTS
  memset(p, 0xfb, bytes);
#else
  (void)p;
  (void)bytes;
#endif
}

#ifdef OTIUM_CHECK_ROOTS
/* Root-checking builds hold freed blocks inaccessible for a few collections,
 * so a pointer that missed a collection faults instead of finding the block
 * reused. */
static void heap_quarantine(oti_heap* h, unsigned char* block) {
  oti_block_list* list = &h->quarantine[h->quarantine_next];
  if (!heap_reserve(list, list->count + 1)) abort();
  list->items[list->count++] = block;
  heap_block_info(block)->state = BLOCK_QUARANTINE;
  mprotect(block, OTI_BLOCK, PROT_NONE);
}

static void heap_release_quarantine(oti_heap* h) {
  h->quarantine_next = (h->quarantine_next + 1) % OTI_QUARANTINE;
  oti_block_list* list = &h->quarantine[h->quarantine_next];
  for (size_t i = 0; i < list->count; i++) {
    mprotect(list->items[i], OTI_BLOCK, PROT_READ | PROT_WRITE);
    heap_block_info(list->items[i])->state = BLOCK_FREE;
  }
  list->count = 0;
}
#endif

static bool heap_current(const oti_heap* h, const unsigned char* block) {
  return block == h->block || (h->overflow_end && block == h->overflow_end - OTI_BLOCK);
}

/* Pick the sparsest blocks, by the last sweep's line counts, for as long as
 * their live lines fit the free blocks there is room for. With every, any
 * block in use may be picked, sparsest first. */
static void heap_select_evacuation(oti_heap* h, bool every) {
  size_t cap = heap_block_cap(h);
  size_t room = cap > h->blocks_used ? cap - h->blocks_used : 0;
  size_t counts[OTI_LINES_PER_BLOCK + 1] = {0};
  size_t sparse = every ? OTI_LINES_PER_BLOCK + 1 : OTI_LINES_PER_BLOCK * OTI_DEFRAG_PERCENT / 100;
  for (oti_chunk* c = h->chunks; c; c = c->next)
    for (size_t b = META_BLOCKS; b < OTI_BLOCKS_PER_CHUNK; b++) {
      oti_block_info* info = &c->blocks[b];
      if (info->state == BLOCK_USED && !heap_current(h, heap_block_at(c, b)) &&
          info->live_lines < sparse)
        counts[info->live_lines]++;
    }
  size_t budget = cap == SIZE_MAX ? SIZE_MAX : room * OTI_LINES_PER_BLOCK * 9 / 10, needed = 0,
         threshold = 0;
  for (size_t l = 0; l < sparse && l <= OTI_LINES_PER_BLOCK; l++) {
    if (counts[l] && counts[l] * l > budget - needed) break;
    needed += counts[l] * l;
    threshold = l + 1;
  }
  size_t picked = 0;
  for (oti_chunk* c = h->chunks; c; c = c->next)
    for (size_t b = META_BLOCKS; b < OTI_BLOCKS_PER_CHUNK; b++) {
      oti_block_info* info = &c->blocks[b];
      if (info->state == BLOCK_USED && !heap_current(h, heap_block_at(c, b)) &&
          info->live_lines < threshold) {
        info->state = BLOCK_EVACUATE;
        picked++;
      }
    }
  h->evacuating = picked > 0;
}

/* Classify every block from its line marks: free, recyclable (with holes) or
 * full. Unmarked lines are cleared, so lines and blocks only ever hold the
 * current epoch or zero, and an old mark can't come round again. */
static void heap_sweep(oti_heap* h) {
  uint8_t epoch = (uint8_t)h->epoch;
  size_t live_lines = 0;
  h->free_blocks.count = h->recyclable.count = 0;
  h->blocks_used = 0;
#ifdef OTIUM_CHECK_ROOTS
  heap_release_quarantine(h);
#endif
  for (oti_chunk* c = h->chunks; c; c = c->next) {
    for (size_t b = OTI_BLOCKS_PER_CHUNK; b-- > META_BLOCKS;) {
      oti_block_info* info = &c->blocks[b];
      unsigned char* block = heap_block_at(c, b);
      uint8_t* lines = c->lines[b];
      if (info->state == BLOCK_FREE) {
        h->free_blocks.items[h->free_blocks.count++] = block;
        continue;
      }
      if (info->state == BLOCK_QUARANTINE) continue;
      /* The allocator keeps going in the blocks it was using, so they stay
       * off the lists and their free lines keep their nil. */
      bool current = heap_current(h, block);
      if (info->mark != epoch && !current) {
        memset(lines, 0, OTI_LINES_PER_BLOCK);
        *info = (oti_block_info){BLOCK_FREE, 0, 0};
        heap_poison(block, OTI_BLOCK);
#ifdef OTIUM_CHECK_ROOTS
        heap_quarantine(h, block);
#else
        h->free_blocks.items[h->free_blocks.count++] = block;
#endif
        continue;
      }
      info->state = BLOCK_USED;
      size_t live = 0, usable = 0;
      bool previous = false;
      for (size_t i = 0; i < OTI_LINES_PER_BLOCK; i++) {
        bool marked = lines[i] == epoch;
        if (marked) live++;
        else {
          lines[i] = 0;
          if (!previous && !current) {
            usable++;
            heap_poison(block + i * OTI_LINE, OTI_LINE);
          }
        }
        previous = marked;
      }
      info->live_lines = (uint16_t)live;
      live_lines += live;
      h->blocks_used++;
      if (usable) h->recyclable.items[h->recyclable.count++] = block;
    }
  }
  for (oti_large *l = h->large, *next; l; l = next) {
    next = l->next;
    oti_object_header* o = heap_header(heap_large_object(l));
    if ((o->gc & OTI_GC_EPOCH) == epoch) continue;
    if (l->previous) l->previous->next = next;
    else h->large = next;
    if (next) next->previous = l->previous;
    h->large_bytes -= o->size;
    free(l);
  }
  h->live_lines = live_lines;
  h->defragment = h->blocks_used >= 8 && live_lines * OTI_LINE * 100 <
                                             h->blocks_used * OTI_BLOCK * OTI_DEFRAG_PERCENT;
}

size_t oti_heap_used(const oti_heap* h) {
  return h->live_bytes + (h->allocated_bytes - h->allocated_at_collection) +
         (h->start ? h->used : 0);
}

void oti_heap_remember(oti_heap* h, ot_value object) {
  ((oti_object_header*)object)->gc &= ~OTI_GC_UNLOGGED;
  if (h->remembered_count == h->remembered_capacity) {
    size_t capacity = h->remembered_capacity ? h->remembered_capacity * 2 : 256;
    ot_value* remembered = realloc(h->remembered, capacity * sizeof(*remembered));
    /* Without room to remember it, the next collection must be full. */
    if (!remembered) {
      h->remembered_overflow = true;
      return;
    }
    h->remembered = remembered;
    h->remembered_capacity = capacity;
  }
  h->remembered[h->remembered_count++] = object;
}

/* Trace the fields of every remembered object, and make it unlogged again. */
static void heap_trace_remembered(oti_heap* h) {
  for (size_t i = 0; i < h->remembered_count; i++) {
    ot_value v = h->remembered[i];
    heap_header(v)->gc |= OTI_GC_UNLOGGED;
    ot_value* fields = object_fields(v);
    for (uint32_t j = 0, n = heap_header(v)->fields; j < n; j++) heap_trace(h, &fields[j]);
    heap_drain(h);
  }
  h->remembered_count = 0;
}

/* --- Verification --------------------------------------------------------- */

/* With OTIUM_GC_VERIFY set, every minor collection is checked against a
 * full trace: anything reachable that it left unmarked would be freed while
 * live, and points to a store that missed the write barrier. */
typedef struct {
  oti_heap* heap;
  uintptr_t* seen;
  size_t capacity, count;
  ot_value* work;
  size_t work_count, work_capacity;
  ot_value parent;
  size_t field;
} oti_verify;

static bool heap_verify_visit(oti_verify* v, ot_value value) {
  if (!value_is_ptr(value)) return true;
  size_t i = (value >> 3) * 0x9e3779b97f4a7c15u % v->capacity;
  while (v->seen[i]) {
    if (v->seen[i] == value) return true;
    i = (i + 1) % v->capacity;
  }
  if (v->count * 2 >= v->capacity) {
    fputs("otium: verify table full\n", stderr);
    abort();
  }
  v->seen[i] = value;
  v->count++;
  if ((heap_header(value)->gc & OTI_GC_EPOCH) != v->heap->epoch) {
    fprintf(stderr,
            "otium: minor collection missed a live object of kind %u, field %zu of a kind %u "
            "object at %p\n",
            object_kind(value), v->field, v->parent ? object_kind(v->parent) : 0,
            (void*)v->parent);
    abort();
  }
  if (v->work_count == v->work_capacity) {
    v->work_capacity = v->work_capacity ? v->work_capacity * 2 : 1024;
    v->work = realloc(v->work, v->work_capacity * sizeof(*v->work));
    if (!v->work) abort();
  }
  v->work[v->work_count++] = value;
  return true;
}

static void heap_verify_root(void* context, ot_value* slot) {
  oti_verify* v = context;
  v->parent = 0;
  v->field = 0;
  heap_verify_visit(v, *slot);
}

static void heap_verify(oti_heap* h) {
  oti_verify v = {.heap = h, .capacity = 1 << 16};
  size_t objects = h->blocks_used * OTI_BLOCK / 16 + h->large_bytes / 16;
  while (v.capacity < objects * 2 + 1024) v.capacity *= 2;
  v.seen = calloc(v.capacity, sizeof(*v.seen));
  if (!v.seen) abort();
  for (oti_roots* r = h->roots; r; r = r->previous)
    for (size_t i = 0; i < r->count; i++) heap_verify_root(&v, &r->slots[i]);
  if (h->hook) h->hook(h->hook_data, heap_verify_root, &v);
  while (v.work_count) {
    ot_value object = v.work[--v.work_count];
    ot_value* fields = object_fields(object);
    for (uint32_t i = 0, n = heap_header(object)->fields; i < n; i++) {
      v.parent = object;
      v.field = i;
      heap_verify_visit(&v, fields[i]);
    }
  }
  free(v.seen);
  free(v.work);
}

/* --- Policy ---------------------------------------------------------------- */

enum { COLLECT_MINOR, COLLECT_MAJOR, COLLECT_DEFRAGMENT };

/* The allocator's hole and overflow block survive a collection: nothing past
 * the cursor was allocated, so it is still free. A minor collection leaves
 * the epoch, so earlier survivors stay marked, and never moves anything. A
 * major one may evacuate the sparsest blocks if the last collection called
 * for it; a defragmenting one considers every block. Root-checking builds
 * make every collection major and move everything. */
static void heap_collect(oti_heap* h, int kind) {
  unsigned char *cursor = h->start ? h->start + h->used : NULL, *hole_end = h->hole_end;
  heap_end_hole(h);
#ifdef OTIUM_CHECK_ROOTS
  kind = COLLECT_MAJOR;
  cursor = NULL;
  h->block = NULL;
  h->overflow = h->overflow_end = NULL;
  heap_select_evacuation(h, true);
#else
  if (kind == COLLECT_DEFRAGMENT || (kind == COLLECT_MAJOR && h->defragment))
    heap_select_evacuation(h, kind == COLLECT_DEFRAGMENT);
#endif
  size_t used = oti_heap_used(h);
  if (used > h->peak_used) h->peak_used = used;
  bool minor = kind == COLLECT_MINOR;
  if (!minor) {
    h->epoch = h->epoch % 3 + 1;
    h->remembered_count = 0;
    h->remembered_overflow = false;
  }
  h->marked_bytes = 0;
  for (oti_roots* r = h->roots; r; r = r->previous)
    for (size_t i = 0; i < r->count; i++) heap_trace(h, &r->slots[i]);
  if (h->hook) h->hook(h->hook_data, heap_trace_root, h);
  heap_drain(h);
  if (minor) heap_trace_remembered(h);
  h->evacuating = false;
  h->evacuation = h->evacuation_end = NULL;
  if (minor && h->verify) heap_verify(h);
  if (h->weak) h->weak(h->hook_data);
  heap_sweep(h);
  /* A minor collection only sees what it promotes; what was already old is
   * counted as live until a major one says otherwise. */
  h->live_bytes = minor ? h->live_bytes + h->marked_bytes : h->marked_bytes;
  if (!minor) {
    size_t room = h->capacity > h->live_bytes ? h->capacity - h->live_bytes : 0;
    h->major_live = h->live_bytes + room / 2;
  } else h->minor_collections++;
  if (used > h->live_bytes) h->reclaimed_bytes += used - h->live_bytes;
  h->allocated_at_collection = h->allocated_bytes;
  h->collections++;
  if (cursor) heap_begin_hole(h, cursor, hole_end);
}

/* Minor collections until promoted objects take up half the room the last
 * major one left. */
static int heap_next_collection(const oti_heap* h) {
  return h->generational && !h->remembered_overflow && h->live_bytes < h->major_live
             ? COLLECT_MINOR
             : COLLECT_MAJOR;
}

void oti_heap_collect(oti_heap* h) { heap_collect(h, heap_next_collection(h)); }

void oti_heap_collect_full(oti_heap* h) { heap_collect(h, COLLECT_MAJOR); }

/* Switching needs a full collection first: it sets or clears every survivor's
 * unlogged bit, and a minor one needs the remembered set from the start. */
void oti_heap_set_generational(oti_heap* h, bool generational) {
  if (h->generational == generational) return;
  h->generational = generational;
  h->major_live = 0;
  heap_collect(h, COLLECT_MAJOR);
}

ot_value oti_heap_survivor(oti_heap* h, ot_value v) {
  oti_object_header* o = heap_header(v);
  if (o->gc & OTI_GC_FORWARDED) return heap_forwarded(v);
  return (o->gc & OTI_GC_EPOCH) == h->epoch ? v : 0;
}

bool oti_heap_contains(const oti_heap* h, ot_value v) {
  for (const oti_chunk* c = h->chunks; c; c = c->next)
    if (heap_chunk_of((void*)v) == c && heap_block_index((void*)v) >= META_BLOCKS) return true;
  for (oti_large* l = h->large; l; l = l->next) {
    ot_value o = heap_large_object(l);
    if (v >= o && v - o < heap_header(o)->size) return true;
  }
  return false;
}

/* --- Interface --------------------------------------------------------------- */

static size_t heap_round_capacity(size_t bytes) { return bytes & ~(size_t)7; }

bool oti_heap_init(oti_heap* h, size_t capacity) {
  *h = (oti_heap){.epoch = 1};
  if (capacity > SIZE_MAX / 2 || !(h->marks = malloc(OTI_BLOCK))) return false;
  *h->marks = (oti_mark_segment){NULL, 0, false};
  h->capacity = h->limit = heap_round_capacity(capacity);
  return true;
}

bool oti_heap_set_limit(oti_heap* h, size_t limit) {
  if (limit > SIZE_MAX / 2) return false;
  limit = heap_round_capacity(limit);
  if (limit < h->capacity) return false;
  h->limit = limit;
  return true;
}

void oti_heap_destroy(oti_heap* h) {
  for (oti_chunk *c = h->chunks, *next; c; c = next) {
    next = c->next;
    heap_unmap_chunk(c);
  }
  for (oti_large *l = h->large, *next; l; l = next) {
    next = l->next;
    free(l);
  }
  free(h->free_blocks.items);
  free(h->recyclable.items);
#ifdef OTIUM_CHECK_ROOTS
  for (size_t i = 0; i < OTI_QUARANTINE; i++) free(h->quarantine[i].items);
#endif
  free(h->marks);
  free(h->remembered);
  *h = (oti_heap){0};
}

static void heap_grow(oti_heap* h, size_t capacity) {
  capacity = heap_round_capacity(capacity);
  if (capacity > h->limit) capacity = h->limit;
  if (capacity > h->capacity) h->capacity = capacity;
}

/* Collect, and grow if that leaves the heap too full, until size bytes can be
 * placed. A minor collection that frees enough is the whole of it; growth
 * only follows full collections, which are the ones that see what's live. */
static unsigned char* heap_place_after_collecting(oti_heap* h, size_t size) {
  int collection = heap_next_collection(h);
  heap_collect(h, collection);
  unsigned char* p;
  if (collection == COLLECT_MINOR) {
    if ((p = heap_place(h, size))) return p;
    heap_collect(h, COLLECT_MAJOR);
  }
  /* As more of the heap stays live, each collection frees less and they
   * come closer together, so grow back to OTI_HEAP_LIVE_PERCENT. */
  size_t needed = h->live_bytes + size;
  if (needed > h->capacity / 100 * OTI_HEAP_LIVE_PERCENT && h->capacity < h->limit) {
    heap_grow(h, needed > h->limit / 100 * OTI_HEAP_LIVE_PERCENT
                ? h->limit
                : needed / OTI_HEAP_LIVE_PERCENT * 100);
    size_t room = h->capacity - h->live_bytes;
    h->major_live = h->live_bytes + room / 2;
  }
  if ((p = heap_place(h, size))) return p;
  /* Holes may be too small for the object, and the budget too small for a
   * fresh block, while the limit still has room. */
  if (h->capacity < h->limit) {
    heap_grow(h, h->live_bytes + size);
    if ((p = heap_place(h, size))) return p;
  }
  /* What is left may be scattered over too many blocks. */
  heap_collect(h, COLLECT_DEFRAGMENT);
  return heap_place(h, size);
}

ot_value oti_heap_alloc(oti_heap* h, uint32_t kind, uint32_t fields, size_t bytes) {
  if (fields && (SIZE_MAX - sizeof(oti_object_header)) / fields < sizeof(ot_value)) return 0;
  size_t base = sizeof(oti_object_header) + (size_t)fields * sizeof(ot_value);
  if (bytes > SIZE_MAX - base - 7) return 0;
  size_t size = (base + bytes + 7) & ~(size_t)7;
  if (size > h->limit || size > UINT32_MAX) return 0;
  unsigned char* p = heap_place(h, size);
  if (!p && !(p = heap_place_after_collecting(h, size))) return 0;
  oti_object_header* o = (oti_object_header*)p;
  *o = (oti_object_header){.size = (uint32_t)size, .fields = fields, .kind = kind & 255};
  ot_value* slots = object_fields((ot_value)o);
  for (uint32_t i = 0; i < fields; i++) slots[i] = OT_NIL;
  memset((unsigned char*)o + base, 0, size - base);
  oti_heap_fill(h);
  return (ot_value)o;
}
