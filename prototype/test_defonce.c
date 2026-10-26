#include "internal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static Value eval(Runtime *rt, const char *source) {
    Value result;
    if (!runtime_eval(rt, source, strlen(source), &result)) {
        fprintf(stderr, "%s\n%s\n", source, runtime_error(rt));
        abort();
    }
    return result;
}
static void rejects(Runtime *rt, const char *source) {
    Value result;
    assert(!runtime_eval(rt, source, strlen(source), &result));
}
int main(void) {
    const size_t modes[] = {0, 1, 31};
    for (size_t mode = 0; mode < 3; mode++) {
        Runtime *rt = runtime_create(16384, 4096);
        assert(rt);
        runtime_gc_stress(rt, true);
        runtime_native(rt, modes[mode]);
        assert(eval(rt, "(def hits 0) (defonce state \"saved state\" "
            "(begin (set! hits (+ hits 1)) [41]))") == V_NIL);
        assert(eval(rt, "(defonce state (error \"must not run\"))") == V_NIL);
        assert(eval(rt, "hits") == value_int(1));
        assert(eval(rt, "(begin (defonce absent nil) (begin (defonce absent 42))) absent") == V_NIL);
        assert(eval(rt, "(defonce false #f) (defonce false 42) false") == V_FALSE);
        assert(eval(rt, "(def existing 42) (defonce existing 99) existing") == value_int(42));
        assert(eval(rt, "(defonce + (fn (a b) 17)) (+ 1 2)") == value_int(17));
        assert(eval(rt, "(defonce + (fn (a b) 99)) (+ 1 2)") == value_int(17));
        runtime_destroy(rt);

        rt = runtime_create(16384, 4096);
        assert(rt);
        runtime_gc_stress(rt, true); runtime_native(rt, modes[mode]);
        eval(rt, "(defonce state [0]) (def alias state) (def (tick) (put! state 0 (+ (state 0) 1)) (state 0)) (def old tick)");
        assert(eval(rt, "(tick)") == value_int(1));
        eval(rt, "(defonce state [100]) (def (tick) (put! state 0 (+ (state 0) 10)) (state 0))");
        assert(eval(rt, "(tick)") == value_int(11));
        assert(eval(rt, "(old)") == value_int(12));
        assert(eval(rt, "(eq? alias state)") == V_TRUE);
        size_t compiled = runtime_native_functions(rt);
        for (size_t i = 0; i < 1000; i++) {
            assert(eval(rt, "(begin (defonce state (fn (duplicate duplicate) duplicate)) "
                "(begin (defonce state (missing-function)) (defonce state 2 3)))") == V_NIL);
            assert(runtime_native_functions(rt) == compiled);
            assert(rt->function_count < 16 && rt->constant_count < 32);
        }
        assert(eval(rt, "(state 0)") == value_int(12));
        rejects(rt, "(defonce retry (error \"first attempt\"))");
        assert(eval(rt, "(defonce retry 42) retry") == value_int(42));
        rejects(rt, "(def broken (fn (x x) x))");
        assert(eval(rt, "(defonce broken 43) broken") == value_int(43));
        rejects(rt, "(begin (def partial 44) (def broken-again (fn (x x) x)))");
        assert(eval(rt, "partial") == value_int(44));
        const char *bad[] = {"(defonce)", "(defonce missing)", "(defonce 1 2)",
            "(defonce (f x) x)", "(defonce new 2 3)", "(defonce state 1 2 3)",
            "(defonce state . 1)", "(+ 1 (defonce local 2))",
            "(fn () (defonce local 2))", "(let () (defonce local 2))",
            "(if #t (defonce local 2) 3)"};
        for (size_t i = 0; i < sizeof(bad)/sizeof(*bad); i++) rejects(rt, bad[i]);
        assert(eval(rt, "(begin (begin) (begin 40 42))") == value_int(42));
        assert(eval(rt, "(begin)") == V_NIL);
        printf("defonce/reload tests passed: threshold=%zu\n", modes[mode]);
        runtime_destroy(rt);
    }
}
