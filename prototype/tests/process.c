#include "helpers.h"
#include <string.h>

static ot_runtime* process_runtime(void) {
  ot_runtime* rt = new_core_runtime();
  ot_gc_stress(rt, true);
  check_true(rt, "(require otium.process :as p) (and (null? (p/arguments)) (nil? (p/script-name)))");
  return rt;
}

TEST(error_output_and_environment) {
  ot_runtime* rt = process_runtime();
  check_true(rt, "(let ((out (open-output-string))) (with-params ((current-error-port out)) "
                 "(p/write-error \"problem λ\")) (equal? (get-output-string out) \"problem λ\"))");
  check_true(rt, "(begin (p/setenv! \"OTIUM_PROTO_PROCESS_TEST\" \"a=b λ\") "
                 "(def env-copy (p/getenv \"OTIUM_PROTO_PROCESS_TEST\")) "
                 "(p/setenv! \"OTIUM_PROTO_PROCESS_TEST\" nil) "
                 "(and (equal? env-copy \"a=b λ\") (nil? (p/getenv \"OTIUM_PROTO_PROCESS_TEST\"))))");
  ot_destroy(rt);
}

/* The runtime copies host arguments and keeps them through failed updates. */
TEST(arguments_are_copied_and_kept) {
  ot_runtime* rt = process_runtime();
  char script[] = "script.ot", argument[] = "copied";
  const char* args[] = {argument, "", "λ", "--heap=1"};
  CHECK(ot_set_arguments(rt, script, 4, args));
  script[0] = 'X';
  argument[0] = 'X';
  const char* expected = "(and (equal? (p/arguments) '(\"copied\" \"\" \"λ\" \"--heap=1\")) "
                         "(equal? (p/script-name) \"script.ot\"))";
  for (size_t i = 0; i < 100; i++) check_true(rt, expected);
  check_true(rt, "(not (eq? (p/arguments) (p/arguments)))");
  CHECK(!ot_set_arguments(rt, "bad", 1, NULL));
  check_true(rt, expected);
  const char* bad[] = {NULL, "allocated before failure"};
  CHECK(!ot_set_arguments(rt, "bad", 2, bad));
  check_true(rt, expected);
  ot_value result;
  const char* wrong = "(p/arguments 1)";
  CHECK(!ot_eval(rt, wrong, strlen(wrong), &result));
  check_true(rt, expected);
  CHECK(ot_set_arguments(rt, NULL, 0, NULL));
  check_true(rt, "(require otium.process :refer (arguments script-name) :reload) (and (null? "
                 "(arguments)) (nil? (script-name)))");
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(error_output_and_environment),
    TEST_ENTRY(arguments_are_copied_and_kept),
};

TEST_MAIN(tests)
