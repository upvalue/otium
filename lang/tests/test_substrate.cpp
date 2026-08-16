// Substrate tests: Vec, Value tags, heap alloc/scavenge, identity, intern.
// Runs standalone against src/heap.cpp + src/intern.cpp (no vm.cpp needed:
// Heap is constructed with vm == nullptr and roots registered via addRoots).
#include "doctest.h"
#include "common.hpp"
#include "vec.hpp"
#include "value.hpp"
#include "heap.hpp"
#include "intern.hpp"
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

using namespace ot;

static bool child_aborts(void (*fn)()) {
  fflush(nullptr);
  pid_t pid = fork();
  if (pid < 0) return false;
  if (pid == 0) {
    (void)freopen("/dev/null", "w", stdout);
    (void)freopen("/dev/null", "w", stderr);
    std::signal(SIGABRT, SIG_DFL);
    fn();
    _exit(0);
  }
  int status = 0;
  if (waitpid(pid, &status, 0) != pid) return false;
  return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

TEST_CASE("Vec growth and basic ops") {
  Vec<int> v;
  CHECK(v.len == 0);
  for (int i = 0; i < 1000; i++) v.push(i);
  CHECK(v.len == 1000);
  CHECK(v.cap >= 1000);
  CHECK(v[0] == 0);
  CHECK(v[999] == 999);
  CHECK(v.pop() == 999);
  CHECK(v.len == 999);
  v.clear();
  CHECK(v.len == 0);
}

TEST_CASE("shared byte helpers") {
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

TEST_CASE("Buf append and printf") {
  Buf b;
  b.appendCstr("abc");
  b.printf(" %d-%s", 42, "x");
  CHECK(b.len == 8);
  CHECK(memcmp(b.data, "abc 42-x", 8) == 0);

  Buf moved(std::move(b));
  CHECK(b.data == nullptr);
  CHECK(b.len == 0);
  CHECK(b.cap == 0);
  CHECK(moved.len == 8);
  CHECK(memcmp(moved.data, "abc 42-x", 8) == 0);

  Buf assigned;
  assigned.appendCstr("discarded");
  assigned = std::move(moved);
  CHECK(moved.data == nullptr);
  CHECK(moved.len == 0);
  CHECK(moved.cap == 0);
  CHECK(assigned.len == 8);
  CHECK(memcmp(assigned.data, "abc 42-x", 8) == 0);
}

TEST_CASE("Value tag round-trips") {
  CHECK(sizeof(Value) == 16);
  Value v = int_v(-12345678901234LL);
  CHECK(v.tag == Tag::Int);
  CHECK(v.i == -12345678901234LL);
  v = float_v(3.5);
  CHECK(v.tag == Tag::Float);
  CHECK(v.f == 3.5);
  v = symbol_v(77);
  CHECK(v.tag == Tag::Symbol);
  CHECK(v.id == 77);
  v = keyword_v(9);
  CHECK(v.tag == Tag::Keyword);
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

// Root helper: register a local Vec<Value> as a GC root set.
static void walkVecRoots(void* ud, Heap::VisitFn visit, void* ctx) {
  Vec<Value>* roots = (Vec<Value>*)ud;
  for (u32 i = 0; i < roots->len; i++) visit(ctx, &roots->data[i]);
}

TEST_CASE("heap buffer payload placement lifetime") {
  Heap heap(nullptr, 4096);
  Vec<Value> roots;
  heap.addRoots(walkVecRoots, &roots);

  roots.push(make_buffer_h(heap));
  as_buffer(roots[0])->buf.appendCstr("survives collection");
  heap.collect();
  Buf& live = as_buffer(roots[0])->buf;
  CHECK(live.len == 19);
  CHECK(memcmp(live.data, "survives collection", 19) == 0);

  // Dropping the root makes the next sweep invoke BufferData::buf.~Buf().
  roots.clear();
  heap.collect();
}

TEST_CASE("heap size overflow guards abort before allocating") {
  CHECK(child_aborts([] {
    Heap heap(nullptr, 1024);
    heap.maxBytes = UINT32_MAX;
    (void)heap.alloc(ObjType::String, UINT32_MAX);
  }));
  CHECK(child_aborts([] {
    Heap heap(nullptr, 1024);
    (void)make_string_h(heap, "", UINT32_MAX);
  }));
  CHECK(child_aborts([] {
    Heap heap(nullptr, 1024);
    Value array = make_array_h(heap, 0);
    as_array(array)->cap = UINT32_MAX / 2 + 1;
    array_reserve(array, UINT32_MAX);
  }));
}

TEST_CASE("alloc + scavenge keeps live, drops dead") {
  Heap heap(nullptr, 4096);
  Vec<Value> roots;
  heap.addRoots(walkVecRoots, &roots);

  Value live = make_string_h(heap, "hello", 5);
  roots.push(live);
  // dead garbage
  for (int i = 0; i < 50; i++) (void)make_string_h(heap, "garbage-string", 14);
  u32 usedBefore = heap.used;
  heap.collect();
  CHECK(heap.used < usedBefore);
  Value survivor = roots[0];
  CHECK(survivor.tag == Tag::String);
  CHECK(as_string(survivor)->len == 5);
  CHECK(memcmp(string_bytes(survivor), "hello", 5) == 0);
}

TEST_CASE("pairs traced transitively; slots updated after move") {
  Heap heap(nullptr, 4096);
  Vec<Value> roots;
  heap.addRoots(walkVecRoots, &roots);

  Value s = make_string_h(heap, "leaf", 4);
  roots.push(s);
  Value p = make_pair_h(heap, roots[0], int_v(7));
  roots.push(p);
  roots.pop();  // keep p rooted only
  roots[0] = p;

  Obj* before = p.obj;
  heap.collect();
  Value p2 = roots[0];
  CHECK(p2.obj != before);  // moved
  CHECK(p2.tag == Tag::Pair);
  CHECK(as_pair(p2)->cdr.i == 7);
  Value leaf = as_pair(p2)->car;
  CHECK(leaf.tag == Tag::String);
  CHECK(memcmp(string_bytes(leaf), "leaf", 4) == 0);
}

TEST_CASE("array items survive GC; dead array items freed") {
  Heap heap(nullptr, 8192);
  Vec<Value> roots;
  heap.addRoots(walkVecRoots, &roots);

  Value arr = make_array_h(heap, 4);
  roots.push(arr);
  {
    Value s = make_string_h(heap, "elem", 4);
    array_reserve(roots[0], 1);
    ArrayData* d = as_array(roots[0]);
    d->items[d->len++] = s;
  }
  // dead array (unrooted) — its C-heap items must be freed by the sweep
  (void)make_array_h(heap, 16);
  CHECK(heap.finalizable.len >= 2);

  heap.collect();
  CHECK(heap.finalizable.len == 1);
  ArrayData* d = as_array(roots[0]);
  CHECK(d->len == 1);
  CHECK(d->items[0].tag == Tag::String);
  CHECK(memcmp(string_bytes(d->items[0]), "elem", 4) == 0);
}

TEST_CASE("identity id stable across collection, lazy stamping") {
  Heap heap(nullptr, 4096);
  Vec<Value> roots;
  heap.addRoots(walkVecRoots, &roots);

  Value a = make_string_h(heap, "a", 1);
  roots.push(a);
  Value b = make_string_h(heap, "b", 1);
  roots.push(b);

  u32 ida = heap.identityOf(roots[0].obj);
  CHECK(ida != 0);
  CHECK(heap.identityOf(roots[0].obj) == ida);  // idempotent
  CHECK(roots[1].obj->ident == 0);              // lazy: b unstamped

  heap.collect();
  CHECK(heap.identityOf(roots[0].obj) == ida);  // stable across move
  u32 idb = heap.identityOf(roots[1].obj);
  CHECK(idb != 0);
  CHECK(idb != ida);
}

TEST_CASE("heap grows when live exceeds half") {
  Heap heap(nullptr, 1024);
  Vec<Value> roots;
  heap.addRoots(walkVecRoots, &roots);
  for (int i = 0; i < 200; i++) roots.push(make_string_h(heap, "live-string-payload-xx", 22));
  CHECK(heap.spaceSize > 1024);
  // all still reachable and intact
  for (u32 i = 0; i < roots.len; i++)
    CHECK(memcmp(string_bytes(roots[i]), "live-string-payload-xx", 22) == 0);
}

TEST_CASE("intern idempotence and dense ids") {
  Intern in;
  u32 a = in.intern("foo", 3);
  u32 b = in.intern("bar", 3);
  u32 c = in.intern("foo", 3);
  CHECK(a == 1);
  CHECK(b == 2);
  CHECK(c == a);
  u32 len = 0;
  const char* s = in.name(a, &len);
  CHECK(len == 3);
  CHECK(memcmp(s, "foo", 3) == 0);
  CHECK(in.name(0, &len) == nullptr);
  CHECK(in.name(99, &len) == nullptr);
  // force growth
  char buf[16];
  for (int i = 0; i < 500; i++) {
    int n = snprintf(buf, sizeof buf, "sym%d", i);
    u32 id = in.intern(buf, (u32)n);
    CHECK(id == (u32)(3 + i));
  }
  CHECK(in.intern("sym250", 6) == 253);
  CHECK(in.intern("foo", 3) == 1);
}
