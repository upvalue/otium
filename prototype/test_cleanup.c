#include "internal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static Value eval(Runtime *rt, const char *s) {
    Value v;
    if (!runtime_eval(rt, s, strlen(s), &v)) { fprintf(stderr, "%s\n%s\n", s, runtime_error(rt)); abort(); }
    assert(!rt->cleanup_count && !rt->cleanup_roots.count);
    return v;
}
int main(void) {
    Runtime *rt = runtime_create(1024*1024, 128*1024); assert(rt);
    assert(runtime_load_core(rt, "../lib")); runtime_gc_stress(rt, true);
    eval(rt, "(def events []) (def (record x) (push! events x)) (defparam p :default)");
    const char *checks[] = {
        "(begin (set! events []) (= (unwind-protect 42 (record :normal)) 42))",
        "(equal? events [:normal])",
        "(begin (set! events []) (try (unwind-protect (unwind-protect (error :body) (record :inner)) (record :outer)) (catch e (record e))) (equal? events [:inner :outer :body]))",
        "(eq? (try (unwind-protect (error :body) (error :cleanup)) (catch e e)) :cleanup)",
        "(eq? (try (restart-case (unwind-protect (invoke-restart 'done) (error :cleanup)) (done () :wrong)) (catch e e)) :cleanup)",
        "(begin (set! events []) (= (restart-case (eval '(unwind-protect (invoke-restart 'done 42) (record :eval))) (done (v) v)) 42))",
        "(equal? events [:eval])",
        "(begin (set! events []) (= (handler-bind ((error? (fn (c) (unwind-protect (invoke-restart 'done 42) (record :handler))))) (restart-case (unwind-protect (error \"bad\") (record :body)) (done (v) v))) 42))",
        "(equal? events [:handler :body])",
        "(begin (set! events []) (with-params ((p :outer)) (try (unwind-protect (with-params ((p :inner)) (error :bad)) (record (p))) (catch e nil))) (equal? events [:outer]))",
        "(begin (set! events []) (with-params ((p :outer)) (with-params ((p :inner)) (unwind-protect 1 (record (p))))) (equal? events [:inner]))",
        "(begin (set! events []) (try (unwind-protect (try (error :inner) (catch e 42)) (record :once)) (catch e nil)) (equal? events [:once]))",
        "(begin (set! events []) (= (call-with-cleanup (fn () 42) (fn (outcome) (record outcome))) 42))",
        "(equal? events [nil])",
        "(begin (set! events []) (try (call-with-cleanup (fn () (error :body)) (fn (outcome) (record outcome) (error :release))) (catch e (record e))) (equal? events [:body :body]))",
        "(eq? (try (call-with-cleanup (fn () 42) (fn (outcome) (error :release))) (catch e e)) :release)",
        "(begin (set! events []) (= (restart-case (call-with-cleanup (fn () (invoke-restart 'done 42)) (fn (outcome) (record outcome) (error :release))) (done (v) v)) 42))",
        "(equal? events [nil])",
        "(begin (set! events []) (= (with ((a 1 record) (b (+ a 1) record)) 42) 42))",
        "(equal? events [2 1])",
        "(= (restart-case (unwind-protect (error :body) (invoke-restart 'done 42)) (done (v) v)) 42)",
        "(begin (set! events []) (unwind-protect 42 (try (error :cleanup) (catch e (record e)))) (equal? events [:cleanup]))",
        "(eq? (try (unwind-protect (exit 7) (error :cleanup)) (catch e e)) :cleanup)",
        "(= (defer 42) 42)"
    };
    for (size_t i=0; i<sizeof(checks)/sizeof(*checks); i++) {
        if (eval(rt, checks[i]) != V_TRUE) { fprintf(stderr, "failed: %s\n", checks[i]); abort(); }
    }
    const char *quit = "(set! events []) (try (call-with-cleanup (fn () (quit 7)) (fn (o) (record o) (error :release))) (catch e (record :wrong)))";
    Value result; assert(!runtime_eval(rt, quit, strlen(quit), &result));
    int status; assert(runtime_exit_status(rt, &status) && status == 7);
    assert(!rt->cleanup_count && !rt->cleanup_roots.count);
    assert(eval(rt, "(equal? events [:quit])") == V_TRUE);
    assert(!runtime_exit_status(rt, &status));
    eval(rt, "(def (quit-host) (unwind-protect (exit 9) (record :host)))");
    assert(!runtime_call(rt, "quit-host", NULL, 0, &result));
    assert(runtime_exit_status(rt, &status) && status == 9);
    assert(eval(rt, "(equal? events [:quit :host])") == V_TRUE);
    runtime_gc_stress(rt, false);
    eval(rt, "(def (deep n) (unwind-protect (if (= n 0) (error :bottom) (deep (- n 1))) (record n))) (set! events [])");
    assert(eval(rt, "(try (deep 4000) (catch e nil)) (= (length events) 4001)") == V_TRUE);
    runtime_destroy(rt); puts("cleanup and exit transfer tests passed");
}
