#include "runtime.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static Value eval(Runtime* rt, const char* source) {
  Value result;
  if (!runtime_eval(rt, source, strlen(source), &result)) {
    fprintf(stderr, "%s: %s\n", source, runtime_error(rt));
    abort();
  }
  return result;
}

int main(void) {
  Runtime* rt = runtime_create(1024 * 1024, 128 * 1024);
  assert(rt);
  assert(runtime_load_expander(rt, "../lib"));
  runtime_gc_stress(rt, true);
  assert(eval(rt, "(defmacro install (name value) `(def ,name ,value)) (let () (install x 7) x)") ==
         value_int(7));
  assert(eval(rt, "(let () (begin (def (f) (g)) (def (g) 3)) (f))") == value_int(3));
  assert(eval(rt, "((fn () (def x 1)))") == V_NIL);
  assert(eval(rt, "(defmacro twice (x) (let ((t (gensym \"t\"))) `(let ((,t ,x)) (+ ,t ,t)))) "
                  "(twice 21)") == value_int(42));
  assert(eval(rt, "(defmacro hygienic (x) #`(let ((tmp# ,x)) (+ tmp# tmp#))) (hygienic 21)") ==
         value_int(42));
  assert(eval(rt, "(let ((x (gensym \"x\"))) (eq? x (string->symbol (symbol->string x))))") ==
         V_FALSE);
  assert(eval(rt, "(cond (#f 1) ((+ 20 22)))") == value_int(42));
  assert(
      eval(rt, "(equal? (list (string-length \"é🙂\") (substring \"aé🙂z\" 1 3)) '(2 \"é🙂\"))") ==
      V_TRUE);
  assert(eval(rt, "(defmacro outer (x) `(inner ,x)) (defmacro inner (x) `(+ ,x 1)) "
                  "(equal? (macroexpand-1 '(outer 2)) '(inner 2))") == V_TRUE);
  assert(eval(rt, "(equal? (macroexpand '(outer 2)) '(+ 2 1))") == V_TRUE);
  assert(eval(rt, "(defmacro distinct () (let ((x (gensym \"x\"))) "
                  "`(let ((,x 42) (,(string->symbol (symbol->string x)) 1)) ,x))) (distinct)") ==
         value_int(42));
  assert(eval(rt, "(equal? (string-append \"é\" \"🙂\" (number->string 42)) \"é🙂42\")") == V_TRUE);
  const char* bad[] = {"(let ((if 1)) if)", "(fn (quote) quote)", "(defmacro begin () 1)",
                       "(fn () (def- x 1))", "(defmacro forever () '(forever)) (forever)"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
    Value result;
    assert(!runtime_eval(rt, bad[i], strlen(bad[i]), &result));
    assert(eval(rt, "(+ 20 22)") == value_int(42));
  }
  runtime_destroy(rt);
  puts("shared expander tests passed");
}
