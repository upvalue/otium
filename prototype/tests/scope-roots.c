#include "helpers.h"
#include <string.h>

TEST(scopes_survive_collection_under_stress) {
  const char* scope = "(let ((p nil) (n 300)) (while (> n 0) "
                      "(set! p (cons n p)) (set! n (- n 1))) (car p)) ";
  char source[8192] = "(def (churn) ";
  for (size_t i = 0; i < 32; i++) strcat(source, scope);
  strcat(source, "42) (churn)");
  ot_state* rt = new_runtime(16384, 8192);
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

/* A call takes stack slots above the height without writing them. Slots left
 * by earlier frames must not hold pointers from before a collection. */
TEST(fresh_frames_after_collections) {
  ot_state* rt = new_runtime(1024 * 1024, 65536);
  check_int(
      rt,
      "(def (deep n acc) (if (= n 0) acc (cons n (deep (- n 1) (cons acc '()))))) "
      "(def (wide a b c d e f) (let ((x (cons a b)) (y (cons c d)) (z (cons e f))) "
      "(cons x (cons y (cons z '()))))) "
      "(def (mix n) (let loop ((i 0) (out '())) (if (= i n) (length out) "
      "(loop (+ i 1) (cons (wide i (deep (modulo i 7) (cons i '())) i i (fn () i) i) out))))) "
      "(def (tails n) (if (= n 0) 0 (begin (wide 1 2 3 4 5 6) (tails (- n 1))))) 0",
      0);
  ot_gc_stress(rt, true);
  for (int i = 0; i < 3; i++)
    check_int(rt, "(+ (mix 120) (tails 100) (length (deep 50 '())))", 171);
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(scopes_survive_collection_under_stress),
    TEST_ENTRY(fresh_frames_after_collections),
};

TEST_MAIN(tests)
