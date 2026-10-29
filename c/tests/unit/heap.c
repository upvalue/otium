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
  oti_heap_collect(&h);
  CHECK(oti_heap_contains(&h, live[0]) && oti_heap_contains(&h, live[1]));
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
  CHECK_EQ_INT(oti_heap_used(&h), 0);
  oti_heap_destroy(&h);
}

TEST(large_objects_live_apart_and_are_freed) {
  oti_heap h;
  CHECK(oti_heap_init(&h, 1 << 20));
  ot_value live[] = {OT_NIL, OT_NIL};
  oti_roots roots;
  oti_roots_push(&h, &roots, live, 2);
  live[0] = oti_heap_alloc(&h, 2, 1, OTI_LARGE_MIN);
  live[1] = oti_heap_alloc(&h, 2, 0, OTI_LARGE_MIN * 2);
  CHECK(live[0] && live[1]);
  memset(object_bytes(live[1]), 0x5a, OTI_LARGE_MIN * 2);
  object_fields(live[0])[0] = live[1];
  CHECK(h.large_bytes >= 3 * OTI_LARGE_MIN);
  ot_value before = live[0];
  oti_heap_collect(&h);
  CHECK(live[0] == before && object_fields(live[0])[0] == live[1]);
  unsigned char* bytes = object_bytes(live[1]);
  for (size_t i = 0; i < OTI_LARGE_MIN * 2; i++) CHECK(bytes[i] == 0x5a);
  live[0] = OT_NIL;
  oti_heap_collect(&h);
  CHECK(h.large_bytes >= 2 * OTI_LARGE_MIN && h.large_bytes < 3 * OTI_LARGE_MIN);
  live[1] = OT_NIL;
  oti_heap_collect(&h);
  CHECK_EQ_INT(h.large_bytes, 0);
  oti_roots_pop(&h, &roots);
  oti_heap_destroy(&h);
}

/* Garbage between survivors becomes holes that later allocation fills, so
 * a steady state doesn't take more blocks. */
TEST(holes_are_reused) {
  oti_heap h;
  CHECK(oti_heap_init(&h, 4 << 20));
  enum { KEPT = 2000 };
  static ot_value kept[KEPT];
  oti_roots roots;
  for (size_t i = 0; i < KEPT; i++) kept[i] = OT_NIL;
  oti_roots_push(&h, &roots, kept, KEPT);
  for (size_t i = 0; i < KEPT * 10; i++) {
    ot_value v = oti_heap_alloc(&h, 1, 2, 0);
    CHECK(v);
    object_fields(v)[0] = ot_int((intptr_t)i);
    if (i % 10 == 0) kept[i / 10] = v;
  }
  oti_heap_collect(&h);
  size_t blocks = h.blocks_used;
  CHECK(h.recyclable.count > 0);
  for (int round = 0; round < 20; round++) {
    for (size_t i = 0; i < KEPT * 8; i++) CHECK(oti_heap_alloc(&h, 1, 2, 0));
    oti_heap_collect(&h);
  }
  CHECK(h.blocks_used <= blocks + 1);
  for (size_t i = 0; i < KEPT; i++) CHECK(ot_to_int(object_fields(kept[i])[0]) == (intptr_t)i * 10);
  oti_roots_pop(&h, &roots);
  oti_heap_destroy(&h);
}

/* Objects bigger than a line that don't fit a hole go to the overflow block
 * and survive like any other. */
TEST(medium_objects_survive) {
  oti_heap h;
  CHECK(oti_heap_init(&h, 4 << 20));
  enum { COUNT = 200 };
  static ot_value live[COUNT];
  oti_roots roots;
  for (size_t i = 0; i < COUNT; i++) live[i] = OT_NIL;
  oti_roots_push(&h, &roots, live, COUNT);
  for (size_t i = 0; i < COUNT; i++) {
    CHECK(oti_heap_alloc(&h, 1, 1, 8));
    size_t bytes = OTI_LINE + (i * 37) % (OTI_LARGE_MIN - OTI_LINE - 64);
    live[i] = oti_heap_alloc(&h, 2, 0, bytes);
    CHECK(live[i]);
    memset(object_bytes(live[i]), (int)(i & 255), bytes);
  }
  oti_heap_collect(&h);
  for (int round = 0; round < 5; round++) {
    for (size_t i = 0; i < 5000; i++) CHECK(oti_heap_alloc(&h, 1, 2, 0));
    oti_heap_collect(&h);
  }
  for (size_t i = 0; i < COUNT; i++) {
    size_t bytes = OTI_LINE + (i * 37) % (OTI_LARGE_MIN - OTI_LINE - 64);
    unsigned char* p = object_bytes(live[i]);
    for (size_t j = 0; j < bytes; j++) CHECK(p[j] == (unsigned char)(i & 255));
  }
  oti_roots_pop(&h, &roots);
  oti_heap_destroy(&h);
}

