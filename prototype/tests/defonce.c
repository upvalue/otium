#include "helpers.h"
#include "internal.h"

TEST(defonce_evaluates_once) {
  ot_runtime* rt = new_runtime(16384, 4096);
  ot_gc_stress(rt, true);
  CHECK(eval(rt, "(def hits 0) (defonce state \"saved state\" "
                 "(begin (set! hits (+ hits 1)) [41]))") == OT_NIL);
  CHECK(eval(rt, "(defonce state (error \"must not run\"))") == OT_NIL);
  check_int(rt, "hits", 1);
  CHECK(eval(rt, "(begin (defonce absent nil) (begin (defonce absent 42))) absent") == OT_NIL);
  CHECK(eval(rt, "(defonce false #f) (defonce false 42) false") == OT_FALSE);
  check_int(rt, "(def existing 42) (defonce existing 99) existing", 42);
  check_int(rt, "(defonce + (fn (a b) 17)) (+ 1 2)", 17);
  check_int(rt, "(defonce + (fn (a b) 99)) (+ 1 2)", 17);
  ot_destroy(rt);
}

TEST(defonce_keeps_state_across_reload) {
  ot_runtime* rt = new_runtime(16384, 4096);
  ot_gc_stress(rt, true);
  eval(rt, "(defonce state [0]) (def alias state) (def (tick) (put! state 0 (+ (state 0) 1)) "
           "(state 0)) (def old tick)");
  check_int(rt, "(tick)", 1);
  eval(rt, "(defonce state [100]) (def (tick) (put! state 0 (+ (state 0) 10)) (state 0))");
  check_int(rt, "(tick)", 11);
  check_int(rt, "(old)", 12);
  check_true(rt, "(eq? alias state)");
  for (size_t i = 0; i < 1000; i++) {
    CHECK(eval(rt, "(begin (defonce state (fn (duplicate duplicate) duplicate)) "
                   "(begin (defonce state (missing-function)) (defonce state 2 3)))") == OT_NIL);
    CHECK(rt->function_count < 16 && rt->constant_count < 32);
  }
  check_int(rt, "(state 0)", 12);
  check_error(rt, "(defonce retry (error \"first attempt\"))", "first attempt");
  check_int(rt, "(defonce retry 42) retry", 42);
  check_error(rt, "(def broken (fn (x x) x))", "duplicate parameter");
  check_int(rt, "(defonce broken 43) broken", 43);
  check_error(rt, "(begin (def partial 44) (def broken-again (fn (x x) x)))",
              "duplicate parameter");
  check_int(rt, "partial", 44);
  const char* shape = "defonce needs a name, optional docstring and value";
  const char* top = "defonce requires top-level context";

  const struct {
    const char* source;
    const char* message;
  } bad[] = {{"(defonce)", shape},
             {"(defonce missing)", shape},
             {"(defonce 1 2)", shape},
             {"(defonce (f x) x)", shape},
             {"(defonce new 2 3)", "definition needs one value"},
             {"(defonce state 1 2 3)", shape},
             {"(defonce state . 1)", shape},
             {"(+ 1 (defonce local 2))", top},
             {"(fn () (defonce local 2))", top},
             {"(let () (defonce local 2))", top},
             {"(if #t (defonce local 2) 3)", top}};

  for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++)
    check_error(rt, bad[i].source, bad[i].message);
  check_int(rt, "(begin (begin) (begin 40 42))", 42);
  CHECK(eval(rt, "(begin)") == OT_NIL);
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(defonce_evaluates_once),
    TEST_ENTRY(defonce_keeps_state_across_reload),
};

TEST_MAIN(tests)
