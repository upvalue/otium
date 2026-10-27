#include "helpers.h"
#include <string.h>

/* Later checks build on the generics defined by earlier ones. */
TEST(core_bootstrap_and_primary_generics) {
  ot_state* rt = new_core_runtime();
  ot_gc_stress(rt, true);
  const char* checks[] = {
      "(begin (defonce map #`(answer 42)) (equal? map '(user/answer 42)))",
      "(begin (ns left) (def (f) x) (def x 42) (ns user) (= (left/f) 42))",
      "(eq? (dispatch-type 42) Number)",
      "(eq? (dispatch-type []) Vector)",
      "(eq? (dispatch-type Number) TypeDescriptor)",
      "(class-subtype? Number Object)",
      "(not (class-subtype? Object Number))",
      "(is-a? 42 Object)",
      "(begin (defgeneric (sample x)) (def saved sample) (defmethod (sample x) 1) "
      "(defmethod (sample (x Number)) (+ x 1)) (= (sample 41) 42))",
      "(= (sample \"text\") 1)",
      "(= (apply sample '(41)) 42)",
      "(begin (defgeneric (sample x)) (eq? sample saved))",
      "(begin (defmethod (sample (x Number)) (+ x 2)) (= (saved 40) 42))",
      "(= (length (generic-methods sample)) 2)",
      "(begin (def descriptions (generic-methods sample)) (set-car! (car descriptions) 'changed) "
      "(eq? (car (car (generic-methods sample))) 'primary))",
      "(begin (defgeneric (ambiguous x y)) (defmethod (ambiguous (x Number) y) 1) "
      "(defmethod (ambiguous x (y Number)) 2) (try (ambiguous 1 2) (catch c #t)))",
      "(begin (defmethod (ambiguous (x Number) (y Number)) 42) (= (ambiguous 1 2) 42))",
      "(begin (defgeneric (none x)) (try (none 1) (catch c #t)))",
      "(try (sample) (catch c #t))",
      "(equal? (array-to-list [1 2 3]) '(1 2 3))",
      "(equal? (read-string \"(1 :two)\") '(1 :two))",
      "(begin (defmacro alias-source (x) `(+ ,x 1)) (def alias alias-source) (= (alias 41) 42))"};
  for (size_t i = 0; i < sizeof(checks) / sizeof(*checks); i++) check_true(rt, checks[i]);
  for (size_t i = 0; i < 200; i++)
    check_int(rt, "(defmethod (sample (x Number)) (+ x 2)) (sample 40)", 42);

  /* Rejected definitions leave the existing generic working. */
  const char* bad[] = {"(defgeneric (sample x y))", "(defgeneric (bad x x))",
                       "(defmethod (sample (x 1)) x)", "(defmethod (sample x y) x)"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
    ot_value result;
    if (ot_eval(rt, bad[i], strlen(bad[i]), &result))
      test_fail(__FILE__, __LINE__, "%s: expected an error", bad[i]);
    check_int(rt, "(sample 40)", 42);
  }
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(core_bootstrap_and_primary_generics),
};

TEST_SUITE(generics, tests);
