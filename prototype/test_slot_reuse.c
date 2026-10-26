#include "internal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static Value eval(Runtime* rt, const char* source) {
  Value result;
  if (!runtime_eval(rt, source, strlen(source), &result)) {
    fprintf(stderr, "%s\n", runtime_error(rt));
    abort();
  }
  return result;
}

static Function* function(Runtime* rt, const char* name) {
  for (size_t i = 0; i < rt->global_count; i++)
    if (!strcmp(rt->names[i], name)) {
      assert(rt_kind(rt->globals[i], K_CLOSURE));
      uint32_t id;
      memcpy(&id, object_bytes(rt->globals[i]), sizeof(id));
      return rt->functions[id];
    }
  abort();
}

int main(void) {
  char source[8192] = "(def (many) ";
  for (size_t i = 0; i < 256; i++) strcat(source, "(let ((x 42)) x) ");
  strcat(source, "42) (many)");
  {
    Runtime* rt = runtime_create(16384, 32);
    assert(rt);

    runtime_gc_stress(rt, true);
    assert(eval(rt, source) == value_int(42));
    assert(function(rt, "many")->locals == 1);
    assert(!function(rt, "many")->boxed[0]);
    assert(eval(rt, "(def (mixed) (let ((a nil) (b nil) (c 0)) "
                    "(let ((x 10)) (set! a (fn () (set! x (+ x 1)) x))) "
                    "(let ((x 20)) (set! b (fn () (set! x (+ x 1)) x))) "
                    "(let ((x (cons 30 nil))) (set! c (car x))) "
                    "(+ (a) (a) (b) c))) (mixed)") == value_int(74));
    Function* f = function(rt, "mixed");
    assert(f->locals == 5);
    size_t boxed = 0;
    for (size_t i = 0; i < f->locals; i++) boxed += f->boxed[i];
    assert(boxed == 1);
    assert(eval(rt, "(def (nested) (let ((left (let ((x 40)) (fn () (fn () x)))) "
                    "(right (let ((x 2)) (fn () x)))) (+ ((left)) (right)))) (nested)") ==
           value_int(42));
    assert(eval(rt, "((fn (a &optional (b (let ((t 1)) (+ a t))) "
                    "(c (let ((t 2)) (+ b t)))) (+ a b c)) 10)") == value_int(34));
    assert(eval(rt, "((fn (a . xs) (let ((x 1)) x) (+ a (car xs))) 40 2)") == value_int(42));
    assert(eval(rt, "(let ((result 0) (i 0)) (while (< i 20) "
                    "(def x (+ i 1)) (if (< i 10) (let ((x 1)) (set! result (+ result x))) "
                    "(let ((x 2)) (set! result (+ result x)))) (set! i x)) "
                    "(let ((x 12)) (+ result x)))") == value_int(42));
    printf("slot reuse tests passed: sequential-locals=1 mixed-locals=5 boxed=1\n");
    runtime_destroy(rt);
  }
}
