#include "helpers.h"

static ot_runtime* new_expander_runtime(void) {
  ot_runtime* rt = new_runtime(1024 * 1024, 256 * 1024);
  if (!ot_load_expander(rt, TEST_LIBRARY_DIR))
    test_fail(__FILE__, __LINE__, "cannot load expander: %s", ot_error(rt));
  ot_gc_stress(rt, true);
  return rt;
}

/* The define-condition cases build on each other. */
TEST(try_catches_conditions_by_type) {
  ot_runtime* rt = new_expander_runtime();
  const char* checks[] = {
      "(= (try 42 (catch c 0)) 42)",
      "(equal? (try (error \"boom\") (catch (error? e) (condition-message e))) \"boom\")",
      "(eq? (try (error #f) (catch c c)) #f)",
      "(nil? (try (error nil) (catch c c)))",
      "(= (try (error 42) (catch c c)) 42)",
      "(= (try (error \"data\" 1 42) (catch c (get (get c :data) 1))) 42)",
      "(try (read-string \"(\") (catch c (and (equal? (get c :code) \"E0001\") "
      "(eq? (condition-type c) 'read-error) (condition-of-type? c 'error))))",
      "(try (read-string \"'\") (catch c (equal? (get c :code) \"E0009\")))",
      "(try (read-string \"\") (catch c (equal? (get c :code) \"E0010\")))",
      "(try (eval '(if)) (catch c (and (equal? (get c :code) \"E0101\") "
      "(eq? (condition-type c) 'syntax-error) (condition-of-type? c 'error))))",
      "(try (car 5) (catch c (and (equal? (get c :code) \"E0401\") "
      "(eq? (condition-type c) 'type-error) (condition-of-type? c 'error))))",
      "(try (missing-name) (catch c (and (equal? (get c :code) \"E0201\") "
      "(eq? (condition-type c) 'name-error) (condition-of-type? c 'error))))",
      "(try ((fn (x) x) 1 2) (catch c (and (equal? (get c :code) \"E0301\") "
      "(eq? (condition-type c) 'arity-error) (condition-of-type? c 'error))))",
      "(= (try (error 0) (catch ((fn (c) #f) c) 0) (catch c 42)) 42)",
      "(= (try (try (error 42) (catch ((fn (c) #f) c) 0)) (catch c c)) 42)",
      "(= (try (try (error 1) (catch c (error 42))) (catch c c)) 42)",
      "(let ((predicate (fn (c) #t))) (= (try (error 42) (catch (predicate predicate) predicate)) "
      "42))",
      "(= (try (/ 1 0) (catch (error? e) 42)) 42)",
      "(= (try (missing-name) (catch c 42)) 42)",
      "(= (try (error 1) (catch c (def x 40) (def y 2) (+ x y))) 42)",
      "(begin (define-condition 'child 'error) (define-condition :grandchild \"child\") (error? "
      "{:type 'grandchild}))",
      "(condition-of-type? {:type 'grandchild} 'child)",
      "(not (condition-of-type? {:type 'child} 'grandchild))",
      "(try (define-condition 'error 'grandchild) (catch (error? e) #t))",
      "(begin (define-condition 'child nil) (not (condition-of-type? {:type 'grandchild} 'error)))",
      "(let ((x [42])) (eq? x (try (error x) (catch c c))))",
      "(= ((try (error [42]) (catch c (fn () (get c 0))))) 42)",
      "(try (error 1 2) (catch (error? e) #t))"};
  for (size_t i = 0; i < sizeof(checks) / sizeof(*checks); i++) check_true(rt, checks[i]);
  ot_destroy(rt);
}

TEST(deep_rethrow_and_repeated_catch) {
  ot_runtime* rt = new_expander_runtime();
  eval(rt, "(def (deep n) (try (if (= n 0) (error 42) (deep (- n 1))) (catch c (error c))))");
  check_int(rt, "(try (deep 10000) (catch c c))", 42);
  for (size_t i = 0; i < 100; i++) check_int(rt, "(try (error \"again\") (catch e 42))", 42);
  ot_destroy(rt);
}

TEST(uncaught_error_leaves_try_working) {
  ot_runtime* rt = new_expander_runtime();
  check_error(rt, "(error 7)", "uncaught condition");
  check_int(rt, "(try (error 8) (catch e e))", 8);
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(try_catches_conditions_by_type),
    TEST_ENTRY(deep_rethrow_and_repeated_catch),
    TEST_ENTRY(uncaught_error_leaves_try_working),
};

TEST_MAIN(tests)
