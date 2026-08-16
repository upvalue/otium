// test_builtins.cpp — compact dict lifecycle, equality matrix (spec 2.4),
// int wrap-on-overflow. Needs the full runtime to link (State::create).
#include "doctest.h"
#include "../src/builtins.hpp"
#include "../src/eval.hpp"
#include "../src/printer.hpp"
#include "../src/ns.hpp"
#include "../src/value.hpp"
#include "../src/state.hpp"
#include "../src/heap.hpp"
#include <cmath>
#include <csignal>
#include <cstdint>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

using namespace ot;

static State* make_vm() {
  StateConfig cfg;
  cfg.heapBytes = 1 << 20;
  cfg.stackSlots = 1024;
  cfg.maxDepth = 256;
  return State::create(cfg);
}

static Value str_v(State& vm, const char* s) { return make_string(vm, s, (u32)strlen(s)); }

static i32 inline_finalized = 0;
static i32 pointer_finalized = 0;
static u32 pointer_finalizer_ns = 0;

static void finalize_inline(State&, void* payload) { inline_finalized += *(i32*)payload; }

static void finalize_pointer(State& vm, void* payload) {
  pointer_finalized += *(i32*)payload;
  pointer_finalizer_ns = vm.currentNs;
  free(payload);
}

static Value call_core(State& vm, const char* name, std::initializer_list<Value> args) {
  Value fn = ns_resolve(vm, symbol_v(vm.intern.intern(name, (u32)strlen(name))));
  if (fn.tag == Tag::Unwind) return fn;
  u32 base = vm.stack.len;
  for (Value arg : args) vm.push(arg);
  Value result = apply(vm, fn, base, (u32)args.size());
  vm.popTo(base);
  return result;
}

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

TEST_CASE("foreign objects move, compare by identity, and finalize once") {
  inline_finalized = 0;
  pointer_finalized = 0;
  pointer_finalizer_ns = 0;
  State* vm = make_vm();
  u32 inlineType = register_foreign_type(*vm, "test/inline", finalize_inline);
  u32 pointerType = register_foreign_type(*vm, "test/pointer", finalize_pointer);
  CHECK(register_foreign_type(*vm, "test/inline", finalize_inline) == inlineType);

  u32 base = vm->stack.len;
  i32 inlinePayload = 7;
  vm->push(make_foreign_inline(*vm, inlineType, &inlinePayload, sizeof inlinePayload));
  i32* pointerPayload = (i32*)malloc(sizeof *pointerPayload);
  REQUIRE(pointerPayload != nullptr);
  *pointerPayload = 11;
  vm->push(make_foreign_pointer(*vm, pointerType, pointerPayload));

  Value before = vm->stack[base];
  u64 hash = val_hash(*vm, before);
  vm->heap.collect();
  CHECK(vm->stack[base].obj != before.obj);
  CHECK(val_eq(vm->stack[base], vm->stack[base]));
  CHECK(!val_eq(vm->stack[base], vm->stack[base + 1]));
  CHECK(val_equal(*vm, vm->stack[base], vm->stack[base]));
  CHECK(val_hash(*vm, vm->stack[base]) == hash);

  Buf repr;
  print_repr(*vm, vm->stack[base], repr);
  CHECK(std::string(repr.data, repr.len) == "#<test/inline>");

  void* checked = nullptr;
  CHECK(!is_unwind(foreign_check(*vm, "test", vm->stack[base], inlineType, &checked)));
  REQUIRE(checked != nullptr);
  CHECK(*(i32*)checked == 7);
  CHECK(!is_unwind(foreign_release(*vm, "test", vm->stack[base], inlineType)));
  CHECK(inline_finalized == 7);
  CHECK(foreign_dead(vm->stack[base]));
  Value deadUse = foreign_check(*vm, "test", vm->stack[base], inlineType, &checked);
  CHECK(is_unwind(deadUse));
  state_cancel_unwind(*vm);

  vm->popTo(base);  // both objects become collectible
  vm->heap.collect();
  CHECK(inline_finalized == 7);    // explicit release was not repeated
  CHECK(pointer_finalized == 11);  // unreachable pointer payload was finalized

  i32* teardownPayload = (i32*)malloc(sizeof *teardownPayload);
  REQUIRE(teardownPayload != nullptr);
  *teardownPayload = 13;
  vm->push(make_foreign_pointer(*vm, pointerType, teardownPayload));
  u32 teardownNs = vm->currentNs;
  vm->destroy();
  CHECK(pointer_finalized == 24);             // live resources finalize at VM teardown
  CHECK(pointer_finalizer_ns == teardownNs);  // the State is intact during teardown finalization
}

// ---------------------------------------------------------------------------

TEST_CASE("compact dict lifecycle") {
  State* vm = make_vm();
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
    State* vm = make_vm();
    Value table = make_table(*vm);
    TableData* data = as_table(table);
    data->entriesLen = UINT32_MAX;
    data->entriesCap = UINT32_MAX;
    (void)table_put(*vm, table, int_v(1), int_v(2));
  }));
  CHECK(child_aborts([] {
    State* vm = make_vm();
    Value table = make_table(*vm);
    TableData* data = as_table(table);
    data->entriesLen = UINT32_MAX / 2 + 1;
    data->entriesCap = UINT32_MAX / 2 + 1;
    (void)table_put(*vm, table, int_v(1), int_v(2));
  }));
}

