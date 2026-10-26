#include "otium.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static ot_value eval(ot_runtime* rt, const char* source) {
  ot_value result;
  if (!ot_eval(rt, source, strlen(source), &result)) {
    fprintf(stderr, "%s: %s\n", source, ot_error(rt));
    abort();
  }
  return result;
}

int main(void) {
  ot_runtime* rt = ot_create(1024 * 1024, 256 * 1024);
  assert(rt);
  assert(ot_load_expander(rt, "../lib"));
  ot_gc_stress(rt, true);
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
  for (size_t i = 0; i < sizeof(checks) / sizeof(*checks); i++)
    assert(eval(rt, checks[i]) == OT_TRUE);
  eval(rt, "(def (deep n) (try (if (= n 0) (error 42) (deep (- n 1))) (catch c (error c))))");
  assert(eval(rt, "(try (deep 10000) (catch c c))") == ot_int(42));
  for (size_t i = 0; i < 100; i++)
    assert(eval(rt, "(try (error \"again\") (catch e 42))") == ot_int(42));
  ot_value result;
  const char* source = "(error 7)";
  assert(!ot_eval(rt, source, strlen(source), &result));
  assert(eval(rt, "(try (error 8) (catch e e))") == ot_int(8));
  ot_destroy(rt);
  puts("condition registry and try tests passed");
}
