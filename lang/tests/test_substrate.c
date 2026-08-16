// Low-level tests: Vec, Value tags, bare-Heap allocation/scavenging, identity,
// and interning. Bare-Heap cases pass nullptr for its optional State owner.
#define OT_HEAP_INTERNALS
#include "ctest.h"
#include "common.h"
#include "vec.h"
#include "value.h"
#include "heap.h"
#include "intern.h"
#include <signal.h>

OT_VEC_TYPE(int, VecInt);

TEST(vec_growth_and_basic_ops) {
  VecInt v = {0};
  CHECK(v.len == 0);
  for (int i = 0; i < 1000; i++) vec_push(&v, i);
  CHECK(v.len == 1000);
  CHECK(v.cap >= 1000);
  CHECK(v.data[0] == 0);
  CHECK(v.data[999] == 999);
  CHECK(vec_pop(&v) == 999);
  CHECK(v.len == 999);
  vec_clear(&v);
  CHECK(v.len == 0);
  vec_deinit(&v);
}

TEST(shared_byte_helpers) {
  CHECK(ascii_whitespace(' '));
  CHECK(ascii_whitespace('\t'));
  CHECK(ascii_whitespace('\n'));
  CHECK(ascii_whitespace('\r'));
  CHECK(ascii_whitespace('\f'));
  CHECK(ascii_whitespace('\v'));
  CHECK(!ascii_whitespace('x'));

  const char utf8[] = {'h', (char)0xC3, (char)0xA9, (char)0xE2, (char)0x98, (char)0x83};
  CHECK(utf8_count(utf8, sizeof utf8) == 3);
  CHECK(utf8_count(nullptr, 0) == 0);
}

TEST(buf_append_and_printf) {
  Buf b = {0};
  buf_append_cstr(&b, "abc");
  buf_printf(&b, " %d-%s", 42, "x");
  CHECK(b.len == 8);
  CHECK(memcmp(b.data, "abc 42-x", 8) == 0);

  // The C++ Buf had move construction/assignment; the C struct transfers
  // ownership by copy + zeroing the source. Assert the same postconditions.
  Buf moved = b;
  b = (Buf){0};
  CHECK(b.data == nullptr);
  CHECK(b.len == 0);
  CHECK(b.cap == 0);
  CHECK(moved.len == 8);
  CHECK(memcmp(moved.data, "abc 42-x", 8) == 0);

  Buf assigned = {0};
  buf_append_cstr(&assigned, "discarded");
  buf_deinit(&assigned);  // move-assignment released the old storage
  assigned = moved;
  moved = (Buf){0};
  CHECK(moved.data == nullptr);
  CHECK(moved.len == 0);
  CHECK(moved.cap == 0);
  CHECK(assigned.len == 8);
  CHECK(memcmp(assigned.data, "abc 42-x", 8) == 0);
  buf_deinit(&assigned);
}

TEST(value_tag_round_trips) {
  CHECK(sizeof(Value) == 16);
  Value v = int_v(-12345678901234LL);
  CHECK(v.tag == Tag_Int);
  CHECK(v.i == -12345678901234LL);
  v = float_v(3.5);
  CHECK(v.tag == Tag_Float);
  CHECK(v.f == 3.5);
  v = symbol_v(77);
  CHECK(v.tag == Tag_Symbol);
  CHECK(v.id == 77);
  v = keyword_v(9);
  CHECK(v.tag == Tag_Keyword);
  CHECK(v.id == 9);
  CHECK(is_nil(nil_v()));
  CHECK(is_falsy(nil_v()));
  CHECK(is_falsy(bool_v(false)));
  CHECK(is_truthy(bool_v(true)));
  CHECK(is_truthy(int_v(0)));
  CHECK(is_truthy(null_v()));  // () is truthy
  CHECK(is_unwind(unwind_v()));
  CHECK(!is_heap(int_v(1)));
  CHECK(val_eq(symbol_v(3), symbol_v(3)));
  CHECK(!val_eq(symbol_v(3), keyword_v(3)));
  CHECK(val_eq(int_v(5), int_v(5)));
  CHECK(!val_eq(int_v(5), int_v(6)));
}

