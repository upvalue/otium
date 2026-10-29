#include "internal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#ifdef OTIUM_CHECK_ROOTS
#include <stdio.h>
#include <sys/mman.h>
#endif

_Static_assert(sizeof(oti_object_header) % 8 == 0, "object alignment");

/* The most of the space that may stay live after a collection before the heap
 * grows, as a percentage. Growth aims to bring it back to this. Lower means
 * fewer collections and more memory. */
#define OTI_HEAP_LIVE_PERCENT 50

#ifdef OTIUM_CHECK_ROOTS
/* Root-checking builds copy every collection into fresh memory and make the old
 * space inaccessible, so a pointer that missed a collection faults when used.
 * Retired spaces stay mapped for a while so new spaces can't reuse their
 * addresses. Only space[active] is used. */
static unsigned char* fresh_space(size_t bytes) {
  void* p = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED) {
    perror("otium: root check space");
    abort();
  }
  return p;
}

static void retire_space(oti_heap* h, unsigned char* space, size_t bytes) {
  oti_retired_space* slot = &h->retired[h->retired_next++ % OTI_RETIRED_SPACES];
  if (slot->space) munmap(slot->space, slot->bytes);
  mprotect(space, bytes, PROT_NONE);
  *slot = (oti_retired_space){space, bytes};
}
#endif

bool oti_heap_init(oti_heap* h, size_t capacity) {
  *h = (oti_heap){0};
  if (capacity < sizeof(oti_object_header) || capacity > SIZE_MAX / 2) return false;
  capacity &= ~(size_t)7;
#ifdef OTIUM_CHECK_ROOTS
  h->space[0] = fresh_space(capacity);
#else
  h->space[0] = malloc(capacity);
  h->space[1] = malloc(capacity);
  if (!h->space[0] || !h->space[1]) {
    oti_heap_destroy(h);
    return false;
  }
#endif
  h->capacity = h->limit = capacity;
  h->start = h->space[0];
  h->end = h->start;
  return true;
}

bool oti_heap_set_limit(oti_heap* h, size_t limit) {
  if (limit > SIZE_MAX / 2) return false;
  limit &= ~(size_t)7;
  if (limit < h->capacity) return false;
  h->limit = limit;
  return true;
}

void oti_heap_destroy(oti_heap* h) {
#ifdef OTIUM_CHECK_ROOTS
  if (h->space[h->active]) munmap(h->space[h->active], h->capacity);
  for (size_t i = 0; i < OTI_RETIRED_SPACES; i++)
    if (h->retired[i].space) munmap(h->retired[i].space, h->retired[i].bytes);
#else
  free(h->space[0]);
  free(h->space[1]);
#endif
  *h = (oti_heap){0};
}

static ot_value evacuate(oti_heap* h, ot_value v, const unsigned char* from, unsigned char* to,
                         size_t* used) {
  if (!value_is_ptr(v)) return v;
  assert(v >= (uintptr_t)from);
  assert(v < (uintptr_t)from + h->used);
  oti_object_header* old = (oti_object_header*)v;
  /* A zero size marks forwarding; the remaining header holds the address. */
  if (!old->size) {
    ot_value forwarded;
    memcpy(&forwarded, &old->fields, sizeof(forwarded));
    return forwarded;
  }
  size_t size = old->size;
  assert(size <= h->capacity - *used);
  oti_object_header* copy = (oti_object_header*)(to + *used);
  /* Most objects are a few words, where calling memcpy costs more than the
   * copy. Fixed 8-byte copies keep clang from turning this back into a call. */
  if (size <= 64) {
    for (size_t i = 0; i < size; i += 8) __builtin_memcpy((char*)copy + i, (char*)old + i, 8);
  } else {
    memcpy(copy, old, size);
  }
  *used += size;
  copy->gc &= ~OTI_GC_RECLAIM;
  ot_value forwarded = (ot_value)copy;
  old->size = 0;
  memcpy(&old->fields, &forwarded, sizeof(forwarded));
  return forwarded;
}

typedef struct {
  oti_heap* heap;
  const unsigned char* from;
  unsigned char* to;
  size_t used;
} oti_copy;

static void evacuate_root(void* context, ot_value* slot) {
  oti_copy* c = context;
  *slot = evacuate(c->heap, *slot, c->from, c->to, &c->used);
}

static void copy_live(oti_heap* h, const unsigned char* from, unsigned char* to) {
  size_t used = 0;
  for (oti_roots* r = h->roots; r; r = r->previous)
    for (size_t i = 0; i < r->count; i++) r->slots[i] = evacuate(h, r->slots[i], from, to, &used);
  if (h->hook) {
    oti_copy c = {h, from, to, used};
    h->hook(h->hook_data, evacuate_root, &c);
    used = c.used;
  }
  for (size_t scan = 0; scan < used;) {
    oti_object_header* o = (oti_object_header*)(to + scan);
    ot_value* fields = object_fields((ot_value)o);
    for (uint32_t i = 0; i < o->fields; i++) fields[i] = evacuate(h, fields[i], from, to, &used);
    scan += o->size;
  }
  if (h->weak) h->weak(h->hook_data);
  h->allocated_bytes += h->used - h->live_bytes;
  h->reclaimed_bytes += h->used - used;
  if (h->used > h->peak_used) h->peak_used = h->used;
  h->live_bytes = used;
  h->used = h->filled = used;
  h->start = to;
  oti_heap_set_ready(h, used);
  h->collections++;
  h->copied_bytes += used;
}

