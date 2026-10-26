#include "internal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static ot_value eval(ot_runtime* rt, const char* source) {
  ot_value result;
  if (!ot_eval(rt, source, strlen(source), &result)) {
    fprintf(stderr, "%s\n%s\n", source, ot_error(rt));
    abort();
  }
  return result;
}

static void rejects(ot_runtime* rt, const char* source) {
  ot_value result;
  assert(!ot_eval(rt, source, strlen(source), &result));
}

int main(void) {
  {
    ot_runtime* rt = ot_create(16384, 4096);
    assert(rt);
    ot_gc_stress(rt, true);

    assert(eval(rt, "(def hits 0) (defonce state \"saved state\" "
                    "(begin (set! hits (+ hits 1)) [41]))") == OT_NIL);
    assert(eval(rt, "(defonce state (error \"must not run\"))") == OT_NIL);
    assert(eval(rt, "hits") == ot_int(1));
    assert(eval(rt, "(begin (defonce absent nil) (begin (defonce absent 42))) absent") == OT_NIL);
    assert(eval(rt, "(defonce false #f) (defonce false 42) false") == OT_FALSE);
    assert(eval(rt, "(def existing 42) (defonce existing 99) existing") == ot_int(42));
    assert(eval(rt, "(defonce + (fn (a b) 17)) (+ 1 2)") == ot_int(17));
    assert(eval(rt, "(defonce + (fn (a b) 99)) (+ 1 2)") == ot_int(17));
    ot_destroy(rt);

    rt = ot_create(16384, 4096);
    assert(rt);
    ot_gc_stress(rt, true);
    eval(rt, "(defonce state [0]) (def alias state) (def (tick) (put! state 0 (+ (state 0) 1)) "
             "(state 0)) (def old tick)");
    assert(eval(rt, "(tick)") == ot_int(1));
    eval(rt, "(defonce state [100]) (def (tick) (put! state 0 (+ (state 0) 10)) (state 0))");
    assert(eval(rt, "(tick)") == ot_int(11));
    assert(eval(rt, "(old)") == ot_int(12));
    assert(eval(rt, "(eq? alias state)") == OT_TRUE);
    for (size_t i = 0; i < 1000; i++) {
      assert(eval(rt, "(begin (defonce state (fn (duplicate duplicate) duplicate)) "
                      "(begin (defonce state (missing-function)) (defonce state 2 3)))") == OT_NIL);
      assert(rt->function_count < 16 && rt->constant_count < 32);
    }
    assert(eval(rt, "(state 0)") == ot_int(12));
    rejects(rt, "(defonce retry (error \"first attempt\"))");
    assert(eval(rt, "(defonce retry 42) retry") == ot_int(42));
    rejects(rt, "(def broken (fn (x x) x))");
    assert(eval(rt, "(defonce broken 43) broken") == ot_int(43));
    rejects(rt, "(begin (def partial 44) (def broken-again (fn (x x) x)))");
    assert(eval(rt, "partial") == ot_int(44));
    const char* bad[] = {"(defonce)",
                         "(defonce missing)",
                         "(defonce 1 2)",
                         "(defonce (f x) x)",
                         "(defonce new 2 3)",
                         "(defonce state 1 2 3)",
                         "(defonce state . 1)",
                         "(+ 1 (defonce local 2))",
                         "(fn () (defonce local 2))",
                         "(let () (defonce local 2))",
                         "(if #t (defonce local 2) 3)"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) rejects(rt, bad[i]);
    assert(eval(rt, "(begin (begin) (begin 40 42))") == ot_int(42));
    assert(eval(rt, "(begin)") == OT_NIL);
    printf("defonce/reload tests passed\n");
    ot_destroy(rt);
  }
}
