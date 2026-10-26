#include "internal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static ot_value eval(ot_runtime* rt, const char* s) {
  ot_value v;
  if (!ot_eval(rt, s, strlen(s), &v)) {
    fprintf(stderr, "%s\n%s\n", s, ot_error(rt));
    abort();
  }
  assert(!rt->restart_scope_count && !rt->restart_roots.count && !rt->restart_pending);
  return v;
}

int main(void) {
  ot_runtime* rt = ot_create(1024 * 1024, 128 * 1024);
  assert(rt);
  assert(ot_load_core(rt, "../lib"));
  ot_gc_stress(rt, true);
  const char* checks[] = {
      "(= (restart-case (+ 1 2) (give (v) v)) 3)",
      "(= (handler-bind ((error? (fn (e) (invoke-restart 'give 42)))) (restart-case (+ 1000 (error "
      "\"bad\")) (give (v) v))) 42)",
      "(eq? (restart-case (restart-case (invoke-restart 'pick) (pick () :inner)) (pick () :outer)) "
      ":inner)",
      "(eq? (restart-case (let ((r (find-restart 'pick))) (restart-case (invoke-restart r) (pick "
      "() :inner))) (pick () :outer)) :outer)",
      "(= (restart-case (invoke-restart :give 9) (give (v) v)) 9)",
      "(= (restart-case (invoke-restart \"give\" 10) (give (v) v)) 10)",
      "(eq? (restart-case (invoke-restart 'pick) (pick () :first) (pick () :last)) :last)",
      "(equal? (restart-case (map restart-name (compute-restarts)) (a () 1) (b () 2)) (array 'b "
      "'a))",
      "(equal? (restart-case (restart-description (find-restart 'give)) (give \"A value\" (v) v)) "
      "\"A value\")",
      "(restart-case (nil? (restart-description (find-restart 'give))) (give (v) v))",
      "(restart-case (and (eq? (type (find-restart 'give)) :restart) (equal? (str (find-restart "
      "'give)) \"#<restart give>\")) (give (v) v))",
      "(begin (def saved nil) (restart-case (set! saved (find-restart 'give)) (give (v) v)) (nil? "
      "(find-restart saved)))",
      "(try (invoke-restart saved) (catch e #t))",
      "(restart-case (invoke-restart 'again) (again () (eq? (find-restart 'again) (get "
      "(compute-restarts) 0))))",
      "(= (restart-case (invoke-restart 'again 0) (again (n) (if (= n 10000) n (invoke-restart "
      "'again (+ n 1))))) 10000)",
      "(eq? (restart-case (invoke-restart 'first) (first () (invoke-restart 'second)) (second () "
      ":done)) :done)",
      "(begin (defparam p :default) (eq? (with-params ((p :outer)) (restart-case (with-params ((p "
      ":inner)) (invoke-restart 'give)) (give () (p)))) :outer))",
      "(= (restart-case (try (invoke-restart 'give 42) (catch e 0)) (give (v) v)) 42)",
      "(= (restart-case (eval '(invoke-restart 'give 42)) (give (v) v)) 42)",
      "(= (eval '(restart-case (invoke-restart 'give 42) (give (v) v))) 42)",
      "(let ((x 40)) (= (restart-case (invoke-restart 'give 2) (give (v) (+ x v))) 42))",
      "(try (restart-case (invoke-restart 'give) (give (v) v)) (catch e (equal? (get e :code) "
      "\"E0301\")))",
      "(begin (try (restart-case (error :bad) (give () 1)) (catch e nil)) (nil? (find-restart "
      "'give)))"};
  for (size_t i = 0; i < sizeof(checks) / sizeof(*checks); i++) {
    if (eval(rt, checks[i]) != OT_TRUE) {
      fprintf(stderr, "failed: %s\n", checks[i]);
      abort();
    }
  }
  const char* bad[] = {
      "(restart-case)",   "(restart-case 1 (42 () 1))", "(find-restart 42)",
      "(restart-name 1)", "(restart-description 1)",    "(compute-restarts 1)",
      "(invoke-restart)", "(invoke-restart 'missing)",  "(restart-case (error :bad) (give () 1))"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
    ot_value v;
    assert(!ot_eval(rt, bad[i], strlen(bad[i]), &v));
    assert(!rt->restart_scope_count && !rt->restart_roots.count);
    assert(eval(rt, "(nil? (find-restart 'give))") == OT_TRUE);
  }
  ot_destroy(rt);
  puts("restart transfer and introspection tests passed");
}
