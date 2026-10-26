#include "helpers.h"
#include "internal.h"
#include <string.h>

/* Every evaluation must leave no parameter bindings or roots behind. */
static void check_no_bindings(ot_runtime* rt) {
  CHECK(rt->param_count == 0 && rt->param_roots.count == 0);
}

/* The failure checks expect p as redefined by the last binding case. */
TEST(parameters_bind_dynamically_and_unwind) {
  ot_runtime* rt = new_core_runtime();
  ot_gc_stress(rt, true);
  eval(rt, "(defparam p \"Parameter.\" '(default)) (defparam q 0) (def (read-p) (p))");
  check_no_bindings(rt);
  const char* checks[] = {
      "(equal? (with-params ((p '(first)) (p (cons 'second (p)))) (p)) '(second first))",
      "(equal? (p) '(default))",
      "(equal? (with-params ((p '(caller))) (read-p)) '(caller))",
      "(equal? (with-params ((p '(outer))) (list (with-params ((p '(inner))) (p)) (p))) '((inner) "
      "(outer)))",
      "(equal? (with-params ((p '(inside))) (try (error :caught) (catch c (p)))) '(inside))",
      "(equal? (try (with-params ((p '(unwind))) (error :caught)) (catch c (p))) '(default))",
      "(equal? (try (with-params ((p '(first)) (q (/ 1 0))) (p)) (catch c (p))) '(default))",
      "(equal? (try (with-params ((p '(first)) (42 1)) (p)) (catch c (p))) '(default))",
      "(equal? (try (with-params ((p '(compile))) (eval '(if))) (catch c (p))) '(default))",
      "(begin (defmacro compile-read () (p)) (= (with-params ((p 42)) (eval '(compile-read))) 42))",
      "(equal? (with-params ((p '(eval))) (eval '(p))) '(eval))",
      "(equal? (try (with-params ((p '(eval))) (eval '(error :caught))) (catch c (p))) '(default))",
      "(begin (def later (with-params ((p '(temporary))) (fn () (p)))) (equal? (later) "
      "'(default)))",
      "(equal? (with-params ((p '(apply))) (apply p '())) '(apply))",
      "(= (with-params ((p q) ((p) 42)) (q)) 42)",
      "(nil? (with-params ((p 42))))",
      "(= (with-params () 42) 42)",
      "(eq? (type p) :param)",
      "(equal? (describe 'p) \"user/p: Parameter.\")",
      "(equal? (str p) \"#<param p>\")",
      "(begin (def old-p p) (defparam p '(new)) (and (not (eq? old-p p)) (equal? (old-p) "
      "'(default)) (equal? (p) '(new))))"};
  for (size_t i = 0; i < sizeof(checks) / sizeof(*checks); i++) {
    check_true(rt, checks[i]);
    check_no_bindings(rt);
  }
  const char* shape = "expected ((expression expression) ...)";
  const char* malformed = "malformed form";
  const char* top = "form requires top-level context";

  const struct {
    const char* source;
    const char* message;
  } bad[] = {{"(p 1)", "params take no arguments"},
             {"(with-params)", malformed},
             {"(with-params ((p)))", shape},
             {"(with-params ((p 1 2)) 3)", shape},
             {"(defparam)", malformed},
             {"(defparam p)", malformed},
             {"(defparam 1 2)", "expected a name"},
             {"(defparam p 1 2)", "expected one value after an optional docstring"},
             {"(fn () (defparam p 1))", top},
             {"(let () (defparam p 1))", top},
             {"(with-params ((p '(leaked))) (error :unhandled))", ":unhandled"}};

  for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
    ot_value ignored;
    if (ot_eval(rt, bad[i].source, strlen(bad[i].source), &ignored))
      test_fail(__FILE__, __LINE__, "%s: expected an error", bad[i].source);
    if (!strstr(ot_error(rt), bad[i].message))
      test_fail(__FILE__, __LINE__, "%s: expected error containing \"%s\", got \"%s\"",
                bad[i].source, bad[i].message, ot_error(rt));
    check_no_bindings(rt);
    check_true(rt, "(equal? (p) '(new))");
    check_no_bindings(rt);
  }
  eval(rt,
       "(def (deep n) (with-params ((p (list n))) (if (= n 0) (error :bottom) (deep (- n 1)))))");
  check_no_bindings(rt);
  check_true(rt, "(try (deep 4000) (catch c (equal? (p) '(new))))");
  check_no_bindings(rt);
  ot_value result;
  CHECK(!ot_call(rt, "deep", (ot_value[]){ot_int(20)}, 1, &result));
  check_no_bindings(rt);
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(parameters_bind_dynamically_and_unwind),
};

TEST_MAIN(tests)
