#include "helpers.h"

/* Later steps redefine macros and functions from earlier ones. */
TEST(macros_expand_and_redefine) {
  ot_state* rt = new_runtime(32768, 8192);
  ot_gc_stress(rt, true);
  CHECK(eval(rt, "(defmacro twice (x) `(+ ,x ,x))") == OT_NIL);
  check_int(rt, "(twice 21)", 42);
  check_int(rt, "(def (f x) (twice x)) (f 21)", 42);
  check_int(rt, "(let ((twice (fn (x) x)) (x (twice 7))) x)", 7);
  check_int(rt, "(let ((twice (fn (x) x))) ((fn () (twice 7))))", 7);
  check_int(rt, "(defmacro outer (x) `(inner ,x)) (defmacro inner (x) `(+ ,x 1)) (outer 41)", 42);
  check_int(rt, "(defmacro install (name value) `(def ,name ,value)) (install answer 42) answer",
            42);
  check_int(rt, "(defmacro add-all args `(+ ,@args)) (add-all 10 20 12)", 42);
  CHECK(eval(rt, "(defmacro nothing () nil) (nothing)") == OT_NIL);
  check_int(rt, "(ns other) (defmacro add (x) `(+ ,x 1)) (ns user) (other/add 41)", 42);
  check_int(rt, "(defmacro twice (x) `(+ ,x 1)) (list (f 21) (twice 41)) (f 21)", 42);
  check_int(rt, "(def twice (fn (x) x)) (twice 7)", 7);
  CHECK(eval(rt, "(defmacro empty (x)) (empty 42)") == OT_NIL);
  check_int(rt, "(defmacro closure-result () (fn () 42)) ((closure-result))", 42);
  check_error(rt, "(defmacro)", "defmacro needs a name and parameters");
  check_error(rt, "(defmacro bad)", "defmacro needs a name and parameters");
  check_error(rt, "(outer)", "wrong number of arguments");
  check_error(rt, "(defmacro forever () '(forever)) (forever)", "macro expansion limit exceeded");
  check_error(rt, "(defmacro cycle () (let ((x (list 1))) (set-cdr! x x) x)) (cycle)", "cyclic");
  for (size_t i = 0; i < 200; i++) check_int(rt, "(defmacro again (x) `(+ ,x 1)) (again 41)", 42);
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(macros_expand_and_redefine),
};

TEST_SUITE(macros, tests);
