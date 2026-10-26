#include "internal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(oti_object_header) % 8 == 0, "object alignment");

bool oti_heap_init(oti_heap* h, size_t capacity) {
  *h = (oti_heap){0};
  if (capacity < sizeof(oti_object_header) || capacity > SIZE_MAX / 2) return false;
  capacity &= ~(size_t)7;
  h->space[0] = malloc(capacity);
  h->space[1] = malloc(capacity);
  if (!h->space[0] || !h->space[1]) {
    oti_heap_destroy(h);
    return false;
  }
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
  free(h->space[0]);
  free(h->space[1]);
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

static ot_value evacuate(oti_heap* h, ot_value v, unsigned char* to, size_t* used) {
  if (!value_is_ptr(v)) return v;
  assert(v >= (uintptr_t)h->space[h->active]);
  assert(v < (uintptr_t)h->space[h->active] + h->used);
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

void oti_heap_collect(oti_heap* h) {
  unsigned char* to = h->space[h->active ^ 1];
  size_t used = 0;
  for (oti_roots* r = h->roots; r; r = r->previous)
    for (size_t i = 0; i < r->count; i++) r->slots[i] = evacuate(h, r->slots[i], to, &used);
  for (size_t scan = 0; scan < used;) {
    oti_object_header* o = (oti_object_header*)(to + scan);
    ot_value* fields = object_fields((ot_value)o);
    for (uint32_t i = 0; i < o->fields; i++) fields[i] = evacuate(h, fields[i], to, &used);
    scan += o->size;
  }
  h->active ^= 1;
  h->used = used;
  h->collections++;
  h->copied_bytes += used;
}

static bool grow(oti_heap* h, size_t needed) {
  if (needed > h->limit) return false;
  size_t capacity = h->capacity;
  while (capacity < needed) capacity = capacity > h->limit / 2 ? h->limit : capacity * 2;
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
  *o = (oti_object_header){size, fields, (kind & 255) | ((++h->next_identity & 0x7fffff) << 8)};
  ot_value* slots = object_fields((ot_value)o);
  for (uint32_t i = 0; i < fields; i++) slots[i] = OT_NIL;
  memset((unsigned char*)o + base, 0, size - base);
  return (ot_value)o;
}
