// test_builtins.c — compact dict lifecycle, equality matrix (spec 2.4),
// int wrap-on-overflow. Needs the full runtime to link (state_create).
#define OT_HEAP_INTERNALS
#include "ctest.h"
#include "../src/builtins.h"
#include "../src/collections.h"
#include "../src/eval.h"
#include "../src/printer.h"
#include "../src/value.h"
#include "../src/state.h"
#include "../src/heap.h"
#include <math.h>
#include <signal.h>
#include <stdint.h>

static State* make_vm(void) {
  StateConfig cfg = state_config_default();
  cfg.heapBytes = 1 << 20;
  cfg.stackSlots = 1024;
  cfg.maxDepth = 256;
  return state_create(&cfg);
}

static Value str_v(State* vm, const char* s) { return make_string(vm, s, (u32)strlen(s)); }

static void repr_value(State* vm, Value value, Buf* out) {
  OT_SCOPE(vm);
  Ref rooted = ot_push(vm);
  ot_set_return(vm, rooted, value);
  ot_repr(vm, rooted, out);
}

// Test convenience: root a transient key/value pair, then put. The pushes
// happen before anything can allocate, so raw arguments are safe here.
static void tput(State* vm, Ref table, Value k, Value v) {
  OT_SCOPE(vm);
  Ref kr = ref_push(vm, k);
  Ref vr = ref_push(vm, v);
  table_put(vm, table, kr, vr);
}

static bool approx(f64 a, f64 b) {
  f64 scale = fabs(b) > 1.0 ? fabs(b) : 1.0;
  return fabs(a - b) <= 1e-9 * scale;
}

static i32 inline_finalized = 0;
static i32 pointer_finalized = 0;
static u32 pointer_finalizer_ns = 0;

static void finalize_inline(State* vm, void* payload) {
  (void)vm;
  inline_finalized += *(i32*)payload;
}

static void finalize_pointer(State* vm, void* payload) {
  pointer_finalized += *(i32*)payload;
  pointer_finalizer_ns = vm->currentNs;
  free(payload);
}

static Value call_core(State* vm, const char* name, const Value* args, u32 n) {
  OT_SCOPE(vm);
  Ref result = ot_push(vm);
  Ref fn = ot_push(vm);
  Ref symbol = ot_push(vm);
  ot_set_symbol(vm, symbol, ot_intern(vm, name, (u32)strlen(name)));
  OT_TRY(ot_resolve(vm, fn, symbol));
  u32 base = ot_top(vm);
  for (u32 i = 0; i < n; i++) {
    Ref arg = ot_push(vm);
    ot_set_return(vm, arg, args[i]);
  }
  OT_TRY(ot_apply(vm, result, fn, base, n));
  return ot_ret(vm, result);
}
#define CALL0(vm, name) call_core((vm), (name), nullptr, 0)
#define CALL(vm, name, ...)                                                                        \
  call_core((vm), (name), (Value[]){__VA_ARGS__},                                                  \
            (u32)(sizeof((Value[]){__VA_ARGS__}) / sizeof(Value)))

// ---------------------------------------------------------------------------

