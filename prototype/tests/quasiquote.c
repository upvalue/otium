#include "helpers.h"
#include "internal.h"
#include <stdlib.h>
#include <string.h>

TEST(templates_build_and_share_structure) {
  ot_state* rt = new_runtime(32768, 4096);
  ot_gc_stress(rt, true);
  const char* checks[] = {
      "(equal? `(a ,(+ 1 2) ,@'(4 5) . tail) '(a 3 4 5 . tail))",
      "(let ((x 42)) (equal? `(a `(b ,x ,,x)) '(a (quasiquote (b (unquote x) (unquote 42))))))",
      "(let ((x 42)) (equal? `(a unquote x) '(a . 42)))",
      "(equal? `(a . ,@'(b c)) '(a unquote-splicing (quote (b c))))",
      "(equal? `[1 ,(+ 1 2)] '(array 1 3))",
      "(let ((x [1 2])) (eq? `(,@x) x))",
      "(let ((x '(1 . 2))) (eq? (cdr `(a ,@x)) x))",
      "(nil? `(,@nil))",
      "(let ((x '(1 2))) (equal? `(,@x ,(begin (set-car! x 9) 3)) '(9 2 3)))",
      "(let ((n 0)) (equal? `(,(begin (set! n (+ n 1)) n) ,(begin (set! n (+ n 1)) n)) '(1 2)))",
      "(begin (def (suffix x) `(,x a b)) (eq? (cdr (suffix 1)) (cdr (suffix 2))))",
      "(begin (def (prefix x) `(a b ,x)) (not (eq? (prefix 1) (prefix 2))))",
      "(let ((x '(1 2))) (let ((y `(,@x 3))) (set-car! y 9) (= (car x) 1)))",
      "(let ((x (cons 1 nil))) (set-cdr! x x) (eq? `(,@x) x))",
      "(let ((x 42)) (equal? ``(,,x) '(quasiquote ((unquote 42)))))",
  };
  /* Definitions used to inspect sharing must be top-level. */
  eval(rt, "(def (fixed) `(a b)) (def (suffix x) `(,x a b)) (def (prefix x) `(a b ,x))");
  for (size_t i = 0; i < sizeof(checks) / sizeof(*checks); i++) check_true(rt, checks[i]);
  check_true(rt, "(eq? (fixed) (fixed))");
  check_true(rt, "(eq? (cdr (suffix 1)) (cdr (suffix 2)))");
  CHECK(eval(rt, "(eq? (prefix 1) (prefix 2))") == OT_FALSE);
  eval(rt, "(def (build n) (let ((result nil)) (while (> n 0) "
           "(set! result `(,n ,@'(2 3) ,n)) (set! n (- n 1))) result))");
  check_true(rt, "(equal? (build 100) '(1 2 3 1))");
  check_true(rt, "(equal? (build 100) '(1 2 3 1))");
  eval(rt, "(def (loop n) (if (= n 0) 42 `(,@(loop (- n 1)))))");
  check_int(rt, "(loop 10000)", 42);
  CHECK(ot_get_stats(rt).max_frames < 8);
  check_error(rt, "`(,@nil 1)", "proper list");
  eval(rt, "(def hits 0)");
  check_error(rt, "`(,@nil ,(begin (set! hits 1) 2))", "proper list");
  check_int(rt, "hits", 1);
  check_error(rt, "`(,@[1 2] 3)", "proper list");
  check_error(rt, "`(,@'(1 . 2) 3)", "proper list");
  check_error(rt, "(let ((x (cons 1 nil))) (set-cdr! x x) `(,@x 2))", "cyclic");
  check_error(rt, "`(unquote)", "operand");
  check_error(rt, "`,@(unquote 1)", "outside");
  check_error(rt, ",1", "outside");
  check_error(rt, "(quasiquote 1 2)", "one datum");
  eval(rt, "(def cons (fn (a b) 99)) (def append (fn (a b) 99))");
  check_true(rt, "(equal? `(a ,@'(b c) ,42) '(a b c 42))");
  ot_destroy(rt);
}

TEST(long_flat_template) {
  ot_state* rt = new_runtime(1024 * 1024, 32768);
  const size_t length = 8000;
  char* source = malloc(length * 3 + 64);
  CHECK(source);
  strcpy(source, "(def x 1) `(");
  for (size_t i = 0; i < length; i++) strcat(source, ",x ");
  strcat(source, ")");
  ot_value list = eval(rt, source);
  size_t seen = 0;
  for (; oti_kind(list, K_PAIR); list = object_fields(list)[1]) {
    CHECK(object_fields(list)[0] == ot_int(1));
    seen++;
  }
  CHECK(list == OT_EMPTY && seen == length);
  free(source);
  ot_destroy(rt);
}

TEST(allocation_failure_recovers) {
  ot_state* rt = new_runtime(2048, 4096);
  char source[512];
  strcpy(source, "`(");
  for (size_t i = 0; i < 200; i++) strcat(source, "1 ");
  strcat(source, ")");
  check_error(rt, source, "heap exhausted");
  check_int(rt, "42", 42);
  ot_gc_stress(rt, true);
  strcpy(source, "(def x '(");
  for (size_t i = 0; i < 40; i++) strcat(source, "1 ");
  strcat(source, ")) `(,@x 9)");
  check_error(rt, source, "heap exhausted");
  check_int(rt, "(car x)", 1);
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(templates_build_and_share_structure),
    TEST_ENTRY(long_flat_template),
    TEST_ENTRY(allocation_failure_recovers),
};

TEST_MAIN(tests)
