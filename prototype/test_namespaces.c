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
    Value value;
    assert(!runtime_eval(rt, source, strlen(source), &value));
}
int main(void) {
    {
        Runtime *rt = runtime_create(32768, 4096);
        assert(rt);
        size_t initial_spaces=rt->namespace_count;
        runtime_gc_stress(rt, true);
        assert(eval(rt, "(def user-value 40) (ns left) (def value 1) "
            "(def (read) value) (def saved read) (ns right) (def value 2) "
            "(+ (left/read) value)") == value_int(3));
        assert(eval(rt, "(set! left/value 41) (left/read)") == value_int(41));
        assert(eval(rt, "(ns left) (def (read) (+ value 1)) (ns right) "
            "(+ (left/read) (left/saved))") == value_int(83));
        assert(eval(rt, "(ns user) user-value") == value_int(40));
        assert(eval(rt, "(ns left) (defonce state [1]) (ns right) (defonce state [2]) "
            "(ns left) (defonce state [99]) (get state 0)") == value_int(1));
        assert(eval(rt, "(ns right) (defonce state [99]) (get state 0)") == value_int(2));
        assert(eval(rt, "(ns left) (def (imported x) (+ x 1)) "
            "(def + (fn (a b) 99)) (def (local x) (+ x 1)) "
            "(list (imported 1) (local 1))") != 0);
        assert(eval(rt, "(ns right) (+ (left/imported 1) (left/local 1))") == value_int(101));
        assert(eval(rt, "(ns left) (def + (fn (a b) 17)) (local 1)") == value_int(17));
        assert(eval(rt, "(imported 1)") == value_int(2));
        assert(eval(rt, "(ns right) (+ 20 22)") == value_int(42));
        assert(eval(rt, "(ns left) (otium.core/+ 20 22)") == value_int(42));
        assert(eval(rt, "(ns right) (def left/value 77) left/value") == value_int(77));
        assert(eval(rt, "(ns left) value") == value_int(41));
        assert(eval(rt, "(ns forward) (def (read) later) (def later 42) (read)") == value_int(42));
        assert(eval(rt, "(ns future) (ns forward) (def (foreign-read) future/value) "
            "(ns future) (def value 43) (forward/foreign-read)") == value_int(43));
        assert(eval(rt, "(ns forward) (def lexical (let ((value 44)) (fn () value))) "
            "(ns future) (forward/lexical)") == value_int(44));
        assert(eval(rt, "(ns otium.core) (def original-add +) "
            "(def + (fn (a b) (original-add a b 10))) (ns future) (left/imported 1)") == value_int(12));
        assert(eval(rt, "(left/local 1)") == value_int(17));
        eval(rt, "(ns otium.core) (def + original-add) (ns future)");
        assert(eval(rt, "(ns ffi) (null? null)") == V_TRUE);
        assert(eval(rt, "(ns future) (ffi/null? ffi/null)") == V_TRUE);
        Value result;
        assert(runtime_call(rt, "left/imported", (Value[]){value_int(41)}, 1, &result));
        assert(result == value_int(42));
        size_t globals = rt->global_count;
        assert(!runtime_call(rt, "no-such-function", NULL, 0, &result));
        assert(rt->global_count == globals);
        for (size_t i = 0; i < 1000; i++) {
            eval(rt, "(ns left) (defonce state (fn (x x) x)) (ns right) (defonce state (error \"skipped\"))");
            assert(rt->namespace_count == initial_spaces + 4);
            assert(rt->global_count == globals);
        }
        const char *bad[] = {"(ns)", "(ns 1)", "(ns x y)", "(ns x . y)",
            "(fn () (ns x))", "(let () (ns x))", "(if #t (ns x) nil)",
            "(ns x (:require y))"};
        for (size_t i = 0; i < sizeof(bad)/sizeof(*bad); i++) rejects(rt, bad[i]);
        assert(eval(rt, "(ns right) value") == value_int(2));
        printf("namespace tests passed: spaces=%zu globals=%zu\n",
            rt->namespace_count, rt->global_count);
        runtime_destroy(rt);
    }
}
