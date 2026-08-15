// test_builtins.cpp — compact dict lifecycle, equality matrix (spec 2.4),
// int wrap-on-overflow. Needs the full runtime to link (Vm::create).
#include "doctest.h"
#include "../src/builtins.hpp"
#include "../src/value.hpp"
#include "../src/vm.hpp"
#include "../src/heap.hpp"
#include <cmath>
#include <cstdint>

using namespace ot;

static Vm* make_vm() {
  VmConfig cfg;
  cfg.heapBytes = 1 << 20;
  cfg.stackSlots = 1024;
  cfg.maxDepth = 256;
  return Vm::create(cfg);
}

static Value str_v(Vm& vm, const char* s) { return make_string(vm, s, (u32)strlen(s)); }

// ---------------------------------------------------------------------------

TEST_CASE("compact dict lifecycle") {
  Vm* vm = make_vm();
  Value t = make_table(*vm);
  u32 root = vm->push(t);

  SUBCASE("insert, update, delete, re-insert ordering") {
    Value ka = keyword_v(vm->intern.intern("a", 1));
    Value kb = keyword_v(vm->intern.intern("b", 1));
    Value kc = keyword_v(vm->intern.intern("c", 1));
    table_put(*vm, t, ka, int_v(1));
    table_put(*vm, t, kb, int_v(2));
    table_put(*vm, t, kc, int_v(3));
    CHECK(table_entry_count(t) == 3);
    CHECK(table_get(*vm, t, kb).i == 2);

    // update keeps position
    table_put(*vm, t, ka, int_v(10));
    Value k, v;
    REQUIRE(table_entry_at(t, 0, &k, &v));
    CHECK(val_eq(k, ka));
    CHECK(v.i == 10);

    // delete (store nil), then re-insert moves to end
    table_put(*vm, t, ka, nil_v());
    CHECK(table_entry_count(t) == 2);
    CHECK(is_nil(table_get(*vm, t, ka)));
    REQUIRE(table_entry_at(t, 0, &k, &v));
    CHECK(val_eq(k, kb));

    table_put(*vm, t, ka, int_v(99));
    CHECK(table_entry_count(t) == 3);
    REQUIRE(table_entry_at(t, 2, &k, &v));  // fresh first insertion: at the end
    CHECK(val_eq(k, ka));
    CHECK(v.i == 99);
  }

  SUBCASE("many inserts and deletes trigger compaction and stay correct") {
    // insert 200 int keys, delete the even ones, verify odds intact + ordered
    for (i64 i = 0; i < 200; i++) table_put(*vm, t, int_v(i), int_v(i * 2));
    CHECK(table_entry_count(t) == 200);
    for (i64 i = 0; i < 200; i += 2) table_put(*vm, t, int_v(i), nil_v());
    CHECK(table_entry_count(t) == 100);
    for (i64 i = 1; i < 200; i += 2) {
      Value v = table_get(*vm, t, int_v(i));
      REQUIRE(v.tag == Tag::Int);
      CHECK(v.i == i * 2);
    }
    // order preserved among survivors
    Value k, v;
    for (u32 j = 0; j < 100; j++) {
      REQUIRE(table_entry_at(t, j, &k, &v));
      CHECK(k.i == (i64)(2 * j + 1));
    }
    // misses stay misses
    CHECK(is_nil(table_get(*vm, t, int_v(0))));
    CHECK(is_nil(table_get(*vm, t, int_v(500))));
  }

  SUBCASE("structural keys: equal strings and pairs hit the same slot") {
    Value s1 = str_v(*vm, "hello");
    u32 r1 = vm->push(s1);
    table_put(*vm, t, s1, int_v(42));
    Value s2 = str_v(*vm, "hello");  // different object, equal bytes
    CHECK(table_get(*vm, t, s2).i == 42);
    vm->popTo(r1);

    Value p1 = make_pair(*vm, int_v(1), int_v(2));
    u32 r2 = vm->push(p1);
    table_put(*vm, t, p1, int_v(7));
    Value p2 = make_pair(*vm, int_v(1), int_v(2));
    CHECK(table_get(*vm, t, p2).i == 7);
    vm->popTo(r2);
  }

  SUBCASE("float keys: NaN hits NaN, -0.0 hits 0.0") {
    table_put(*vm, t, float_v(NAN), int_v(1));
    CHECK(table_get(*vm, t, float_v(NAN)).i == 1);
    table_put(*vm, t, float_v(0.0), int_v(2));
    CHECK(table_get(*vm, t, float_v(-0.0)).i == 2);
    // type-strict: int 0 is a different key from float 0.0
    CHECK(is_nil(table_get(*vm, t, int_v(0))));
  }

  SUBCASE("mutable keys: identity, stable across GC") {
    Value a1 = make_array(*vm, 4);
    u32 r = vm->push(a1);
    table_put(*vm, t, a1, int_v(5));
    Value a2 = make_array(*vm, 4);  // distinct identity, same shape
    u32 r2 = vm->push(a2);
    CHECK(is_nil(table_get(*vm, t, a2)));
    vm->heap.collect();  // key must survive by stamped identity
    a1 = vm->stack[r];   // re-fetch possibly-moved values
    t = vm->stack[root];
    CHECK(table_get(*vm, t, a1).i == 5);
    vm->popTo(r);
    (void)r2;
  }

  vm->popTo(root);
  vm->destroy();
}

// ---------------------------------------------------------------------------

TEST_CASE("equality matrix (spec 2.4)") {
  Vm* vm = make_vm();

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
  Value s1 = str_v(*vm, "abc");
  u32 r = vm->push(s1);
  Value s2 = str_v(*vm, "abc");
  u32 r2 = vm->push(s2);
  CHECK(val_equal(*vm, s1, s2));
  CHECK(!val_eq(s1, s2));  // distinct objects
  CHECK(val_eq(s1, s1));

  // pairs: recursive
  Value p1 = make_pair(*vm, int_v(1), make_pair(*vm, str_v(*vm, "x"), null_v()));
  u32 r3 = vm->push(p1);
  Value p2 = make_pair(*vm, int_v(1), make_pair(*vm, str_v(*vm, "x"), null_v()));
  u32 r4 = vm->push(p2);
  CHECK(val_equal(*vm, p1, p2));
  CHECK(!val_eq(p1, p2));

  // mutables: identity only
  Value a1 = make_array(*vm, 2);
  u32 r5 = vm->push(a1);
  Value a2 = make_array(*vm, 2);
  CHECK(!val_equal(*vm, a1, a2));
  CHECK(val_equal(*vm, a1, a1));

  // hashes agree with equal?
  CHECK(val_hash(*vm, s1) == val_hash(*vm, s2));
  CHECK(val_hash(*vm, p1) == val_hash(*vm, p2));
  CHECK(val_hash(*vm, float_v(0.0)) == val_hash(*vm, float_v(-0.0)));
  CHECK(val_hash(*vm, float_v(NAN)) == val_hash(*vm, float_v(NAN)));
  CHECK(val_hash(*vm, int_v(1)) != val_hash(*vm, float_v(1.0)));  // type-strict

  vm->popTo(r);
  (void)r2;
  (void)r3;
  (void)r4;
  (void)r5;
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
