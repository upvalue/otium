#include "runtime.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>

static Value eval(Runtime* rt, const char* source) {
  Value result;
  if (!runtime_eval(rt, source, strlen(source), &result)) {
    fprintf(stderr, "%s: %s\n", source, runtime_error(rt));
    abort();
  }
  return result;
}

int main(void) {
  Runtime* rt = runtime_create(32768, 8192);
  assert(rt);
  runtime_gc_stress(rt, true);
  assert(eval(rt, "(defmacro twice (x) `(+ ,x ,x))") == V_NIL);
  assert(eval(rt, "(twice 21)") == value_int(42));
  assert(eval(rt, "(def (f x) (twice x)) (f 21)") == value_int(42));
  assert(eval(rt, "(let ((twice (fn (x) x)) (x (twice 7))) x)") == value_int(7));
  assert(eval(rt, "(let ((twice (fn (x) x))) ((fn () (twice 7))))") == value_int(7));
  assert(eval(rt, "(defmacro outer (x) `(inner ,x)) (defmacro inner (x) `(+ ,x 1)) (outer 41)") ==
         value_int(42));
  assert(
      eval(rt, "(defmacro install (name value) `(def ,name ,value)) (install answer 42) answer") ==
      value_int(42));
  assert(eval(rt, "(defmacro add-all args `(+ ,@args)) (add-all 10 20 12)") == value_int(42));
  assert(eval(rt, "(defmacro nothing () nil) (nothing)") == V_NIL);
  assert(eval(rt, "(ns other) (defmacro add (x) `(+ ,x 1)) (ns user) (other/add 41)") ==
         value_int(42));
  assert(eval(rt, "(defmacro twice (x) `(+ ,x 1)) (list (f 21) (twice 41)) (f 21)") ==
         value_int(42));
  assert(eval(rt, "(def twice (fn (x) x)) (twice 7)") == value_int(7));
  assert(eval(rt, "(defmacro empty (x)) (empty 42)") == V_NIL);
  assert(eval(rt, "(defmacro closure-result () (fn () 42)) ((closure-result))") == value_int(42));
  const char* bad[] = {"(defmacro)", "(defmacro bad)", "(outer)",
                       "(defmacro forever () '(forever)) (forever)",
                       "(defmacro cycle () (let ((x (list 1))) (set-cdr! x x) x)) (cycle)"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
    Value result;
    assert(!runtime_eval(rt, bad[i], strlen(bad[i]), &result));
    assert(eval(rt, "(+ 20 22)") == value_int(42));
  }
  for (size_t i = 0; i < 200; i++)
    assert(eval(rt, "(defmacro again (x) `(+ ,x 1)) (again 41)") == value_int(42));
  runtime_destroy(rt);
  puts("macro expansion tests passed");
}
