#include "runtime.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static ot_value eval(ot_runtime* rt, const char* source) {
  ot_value result;
  if (!ot_eval(rt, source, strlen(source), &result)) {
    fprintf(stderr, "%s: %s\n", source, ot_error(rt));
    abort();
  }
  return result;
}

int main(void) {
  ot_runtime* rt = ot_create(1024 * 1024, 128 * 1024);
  assert(rt);
  assert(ot_load_expander(rt, "../lib"));
  ot_gc_stress(rt, true);
  assert(eval(rt, "(defmacro install (name value) `(def ,name ,value)) (let () (install x 7) x)") ==
         ot_int(7));
  assert(eval(rt, "(let () (begin (def (f) (g)) (def (g) 3)) (f))") == ot_int(3));
  assert(eval(rt, "((fn () (def x 1)))") == OT_NIL);
  assert(eval(rt, "(defmacro twice (x) (let ((t (gensym \"t\"))) `(let ((,t ,x)) (+ ,t ,t)))) "
                  "(twice 21)") == ot_int(42));
  assert(eval(rt, "(defmacro hygienic (x) #`(let ((tmp# ,x)) (+ tmp# tmp#))) (hygienic 21)") ==
         ot_int(42));
  assert(eval(rt, "(let ((x (gensym \"x\"))) (eq? x (string->symbol (symbol->string x))))") ==
         OT_FALSE);
  assert(eval(rt, "(cond (#f 1) ((+ 20 22)))") == ot_int(42));
  assert(
      eval(rt, "(equal? (list (string-length \"é🙂\") (substring \"aé🙂z\" 1 3)) '(2 \"é🙂\"))") ==
      OT_TRUE);
  assert(eval(rt, "(defmacro outer (x) `(inner ,x)) (defmacro inner (x) `(+ ,x 1)) "
                  "(equal? (macroexpand-1 '(outer 2)) '(inner 2))") == OT_TRUE);
  assert(eval(rt, "(equal? (macroexpand '(outer 2)) '(+ 2 1))") == OT_TRUE);
  assert(eval(rt, "(defmacro distinct () (let ((x (gensym \"x\"))) "
                  "`(let ((,x 42) (,(string->symbol (symbol->string x)) 1)) ,x))) (distinct)") ==
         ot_int(42));
  assert(eval(rt, "(equal? (string-append \"é\" \"🙂\" (number->string 42)) \"é🙂42\")") == OT_TRUE);
  const char* bad[] = {"(let ((if 1)) if)", "(fn (quote) quote)", "(defmacro begin () 1)",
                       "(fn () (def- x 1))", "(defmacro forever () '(forever)) (forever)"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
    ot_value result;
    assert(!ot_eval(rt, bad[i], strlen(bad[i]), &result));
    assert(eval(rt, "(+ 20 22)") == ot_int(42));
  }
  ot_destroy(rt);
  puts("shared expander tests passed");
}
