#include "heap.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef HeapObject Object;

_Static_assert(sizeof(Object) % 8 == 0, "object alignment");

bool heap_init(Heap* h, size_t capacity) {
  *h = (Heap){0};
  if (capacity < sizeof(Object) || capacity > SIZE_MAX / 2) return false;
  capacity &= ~(size_t)7;
  h->space[0] = malloc(capacity);
  h->space[1] = malloc(capacity);
  if (!h->space[0] || !h->space[1]) {
    heap_destroy(h);
    return false;
  }
  h->capacity = h->limit = capacity;
  return true;
}

bool heap_set_limit(Heap* h, size_t limit) {
  if (limit > SIZE_MAX / 2) return false;
  limit &= ~(size_t)7;
  if (limit < h->capacity) return false;
  h->limit = limit;
  return true;
}

void heap_destroy(Heap* h) {
  free(h->space[0]);
  free(h->space[1]);
  *h = (Heap){0};
}

void roots_push(Heap* h, Roots* r, Value* slots, size_t count) {
  *r = (Roots){h->roots, slots, count};
  h->roots = r;
}

void roots_pop(Heap* h, Roots* r) {
  assert(h->roots == r);
  h->roots = r->previous;
}

static Value evacuate(Heap* h, Value v, unsigned char* to, size_t* used) {
  if (!value_is_ptr(v)) return v;
  assert(v >= (uintptr_t)h->space[h->active]);
  assert(v < (uintptr_t)h->space[h->active] + h->used);
  Object* old = (Object*)v;
  /* A zero size marks forwarding; the remaining header holds the address. */
  if (!old->size) {
    Value forwarded;
    memcpy(&forwarded, &old->fields, sizeof(forwarded));
    return forwarded;
  }
  size_t size = old->size;
  assert(size <= h->capacity - *used);
  Object* copy = (Object*)(to + *used);
  memcpy(copy, old, size);
  *used += size;
  Value forwarded = (Value)copy;
  old->size = 0;
  memcpy(&old->fields, &forwarded, sizeof(forwarded));
  return forwarded;
}

void heap_collect(Heap* h) {
  unsigned char* to = h->space[h->active ^ 1];
  size_t used = 0;
  for (Roots* r = h->roots; r; r = r->previous)
    for (size_t i = 0; i < r->count; i++) r->slots[i] = evacuate(h, r->slots[i], to, &used);
  for (size_t scan = 0; scan < used;) {
    Object* o = (Object*)(to + scan);
    Value* fields = object_fields((Value)o);
    for (uint32_t i = 0; i < o->fields; i++) fields[i] = evacuate(h, fields[i], to, &used);
    scan += o->size;
  }
  h->active ^= 1;
  h->used = used;
  h->collections++;
  h->copied_bytes += used;
}

static bool grow(Heap* h, size_t needed) {
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
  heap_collect(h);
  h->space[h->active ^ 1] = spare;
  free(old_from);
  free(old_spare);
  return true;
}

Value heap_alloc(Heap* h, uint32_t kind, uint32_t fields, size_t bytes) {
  if (fields && (SIZE_MAX - sizeof(Object)) / fields < sizeof(Value)) return 0;
  size_t base = sizeof(Object) + (size_t)fields * sizeof(Value);
  if (bytes > SIZE_MAX - base - 7) return 0;
  size_t size = (base + bytes + 7) & ~(size_t)7;
  if (size > h->limit) return 0;
  if (size > h->capacity - h->used) heap_collect(h);
  if (size > h->capacity - h->used && (size > h->limit - h->used || !grow(h, h->used + size)))
    return 0;
  Object* o = (Object*)(h->space[h->active] + h->used);
  h->used += size;
  *o = (Object){size, fields, (kind & 255) | ((++h->next_identity & 0x7fffff) << 8)};
  Value* slots = object_fields((Value)o);
  for (uint32_t i = 0; i < fields; i++) slots[i] = V_NIL;
  memset((unsigned char*)o + base, 0, size - base);
  return (Value)o;
}
