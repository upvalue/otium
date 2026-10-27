#include "heap.h"
#include "test.h"
#include <string.h>

TEST(growth_keeps_cycles_and_aliases_and_recovers_from_exhaustion) {
  oti_heap h;
  CHECK(oti_heap_init(&h, 256));
  CHECK(h.capacity == h.limit);
  ot_value live[] = {OT_NIL, OT_NIL, OT_NIL};
  oti_roots roots;
  oti_roots_push(&h, &roots, live, 3);
  live[0] = oti_heap_alloc(&h, 1, 2, 0);
  CHECK(live[0]);
  object_fields(live[0])[0] = live[0];
  object_fields(live[0])[1] = ot_int(42);
  live[1] = live[0];
  uint32_t identity = object_identity(live[0]);
  CHECK(!oti_heap_alloc(&h, 2, 0, 300));
  CHECK_EQ_INT(h.capacity, 256);
  CHECK(oti_heap_set_limit(&h, 4096));
  live[2] = oti_heap_alloc(&h, 2, 0, 1000);
  CHECK(live[2]);
  CHECK(h.capacity == 2048 && live[0] == live[1]);
  CHECK(object_fields(live[0])[0] == live[0] && object_identity(live[0]) == identity);
  memset(object_bytes(live[2]), 0x55, 1000);
  object_fields(live[0])[1] = live[2];
  live[2] = oti_heap_alloc(&h, 3, 0, 2000);
  CHECK(live[2]);
  CHECK_EQ_INT(h.capacity, 4096);
  CHECK(live[0] == live[1] && object_fields(live[0])[0] == live[0]);
  CHECK(object_identity(live[0]) == identity);
  unsigned char* bytes = object_bytes(object_fields(live[0])[1]);
  for (size_t i = 0; i < 1000; i++) CHECK(bytes[i] == 0x55);
  CHECK(!oti_heap_set_limit(&h, 2048));
  CHECK(!oti_heap_set_limit(&h, SIZE_MAX));
  CHECK_EQ_INT(h.limit, 4096);
  CHECK(!oti_heap_alloc(&h, 4, 0, 2000));
  CHECK_EQ_INT(h.capacity, 4096);
  CHECK(live[0] == live[1] && object_fields(live[0])[0] == live[0]);
  live[2] = OT_NIL;
  CHECK(oti_heap_alloc(&h, 4, 0, 2000));
  oti_roots_pop(&h, &roots);
  oti_heap_destroy(&h);
}

TEST(garbage_does_not_grow_heap) {
  oti_heap h;
  CHECK(oti_heap_init(&h, 256));
  CHECK(oti_heap_set_limit(&h, 4096));
  for (size_t i = 0; i < 1000; i++) CHECK(oti_heap_alloc(&h, 1, 0, 128));
  CHECK_EQ_INT(h.capacity, 256);
  oti_heap_destroy(&h);
}

static const test_case tests[] = {
    TEST_ENTRY(growth_keeps_cycles_and_aliases_and_recovers_from_exhaustion),
    TEST_ENTRY(garbage_does_not_grow_heap),
};

TEST_SUITE(heap_growth, tests);
