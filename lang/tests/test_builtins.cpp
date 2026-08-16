// test_builtins.cpp — compact dict lifecycle, equality matrix (spec 2.4),
// int wrap-on-overflow. Needs the full runtime to link (Vm::create).
#include "doctest.h"
#include "../src/builtins.hpp"
#include "../src/printer.hpp"
#include "../src/value.hpp"
#include "../src/vm.hpp"
#include "../src/heap.hpp"
#include <cmath>
#include <csignal>
#include <cstdint>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

using namespace ot;

static Vm* make_vm() {
  VmConfig cfg;
  cfg.heapBytes = 1 << 20;
  cfg.stackSlots = 1024;
  cfg.maxDepth = 256;
  return Vm::create(cfg);
}

static Value str_v(Vm& vm, const char* s) { return make_string(vm, s, (u32)strlen(s)); }

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

// ---------------------------------------------------------------------------

TEST_CASE("compact dict lifecycle") {
  Vm* vm = make_vm();
  u32 root = vm->push(make_table(*vm));

  SUBCASE("insert, update, delete, re-insert ordering") {
    Value ka = keyword_v(vm->intern.intern("a", 1));
    Value kb = keyword_v(vm->intern.intern("b", 1));
    Value kc = keyword_v(vm->intern.intern("c", 1));
    table_put(*vm, vm->stack[root], ka, int_v(1));
    table_put(*vm, vm->stack[root], kb, int_v(2));
    table_put(*vm, vm->stack[root], kc, int_v(3));
    CHECK(table_entry_count(vm->stack[root]) == 3);
    CHECK(table_get(*vm, vm->stack[root], kb).i == 2);

    // update keeps position
    table_put(*vm, vm->stack[root], ka, int_v(10));
    Value k, v;
    u32 cursor = 0;
    REQUIRE(table_iter_next(vm->stack[root], &cursor, &k, &v));
    CHECK(val_eq(k, ka));
    CHECK(v.i == 10);

    // delete (store nil), then re-insert moves to end
    table_put(*vm, vm->stack[root], ka, nil_v());
    CHECK(table_entry_count(vm->stack[root]) == 2);
    CHECK(is_nil(table_get(*vm, vm->stack[root], ka)));
    cursor = 0;
    REQUIRE(table_iter_next(vm->stack[root], &cursor, &k, &v));
    CHECK(val_eq(k, kb));

    table_put(*vm, vm->stack[root], ka, int_v(99));
    CHECK(table_entry_count(vm->stack[root]) == 3);
    cursor = 0;
    REQUIRE(table_iter_next(vm->stack[root], &cursor, &k, &v));
    REQUIRE(table_iter_next(vm->stack[root], &cursor, &k, &v));
    REQUIRE(table_iter_next(vm->stack[root], &cursor, &k, &v));  // fresh insertion: at the end
    CHECK(val_eq(k, ka));
    CHECK(v.i == 99);
  }

  SUBCASE("many inserts and deletes trigger compaction and stay correct") {
    // insert 200 int keys, delete the even ones, verify odds intact + ordered
    for (i64 i = 0; i < 200; i++) table_put(*vm, vm->stack[root], int_v(i), int_v(i * 2));
    CHECK(table_entry_count(vm->stack[root]) == 200);
    for (i64 i = 0; i < 200; i += 2) table_put(*vm, vm->stack[root], int_v(i), nil_v());
    CHECK(table_entry_count(vm->stack[root]) == 100);
    for (i64 i = 1; i < 200; i += 2) {
      Value v = table_get(*vm, vm->stack[root], int_v(i));
      REQUIRE(v.tag == Tag::Int);
      CHECK(v.i == i * 2);
    }
    // order preserved among survivors
    Value k, v;
    u32 cursor = 0;
    for (u32 j = 0; j < 100; j++) {
      REQUIRE(table_iter_next(vm->stack[root], &cursor, &k, &v));
      CHECK(k.i == (i64)(2 * j + 1));
    }
    CHECK(!table_iter_next(vm->stack[root], &cursor, &k, &v));
    // misses stay misses
    CHECK(is_nil(table_get(*vm, vm->stack[root], int_v(0))));
    CHECK(is_nil(table_get(*vm, vm->stack[root], int_v(500))));
  }

  SUBCASE("structural keys: equal strings and pairs hit the same slot") {
    Value s1 = str_v(*vm, "hello");
    u32 r1 = vm->push(s1);
    table_put(*vm, vm->stack[root], s1, int_v(42));
    Value s2 = str_v(*vm, "hello");  // different object, equal bytes
    CHECK(table_get(*vm, vm->stack[root], s2).i == 42);
    vm->popTo(r1);

    Value p1 = make_pair(*vm, int_v(1), int_v(2));
    u32 r2 = vm->push(p1);
    table_put(*vm, vm->stack[root], p1, int_v(7));
    Value p2 = make_pair(*vm, int_v(1), int_v(2));
    CHECK(table_get(*vm, vm->stack[root], p2).i == 7);
    vm->popTo(r2);
  }

  SUBCASE("float keys: NaN hits NaN, -0.0 hits 0.0") {
    table_put(*vm, vm->stack[root], float_v(NAN), int_v(1));
    CHECK(table_get(*vm, vm->stack[root], float_v(NAN)).i == 1);
    table_put(*vm, vm->stack[root], float_v(0.0), int_v(2));
    CHECK(table_get(*vm, vm->stack[root], float_v(-0.0)).i == 2);
    // type-strict: int 0 is a different key from float 0.0
    CHECK(is_nil(table_get(*vm, vm->stack[root], int_v(0))));
  }

  SUBCASE("mutable keys: identity, stable across GC") {
    Value a1 = make_array(*vm, 4);
    u32 r = vm->push(a1);
    table_put(*vm, vm->stack[root], a1, int_v(5));
    Value a2 = make_array(*vm, 4);  // distinct identity, same shape
    u32 r2 = vm->push(a2);
    CHECK(is_nil(table_get(*vm, vm->stack[root], a2)));
    vm->heap.collect();  // key must survive by stamped identity
    a1 = vm->stack[r];   // re-fetch possibly-moved values
    CHECK(table_get(*vm, vm->stack[root], a1).i == 5);
    vm->popTo(r);
    (void)r2;
  }

  vm->popTo(root);
  vm->destroy();
}

