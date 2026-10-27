#include "helpers.h"

TEST(expander_forms_and_macros) {
  ot_state* rt = new_runtime(1024 * 1024, 128 * 1024);
  if (!ot_load_expander(rt, TEST_LIBRARY_DIR))
    test_fail(__FILE__, __LINE__, "cannot load expander: %s", ot_error(rt));
  ot_gc_stress(rt, true);
  check_int(rt, "(defmacro install (name value) `(def ,name ,value)) (let () (install x 7) x)", 7);
  check_int(rt, "(let () (begin (def (f) (g)) (def (g) 3)) (f))", 3);
  CHECK(eval(rt, "((fn () (def x 1)))") == OT_NIL);
  check_int(rt,
            "(defmacro twice (x) (let ((t (gensym \"t\"))) `(let ((,t ,x)) (+ ,t ,t)))) "
            "(twice 21)",
            42);
  check_int(rt, "(defmacro hygienic (x) #`(let ((tmp# ,x)) (+ tmp# tmp#))) (hygienic 21)", 42);
  CHECK(eval(rt, "(let ((x (gensym \"x\"))) (eq? x (string->symbol (symbol->string x))))") ==
        OT_FALSE);
  check_int(rt, "(cond (#f 1) ((+ 20 22)))", 42);
  check_true(rt, "(equal? (list (string-length \"é🙂\") (substring \"aé🙂z\" 1 3)) '(2 \"é🙂\"))");
  check_true(rt, "(defmacro outer (x) `(inner ,x)) (defmacro inner (x) `(+ ,x 1)) "
                 "(equal? (macroexpand-1 '(outer 2)) '(inner 2))");
  check_true(rt, "(equal? (macroexpand '(outer 2)) '(+ 2 1))");
  check_int(rt,
            "(defmacro distinct () (let ((x (gensym \"x\"))) "
            "`(let ((,x 42) (,(string->symbol (symbol->string x)) 1)) ,x))) (distinct)",
            42);
  check_true(rt, "(equal? (string-append \"é\" \"🙂\" (number->string 42)) \"é🙂42\")");
  const char* rebind = "cannot rebind a core form name";
  check_error(rt, "(let ((if 1)) if)", rebind);
  check_error(rt, "(fn (quote) quote)", rebind);
  check_error(rt, "(defmacro begin () 1)", rebind);
  check_error(rt, "(fn () (def- x 1))", "private definitions are only allowed at top level");
  check_error(rt, "(defmacro forever () '(forever)) (forever)", "macro expansion limit exceeded");
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(expander_forms_and_macros),
};

TEST_SUITE(shared_expander, tests);
