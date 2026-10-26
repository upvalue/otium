#include "otium.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static ot_value evaluate(ot_runtime* rt, const char* source) {
  ot_value result;
  if (!ot_eval(rt, source, strlen(source), &result)) {
    fprintf(stderr, "%s\n", ot_error(rt));
    abort();
  }
  return result;
}

int main(void) {
  const char* scope = "(let ((p nil) (n 300)) (while (> n 0) "
                      "(set! p (cons n p)) (set! n (- n 1))) (car p)) ";
  char source[8192] = "(def (churn) ";
  for (size_t i = 0; i < 32; i++) strcat(source, scope);
  strcat(source, "42) (churn)");
  {
    ot_runtime* rt = ot_create(16384, 8192);
    assert(rt);

    assert(evaluate(rt, source) == ot_int(42));
    ot_gc_stress(rt, true);
    assert(evaluate(rt, "(churn)") == ot_int(42));
    assert(evaluate(rt, "(def (escaped) (let ((saved nil)) "
                        "(let ((x 40)) (set! saved (fn () (set! x (+ x 1)) x))) "
                        "(churn) (saved) (saved))) (escaped)") == ot_int(42));
    assert(evaluate(rt, "(def (loop-captures) (let ((saved []) (i 0)) "
                        "(while (< i 10) (def x i) (push! saved (fn () x)) (set! i (+ i 1))) "
                        "(churn) (+ ((get saved 0)) ((get saved 9))))) (loop-captures)") ==
           ot_int(9));
    assert(evaluate(rt, "(let ((p (let ((x (cons 42 nil))) x))) (churn) (car p))") ==
           ot_int(42));
    assert(evaluate(rt, "(let ((f (let loop ((n 40)) (if (= n 0) (fn () 42) (loop (- n 1)))))) "
                        "(churn) (f))") == ot_int(42));
    printf("scope root tests passed: collections=%zu\n", ot_get_stats(rt).collections);
    ot_destroy(rt);
  }
}