/* A sparse heap gets its sparsest blocks evacuated: survivors move, keep
 * their contents, identity and links, and pinned objects stay put. */
TEST(evacuation_moves_survivors_out_of_sparse_blocks) {
  oti_heap h;
  CHECK(oti_heap_init(&h, 8 << 20));
  enum { KEPT = 4000 };
  static ot_value kept[KEPT];
  static uint32_t identities[KEPT];
  oti_roots roots;
  for (size_t i = 0; i < KEPT; i++) kept[i] = OT_NIL;
  oti_roots_push(&h, &roots, kept, KEPT);
  for (size_t i = 0; i < KEPT * 20; i++) {
    ot_value v = oti_heap_alloc(&h, 1, 2, 0);
    CHECK(v);
    object_fields(v)[0] = ot_int((intptr_t)i);
    if (i % 20 == 0) {
      size_t k = i / 20;
      object_fields(v)[1] = k ? kept[k - 1] : OT_NIL;
      kept[k] = v;
      identities[k] = object_identity(v);
    }
  }
  object_pin(kept[7]);
  ot_value pinned = kept[7];
  oti_heap_collect(&h);
  CHECK(h.defragment);
  ot_value first = kept[100];
  oti_heap_collect(&h);
  CHECK(h.copied_bytes > 0);
  CHECK(kept[100] != first);
  CHECK(kept[7] == pinned);
  for (size_t k = 0; k < KEPT; k++) {
    CHECK(ot_to_int(object_fields(kept[k])[0]) == (intptr_t)k * 20);
    CHECK(object_fields(kept[k])[1] == (k ? kept[k - 1] : OT_NIL));
    CHECK(object_identity(kept[k]) == identities[k]);
  }
  oti_roots_pop(&h, &roots);
  oti_heap_destroy(&h);
}

/* Marking a wide object queues every field, far past one segment of mark
 * stack. Free blocks hold the rest and go back on the free list after. */
TEST(mark_stack_borrows_free_blocks) {
  oti_heap h;
  CHECK(oti_heap_init(&h, 16 << 20));
  enum { WIDTH = 100000 };
  ot_value live[] = {OT_NIL};
  oti_roots roots;
  oti_roots_push(&h, &roots, live, 1);
  live[0] = oti_heap_alloc(&h, 2, WIDTH, 0);
  CHECK(live[0]);
  for (size_t i = 0; i < WIDTH; i++) {
    ot_value leaf = oti_heap_alloc(&h, 1, 1, 0);
    CHECK(leaf);
    object_fields(leaf)[0] = ot_int((intptr_t)i);
    object_fields(live[0])[i] = leaf;
  }
  oti_heap_collect(&h);
  size_t free_blocks = h.free_blocks.count;
  oti_heap_collect(&h);
  CHECK_EQ_INT(h.free_blocks.count, free_blocks);
  for (size_t i = 0; i < WIDTH; i++)
    CHECK(ot_to_int(object_fields(object_fields(live[0])[i])[0]) == (intptr_t)i);
  oti_roots_pop(&h, &roots);
  oti_heap_destroy(&h);
}

static const test_case tests[] = {
    TEST_ENTRY(integer_range),
    TEST_ENTRY(collection_keeps_roots_and_exhaustion_recovers),
    TEST_ENTRY(large_objects_live_apart_and_are_freed),
    TEST_ENTRY(holes_are_reused),
    TEST_ENTRY(medium_objects_survive),
    TEST_ENTRY(evacuation_moves_survivors_out_of_sparse_blocks),
    TEST_ENTRY(mark_stack_borrows_free_blocks),
};

TEST_SUITE(heap, tests);
