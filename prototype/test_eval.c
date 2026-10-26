#include "runtime.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static Value eval(Runtime *rt, const char *source) {
    Value result;
    if (!runtime_eval(rt, source, strlen(source), &result)) {
        fprintf(stderr, "%s: %s\n", source, runtime_error(rt)); abort();
    }
    return result;
}
static void checks(Runtime *rt) {
    runtime_gc_stress(rt, true);
    const char *cases[] = {
        "(let ((a [1 '(+ 1 2)])) (eq? (eval a) a))",
        "(let ((a {:x 42})) (eq? (eval a) a))",
        "(let ((f (fn () 42))) (eq? (eval f) f))",
        "(let ((a [1])) (eq? (eval (list 'quote a)) a))",
        "(begin (defmacro opaque-begin () (list 'begin [1 2] (fn () 42))) (= ((opaque-begin)) 42))",
        "(= (eval '(+ 20 22)) 42)",
        "(= (+ 2 (eval '(eval '(+ 20 20)))) 42)",
        "(begin (def x 42) (let ((x 0)) (= (eval 'x) 42)))",
        "(begin (eval '(begin (def y 40) (set! y (+ y 2)))) (= y 42))",
        "(begin (def saved (eval '(let ((x 40)) (fn (y) (+ x y))))) (= (saved 2) 42))",
        "(let ((kept (list 40 2))) (eval '(cons 1 2)) (= (+ (car kept) (car (cdr kept))) 42))",
        "(eq? (try (eval '(error :original)) (catch c c)) :original)",
        "(= (eval '(try (error 40) (catch c (+ c 2)))) 42)",
        "(try (eval '(if)) (catch c #t))",
        "(try (eval '(fn (x x) x)) (catch c #t))",
        "(= (eval '(+ 40 2)) 42)",
        "(begin (eval '(defmacro plus-one (x) `(+ ,x 1))) (= (eval '(plus-one 41)) 42))",
        "(begin (defmacro nested (x) (eval '(+ 20 22))) (= (nested ignored) 42))",
        "(begin (defmacro nested-closure () (eval '(def survivor (fn () 42))) 0) (nested-closure) (= (survivor) 42))",
        "(begin (defmacro nested-caught () (try (eval '(if)) (catch c 42))) (= (nested-caught) 42))"
    };
    for (size_t i = 0; i < sizeof(cases)/sizeof(*cases); i++) assert(eval(rt, cases[i]) == V_TRUE);
    const char *failed = "(defmacro retain-then-fail () (eval '(def retained (fn () 'held-symbol))) '(if)) (retain-then-fail)";
    Value result;
    assert(!runtime_eval(rt, failed, strlen(failed), &result));
    assert(eval(rt, "(eq? (retained) 'held-symbol)") == V_TRUE);
    assert(eval(rt, "(begin (ns eval-left) (def x 1) (def (f) (eval 'x)) (def e eval) (ns eval-right) (def x 42) (equal? (list (eval-left/f) (eval-left/e 'x) (eval 'x)) '(1 1 42)))") == V_TRUE);
    eval(rt, "(ns user)");
    assert(runtime_call(rt, "eval", (Value[]){value_int(42)}, 1, &result));
    assert(result == value_int(42));
    for (size_t i = 0; i < 100; i++) assert(eval(rt, "(eval '(let ((x 40)) (+ x 2)))") == value_int(42));
    assert(eval(rt, "(begin (def (recursive-eval) (eval '(recursive-eval))) (try (recursive-eval) (catch c #t)))") == V_TRUE);
    assert(eval(rt, "(saved 2)") == value_int(42));
}
int main(void) {
    Runtime *rt = runtime_create(1024 * 1024, 128 * 1024); assert(rt);
    checks(rt); runtime_destroy(rt);
    rt = runtime_create(1024 * 1024, 128 * 1024); assert(rt);
    assert(runtime_load_core(rt, "../lib"));
    checks(rt); runtime_destroy(rt);
    puts("nested evaluation tests passed");
}
