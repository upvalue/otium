#include "heap.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  Heap h;
  assert(heap_init(&h, 4096));
  assert(value_to_int(value_int(INTPTR_MIN / 2)) == INTPTR_MIN / 2);
  assert(value_to_int(value_int(INTPTR_MAX / 2)) == INTPTR_MAX / 2);
  assert(value_to_int(value_int(-1)) == -1);
  Value live[] = {V_NIL, V_NIL, V_NIL};
  Roots roots;
  roots_push(&h, &roots, live, 3);
  live[0] = heap_alloc(&h, 1, 2, 0);
  uint32_t identity = object_identity(live[0]);
  live[1] = heap_alloc(&h, 2, 0, 16);
  memcpy(object_bytes(live[1]), "opaque payload", 15);
  object_fields(live[0])[0] = live[0];
  object_fields(live[0])[1] = live[1];
  live[2] = live[0];
  Value old = live[0];
  heap_collect(&h);
  assert(live[0] != old);
  for (int i = 0; i < 100000; i++) {
    assert(heap_alloc(&h, 3, 3, 17));
    assert(live[0] == live[2]);
    assert(object_fields(live[0])[0] == live[0]);
    assert(object_fields(live[0])[1] == live[1]);
    assert(object_kind(live[1]) == 2);
    assert(object_identity(live[0]) == identity);
    assert(!strcmp(object_bytes(live[1]), "opaque payload"));
  }
  assert(h.collections > 1000);
  assert(!heap_alloc(&h, 1, 0, SIZE_MAX));
  live[2] = heap_alloc(&h, 4, 0, 3900);
  assert(live[2]);
  assert(!heap_alloc(&h, 4, 0, 128));
  live[2] = V_NIL;
  assert(heap_alloc(&h, 4, 0, 128));
  roots_pop(&h, &roots);
  heap_collect(&h);
  assert(h.used == 0);
  heap_destroy(&h);
  puts("heap tests passed");
}