// Root helper: register a local VecValue as a GC root set.
static void walk_vec_roots(void* ud, VisitFn visit, void* ctx) {
  VecValue* roots = (VecValue*)ud;
  for (u32 i = 0; i < roots->len; i++) visit(ctx, &roots->data[i]);
}

TEST(heap_buffer_payload_placement_lifetime) {
  Heap heap;
  heap_init(&heap, nullptr, 4096, OT_HEAP_MAX_DEFAULT);
  VecValue roots = {0};
  heap_add_roots(&heap, walk_vec_roots, &roots);

  vec_push(&roots, make_buffer_h(&heap));
  buffer_append_h(&heap, roots.data[0], "survives collection", 19);
  // Buffer bytes are a GC object, so the collector moves them with everything
  // else and nothing has to be freed when the buffer dies.
  heap_collect(&heap);
  CHECK(buffer_len(roots.data[0]) == 19);
  CHECK(memcmp(buffer_data(roots.data[0]), "survives collection", 19) == 0);

  // Growth past the initial capacity reallocates the storage object.
  for (u32 i = 0; i < 100; i++) buffer_append_h(&heap, roots.data[0], "xyz", 3);
  CHECK(buffer_len(roots.data[0]) == 19 + 300);
  heap_collect(&heap);
  CHECK(buffer_len(roots.data[0]) == 19 + 300);
  CHECK(memcmp(buffer_data(roots.data[0]), "survives collection", 19) == 0);

  vec_clear(&roots);
  heap_collect(&heap);
  // Nothing but Foreign is ever finalizable now.
  CHECK(heap.finalizable.len == 0);
  heap_deinit(&heap);
  vec_deinit(&roots);
}

// Death-test bodies for the overflow guards; CHECK_ABORTS takes a single
// statement, so multi-statement scenarios live in helper functions (the C++
// test used lambdas).
static void abort_vec_reserve_overflow(void) {
  VecInt values = {0};
  vec_reserve(&values, UINT32_MAX);
}
static void abort_heap_alloc_overflow(void) {
  Heap heap;
  heap_init(&heap, nullptr, 1024, OT_HEAP_MAX_DEFAULT);
  heap.maxBytes = UINT32_MAX;
  (void)heap_alloc(&heap, ObjType_String, UINT32_MAX);
}
static void abort_string_size_overflow(void) {
  Heap heap;
  heap_init(&heap, nullptr, 1024, OT_HEAP_MAX_DEFAULT);
  (void)make_string_h(&heap, "", UINT32_MAX);
}
static void abort_array_reserve_overflow(void) {
  Heap heap;
  heap_init(&heap, nullptr, 1024, OT_HEAP_MAX_DEFAULT);
  // A capacity this large overflows the storage payload size before any
  // allocation is attempted.
  array_reserve_h(&heap, make_array_h(&heap, 0), UINT32_MAX);
}

TEST(heap_size_overflow_guards_abort_before_allocating) {
  CHECK_ABORTS(abort_vec_reserve_overflow());
  CHECK_ABORTS(abort_heap_alloc_overflow());
  CHECK_ABORTS(abort_string_size_overflow());
  CHECK_ABORTS(abort_array_reserve_overflow());
}

TEST(alloc_scavenge_keeps_live_drops_dead) {
  Heap heap;
  heap_init(&heap, nullptr, 4096, OT_HEAP_MAX_DEFAULT);
  VecValue roots = {0};
  heap_add_roots(&heap, walk_vec_roots, &roots);

  Value live = make_string_h(&heap, "hello", 5);
  vec_push(&roots, live);
  // dead garbage
  for (int i = 0; i < 50; i++) (void)make_string_h(&heap, "garbage-string", 14);
  u32 usedBefore = heap.used;
  heap_collect(&heap);
  CHECK(heap.used < usedBefore);
  Value survivor = roots.data[0];
  CHECK(survivor.tag == Tag_String);
  CHECK(as_string(survivor)->len == 5);
  CHECK(memcmp(string_bytes(survivor), "hello", 5) == 0);
  heap_deinit(&heap);
  vec_deinit(&roots);
}