TEST(foreign_objects_move_compare_by_identity_and_finalize_once) {
  inline_finalized = 0;
  pointer_finalized = 0;
  pointer_finalizer_ns = 0;
  State* vm = make_vm();
  u32 inlineType = ot_register_foreign_type(vm, "test/inline", finalize_inline);
  u32 pointerType = ot_register_foreign_type(vm, "test/pointer", finalize_pointer);
  CHECK(ot_register_foreign_type(vm, "test/inline", finalize_inline) == inlineType);

  u32 base = vm->stack.len;
  i32 inlinePayload = 7;
  Ref inlineObject = {state_push(vm, nil_v())};
  ot_make_foreign_inline(vm, inlineObject, inlineType, &inlinePayload, sizeof inlinePayload);
  i32* pointerPayload = (i32*)malloc(sizeof *pointerPayload);
  CHECK(pointerPayload != nullptr);
  *pointerPayload = 11;
  Ref pointerObject = {state_push(vm, nil_v())};
  ot_make_foreign_pointer(vm, pointerObject, pointerType, pointerPayload);

  Value before = vm->stack.data[base];
  u64 hash = val_hash(vm, before);
  heap_collect(&vm->heap);
  CHECK(vm->stack.data[base].obj != before.obj);
  CHECK(val_eq(vm->stack.data[base], vm->stack.data[base]));
  CHECK(!val_eq(vm->stack.data[base], vm->stack.data[base + 1]));
  CHECK(val_equal(vm, vm->stack.data[base], vm->stack.data[base]));
  CHECK(val_hash(vm, vm->stack.data[base]) == hash);

  Buf repr = {0};
  repr_value(vm, vm->stack.data[base], &repr);
  CHECK_MEM(repr.data, repr.len, "#<test/inline>");
  buf_deinit(&repr);

  void* checked = nullptr;
  CHECK(!is_unwind(ot_foreign_check(vm, "test", inlineObject, inlineType, &checked)));
  CHECK(checked != nullptr);
  if (checked) CHECK(*(i32*)checked == 7);
  CHECK(!is_unwind(ot_foreign_release(vm, "test", inlineObject, inlineType)));
  CHECK(inline_finalized == 7);
  CHECK(foreign_dead(vm->stack.data[base]));
  Value deadUse = ot_foreign_check(vm, "test", inlineObject, inlineType, &checked);
  CHECK(is_unwind(deadUse));
  state_cancel_unwind(vm);

  state_pop_to(vm, base);  // both objects become collectible
  heap_collect(&vm->heap);
  CHECK(inline_finalized == 7);    // explicit release was not repeated
  CHECK(pointer_finalized == 11);  // unreachable pointer payload was finalized

  i32* teardownPayload = (i32*)malloc(sizeof *teardownPayload);
  CHECK(teardownPayload != nullptr);
  *teardownPayload = 13;
  Ref teardownObject = {state_push(vm, nil_v())};
  ot_make_foreign_pointer(vm, teardownObject, pointerType, teardownPayload);
  u32 teardownNs = vm->currentNs;
  state_destroy(vm);
  CHECK(pointer_finalized == 24);             // live resources finalize at VM teardown
  CHECK(pointer_finalizer_ns == teardownNs);  // the State is intact during teardown finalization
}

// ---------------------------------------------------------------------------
// compact dict lifecycle (the C++ SUBCASEs, flattened)

TEST(compact_dict_insert_update_delete_reinsert_ordering) {
  State* vm = make_vm();
  u32 root = state_push(vm, make_table(vm));

  Value ka = keyword_v(intern_id(&vm->intern, "a", 1));
  Value kb = keyword_v(intern_id(&vm->intern, "b", 1));
  Value kc = keyword_v(intern_id(&vm->intern, "c", 1));
  tput(vm, (Ref){root}, ka, int_v(1));
  tput(vm, (Ref){root}, kb, int_v(2));
  tput(vm, (Ref){root}, kc, int_v(3));
  CHECK(table_entry_count(vm->stack.data[root]) == 3);
  CHECK(table_get(vm, vm->stack.data[root], kb).i == 2);

  // update keeps position
  tput(vm, (Ref){root}, ka, int_v(10));
  Value k, v;
  u32 cursor = 0;
  CHECK(table_iter_next(vm->stack.data[root], &cursor, &k, &v));
  CHECK(val_eq(k, ka));
  CHECK(v.i == 10);

  // delete (store nil), then re-insert moves to end
  tput(vm, (Ref){root}, ka, nil_v());
  CHECK(table_entry_count(vm->stack.data[root]) == 2);
  CHECK(is_nil(table_get(vm, vm->stack.data[root], ka)));
  cursor = 0;
  CHECK(table_iter_next(vm->stack.data[root], &cursor, &k, &v));
  CHECK(val_eq(k, kb));

  tput(vm, (Ref){root}, ka, int_v(99));
  CHECK(table_entry_count(vm->stack.data[root]) == 3);
  cursor = 0;
  CHECK(table_iter_next(vm->stack.data[root], &cursor, &k, &v));
  CHECK(table_iter_next(vm->stack.data[root], &cursor, &k, &v));
  CHECK(table_iter_next(vm->stack.data[root], &cursor, &k, &v));  // fresh insertion: at the end
  CHECK(val_eq(k, ka));
  CHECK(v.i == 99);

  state_pop_to(vm, root);
  state_destroy(vm);
}

