#include "internal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static ot_value eval(ot_runtime* rt, const char* source) {
  ot_value value;
  if (!ot_eval(rt, source, strlen(source), &value)) {
    fprintf(stderr, "%s: %s\n", source, ot_error(rt));
    abort();
  }
  assert(rt->param_count == 0 && rt->param_roots.count == 0);
  return value;
}

int main(void) {
  ot_runtime* rt = ot_create(1024 * 1024, 128 * 1024);
  assert(rt);
  assert(ot_load_core(rt, "../lib"));
  ot_gc_stress(rt, true);
  eval(rt, "(defparam p \"Parameter.\" '(default)) (defparam q 0) (def (read-p) (p))");
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
  for (size_t i = 0; i < sizeof(checks) / sizeof(*checks); i++)
    assert(eval(rt, checks[i]) == OT_TRUE);
  const char* bad[] = {"(p 1)",
                       "(with-params)",
                       "(with-params ((p)))",
                       "(with-params ((p 1 2)) 3)",
                       "(defparam)",
                       "(defparam p)",
                       "(defparam 1 2)",
                       "(defparam p 1 2)",
                       "(fn () (defparam p 1))",
                       "(let () (defparam p 1))",
                       "(with-params ((p '(leaked))) (error :unhandled))"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
    ot_value ignored;
    assert(!ot_eval(rt, bad[i], strlen(bad[i]), &ignored));
    assert(rt->param_count == 0 && rt->param_roots.count == 0);
    assert(eval(rt, "(equal? (p) '(new))") == OT_TRUE);
  }
  eval(rt,
       "(def (deep n) (with-params ((p (list n))) (if (= n 0) (error :bottom) (deep (- n 1)))))");
  assert(eval(rt, "(try (deep 4000) (catch c (equal? (p) '(new))))") == OT_TRUE);
  ot_value result;
  assert(!ot_call(rt, "deep", (ot_value[]){ot_int(20)}, 1, &result));
  assert(rt->param_count == 0 && rt->param_roots.count == 0);
  ot_destroy(rt);
  puts("dynamic parameter tests passed");
}
