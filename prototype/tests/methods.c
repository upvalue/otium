#include "helpers.h"

TEST(class_checks_script) {
  ot_state* rt = new_core_runtime();
  ot_gc_stress(rt, true);
  eval_file(rt, "../tests/classes/checks.ot");
  ot_destroy(rt);
}

/* Each check builds on the generics defined by earlier ones. */
TEST(method_snapshots_and_tail_calls) {
  ot_state* rt = new_core_runtime();
  ot_gc_stress(rt, true);
  const char* checks[] = {
      "(defgeneric (snapshot x)) (defmethod (snapshot x) :old) (defmethod (snapshot (x String)) "
      ":excluded) (defmethod (snapshot (x Number)) :next next next) (def saved-next (snapshot 1)) "
      "(and (procedure? saved-next) (eq? (type saved-next) :function) (is-a? saved-next Procedure) "
      "(not (generic? saved-next)))",
      "(remove-method! snapshot (list Object)) (defmethod (snapshot x) :new) (and (eq? (saved-next "
      "2) :old) (eq? (saved-next \"string\") :old) (eq? ((snapshot 1) 2) :new))",
      "(def old-snapshot snapshot) (reset-generic! (snapshot x)) (defmethod (snapshot x) :reset) "
      "(and (not (eq? snapshot old-snapshot)) (eq? (snapshot 2) :reset) (eq? ((old-snapshot 1) 2) "
      ":new) (eq? (saved-next 2) :old))",
      "(defgeneric (tail-next n)) (defmethod (tail-next n) (if (= n 0) #t (tail-next (- n 1)))) "
      "(defmethod (tail-next (n Number)) :next next (next n)) (tail-next 2000)",
      "(defgeneric (around-next x)) (defmethod (around-next x) x) (defmethod :around (around-next "
      "(x Number)) :next next (next (+ x 1))) (= (around-next 41) 42)",
      "(try (saved-next) (catch e #t))",
      "(try (saved-next 1 2) (catch e #t))",
      "(eq? (saved-next 2) :old)"};
  for (size_t i = 0; i < sizeof(checks) / sizeof(*checks); i++) check_true(rt, checks[i]);
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(class_checks_script),
    TEST_ENTRY(method_snapshots_and_tail_calls),
};

TEST_MAIN(tests)