TEST(compact_dict_many_inserts_and_deletes_trigger_compaction) {
  State* vm = make_vm();
  u32 root = state_push(vm, make_table(vm));

  // insert 200 int keys, delete the even ones, verify odds intact + ordered
  for (i64 i = 0; i < 200; i++) tput(vm, (Ref){root}, int_v(i), int_v(i * 2));
  CHECK(table_entry_count(vm->stack.data[root]) == 200);
  for (i64 i = 0; i < 200; i += 2) tput(vm, (Ref){root}, int_v(i), nil_v());
  CHECK(table_entry_count(vm->stack.data[root]) == 100);
  for (i64 i = 1; i < 200; i += 2) {
    Value v = table_get(vm, vm->stack.data[root], int_v(i));
    CHECK(v.tag == Tag_Int);
    if (v.tag == Tag_Int) CHECK(v.i == i * 2);
  }
  // order preserved among survivors
  Value k, v;
  u32 cursor = 0;
  for (u32 j = 0; j < 100; j++) {
    CHECK(table_iter_next(vm->stack.data[root], &cursor, &k, &v));
    CHECK(k.i == (i64)(2 * j + 1));
  }
  CHECK(!table_iter_next(vm->stack.data[root], &cursor, &k, &v));
  // misses stay misses
  CHECK(is_nil(table_get(vm, vm->stack.data[root], int_v(0))));
  CHECK(is_nil(table_get(vm, vm->stack.data[root], int_v(500))));

  state_pop_to(vm, root);
  state_destroy(vm);
}

TEST(compact_dict_structural_keys_equal_strings_and_pairs_hit_same_slot) {
  State* vm = make_vm();
  u32 root = state_push(vm, make_table(vm));

  Value s1 = str_v(vm, "hello");
  u32 r1 = state_push(vm, s1);
  tput(vm, (Ref){root}, s1, int_v(42));
  Value s2 = str_v(vm, "hello");  // different object, equal bytes
  CHECK(table_get(vm, vm->stack.data[root], s2).i == 42);
  state_pop_to(vm, r1);

  Value p1 = make_pair(vm, int_v(1), int_v(2));
  u32 r2 = state_push(vm, p1);
  tput(vm, (Ref){root}, p1, int_v(7));
  Value p2 = make_pair(vm, int_v(1), int_v(2));
  CHECK(table_get(vm, vm->stack.data[root], p2).i == 7);
  state_pop_to(vm, r2);

  state_pop_to(vm, root);
  state_destroy(vm);
}

TEST(compact_dict_float_keys_nan_hits_nan_negzero_hits_zero) {
  State* vm = make_vm();
  u32 root = state_push(vm, make_table(vm));

  tput(vm, (Ref){root}, float_v(NAN), int_v(1));
  CHECK(table_get(vm, vm->stack.data[root], float_v(NAN)).i == 1);
  tput(vm, (Ref){root}, float_v(0.0), int_v(2));
  CHECK(table_get(vm, vm->stack.data[root], float_v(-0.0)).i == 2);
  // type-strict: int 0 is a different key from float 0.0
  CHECK(is_nil(table_get(vm, vm->stack.data[root], int_v(0))));

  state_pop_to(vm, root);
  state_destroy(vm);
}

TEST(compact_dict_mutable_keys_identity_stable_across_gc) {
  State* vm = make_vm();
  u32 root = state_push(vm, make_table(vm));

  Value a1 = make_array(vm, 4);
  u32 r = state_push(vm, a1);
  tput(vm, (Ref){root}, a1, int_v(5));
  Value a2 = make_array(vm, 4);  // distinct identity, same shape
  u32 r2 = state_push(vm, a2);
  CHECK(is_nil(table_get(vm, vm->stack.data[root], a2)));
  heap_collect(&vm->heap);  // key must survive by stamped identity
  a1 = vm->stack.data[r];   // re-fetch possibly-moved values
  CHECK(table_get(vm, vm->stack.data[root], a1).i == 5);
  state_pop_to(vm, r);
  (void)r2;

  state_pop_to(vm, root);
  state_destroy(vm);
}