#ifdef OTIUM_CHECK_ROOTS
/* Copy into a fresh space of the current capacity and retire the old one. */
static void relocate(oti_heap* h, size_t old_capacity) {
  unsigned char* from = h->space[h->active];
  h->space[h->active] = fresh_space(h->capacity);
  copy_live(h, from, h->space[h->active]);
  retire_space(h, from, old_capacity);
}
#endif

void oti_heap_collect(oti_heap* h) {
#ifdef OTIUM_CHECK_ROOTS
  relocate(h, h->capacity);
#else
  copy_live(h, h->space[h->active], h->space[h->active ^ 1]);
  h->active ^= 1;
#endif
}

void oti_heap_collect_full(oti_heap* h) { oti_heap_collect(h); }

ot_value oti_heap_survivor(oti_heap* h, ot_value v) {
  (void)h;
  oti_object_header* o = (oti_object_header*)v;
  if (o->size) return 0;
  ot_value forwarded;
  memcpy(&forwarded, &o->fields, sizeof(forwarded));
  return forwarded;
}

bool oti_heap_contains(const oti_heap* h, ot_value v) {
  uintptr_t start = (uintptr_t)h->start;
  return v >= start && v - start < h->used;
}

static bool grow(oti_heap* h, size_t needed) {
  if (needed > h->limit) return false;
  size_t capacity = h->capacity;
  while (capacity < needed) capacity = capacity > h->limit / 2 ? h->limit : capacity * 2;
#ifdef OTIUM_CHECK_ROOTS
  size_t old_capacity = h->capacity;
  h->capacity = capacity;
  relocate(h, old_capacity);
  return true;
#endif
  unsigned char *to = malloc(capacity), *spare = malloc(capacity);
  if (!to || !spare) {
    free(to);
    free(spare);
    return false;
  }
  unsigned char *old_from = h->space[h->active], *old_spare = h->space[h->active ^ 1];
  h->space[h->active ^ 1] = to;
  h->capacity = capacity;
  oti_heap_collect(h);
  h->space[h->active ^ 1] = spare;
  free(old_from);
  free(old_spare);
  return true;
}

ot_value oti_heap_alloc(oti_heap* h, uint32_t kind, uint32_t fields, size_t bytes) {
  if (fields && (SIZE_MAX - sizeof(oti_object_header)) / fields < sizeof(ot_value)) return 0;
  size_t base = sizeof(oti_object_header) + (size_t)fields * sizeof(ot_value);
  if (bytes > SIZE_MAX - base - 7) return 0;
  size_t size = (base + bytes + 7) & ~(size_t)7;
  if (size > h->limit || size > UINT32_MAX) return 0;
  if (size > h->capacity - h->used) {
    oti_heap_collect(h);
    /* As more of the space stays live, each collection frees less and they
     * come closer together, so grow back to OTI_HEAP_LIVE_PERCENT. A failed
     * grow is fine while the allocation still fits. */
    size_t needed = h->used + size;
    if (needed > h->capacity / 100 * OTI_HEAP_LIVE_PERCENT && h->capacity < h->limit)
      grow(h, needed > h->limit / 100 * OTI_HEAP_LIVE_PERCENT
                  ? h->limit
                  : needed / OTI_HEAP_LIVE_PERCENT * 100);
  }
  if (size > h->capacity - h->used) return 0;
  oti_object_header* o = (oti_object_header*)(h->start + h->used);
  h->used += size;
  *o = (oti_object_header){.size = size, .fields = fields, .kind = kind & 255};
  ot_value* slots = object_fields((ot_value)o);
  for (uint32_t i = 0; i < fields; i++) slots[i] = OT_NIL;
  memset((unsigned char*)o + base, 0, size - base);
  oti_heap_fill(h);
  return (ot_value)o;
}

/* Fill the next chunk past used with nil so inline allocation can skip it. */
void oti_heap_fill(oti_heap* h) {
  if (h->filled < h->used) h->filled = h->used;
  size_t end = h->capacity - h->filled > OTI_FILL_CHUNK ? h->filled + OTI_FILL_CHUNK : h->capacity;
  ot_value* slot = (ot_value*)(h->start + h->filled);
  for (size_t i = 0, n = (end - h->filled) / sizeof(ot_value); i < n; i++) slot[i] = OT_NIL;
  h->filled = end;
  oti_heap_set_ready(h, end);
}
