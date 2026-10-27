#include "helpers.h"
#include <string.h>

/* Each case must evaluate to #t, with collection before every allocation. */
static void nested_evaluation(ot_state* rt) {
  ot_gc_stress(rt, true);
  const char* cases[] = {
      "(let ((a [1 '(+ 1 2)])) (eq? (eval a) a))",
      "(let ((a {:x 42})) (eq? (eval a) a))",
      "(let ((f (fn () 42))) (eq? (eval f) f))",
      "(let ((a [1])) (eq? (eval (list 'quote a)) a))",
      "(begin (defmacro opaque-begin () (list 'begin [1 2] (fn () 42))) (= ((opaque-begin)) 42))",
      "(= (eval '(+ 20 22)) 42)",
      "(= (+ 2 (eval '(eval '(+ 20 20)))) 42)",
      "(begin (def x 42) (let ((x 0)) (= (eval 'x) 42)))",
      "(begin (eval '(begin (def y 40) (set! y (+ y 2)))) (= y 42))",
      "(begin (def saved (eval '(let ((x 40)) (fn (y) (+ x y))))) (= (saved 2) 42))",
      "(let ((kept (list 40 2))) (eval '(cons 1 2)) (= (+ (car kept) (car (cdr kept))) 42))",
      "(eq? (try (eval '(error :original)) (catch c c)) :original)",
      "(= (eval '(try (error 40) (catch c (+ c 2)))) 42)",
      "(try (eval '(if)) (catch c #t))",
      "(try (eval '(fn (x x) x)) (catch c #t))",
      "(= (eval '(+ 40 2)) 42)",
      "(begin (eval '(defmacro plus-one (x) `(+ ,x 1))) (= (eval '(plus-one 41)) 42))",
      "(begin (defmacro nested (x) (eval '(+ 20 22))) (= (nested ignored) 42))",
      "(begin (defmacro nested-closure () (eval '(def survivor (fn () 42))) 0) (nested-closure) "
      "(= (survivor) 42))",
      "(begin (defmacro nested-caught () (try (eval '(if)) (catch c 42))) (= (nested-caught) 42))"};
  for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) check_true(rt, cases[i]);

  /* A macro that defines a global and then fails keeps the definition. */
  const char* failed = "(defmacro retain-then-fail () (eval '(def retained (fn () 'held-symbol))) "
                       "'(if)) (retain-then-fail)";
  ot_value result;
  CHECK(!ot_eval(rt, failed, strlen(failed), &result));
  check_true(rt, "(eq? (retained) 'held-symbol)");

  check_true(rt, "(begin (ns eval-left) (def x 1) (def (f) (eval 'x)) (def e eval) "
                 "(ns eval-right) (def x 42) "
                 "(equal? (list (eval-left/f) (eval-left/e 'x) (eval 'x)) '(1 1 42)))");
  eval(rt, "(ns user)");
  CHECK(ot_call(rt, "eval", (ot_value[]){ot_int(42)}, 1, &result));
  CHECK(result == ot_int(42));
  for (size_t i = 0; i < 100; i++) check_int(rt, "(eval '(let ((x 40)) (+ x 2)))", 42);
  check_true(rt, "(begin (def (recursive-eval) (eval '(recursive-eval))) "
                 "(try (recursive-eval) (catch c #t)))");
  check_int(rt, "(saved 2)", 42);
}

TEST(nested_evaluation_without_core) {
  ot_state* rt = new_runtime(1024 * 1024, 128 * 1024);
  nested_evaluation(rt);
  ot_destroy(rt);
}

TEST(nested_evaluation_with_core) {
  ot_state* rt = new_core_runtime();
  nested_evaluation(rt);
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(nested_evaluation_without_core),
    TEST_ENTRY(nested_evaluation_with_core),
};

TEST_SUITE(evaluation, tests);
