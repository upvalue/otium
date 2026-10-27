#include "helpers.h"

/* Loops, tests and calls that the compiler turns into special code: named lets
 * in tail position become jumps, boolean tests branch directly, and calls to
 * globals read the global in the call instruction. */

TEST(named_let_loops) {
  ot_state* rt = new_runtime(1024 * 1024, 4096);
  ot_gc_stress(rt, true);
  check_int(
      rt, "(def (sum n) (let loop ((i 0) (s 0)) (if (= i n) s (loop (+ i 1) (+ s i))))) (sum 100)",
      4950);
  check_int(rt, "(let loop ((i 0) (s 0)) (if (= i 10) s (loop (+ i 1) (+ s i))))", 45);
  /* Arguments are all evaluated before any variable changes. */
  check_int(rt,
            "(def (swap n) (let loop ((a 1) (b 2) (n n)) "
            "(if (= n 0) (- (* 10 a) b) (loop b a (- n 1))))) (swap 3)",
            19);
  check_int(rt,
            "(let loop ((n 5) (a 0) (b 0)) (if (= n 0) (+ (* 100 a) b) (loop (- n 1) (+ a 1) a)))",
            504);
  check_int(rt,
            "(def (nested n) (let outer ((i 0) (acc 0)) (if (= i n) acc "
            "(outer (+ i 1) (let inner ((j 0) (acc acc)) "
            "(if (= j i) acc (inner (+ j 1) (+ acc j)))))))) (nested 10)",
            120);
  check_int(rt, "(let loop ((i 0) (s 0)) (def d (* i 2)) (if (= i 4) s (loop (+ i 1) (+ s d))))",
            12);
  check_int(rt, "(let loop ((i 0)) (let ((loop (fn (x) (* x 100)))) (loop 2)))", 200);
  ot_destroy(rt);
}

TEST(named_let_names_used_as_values) {
  ot_state* rt = new_runtime(1024 * 1024, 4096);
  ot_gc_stress(rt, true);
  check_int(rt,
            "(def (len l) (let walk ((l l)) (if (null? l) 0 (+ 1 (walk (cdr l)))))) (len '(1 2 3))",
            3);
  check_int(rt, "(let loop ((i 0)) (if (< i 3) ((fn () (loop (+ i 1)))) i))", 3);
  check_int(rt, "(let loop ((i 0)) (if (< i 3) (loop (+ i 1)) (begin (set! loop 5) loop)))", 5);
  check_int(rt,
            "(let ((again ((let loop ((i 0) (n 0)) (if (< i 3) (loop (+ i 1) (+ n 1)) "
            "(if (= n 3) (fn () loop) n)))))) (again 0 5))",
            8);
  check_error(rt, "(let loop ((i 0)) (if (< i 3) (loop) i))", "wrong number of arguments");
  ot_destroy(rt);
}

TEST(named_let_bindings_are_fresh_each_iteration) {
  ot_state* rt = new_runtime(1024 * 1024, 4096);
  ot_gc_stress(rt, true);
  check_int(rt,
            "(def (thunks) (let loop ((i 0) (acc '())) "
            "(if (= i 3) acc (loop (+ i 1) (cons (fn () i) acc))))) "
            "(let ((t (thunks))) (+ ((car t)) (* 10 ((car (cdr t))))))",
            12);
  check_printed(rt,
                "(def (counters) (let loop ((i 0) (acc '())) "
                "(if (= i 3) acc (loop (+ i 1) (cons (fn () (set! i (+ i 10)) i) acc))))) "
                "(let ((fs (counters))) (list ((car fs)) ((car fs)) ((car (cdr fs)))))",
                "(12 22 11)");
  ot_destroy(rt);
}

TEST(while_loops) {
  ot_state* rt = new_runtime(1024 * 1024, 4096);
  check_int(rt, "(let ((i 0) (s 0)) (while (< i 5) (set! s (+ s i)) (set! i (+ i 1))) s)", 10);
  check_int(rt, "(let ((n 0)) (while (> n 0) (set! n 1)) n)", 0);
  check_printed(rt, "(while #f 1)", "nil");
  check_int(rt, "(let ((i 0)) (while (and (< i 10) (not (= i 4))) (set! i (+ i 1))) i)", 4);
  check_int(
      rt, "(let ((i 0) (l '(1 2 3))) (while (pair? l) (set! i (+ i (car l))) (set! l (cdr l))) i)",
      6);
  ot_destroy(rt);
}

TEST(and_or_values) {
  ot_state* rt = new_runtime(1024 * 1024, 4096);
  check_printed(rt, "(and (= 1 1) nil)", "nil");
  check_int(rt, "(and (= 1 1) 5)", 5);
  check_printed(rt, "(and (< 2 1) 5)", "#f");
  check_printed(rt, "(and 1 (< 2 1) 3)", "#f");
  check_printed(rt, "(or (= 1 2) nil)", "nil");
  check_true(rt, "(or (< 1 2) (car 5))");
  check_int(rt, "(or (= 1 2) (null? 5) 7)", 7);
  check_printed(rt, "(def (both a b) (and (pair? a) (pair? b))) (both '(1) 2)", "#f");
  check_true(rt, "(both '(1) '(2))");
  check_true(rt, "(not (and (null? '()) nil))");
  check_printed(rt, "(let ((x (or (eq? 1 2) (not 1)))) x)", "#f");
  ot_destroy(rt);
}

TEST(comparisons_with_constants) {
  ot_state* rt = new_runtime(1024 * 1024, 4096);
  check_int(rt,
            "(def (kind x) (cond ((eq? x 'a) 1) ((eq? 'b x) 2) ((eq? x 3) 3) "
            "((eq? x #\\c) 4) ((eq? x :k) 5) (else 6))) "
            "(+ (kind 'a) (* 10 (kind 'b)) (* 100 (kind 3)) (* 1000 (kind #\\c)) "
            "(* 10000 (kind :k)) (* 100000 (kind \"z\")))",
            654321);
  ot_destroy(rt);
}

TEST(global_calls) {
  ot_state* rt = new_runtime(1024 * 1024, 4096);
  check_int(rt, "(def (f x) (+ x 1)) (def (g y) (f y)) (g 1)", 2);
  check_int(rt, "(def (f x) (* x 10)) (g 1)", 10);
  check_int(rt, "(def (h y) (+ 0 (f (- y 1)))) (h 3)", 20);
  check_int(rt, "(def (len l) (length l)) (len '(1 2))", 2);
  check_int(rt, "(def (len2 l) (+ 1 (length l))) (len2 '(1 2))", 3);
  check_error(rt, "(def (missing) (missing-function 1)) (missing)",
              "unbound name `missing-function`");
  check_error(rt, "(def (missing-tail x) (+ 1 (missing-function x))) (missing-tail 1)",
              "unbound name `missing-function`");
  check_printed(rt, "(def (truth x) (if x #t #f)) (list (truth 1) (truth #f))", "(#t #f)");
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(named_let_loops),
    TEST_ENTRY(named_let_names_used_as_values),
    TEST_ENTRY(named_let_bindings_are_fresh_each_iteration),
    TEST_ENTRY(while_loops),
    TEST_ENTRY(and_or_values),
    TEST_ENTRY(comparisons_with_constants),
    TEST_ENTRY(global_calls),
};

TEST_SUITE(control, tests);