// ---------------------------------------------------------------------------

static void abort_table_overflow_full(void) {
  State* vm = make_vm();
  u32 root = state_push(vm, make_table(vm));
  TableData* data = as_table(vm->stack.data[root]);
  data->entriesLen = UINT32_MAX;
  data->entriesCap = UINT32_MAX;
  table_put_ii(vm, (Ref){root}, int_v(1), int_v(2));
}

static void abort_table_overflow_half(void) {
  State* vm = make_vm();
  u32 root = state_push(vm, make_table(vm));
  TableData* data = as_table(vm->stack.data[root]);
  data->entriesLen = UINT32_MAX / 2 + 1;
  data->entriesCap = UINT32_MAX / 2 + 1;
  table_put_ii(vm, (Ref){root}, int_v(1), int_v(2));
}

TEST(table_capacity_overflow_guards_abort_before_allocating) {
  CHECK_ABORTS(abort_table_overflow_full());
  CHECK_ABORTS(abort_table_overflow_half());
}

// ---------------------------------------------------------------------------

TEST(table_printing_preserves_insertion_order) {
  State* vm = make_vm();
  // Read the table through its rooted slot: table_put allocates, so a raw
  // local goes stale the moment growth moves the object.
  u32 root = state_push(vm, make_table(vm));
#define T (vm->stack.data[root])
  Value ka = keyword_v(intern_id(&vm->intern, "a", 1));
  Value kb = keyword_v(intern_id(&vm->intern, "b", 1));
  Value kc = keyword_v(intern_id(&vm->intern, "c", 1));

  tput(vm, (Ref){root}, ka, int_v(1));
  tput(vm, (Ref){root}, kb, int_v(2));
  tput(vm, (Ref){root}, kc, int_v(3));

  Buf out = {0};
  repr_value(vm, T, &out);
  CHECK_MEM(out.data, out.len, "{:a 1 :b 2 :c 3}");

  // One tombstone remains in storage: it is skipped, and reinsertion appends.
  tput(vm, (Ref){root}, kb, nil_v());
  buf_clear(&out);
  repr_value(vm, T, &out);
  CHECK_MEM(out.data, out.len, "{:a 1 :c 3}");

  tput(vm, (Ref){root}, kb, int_v(20));
  buf_clear(&out);
  repr_value(vm, T, &out);
  CHECK_MEM(out.data, out.len, "{:a 1 :c 3 :b 20}");
  buf_deinit(&out);

#undef T
  state_pop_to(vm, root);
  state_destroy(vm);
}

TEST(pair_mutation_preserves_acyclic_and_table_key_invariants) {
  State* vm = make_vm();
  {
    OT_SCOPE(vm);
    Ref pair = ref_push(vm, make_pair(vm, int_v(1), null_v()));

    Value r = CALL(vm, "set-car!", ref_get(vm, pair), int_v(2));
    CHECK(r.tag == Tag_Pair);
    CHECK(as_pair(ref_get(vm, pair))->car.i == 2);
    // Root the new pair before the call rather than allocating inside CALL's
    // argument list: the compound literal would read ref_get(vm, pair) into the
    // array and then make_pair could collect and move it.
    Ref fresh = ref_push(vm, make_pair(vm, int_v(3), null_v()));
    r = CALL(vm, "set-cdr!", ref_get(vm, pair), ref_get(vm, fresh));
    CHECK(r.tag == Tag_Pair);
    CHECK(as_pair(as_pair(ref_get(vm, pair))->cdr)->car.i == 3);

    r = CALL(vm, "set-cdr!", ref_get(vm, pair), ref_get(vm, pair));
    CHECK(r.tag == Tag_Unwind);
    state_cancel_unwind(vm);

    Ref table = ref_push(vm, make_table(vm));
    tput(vm, table, ref_get(vm, pair), int_v(9));
    r = CALL(vm, "set-car!", ref_get(vm, pair), int_v(4));
    CHECK(r.tag == Tag_Unwind);
    state_cancel_unwind(vm);
    CHECK(table_get(vm, ref_get(vm, table), ref_get(vm, pair)).i == 9);
  }
  state_destroy(vm);
}

