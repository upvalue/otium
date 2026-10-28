#include "helpers.h"
#include "internal.h"
#include <string.h>

/* Every evaluation must leave no handlers or handler roots behind. */
static void check_no_handlers(ot_state* rt) {
  CHECK(rt->handler_count == 0 && rt->handler_top == 0);
}

static ot_state* new_handler_runtime(void) {
  ot_state* rt = new_core_runtime();
  ot_gc_stress(rt, true);
  eval(rt,
       "(def events []) (def (record x) (push! events x)) (def (yes _) #t) (defparam p :default)");
  check_no_handlers(rt);
  return rt;
}

TEST(handlers_run_at_signal_site) {
  ot_state* rt = new_handler_runtime();
  const char* checks[] = {
      "(nil? (signal 'unhandled))",
      "(= (handler-bind () 42) 42)",
      "(begin (set! events []) (handler-bind ((yes (fn (c) (record :outer)))) (handler-bind ((yes "
      "(fn (c) (record :one))) (yes (fn (c) (record :two) (signal :nested)))) (signal :original))) "
      "(equal? events [:two :one :outer :one :outer]))",
      "(begin (set! events []) (handler-bind ((yes (fn (c) (record c)))) (try (handler-bind ((yes "
      "(fn (c) #f))) (try (error :once) (catch ((fn (_) #f) c) :wrong))) (catch c #f))) (equal? "
      "events [:once]))",
      "(begin (set! events []) (handler-bind ((yes (fn (c) (record :outer)))) (handler-bind "
      "(((begin (record :pred) (signal :init) yes) (begin (record :handler) (fn (c) (record "
      ":inner))))) (signal :body))) (equal? events [:pred :outer :handler :inner :outer]))",
      "(begin (set! events []) (handler-bind ((yes (fn (c) (record (p))))) (with-params ((p "
      ":site)) (try (car 42) (catch c :caught)))) (equal? events [:site]))",
      "(begin (set! events []) (try (handler-bind ((yes (fn (c) (record :old) (error "
      ":replacement)))) (error :original)) (catch c (record c))) (equal? events [:old "
      ":replacement]))",
      "(begin (set! events []) (handler-bind ((yes (fn (c) (record :outer)))) (try (handler-bind "
      "((yes car)) (signal 1)) (catch c :caught))) (equal? events [:outer]))",
      "(begin (set! events []) (handler-bind ((yes (fn (c) (record :outer)))) (handler-bind ((yes "
      "(fn (c) (handler-bind ((yes (fn (d) (record :new)))) (signal :nested))))) (signal "
      ":original))) (equal? events [:new :outer :outer]))",
      "(begin (set! events []) (handler-bind ((yes (fn (c) (record c)))) (eval '(signal :eval))) "
      "(equal? events [:eval]))",
      "(begin (set! events []) (handler-bind ((yes (fn (c) (record (get c :code))))) (try ((fn (x) "
      "x)) (catch c nil))) (equal? events [\"E0301\"]))",
      "(begin (set! events []) (handler-bind ((yes (fn (c) (record c)))) (signal #f) (signal nil)) "
      "(equal? events [#f nil]))",
      "(begin (set! events []) (handler-bind ((yes (fn (c) (record (list (condition-type c) "
      "(condition-message c) (get c :data)))))) (signal \"message\" 1 2)) (equal? events [(list "
      "'error \"message\" [1 2])]))"};
  for (size_t i = 0; i < sizeof(checks) / sizeof(*checks); i++) {
    check_true(rt, checks[i]);
    check_no_handlers(rt);
  }
  ot_destroy(rt);
}

TEST(failures_leave_no_handlers) {
  ot_state* rt = new_handler_runtime();
  const char* shape = "expected ((expression expression) ...)";

  const struct {
    const char* source;
    const char* message;
  } bad[] = {{"(handler-bind)", "malformed form"},
             {"(handler-bind ((yes)))", shape},
             {"(handler-bind ((yes yes yes)))", shape},
             {"(signal)", "signal needs an argument"},
             {"(signal :x 1)", "extra arguments after non-string condition"},
             {"(handler-bind ((yes (fn (c) nil))) (error :unhandled))", ":unhandled"}};

  for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
    ot_value ignored;
    if (ot_eval(rt, bad[i].source, strlen(bad[i].source), &ignored))
      test_fail(__FILE__, __LINE__, "%s: expected an error", bad[i].source);
    if (!strstr(ot_error(rt), bad[i].message))
      test_fail(__FILE__, __LINE__, "%s: expected error containing \"%s\", got \"%s\"",
                bad[i].source, bad[i].message, ot_error(rt));
    CHECK(rt->handler_count == 0);
    check_int(rt, "(+ 20 22)", 42);
    check_no_handlers(rt);
  }
  ot_destroy(rt);
}

TEST(deeply_nested_handlers) {
  ot_state* rt = new_handler_runtime();
  eval(rt, "(def hits 0) (def (deep n) (if (= n 0) (signal :bottom) (handler-bind ((yes (fn (c) "
           "(set! hits (+ hits 1))))) (deep (- n 1)))))");
  check_no_handlers(rt);
  check_int(rt, "(deep 4000) hits", 4000);
  check_no_handlers(rt);
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(handlers_run_at_signal_site),
    TEST_ENTRY(failures_leave_no_handlers),
    TEST_ENTRY(deeply_nested_handlers),
};

TEST_SUITE(handlers, tests);