// ---------------------------------------------------------------------------

TEST_CASE("table capacity overflow guards abort before allocating") {
  CHECK(child_aborts([] {
    Vm* vm = make_vm();
    Value table = make_table(*vm);
    TableData* data = as_table(table);
    data->entriesLen = UINT32_MAX;
    data->entriesCap = UINT32_MAX;
    (void)table_put(*vm, table, int_v(1), int_v(2));
  }));
  CHECK(child_aborts([] {
    Vm* vm = make_vm();
    Value table = make_table(*vm);
    TableData* data = as_table(table);
    data->entriesLen = UINT32_MAX / 2 + 1;
    data->entriesCap = UINT32_MAX / 2 + 1;
    (void)table_put(*vm, table, int_v(1), int_v(2));
  }));
}

// ---------------------------------------------------------------------------

TEST_CASE("table printing preserves insertion order") {
  Vm* vm = make_vm();
  Value t = make_table(*vm);
  u32 root = vm->push(t);
  Value ka = keyword_v(vm->intern.intern("a", 1));
  Value kb = keyword_v(vm->intern.intern("b", 1));
  Value kc = keyword_v(vm->intern.intern("c", 1));

  table_put(*vm, t, ka, int_v(1));
  table_put(*vm, t, kb, int_v(2));
  table_put(*vm, t, kc, int_v(3));

  Buf out;
  print_repr(*vm, t, out);
  CHECK(std::string(out.data, out.len) == "{:a 1 :b 2 :c 3}");

  // One tombstone remains in storage: it is skipped, and reinsertion appends.
  table_put(*vm, t, kb, nil_v());
  out.clear();
  print_repr(*vm, t, out);
  CHECK(std::string(out.data, out.len) == "{:a 1 :c 3}");

  table_put(*vm, t, kb, int_v(20));
  out.clear();
  print_repr(*vm, t, out);
  CHECK(std::string(out.data, out.len) == "{:a 1 :c 3 :b 20}");

  vm->popTo(root);
  vm->destroy();
}

// ---------------------------------------------------------------------------