// ---------------------------------------------------------------------------

TEST(equality_matrix_spec_2_4) {
  State* vm = make_vm();
  {
    OT_SCOPE(vm);

    // type-strict across the board
    CHECK(!val_equal(vm, int_v(1), float_v(1.0)));
    CHECK(!val_equal(vm, nil_v(), null_v()));
    CHECK(!val_equal(vm, nil_v(), bool_v(false)));
    CHECK(!val_equal(vm, null_v(), bool_v(false)));

    // immediates
    CHECK(val_equal(vm, int_v(42), int_v(42)));
    CHECK(val_equal(vm, bool_v(true), bool_v(true)));
    CHECK(!val_equal(vm, bool_v(true), bool_v(false)));

    // floats: NaN equals NaN, 0.0 equals -0.0
    CHECK(val_equal(vm, float_v(NAN), float_v(NAN)));
    CHECK(val_equal(vm, float_v(0.0), float_v(-0.0)));

    // symbols/keywords interned
    u32 id = intern_id(&vm->intern, "foo", 3);
    CHECK(val_equal(vm, symbol_v(id), symbol_v(id)));
    CHECK(!val_equal(vm, symbol_v(id), keyword_v(id)));  // different types

    // strings: deep structural
    Ref s1 = ref_push(vm, str_v(vm, "abc"));
    Ref s2 = ref_push(vm, str_v(vm, "abc"));
    CHECK(val_equal(vm, ref_get(vm, s1), ref_get(vm, s2)));
    CHECK(!val_eq(ref_get(vm, s1), ref_get(vm, s2)));  // distinct objects
    CHECK(val_eq(ref_get(vm, s1), ref_get(vm, s1)));

    // pairs: recursive
    Ref p1 = ref_push(vm, make_pair(vm, int_v(1), make_pair(vm, str_v(vm, "x"), null_v())));
    Ref p2 = ref_push(vm, make_pair(vm, int_v(1), make_pair(vm, str_v(vm, "x"), null_v())));
    CHECK(val_equal(vm, ref_get(vm, p1), ref_get(vm, p2)));
    CHECK(!val_eq(ref_get(vm, p1), ref_get(vm, p2)));

    // arrays compare structurally
    Ref a1 = ref_push(vm, make_array(vm, 2));
    Ref a2 = ref_push(vm, make_array(vm, 2));
    array_push_im(vm, a1, int_v(1));
    array_push(vm, a1, p1);
    array_push_im(vm, a2, int_v(1));
    array_push(vm, a2, p2);
    CHECK(val_equal(vm, ref_get(vm, a1), ref_get(vm, a2)));
    CHECK(val_equal(vm, ref_get(vm, a1), ref_get(vm, a1)));
    array_items(ref_get(vm, a2))[0] = int_v(2);
    CHECK(!val_equal(vm, ref_get(vm, a1), ref_get(vm, a2)));

    // hashes agree with equal?
    CHECK(val_hash(vm, ref_get(vm, s1)) == val_hash(vm, ref_get(vm, s2)));
    CHECK(val_hash(vm, ref_get(vm, p1)) == val_hash(vm, ref_get(vm, p2)));
    CHECK(val_hash(vm, float_v(0.0)) == val_hash(vm, float_v(-0.0)));
    CHECK(val_hash(vm, float_v(NAN)) == val_hash(vm, float_v(NAN)));
    CHECK(val_hash(vm, int_v(1)) != val_hash(vm, float_v(1.0)));  // type-strict
  }
  state_destroy(vm);
}

// ---------------------------------------------------------------------------

