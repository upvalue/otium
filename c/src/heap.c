#include "internal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#ifdef OTIUM_CHECK_ROOTS
#include <stdio.h>
#include <sys/mman.h>
#endif

_Static_assert(sizeof(oti_object_header) % 8 == 0, "object alignment");

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

void oti_roots_push(oti_heap* h, oti_roots* r, ot_value* slots, size_t count) {
  *r = (oti_roots){h->roots, slots, count};
  h->roots = r;
}

void oti_roots_pop(oti_heap* h, oti_roots* r) {
  assert(h->roots == r);
  h->roots = r->previous;
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
  memcpy(copy, old, size);
  *used += size;
  ot_value forwarded = (ot_value)copy;
  old->size = 0;
  memcpy(&old->fields, &forwarded, sizeof(forwarded));
  return forwarded;
}

static void copy_live(oti_heap* h, const unsigned char* from, unsigned char* to) {
  size_t used = 0;
  for (oti_roots* r = h->roots; r; r = r->previous)
    for (size_t i = 0; i < r->count; i++) r->slots[i] = evacuate(h, r->slots[i], from, to, &used);
  for (size_t scan = 0; scan < used;) {
    oti_object_header* o = (oti_object_header*)(to + scan);
    ot_value* fields = object_fields((ot_value)o);
    for (uint32_t i = 0; i < o->fields; i++) fields[i] = evacuate(h, fields[i], from, to, &used);
    scan += o->size;
  }
  h->used = used;
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
  if (size > h->limit) return 0;
  if (size > h->capacity - h->used) oti_heap_collect(h);
  if (size > h->capacity - h->used && (size > h->limit - h->used || !grow(h, h->used + size)))
    return 0;
  oti_object_header* o = (oti_object_header*)(h->space[h->active] + h->used);
  h->used += size;
  *o = (oti_object_header){.size = size,
                           .fields = fields,
                           .kind = (kind & 255) | ((++h->next_identity & 0x7fffff) << 8)};
  ot_value* slots = object_fields((ot_value)o);
  for (uint32_t i = 0; i < fields; i++) slots[i] = OT_NIL;
  memset((unsigned char*)o + base, 0, size - base);
  return (ot_value)o;
}
