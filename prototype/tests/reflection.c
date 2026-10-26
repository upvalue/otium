#include "helpers.h"
#include <string.h>

/* Later checks redefine names documented by earlier ones. */
TEST(types_documentation_and_eof) {
  ot_runtime* rt = new_core_runtime();
  ot_gc_stress(rt, true);
  const char* checks[] = {
      "(equal? (map type (list nil '() #f 1 1.0 'a :a #\\a \"a\" '(a) [] {} (buffer) +)) '(:nil "
      ":null :boolean :int :float :symbol :keyword :char :string :pair :array :table :buffer "
      ":function))",
      "(begin (defmacro m () 1) (eq? (type m) :macro))",
      "(and (eq? (type (eof-object)) :eof) (eof-object? (eof-object)) (not (eof-object? nil)) (eq? "
      "(eof-object) (eof-object)))",
      "(equal? (str [(eof-object)]) \"[#<eof>]\")",
      "(eq? (eval (eof-object)) (eof-object))",
      "(begin (def answer \"The answer.\" 42) (equal? (describe 'answer) \"user/answer: The "
      "answer.\"))",
      "(begin (def (square x) \"Square.\" (* x x)) (equal? (describe 'square) \"user/square: "
      "Square.\"))",
      "(begin (defmacro twice (x) \"Twice.\" `(+ ,x ,x)) (equal? (describe 'twice) \"user/twice: "
      "Twice.\"))",
      "(begin (defonce state \"First.\" []) (defonce state \"Second.\" 1) (equal? (describe "
      "'state) \"user/state: First.\"))",
      "(begin (def answer 43) (equal? (describe 'answer) \"user/answer: no documentation\"))",
      "(begin (def nul-doc \"a\\0b\" 1) (equal? (describe 'nul-doc) \"user/nul-doc: a\\0b\"))",
      "(nil? (describe 'missing-reflection-name))",
      "(begin (ns reflect-first) (def x \"one\" 1) (def (where) (current-ns)) (def (help) "
      "(describe 'x)) (ns reflect-second) (def x \"two\" 2) (and (eq? (reflect-first/where) "
      "'reflect-second) (equal? (reflect-first/help) \"reflect-second/x: two\")))",
      "(begin (ns user) (equal? (describe 'reflect-first/x) \"reflect-first/x: one\"))",
      "(equal? (put! [1 2] 0 3 1 4) [3 4])",
      "(let ((t {})) (put! t 1 :int 1.0 :float :a 1 :a 2 :b nil) (and (= (length t) 3) (eq? (get t "
      "1) :int) (eq? (get t 1.0) :float) (= (get t :a) 2)))"};
  for (size_t i = 0; i < sizeof(checks) / sizeof(*checks); i++) check_true(rt, checks[i]);
  for (size_t i = 0; i < 100; i++)
    check_true(rt, "(def (square x) \"Updated.\" x) (equal? (describe 'square) \"user/square: "
                   "Updated.\")");

  /* A failed definition keeps the previous documentation. */
  ot_value ignored;
  const char* bad = "(def square \"Failed.\" (/ 1 0))";
  CHECK(!ot_eval(rt, bad, strlen(bad), &ignored));
  check_true(rt, "(equal? (describe 'square) \"user/square: Updated.\")");
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(types_documentation_and_eof),
};

TEST_MAIN(tests)
