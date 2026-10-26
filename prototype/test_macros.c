#include "runtime.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>

static ot_value eval(ot_runtime* rt, const char* source) {
  ot_value result;
  if (!ot_eval(rt, source, strlen(source), &result)) {
    fprintf(stderr, "%s: %s\n", source, ot_error(rt));
    abort();
  }
  return result;
}

int main(void) {
  ot_runtime* rt = ot_create(32768, 8192);
  assert(rt);
  ot_gc_stress(rt, true);
  assert(eval(rt, "(defmacro twice (x) `(+ ,x ,x))") == OT_NIL);
  assert(eval(rt, "(twice 21)") == ot_int(42));
  assert(eval(rt, "(def (f x) (twice x)) (f 21)") == ot_int(42));
  assert(eval(rt, "(let ((twice (fn (x) x)) (x (twice 7))) x)") == ot_int(7));
  assert(eval(rt, "(let ((twice (fn (x) x))) ((fn () (twice 7))))") == ot_int(7));
  assert(eval(rt, "(defmacro outer (x) `(inner ,x)) (defmacro inner (x) `(+ ,x 1)) (outer 41)") ==
         ot_int(42));
  assert(
      eval(rt, "(defmacro install (name value) `(def ,name ,value)) (install answer 42) answer") ==
      ot_int(42));
  assert(eval(rt, "(defmacro add-all args `(+ ,@args)) (add-all 10 20 12)") == ot_int(42));
  assert(eval(rt, "(defmacro nothing () nil) (nothing)") == OT_NIL);
  assert(eval(rt, "(ns other) (defmacro add (x) `(+ ,x 1)) (ns user) (other/add 41)") ==
         ot_int(42));
  assert(eval(rt, "(defmacro twice (x) `(+ ,x 1)) (list (f 21) (twice 41)) (f 21)") ==
         ot_int(42));
  assert(eval(rt, "(def twice (fn (x) x)) (twice 7)") == ot_int(7));
  assert(eval(rt, "(defmacro empty (x)) (empty 42)") == OT_NIL);
  assert(eval(rt, "(defmacro closure-result () (fn () 42)) ((closure-result))") == ot_int(42));
  const char* bad[] = {"(defmacro)", "(defmacro bad)", "(outer)",
                       "(defmacro forever () '(forever)) (forever)",
                       "(defmacro cycle () (let ((x (list 1))) (set-cdr! x x) x)) (cycle)"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
    ot_value result;
    assert(!ot_eval(rt, bad[i], strlen(bad[i]), &result));
    assert(eval(rt, "(+ 20 22)") == ot_int(42));
  }
  for (size_t i = 0; i < 200; i++)
    assert(eval(rt, "(defmacro again (x) `(+ ,x 1)) (again 41)") == ot_int(42));
  ot_destroy(rt);
  puts("macro expansion tests passed");
}
