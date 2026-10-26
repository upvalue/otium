#include "internal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static Value eval(Runtime *rt, const char *source) {
    Value result;
    if (!runtime_eval(rt, source, strlen(source), &result)) {
        fprintf(stderr, "%s: %s\n", source, runtime_error(rt)); abort();
    }
    assert(rt->handler_count == 0 && rt->handler_top == 0 && rt->handler_roots.count == 0);
    return result;
}
int main(void) {
    Runtime *rt = runtime_create(1024 * 1024, 128 * 1024); assert(rt);
    assert(runtime_load_core(rt, "../lib"));
    runtime_gc_stress(rt, true);
    eval(rt, "(def events []) (def (record x) (push! events x)) (def (yes _) #t) (defparam p :default)");
    const char *checks[] = {
        "(nil? (signal 'unhandled))",
        "(= (handler-bind () 42) 42)",
        "(begin (set! events []) (handler-bind ((yes (fn (c) (record :outer)))) (handler-bind ((yes (fn (c) (record :one))) (yes (fn (c) (record :two) (signal :nested)))) (signal :original))) (equal? events [:two :one :outer :one :outer]))",
        "(begin (set! events []) (handler-bind ((yes (fn (c) (record c)))) (try (handler-bind ((yes (fn (c) #f))) (try (error :once) (catch ((fn (_) #f) c) :wrong))) (catch c #f))) (equal? events [:once]))",
        "(begin (set! events []) (handler-bind ((yes (fn (c) (record :outer)))) (handler-bind (((begin (record :pred) (signal :init) yes) (begin (record :handler) (fn (c) (record :inner))))) (signal :body))) (equal? events [:pred :outer :handler :inner :outer]))",
        "(begin (set! events []) (handler-bind ((yes (fn (c) (record (p))))) (with-params ((p :site)) (try (car 42) (catch c :caught)))) (equal? events [:site]))",
        "(begin (set! events []) (try (handler-bind ((yes (fn (c) (record :old) (error :replacement)))) (error :original)) (catch c (record c))) (equal? events [:old :replacement]))",
        "(begin (set! events []) (handler-bind ((yes (fn (c) (record :outer)))) (try (handler-bind ((yes car)) (signal 1)) (catch c :caught))) (equal? events [:outer]))",
        "(begin (set! events []) (handler-bind ((yes (fn (c) (record :outer)))) (handler-bind ((yes (fn (c) (handler-bind ((yes (fn (d) (record :new)))) (signal :nested))))) (signal :original))) (equal? events [:new :outer :outer]))",
        "(begin (set! events []) (handler-bind ((yes (fn (c) (record c)))) (eval '(signal :eval))) (equal? events [:eval]))",
        "(begin (set! events []) (handler-bind ((yes (fn (c) (record (get c :code))))) (try ((fn (x) x)) (catch c nil))) (equal? events [\"E0301\"]))",
        "(begin (set! events []) (handler-bind ((yes (fn (c) (record c)))) (signal #f) (signal nil)) (equal? events [#f nil]))",
        "(begin (set! events []) (handler-bind ((yes (fn (c) (record (list (condition-type c) (condition-message c) (get c :data)))))) (signal \"message\" 1 2)) (equal? events [(list 'error \"message\" [1 2])]))"
    };
    for (size_t i = 0; i < sizeof(checks)/sizeof(*checks); i++) assert(eval(rt, checks[i]) == V_TRUE);
    const char *bad[] = {"(handler-bind)", "(handler-bind ((yes)))", "(handler-bind ((yes yes yes)))", "(signal)", "(signal :x 1)",
        "(handler-bind ((yes (fn (c) nil))) (error :unhandled))"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(*bad); i++) {
        Value ignored; assert(!runtime_eval(rt, bad[i], strlen(bad[i]), &ignored));
        assert(rt->handler_count == 0 && rt->handler_roots.count == 0);
        assert(eval(rt, "(+ 20 22)") == value_int(42));
    }
    eval(rt, "(def hits 0) (def (deep n) (if (= n 0) (signal :bottom) (handler-bind ((yes (fn (c) (set! hits (+ hits 1))))) (deep (- n 1)))))");
    assert(eval(rt, "(deep 4000) hits") == value_int(4000));
    runtime_destroy(rt); puts("signal-site handler tests passed");
}
