#include "internal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static Value eval(Runtime* rt, const char* source) {
  Value result;
  if (!runtime_eval(rt, source, strlen(source), &result)) {
    fprintf(stderr, "%s\n%s\n", source, runtime_error(rt));
    abort();
  }
  return result;
}

static void rejects(Runtime* rt, const char* source, const char* message) {
  Value result;
  assert(!runtime_eval(rt, source, strlen(source), &result));
  assert(strstr(runtime_error(rt), message));
}

int main(void) {
  {
    Runtime* rt = runtime_create(32768, 4096);
    assert(rt);
    runtime_gc_stress(rt, true);
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
    for (size_t i = 0; i < sizeof(checks) / sizeof(*checks); i++) {
      assert(eval(rt, checks[i]) == V_TRUE);
    }
    assert(eval(rt, "(eq? (fixed) (fixed))") == V_TRUE);
    assert(eval(rt, "(eq? (cdr (suffix 1)) (cdr (suffix 2)))") == V_TRUE);
    assert(eval(rt, "(eq? (prefix 1) (prefix 2))") == V_FALSE);
    eval(rt, "(def (build n) (let ((result nil)) (while (> n 0) "
             "(set! result `(,n ,@'(2 3) ,n)) (set! n (- n 1))) result))");
    assert(eval(rt, "(equal? (build 100) '(1 2 3 1))") == V_TRUE);
    assert(eval(rt, "(equal? (build 100) '(1 2 3 1))") == V_TRUE);
    eval(rt, "(def (loop n) (if (= n 0) 42 `(,@(loop (- n 1)))))");
    assert(eval(rt, "(loop 10000)") == value_int(42));
    assert(runtime_max_frames(rt) < 8);
    rejects(rt, "`(,@nil 1)", "proper list");
    eval(rt, "(def hits 0)");
    rejects(rt, "`(,@nil ,(begin (set! hits 1) 2))", "proper list");
    assert(eval(rt, "hits") == value_int(1));
    rejects(rt, "`(,@[1 2] 3)", "proper list");
    rejects(rt, "`(,@'(1 . 2) 3)", "proper list");
    rejects(rt, "(let ((x (cons 1 nil))) (set-cdr! x x) `(,@x 2))", "cyclic");
    rejects(rt, "`(unquote)", "operand");
    rejects(rt, "`,@(unquote 1)", "outside");
    rejects(rt, ",1", "outside");
    rejects(rt, "(quasiquote 1 2)", "one datum");
    eval(rt, "(def cons (fn (a b) 99)) (def append (fn (a b) 99))");
    assert(eval(rt, "(equal? `(a ,@'(b c) ,42) '(a b c 42))") == V_TRUE);
    printf("quasiquote tests passed\n");
    runtime_destroy(rt);
  }
  Runtime* rt = runtime_create(1024 * 1024, 32768);
  assert(rt);
  const size_t length = 8000;
  char* source = malloc(length * 3 + 64);
  assert(source);
  strcpy(source, "(def x 1) `(");
  for (size_t i = 0; i < length; i++) strcat(source, ",x ");
  strcat(source, ")");
  Value list = eval(rt, source);
  size_t seen = 0;
  for (; rt_kind(list, K_PAIR); list = object_fields(list)[1]) {
    assert(object_fields(list)[0] == value_int(1));
    seen++;
  }
  assert(list == V_EMPTY && seen == length);
  runtime_destroy(rt);
  rt = runtime_create(2048, 4096);
  assert(rt);
  strcpy(source, "`(");
  for (size_t i = 0; i < 200; i++) strcat(source, "1 ");
  strcat(source, ")");
  rejects(rt, source, "heap exhausted");
  assert(eval(rt, "42") == value_int(42));
  runtime_gc_stress(rt, true);
  strcpy(source, "(def x '(");
  for (size_t i = 0; i < 40; i++) strcat(source, "1 ");
  strcat(source, ")) `(,@x 9)");
  rejects(rt, source, "heap exhausted");
  assert(eval(rt, "(car x)") == value_int(1));
  runtime_destroy(rt);
  free(source);
  puts("long flat template and allocation-failure recovery passed");
}