// ---------------------------------------------------------------------------

TEST_CASE("table printing preserves insertion order") {
  State* vm = make_vm();
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

TEST_CASE("pair mutation preserves acyclic and table-key invariants") {
  State* vm = make_vm();
  {
    Scope roots(*vm);
    Slot pair = roots.push(make_pair(*vm, int_v(1), null_v()));

    Value r = call_core(*vm, "set-car!", {pair.get(), int_v(2)});
    REQUIRE(r.tag == Tag::Pair);
    CHECK(as_pair(pair.get())->car.i == 2);
    r = call_core(*vm, "set-cdr!", {pair.get(), make_pair(*vm, int_v(3), null_v())});
    REQUIRE(r.tag == Tag::Pair);
    CHECK(as_pair(as_pair(pair.get())->cdr)->car.i == 3);

    r = call_core(*vm, "set-cdr!", {pair.get(), pair.get()});
    CHECK(r.tag == Tag::Unwind);
    state_cancel_unwind(*vm);

    Slot table = roots.push(make_table(*vm));
    table_put(*vm, table.get(), pair.get(), int_v(9));
    r = call_core(*vm, "set-car!", {pair.get(), int_v(4)});
    CHECK(r.tag == Tag::Unwind);
    state_cancel_unwind(*vm);
    CHECK(table_get(*vm, table.get(), pair.get()).i == 9);
  }
  vm->destroy();
}

// ---------------------------------------------------------------------------

TEST_CASE("equality matrix (spec 2.4)") {
  State* vm = make_vm();
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

    // arrays compare structurally
    Slot a1 = roots.push(make_array(*vm, 2));
    Slot a2 = roots.push(make_array(*vm, 2));
    array_push(*vm, a1.get(), int_v(1));
    array_push(*vm, a1.get(), p1.get());
    array_push(*vm, a2.get(), int_v(1));
    array_push(*vm, a2.get(), p2.get());
    CHECK(val_equal(*vm, a1.get(), a2.get()));
    CHECK(val_equal(*vm, a1.get(), a1.get()));
    as_array(a2.get())->items[0] = int_v(2);
    CHECK(!val_equal(*vm, a1.get(), a2.get()));

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
  State* vm = make_vm();
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

TEST_CASE("numeric extensions") {
  State* vm = make_vm();

  CHECK(call_core(*vm, "sqrt", {int_v(9)}).f == doctest::Approx(3.0));
  CHECK(call_core(*vm, "sin", {float_v(0.0)}).f == doctest::Approx(0.0));
  CHECK(call_core(*vm, "cos", {float_v(0.0)}).f == doctest::Approx(1.0));
  CHECK(call_core(*vm, "atan", {int_v(0), int_v(-1)}).f == doctest::Approx(std::atan2(0, -1)));

  Value r = call_core(*vm, "expt", {int_v(2), int_v(10)});
  REQUIRE(r.tag == Tag::Int);
  CHECK(r.i == 1024);
  r = call_core(*vm, "expt", {int_v(4), int_v(-1)});
  REQUIRE(r.tag == Tag::Float);
  CHECK(r.f == doctest::Approx(0.25));

  CHECK(call_core(*vm, "truncate", {float_v(-3.75)}).i == -3);
  CHECK(call_core(*vm, "exact", {float_v(7.0)}).i == 7);
  CHECK(call_core(*vm, "inexact", {int_v(7)}).f == doctest::Approx(7.0));
  CHECK(is_truthy(call_core(*vm, "exact?", {int_v(1)})));
  CHECK(is_truthy(call_core(*vm, "inexact?", {float_v(1.0)})));
  CHECK(is_truthy(call_core(*vm, "integer?", {float_v(1.0)})));
  CHECK(is_truthy(call_core(*vm, "nan?", {float_v(NAN)})));
  CHECK(is_truthy(call_core(*vm, "infinite?", {float_v(INFINITY)})));
  CHECK(is_truthy(call_core(*vm, "finite?", {int_v(1)})));
  CHECK(is_falsy(call_core(*vm, "finite?", {float_v(INFINITY)})));

  r = call_core(*vm, "exact", {float_v(1.5)});
  CHECK(r.tag == Tag::Unwind);
  state_cancel_unwind(*vm);
  vm->destroy();
}

TEST_CASE("clock extensions") {
  State* vm = make_vm();
  Value hz = call_core(*vm, "jiffies-per-second", {});
  REQUIRE(hz.tag == Tag::Int);
  CHECK(hz.i == 1000000000);

  Value j0 = call_core(*vm, "current-jiffy", {});
  Value j1 = call_core(*vm, "current-jiffy", {});
  REQUIRE(j0.tag == Tag::Int);
  REQUIRE(j1.tag == Tag::Int);
  CHECK(j1.i >= j0.i);

  Value seconds = call_core(*vm, "current-second", {});
  REQUIRE(seconds.tag == Tag::Float);
  CHECK(seconds.f > 0.0);
  vm->destroy();
}
