#include "helpers.h"
#include "internal.h"
#include <string.h>

/* Every evaluation must leave no restart scopes, roots or pending transfer. */
static void check_no_restarts(ot_state* rt) {
  CHECK(!rt->restart_scope_count && !rt->restart_roots.count && !rt->restart_pending);
}

static ot_state* new_restart_runtime(void) {
  ot_state* rt = new_core_runtime();
  ot_gc_stress(rt, true);
  return rt;
}

/* Some cases use definitions made by earlier ones (saved, p). */
TEST(restarts_transfer_and_introspect) {
  ot_state* rt = new_restart_runtime();
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
    check_true(rt, checks[i]);
    check_no_restarts(rt);
  }
  ot_destroy(rt);
}

TEST(failures_leave_no_restarts) {
  ot_state* rt = new_restart_runtime();
  const char* arguments = "wrong number of restart arguments";

  const struct {
    const char* source;
    const char* message;
  } bad[] = {{"(restart-case)", "malformed form"},
             {"(restart-case 1 (42 () 1))", "expected (name [description] parameters body...)"},
             {"(find-restart 42)", "restart name must be a symbol, keyword, string or restart"},
             {"(restart-name 1)", "expected restart"},
             {"(restart-description 1)", "expected restart"},
             {"(compute-restarts 1)", arguments},
             {"(invoke-restart)", arguments},
             {"(invoke-restart 'missing)", "no active restart"},
             {"(restart-case (error :bad) (give () 1))", ":bad"}};

  for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
    ot_value ignored;
    if (ot_eval(rt, bad[i].source, strlen(bad[i].source), &ignored))
      test_fail(__FILE__, __LINE__, "%s: expected an error", bad[i].source);
    if (!strstr(ot_error(rt), bad[i].message))
      test_fail(__FILE__, __LINE__, "%s: expected error containing \"%s\", got \"%s\"",
                bad[i].source, bad[i].message, ot_error(rt));
    CHECK(!rt->restart_scope_count && !rt->restart_roots.count);
    check_true(rt, "(nil? (find-restart 'give))");
    check_no_restarts(rt);
  }
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(restarts_transfer_and_introspect),
    TEST_ENTRY(failures_leave_no_restarts),
};

TEST_SUITE(restarts, tests);
