#include "helpers.h"
#include "internal.h"
#include <string.h>

/* Every evaluation must leave no cleanup frames or cleanup roots behind. */
static void check_no_cleanups(ot_runtime* rt) {
  CHECK(!rt->cleanup_count && !rt->cleanup_roots.count);
}

static ot_runtime* new_cleanup_runtime(void) {
  ot_runtime* rt = new_core_runtime();
  ot_gc_stress(rt, true);
  eval(rt, "(def events []) (def (record x) (push! events x)) (defparam p :default)");
  check_no_cleanups(rt);
  return rt;
}

/* Some cases check the events recorded by the case before them. */
TEST(cleanups_run_on_every_exit) {
  ot_runtime* rt = new_cleanup_runtime();
  const char* checks[] = {
      "(begin (set! events []) (= (unwind-protect 42 (record :normal)) 42))",
      "(equal? events [:normal])",
      "(begin (set! events []) (try (unwind-protect (unwind-protect (error :body) (record :inner)) "
      "(record :outer)) (catch e (record e))) (equal? events [:inner :outer :body]))",
      "(eq? (try (unwind-protect (error :body) (error :cleanup)) (catch e e)) :cleanup)",
      "(eq? (try (restart-case (unwind-protect (invoke-restart 'done) (error :cleanup)) (done () "
      ":wrong)) (catch e e)) :cleanup)",
      "(begin (set! events []) (= (restart-case (eval '(unwind-protect (invoke-restart 'done 42) "
      "(record :eval))) (done (v) v)) 42))",
      "(equal? events [:eval])",
      "(begin (set! events []) (= (handler-bind ((error? (fn (c) (unwind-protect (invoke-restart "
      "'done 42) (record :handler))))) (restart-case (unwind-protect (error \"bad\") (record "
      ":body)) (done (v) v))) 42))",
      "(equal? events [:handler :body])",
      "(begin (set! events []) (with-params ((p :outer)) (try (unwind-protect (with-params ((p "
      ":inner)) (error :bad)) (record (p))) (catch e nil))) (equal? events [:outer]))",
      "(begin (set! events []) (with-params ((p :outer)) (with-params ((p :inner)) (unwind-protect "
      "1 (record (p))))) (equal? events [:inner]))",
      "(begin (set! events []) (try (unwind-protect (try (error :inner) (catch e 42)) (record "
      ":once)) (catch e nil)) (equal? events [:once]))",
      "(begin (set! events []) (= (call-with-cleanup (fn () 42) (fn (outcome) (record outcome))) "
      "42))",
      "(equal? events [nil])",
      "(begin (set! events []) (try (call-with-cleanup (fn () (error :body)) (fn (outcome) (record "
      "outcome) (error :release))) (catch e (record e))) (equal? events [:body :release]))",
      "(eq? (try (call-with-cleanup (fn () 42) (fn (outcome) (error :release))) (catch e e)) "
      ":release)",
      "(begin (set! events []) (eq? (try (restart-case (call-with-cleanup (fn () (invoke-restart "
      "'done 42)) (fn (outcome) (record outcome) (error :release))) (done (v) v)) "
      "(catch e e)) :release))",
      "(equal? events [nil])",
      "(begin (set! events []) (= (with ((a 1 record) (b (+ a 1) record)) 42) 42))",
      "(equal? events [2 1])",
      "(= (restart-case (unwind-protect (error :body) (invoke-restart 'done 42)) (done (v) v)) 42)",
      "(begin (set! events []) (unwind-protect 42 (try (error :cleanup) (catch e (record e)))) "
      "(equal? events [:cleanup]))",
      "(eq? (try (unwind-protect (exit 7) (error :cleanup)) (catch e e)) :cleanup)",
      "(= (defer 42) 42)"};
  for (size_t i = 0; i < sizeof(checks) / sizeof(*checks); i++) {
    check_true(rt, checks[i]);
    check_no_cleanups(rt);
  }
  ot_destroy(rt);
}

TEST(quit_and_exit_run_cleanups) {
  ot_runtime* rt = new_cleanup_runtime();
  const char* quit = "(set! events []) (try (call-with-cleanup (fn () (quit 7)) (fn (o) (record o) "
                     "(error :release))) (catch e (record e)))";
  ot_value result;
  CHECK(ot_eval(rt, quit, strlen(quit), &result));
  int status;
  CHECK(!ot_exit_status(rt, &status));
  check_no_cleanups(rt);
  check_true(rt, "(equal? events [:quit :release])");
  check_no_cleanups(rt);
  CHECK(!ot_exit_status(rt, &status));
  eval(rt, "(def (quit-host) (unwind-protect (exit 9) (record :host)))");
  check_no_cleanups(rt);
  CHECK(!ot_call(rt, "quit-host", NULL, 0, &result));
  CHECK(ot_exit_status(rt, &status) && status == 9);
  check_true(rt, "(equal? events [:quit :release :host])");
  check_no_cleanups(rt);
  ot_destroy(rt);
}

TEST(deep_unwind_runs_every_cleanup) {
  ot_runtime* rt = new_cleanup_runtime();
  ot_gc_stress(rt, false);
  eval(rt, "(def (deep n) (unwind-protect (if (= n 0) (error :bottom) (deep (- n 1))) (record n))) "
           "(set! events [])");
  check_no_cleanups(rt);
  check_true(rt, "(try (deep 4000) (catch e nil)) (= (length events) 4001)");
  check_no_cleanups(rt);
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(cleanups_run_on_every_exit),
    TEST_ENTRY(quit_and_exit_run_cleanups),
    TEST_ENTRY(deep_unwind_runs_every_cleanup),
};

TEST_MAIN(tests)
