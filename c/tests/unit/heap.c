#include "heap.h"
#include "test.h"
#include <string.h>

TEST(integer_range) {
  CHECK(ot_to_int(ot_int(INTPTR_MIN / 2)) == INTPTR_MIN / 2);
  CHECK(ot_to_int(ot_int(INTPTR_MAX / 2)) == INTPTR_MAX / 2);
  CHECK(ot_to_int(ot_int(-1)) == -1);
}

TEST(collection_keeps_roots_and_exhaustion_recovers) {
  oti_heap h;
  CHECK(oti_heap_init(&h, 4096));
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
  CHECK(live[0] != old);
  for (int i = 0; i < 100000; i++) {
    CHECK(oti_heap_alloc(&h, 3, 3, 17));
    CHECK(live[0] == live[2]);
    CHECK(object_fields(live[0])[0] == live[0]);
    CHECK(object_fields(live[0])[1] == live[1]);
    CHECK(object_kind(live[1]) == 2);
    CHECK(object_identity(live[0]) == identity);
    CHECK_STR(object_bytes(live[1]), "opaque payload");
  }
  CHECK(h.collections > 1000);
  CHECK(!oti_heap_alloc(&h, 1, 0, SIZE_MAX));
  live[2] = oti_heap_alloc(&h, 4, 0, 3900);
  CHECK(live[2]);
  CHECK(!oti_heap_alloc(&h, 4, 0, 128));
  live[2] = OT_NIL;
  CHECK(oti_heap_alloc(&h, 4, 0, 128));
  oti_roots_pop(&h, &roots);
  oti_heap_collect(&h);
  CHECK_EQ_INT(h.used, 0);
  oti_heap_destroy(&h);
}

static const test_case tests[] = {
    TEST_ENTRY(integer_range),
    TEST_ENTRY(collection_keeps_roots_and_exhaustion_recovers),
};

TEST_SUITE(heap, tests);
