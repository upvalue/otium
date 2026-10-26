#include "helpers.h"
#include <string.h>

TEST(scopes_survive_collection_under_stress) {
  const char* scope = "(let ((p nil) (n 300)) (while (> n 0) "
                      "(set! p (cons n p)) (set! n (- n 1))) (car p)) ";
  char source[8192] = "(def (churn) ";
  for (size_t i = 0; i < 32; i++) strcat(source, scope);
  strcat(source, "42) (churn)");
  ot_runtime* rt = new_runtime(16384, 8192);
  check_int(rt, source, 42);
  ot_gc_stress(rt, true);
  check_int(rt, "(churn)", 42);
  check_int(rt,
            "(def (escaped) (let ((saved nil)) "
            "(let ((x 40)) (set! saved (fn () (set! x (+ x 1)) x))) "
            "(churn) (saved) (saved))) (escaped)",
            42);
  check_int(rt,
            "(def (loop-captures) (let ((saved []) (i 0)) "
            "(while (< i 10) (def x i) (push! saved (fn () x)) (set! i (+ i 1))) "
            "(churn) (+ ((get saved 0)) ((get saved 9))))) (loop-captures)",
            9);
  check_int(rt, "(let ((p (let ((x (cons 42 nil))) x))) (churn) (car p))", 42);
  check_int(rt,
            "(let ((f (let loop ((n 40)) (if (= n 0) (fn () 42) (loop (- n 1)))))) "
            "(churn) (f))",
            42);
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(scopes_survive_collection_under_stress),
};

TEST_MAIN(tests)