TEST(arithmetic_wraps_twos_complement) {
  State* vm = make_vm();
  const i64 MAX = INT64_MAX, MIN = INT64_MIN;

  // (+ MAX 1) wraps to MIN
  u32 b = vm->stack.len;
  state_push(vm, int_v(MAX));
  state_push(vm, int_v(1));
  Value r = nat_add(vm, b, 2);
  state_pop_to(vm, b);
  CHECK(r.tag == Tag_Int);
  CHECK(r.i == MIN);

  // (- MIN 1) wraps to MAX
  state_push(vm, int_v(MIN));
  state_push(vm, int_v(1));
  r = nat_sub(vm, b, 2);
  state_pop_to(vm, b);
  CHECK(r.i == MAX);

  // (- MIN) wraps to MIN (negation overflow)
  state_push(vm, int_v(MIN));
  r = nat_sub(vm, b, 1);
  state_pop_to(vm, b);
  CHECK(r.i == MIN);

  // (* MAX 2) wraps to -2
  state_push(vm, int_v(MAX));
  state_push(vm, int_v(2));
  r = nat_mul(vm, b, 2);
  state_pop_to(vm, b);
  CHECK(r.i == -2);

  // float contamination
  state_push(vm, int_v(1));
  state_push(vm, float_v(0.5));
  r = nat_add(vm, b, 2);
  state_pop_to(vm, b);
  CHECK(r.tag == Tag_Float);
  CHECK(approx(r.f, 1.5));

  // exact int division stays int; inexact goes float; div by zero unwinds
  state_push(vm, int_v(6));
  state_push(vm, int_v(2));
  r = nat_div(vm, b, 2);
  state_pop_to(vm, b);
  CHECK(r.tag == Tag_Int);
  CHECK(r.i == 3);

  state_push(vm, int_v(7));
  state_push(vm, int_v(2));
  r = nat_div(vm, b, 2);
  state_pop_to(vm, b);
  CHECK(r.tag == Tag_Float);
  CHECK(approx(r.f, 3.5));

  state_push(vm, int_v(1));
  state_push(vm, int_v(0));
  r = nat_div(vm, b, 2);
  state_pop_to(vm, b);
  CHECK(r.tag == Tag_Unwind);
  vm->unwindCondition = nil_v();  // clear for cleanliness

  state_destroy(vm);
}

TEST(numeric_extensions) {
  State* vm = make_vm();

  CHECK(approx(CALL(vm, "sqrt", int_v(9)).f, 3.0));
  CHECK(approx(CALL(vm, "sin", float_v(0.0)).f, 0.0));
  CHECK(approx(CALL(vm, "cos", float_v(0.0)).f, 1.0));
  CHECK(approx(CALL(vm, "atan", int_v(0), int_v(-1)).f, atan2(0, -1)));

  Value r = CALL(vm, "expt", int_v(2), int_v(10));
  CHECK(r.tag == Tag_Int);
  CHECK(r.i == 1024);
  r = CALL(vm, "expt", int_v(4), int_v(-1));
  CHECK(r.tag == Tag_Float);
  CHECK(approx(r.f, 0.25));

  CHECK(CALL(vm, "truncate", float_v(-3.75)).i == -3);
  CHECK(CALL(vm, "exact", float_v(7.0)).i == 7);
  CHECK(approx(CALL(vm, "inexact", int_v(7)).f, 7.0));
  CHECK(is_truthy(CALL(vm, "exact?", int_v(1))));
  CHECK(is_truthy(CALL(vm, "inexact?", float_v(1.0))));
  CHECK(is_truthy(CALL(vm, "integer?", float_v(1.0))));
  CHECK(is_truthy(CALL(vm, "nan?", float_v(NAN))));
  CHECK(is_truthy(CALL(vm, "infinite?", float_v(INFINITY))));
  CHECK(is_truthy(CALL(vm, "finite?", int_v(1))));
  CHECK(is_falsy(CALL(vm, "finite?", float_v(INFINITY))));

  r = CALL(vm, "exact", float_v(1.5));
  CHECK(r.tag == Tag_Unwind);
  state_cancel_unwind(vm);
  state_destroy(vm);
}

TEST(clock_extensions) {
  State* vm = make_vm();
  Value hz = CALL0(vm, "jiffies-per-second");
  CHECK(hz.tag == Tag_Int);
  CHECK(hz.i == 1000000000);

  Value j0 = CALL0(vm, "current-jiffy");
  Value j1 = CALL0(vm, "current-jiffy");
  CHECK(j0.tag == Tag_Int);
  CHECK(j1.tag == Tag_Int);
  CHECK(j1.i >= j0.i);

  Value seconds = CALL0(vm, "current-second");
  CHECK(seconds.tag == Tag_Float);
  CHECK(seconds.f > 0.0);
  state_destroy(vm);
}
