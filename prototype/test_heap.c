#include "heap.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  oti_heap h;
  assert(oti_heap_init(&h, 4096));
  assert(ot_to_int(ot_int(INTPTR_MIN / 2)) == INTPTR_MIN / 2);
  assert(ot_to_int(ot_int(INTPTR_MAX / 2)) == INTPTR_MAX / 2);
  assert(ot_to_int(ot_int(-1)) == -1);
  ot_value live[] = {OT_NIL, OT_NIL, OT_NIL};
  oti_roots roots;
  oti_roots_push(&h, &roots, live, 3);
  live[0] = oti_heap_alloc(&h, 1, 2, 0);
  uint32_t identity = object_identity(live[0]);
  live[1] = oti_heap_alloc(&h, 2, 0, 16);
  memcpy(object_bytes(live[1]), "opaque payload", 15);
  object_fields(live[0])[0] = live[0];
  object_fields(live[0])[1] = live[1];
  live[2] = live[0];
  ot_value old = live[0];
  oti_heap_collect(&h);
  assert(live[0] != old);
  for (int i = 0; i < 100000; i++) {
    assert(oti_heap_alloc(&h, 3, 3, 17));
    assert(live[0] == live[2]);
    assert(object_fields(live[0])[0] == live[0]);
    assert(object_fields(live[0])[1] == live[1]);
    assert(object_kind(live[1]) == 2);
    assert(object_identity(live[0]) == identity);
    assert(!strcmp(object_bytes(live[1]), "opaque payload"));
  }
  assert(h.collections > 1000);
  assert(!oti_heap_alloc(&h, 1, 0, SIZE_MAX));
  live[2] = oti_heap_alloc(&h, 4, 0, 3900);
  assert(live[2]);
  assert(!oti_heap_alloc(&h, 4, 0, 128));
  live[2] = OT_NIL;
  assert(oti_heap_alloc(&h, 4, 0, 128));
  oti_roots_pop(&h, &roots);
  oti_heap_collect(&h);
  assert(h.used == 0);
  oti_heap_destroy(&h);
  puts("heap tests passed");
}
