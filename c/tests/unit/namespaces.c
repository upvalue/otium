#include "helpers.h"
#include "internal.h"

/* Every step builds on the namespaces defined by the ones before it. */
TEST(namespaces_isolate_and_share_definitions) {
  ot_state* rt = new_runtime(32768, 4096);
  size_t initial_spaces = rt->namespace_count;
  ot_gc_stress(rt, true);
  check_int(rt,
            "(def user-value 40) (ns left) (def value 1) "
            "(def (read) value) (def saved read) (ns right) (def value 2) "
            "(+ (left/read) value)",
            3);
  check_int(rt, "(set! left/value 41) (left/read)", 41);
  check_int(rt,
            "(ns left) (def (read) (+ value 1)) (ns right) "
            "(+ (left/read) (left/saved))",
            83);
  check_int(rt, "(ns user) user-value", 40);
  check_int(rt,
            "(ns left) (defonce state [1]) (ns right) (defonce state [2]) "
            "(ns left) (defonce state [99]) (get state 0)",
            1);
  check_int(rt, "(ns right) (defonce state [99]) (get state 0)", 2);
  CHECK(eval(rt, "(ns left) (def (imported x) (+ x 1)) "
                 "(def + (fn (a b) 99)) (def (local x) (+ x 1)) "
                 "(list (imported 1) (local 1))") != 0);
  check_int(rt, "(ns right) (+ (left/imported 1) (left/local 1))", 101);
  check_int(rt, "(ns left) (def + (fn (a b) 17)) (local 1)", 17);
  check_int(rt, "(imported 1)", 2);
  check_int(rt, "(ns right) (+ 20 22)", 42);
  check_int(rt, "(ns left) (otium.core/+ 20 22)", 42);
  check_int(rt, "(ns right) (def left/value 77) left/value", 77);
  check_int(rt, "(ns left) value", 41);
  check_int(rt, "(ns forward) (def (read) later) (def later 42) (read)", 42);
  check_int(rt,
            "(ns future) (ns forward) (def (foreign-read) future/value) "
            "(ns future) (def value 43) (forward/foreign-read)",
            43);
  check_int(rt,
            "(ns forward) (def lexical (let ((value 44)) (fn () value))) "
            "(ns future) (forward/lexical)",
            44);
  /* Inlined builtins are immutable; namespaces can still shadow them. */
  check_error(rt, "(ns otium.core) (def + (fn (a b) 10))", "cannot redefine builtin");
  check_error(rt, "(ns otium.core) (set! + (fn (a b) 10))", "cannot redefine builtin");
  check_int(rt, "(ns future) (left/imported 1)", 2);
  check_int(rt, "(left/local 1)", 17);
  eval(rt, "(ns future)");
  check_true(rt, "(ns otium.ffi) (null? null)");
  check_true(rt, "(ns future) (otium.ffi/null? otium.ffi/null)");
  ot_value result;
  CHECK(ot_call(rt, "left/imported", (ot_value[]){ot_int(41)}, 1, &result));
  CHECK(result == ot_int(42));
  size_t globals = rt->global_count;
  CHECK(!ot_call(rt, "no-such-function", NULL, 0, &result));
  CHECK_EQ_INT(rt->global_count, globals);
  for (size_t i = 0; i < 1000; i++) {
    eval(rt,
         "(ns left) (defonce state (fn (x x) x)) (ns right) (defonce state (error \"skipped\"))");
    CHECK_EQ_INT(rt->namespace_count, initial_spaces + 4);
    CHECK_EQ_INT(rt->global_count, globals);
  }
  const char* symbol = "ns needs a namespace symbol";
  const char* nested = "form not implemented by the compiler";

  const struct {
    const char* source;
    const char* message;
  } bad[] = {{"(ns)", symbol},
             {"(ns 1)", symbol},
             {"(ns x y)", "ns only accepts require clauses"},
             {"(ns x . y)", symbol},
             {"(fn () (ns x))", nested},
             {"(let () (ns x))", nested},
             {"(if #t (ns x) nil)", nested},
             {"(ns x (:require y))", "module file not found"}};

  for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++)
    check_error(rt, bad[i].source, bad[i].message);
  check_int(rt, "(ns right) value", 2);
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(namespaces_isolate_and_share_definitions),
};

TEST_SUITE(namespaces, tests);