TEST(pairs_traced_transitively_slots_updated_after_move) {
  Heap heap;
  heap_init(&heap, nullptr, 4096, OT_HEAP_MAX_DEFAULT);
  VecValue roots = {0};
  heap_add_roots(&heap, walk_vec_roots, &roots);

  Value s = make_string_h(&heap, "leaf", 4);
  vec_push(&roots, s);
  Value p = make_pair_h(&heap, roots.data[0], int_v(7));
  vec_push(&roots, p);
  (void)vec_pop(&roots);  // keep p rooted only
  roots.data[0] = p;

  Obj* before = p.obj;
  heap_collect(&heap);
  Value p2 = roots.data[0];
  CHECK(p2.obj != before);  // moved
  CHECK(p2.tag == Tag_Pair);
  CHECK(as_pair(p2)->cdr.i == 7);
  Value leaf = as_pair(p2)->car;
  CHECK(leaf.tag == Tag_String);
  CHECK(memcmp(string_bytes(leaf), "leaf", 4) == 0);
  heap_deinit(&heap);
  vec_deinit(&roots);
}

TEST(array_items_survive_gc_dead_array_items_freed) {
  Heap heap;
  heap_init(&heap, nullptr, 8192, OT_HEAP_MAX_DEFAULT);
  VecValue roots = {0};
  heap_add_roots(&heap, walk_vec_roots, &roots);

  Value arr = make_array_h(&heap, 4);
  vec_push(&roots, arr);
  {
    Value s = make_string_h(&heap, "elem", 4);
    array_reserve_h(&heap, roots.data[0], 1);
    // Derived after the reserve: growth moves the storage object.
    ArrayData* d = as_array(roots.data[0]);
    array_items(roots.data[0])[d->len++] = s;
  }
  // A dead unrooted array needs no sweeping: its storage is just garbage.
  (void)make_array_h(&heap, 16);
  CHECK(heap.finalizable.len == 0);

  heap_collect(&heap);
  CHECK(heap.finalizable.len == 0);
  CHECK(as_array(roots.data[0])->len == 1);
  CHECK(array_items(roots.data[0])[0].tag == Tag_String);
  CHECK(memcmp(string_bytes(array_items(roots.data[0])[0]), "elem", 4) == 0);

  // Growth across a collection keeps the elements and moves the storage.
  for (u32 i = 0; i < 64; i++) {
    array_reserve_h(&heap, roots.data[0], as_array(roots.data[0])->len + 1);
    ArrayData* d = as_array(roots.data[0]);
    array_items(roots.data[0])[d->len++] = int_v((i64)i);
  }
  heap_collect(&heap);
  CHECK(as_array(roots.data[0])->len == 65);
  CHECK(memcmp(string_bytes(array_items(roots.data[0])[0]), "elem", 4) == 0);
  CHECK(array_items(roots.data[0])[64].i == 63);
  heap_deinit(&heap);
  vec_deinit(&roots);
}

TEST(identity_id_stable_across_collection_lazy_stamping) {
  Heap heap;
  heap_init(&heap, nullptr, 4096, OT_HEAP_MAX_DEFAULT);
  VecValue roots = {0};
  heap_add_roots(&heap, walk_vec_roots, &roots);

  Value a = make_string_h(&heap, "a", 1);
  vec_push(&roots, a);
  Value b = make_string_h(&heap, "b", 1);
  vec_push(&roots, b);

  u32 ida = heap_identity_of(&heap, roots.data[0].obj);
  CHECK(ida != 0);
  CHECK(heap_identity_of(&heap, roots.data[0].obj) == ida);  // idempotent
  CHECK(roots.data[1].obj->ident == 0);                      // lazy: b unstamped

  heap_collect(&heap);
  CHECK(heap_identity_of(&heap, roots.data[0].obj) == ida);  // stable across move
  u32 idb = heap_identity_of(&heap, roots.data[1].obj);
  CHECK(idb != 0);
  CHECK(idb != ida);
  heap_deinit(&heap);
  vec_deinit(&roots);
}