TEST_CASE("equality matrix (spec 2.4)") {
  Vm* vm = make_vm();
  {
    Scope roots(*vm);

    // type-strict across the board
    CHECK(!val_equal(*vm, int_v(1), float_v(1.0)));
    CHECK(!val_equal(*vm, nil_v(), null_v()));
    CHECK(!val_equal(*vm, nil_v(), bool_v(false)));
    CHECK(!val_equal(*vm, null_v(), bool_v(false)));

    // immediates
    CHECK(val_equal(*vm, int_v(42), int_v(42)));
    CHECK(val_equal(*vm, bool_v(true), bool_v(true)));
    CHECK(!val_equal(*vm, bool_v(true), bool_v(false)));

    // floats: NaN equals NaN, 0.0 equals -0.0
    CHECK(val_equal(*vm, float_v(NAN), float_v(NAN)));
    CHECK(val_equal(*vm, float_v(0.0), float_v(-0.0)));

    // symbols/keywords interned
    u32 id = vm->intern.intern("foo", 3);
    CHECK(val_equal(*vm, symbol_v(id), symbol_v(id)));
    CHECK(!val_equal(*vm, symbol_v(id), keyword_v(id)));  // different types

    // strings: deep structural
    Slot s1 = roots.push(str_v(*vm, "abc"));
    Slot s2 = roots.push(str_v(*vm, "abc"));
    CHECK(val_equal(*vm, s1.get(), s2.get()));
    CHECK(!val_eq(s1.get(), s2.get()));  // distinct objects
    CHECK(val_eq(s1.get(), s1.get()));

    // pairs: recursive
    Slot p1 = roots.push(make_pair(*vm, int_v(1), make_pair(*vm, str_v(*vm, "x"), null_v())));
    Slot p2 = roots.push(make_pair(*vm, int_v(1), make_pair(*vm, str_v(*vm, "x"), null_v())));
    CHECK(val_equal(*vm, p1.get(), p2.get()));
    CHECK(!val_eq(p1.get(), p2.get()));

    // mutables: identity only
    Slot a1 = roots.push(make_array(*vm, 2));
    Slot a2 = roots.push(make_array(*vm, 2));
    CHECK(!val_equal(*vm, a1.get(), a2.get()));
    CHECK(val_equal(*vm, a1.get(), a1.get()));

    // hashes agree with equal?
    CHECK(val_hash(*vm, s1.get()) == val_hash(*vm, s2.get()));
    CHECK(val_hash(*vm, p1.get()) == val_hash(*vm, p2.get()));
    CHECK(val_hash(*vm, float_v(0.0)) == val_hash(*vm, float_v(-0.0)));
    CHECK(val_hash(*vm, float_v(NAN)) == val_hash(*vm, float_v(NAN)));
    CHECK(val_hash(*vm, int_v(1)) != val_hash(*vm, float_v(1.0)));  // type-strict
  }
  vm->destroy();
}

// ---------------------------------------------------------------------------

TEST_CASE("arithmetic wraps two's-complement") {
  Vm* vm = make_vm();
  const i64 MAX = INT64_MAX, MIN = INT64_MIN;

  // (+ MAX 1) wraps to MIN
  u32 b = vm->stack.len;
  vm->push(int_v(MAX));
  vm->push(int_v(1));
  Value r = nat_add(*vm, b, 2);
  vm->popTo(b);
  REQUIRE(r.tag == Tag::Int);
  CHECK(r.i == MIN);

  // (- MIN 1) wraps to MAX
  vm->push(int_v(MIN));
  vm->push(int_v(1));
  r = nat_sub(*vm, b, 2);
  vm->popTo(b);
  CHECK(r.i == MAX);

  // (- MIN) wraps to MIN (negation overflow)
  vm->push(int_v(MIN));
  r = nat_sub(*vm, b, 1);
  vm->popTo(b);
  CHECK(r.i == MIN);

  // (* MAX 2) wraps to -2
  vm->push(int_v(MAX));
  vm->push(int_v(2));
  r = nat_mul(*vm, b, 2);
  vm->popTo(b);
  CHECK(r.i == -2);

  // float contamination
  vm->push(int_v(1));
  vm->push(float_v(0.5));
  r = nat_add(*vm, b, 2);
  vm->popTo(b);
  REQUIRE(r.tag == Tag::Float);
  CHECK(r.f == doctest::Approx(1.5));

  // exact int division stays int; inexact goes float; div by zero unwinds
  vm->push(int_v(6));
  vm->push(int_v(2));
  r = nat_div(*vm, b, 2);
  vm->popTo(b);
  REQUIRE(r.tag == Tag::Int);
  CHECK(r.i == 3);

  vm->push(int_v(7));
  vm->push(int_v(2));
  r = nat_div(*vm, b, 2);
  vm->popTo(b);
  REQUIRE(r.tag == Tag::Float);
  CHECK(r.f == doctest::Approx(3.5));

  vm->push(int_v(1));
  vm->push(int_v(0));
  r = nat_div(*vm, b, 2);
  vm->popTo(b);
  CHECK(r.tag == Tag::Unwind);
  vm->unwindCondition = nil_v();  // clear for cleanliness

  vm->destroy();
}