TEST(heap_grows_when_live_exceeds_half) {
  Heap heap;
  heap_init(&heap, nullptr, 1024, OT_HEAP_MAX_DEFAULT);
  VecValue roots = {0};
  heap_add_roots(&heap, walk_vec_roots, &roots);
  for (int i = 0; i < 200; i++)
    vec_push(&roots, make_string_h(&heap, "live-string-payload-xx", 22));
  CHECK(heap.spaceSize > 1024);
  // all still reachable and intact
  for (u32 i = 0; i < roots.len; i++)
    CHECK(memcmp(string_bytes(roots.data[i]), "live-string-payload-xx", 22) == 0);
  heap_deinit(&heap);
  vec_deinit(&roots);
}

TEST(intern_idempotence_and_dense_ids) {
  Intern in;
  intern_init(&in);
  u32 a = intern_id(&in, "foo", 3);
  u32 b = intern_id(&in, "bar", 3);
  u32 c = intern_id(&in, "foo", 3);
  CHECK(a == 1);
  CHECK(b == 2);
  CHECK(c == a);
  u32 len = 0;
  const char* s = intern_name(&in, a, &len);
  CHECK(len == 3);
  CHECK(memcmp(s, "foo", 3) == 0);
  CHECK(intern_name(&in, 0, &len) == nullptr);
  CHECK(intern_name(&in, 99, &len) == nullptr);
  // force growth
  char buf[16];
  for (int i = 0; i < 500; i++) {
    int n = snprintf(buf, sizeof buf, "sym%d", i);
    u32 id = intern_id(&in, buf, (u32)n);
    CHECK(id == (u32)(3 + i));
  }
  CHECK(intern_id(&in, "sym250", 6) == 253);
  CHECK(intern_id(&in, "foo", 3) == 1);
  intern_deinit(&in);
}

// The point of moving backing storage onto the GC heap: heapMaxBytes bounds it.
// Before, array/table/buffer bulk sat in the C heap and escaped the cap
// entirely, so a program could exhaust memory while nominally inside its limit.
TEST(collection_storage_is_accounted_against_the_heap) {
  Heap heap;
  heap_init(&heap, nullptr, 1 << 20, OT_HEAP_MAX_DEFAULT);
  VecValue roots = {0};
  heap_add_roots(&heap, walk_vec_roots, &roots);

  vec_push(&roots, make_array_h(&heap, 0));
  u32 before = heap.used;
  const u32 count = 4096;
  for (u32 i = 0; i < count; i++) {
    array_reserve_h(&heap, roots.data[0], as_array(roots.data[0])->len + 1);
    ArrayData* d = as_array(roots.data[0]);
    array_items(roots.data[0])[d->len++] = int_v((i64)i);
  }
  // The live elements alone are count * sizeof(Value); the heap must have grown
  // by at least that much, which is only true if the storage lives here.
  CHECK(heap.used - before >= count * sizeof(Value));
  CHECK(as_array(roots.data[0])->len == count);

  heap_collect(&heap);
  CHECK(as_array(roots.data[0])->len == count);
  CHECK(array_items(roots.data[0])[count - 1].i == count - 1);
  // A collection with the array live must retain its storage, and nothing is
  // finalizable because nothing owns C-heap memory any more.
  CHECK(heap.used >= count * sizeof(Value));
  CHECK(heap.finalizable.len == 0);

  vec_clear(&roots);
  heap_collect(&heap);
  CHECK(heap.used < count * sizeof(Value));  // storage collected with the array

  heap_deinit(&heap);
  vec_deinit(&roots);
}
