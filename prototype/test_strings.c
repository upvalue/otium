#include "runtime.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static ot_value eval(ot_runtime* rt, const char* source) {
  ot_value value;
  if (!ot_eval(rt, source, strlen(source), &value)) {
    fprintf(stderr, "%s: %s\n", source, ot_error(rt));
    abort();
  }
  return value;
}

int main(void) {
  ot_runtime* rt = ot_create(1024 * 1024, 128 * 1024);
  assert(rt);
  assert(ot_load_core(rt, "../lib"));
  ot_gc_stress(rt, true);
  const char* checks[] = {
      "(equal? (substring \"aé🙂z\" 1 3) \"é🙂\")",
      "(equal? (substring \"ab\" -5 99) \"ab\")",
      "(equal? (substring \"ab\" 99) \"\")",
      "(equal? (substring \"ab\" 1 0) \"\")",
      "(equal? (substring \"\" -1 2) \"\")",
      "(= (char->integer (get \"aé🙂\" 2)) 128578)",
      "(eq? (get \"a\" -1 :missing) :missing)",
      "(equal? (list->string (string->list \"aé🙂\")) \"aé🙂\")",
      "(equal? (str (integer->char 955) \"bc\") \"λbc\")",
      "(begin (def b (buffer \"é\")) (def before (buffer->string b)) (buffer-push! b \"🙂\" 7) "
      "(and (= (length b) 3) (equal? before \"é\") (equal? (buffer->string b) \"é🙂7\")))",
      "(let ((i 0)) (while (< i 500) (buffer-push! b \"λ\") (set! i (+ i 1))) (= (length b) 503))",
      "(equal? (string-split \"a,,b,\" \",\") [\"a\" \"\" \"b\" \"\"])",
      "(equal? (string-split \"  a\\tb\\nc \" ) [\"a\" \"b\" \"c\"])",
      "(equal? (string-join \"-\" '(1 2 3)) \"1-2-3\")",
      "(equal? (string-replace \"ab\" \"\" \"X\") \"ab\")",
      "(equal? (string-replace \"a🙂b🙂\" \"🙂\" \"λ\") \"aλbλ\")",
      "(equal? (string-trim \" \\té\\n\") \"é\")",
      "(= (string-index \"a🙂é🙂\" \"🙂\" 2) 3)",
      "(equal? (string-repeat \"é🙂\" 2) \"é🙂é🙂\")",
      "(and (contains? \"a🙂b\" \"🙂\") (contains? '(1 2 3) 2) (not (contains? nil 1)))",
      "(and (= (string->number \" 42\\n\") 42) (nil? (string->number \"1 ;hi\")) (nil? "
      "(string->number \"1 2\")))",
      "(and (string<? \"a\\0b\" \"a\\0c\") (equal? (substring \"a\\0b\" 1 2) \"\\0\"))",
      "(equal? (name :hello) \"hello\")"};
  for (size_t i = 0; i < sizeof(checks) / sizeof(*checks); i++)
    assert(eval(rt, checks[i]) == OT_TRUE);
  const char* bad[] = {
      "(string-ref \"a\" -1)",      "(string-ref \"a\" 1)",     "(substring \"a\" 0.5)",
      "(string-split \"a\" \"\")",  "(string-split \"a\" nil)", "(list->string '(1))",
      "(list->string '(#\\a . 2))", "(integer->char 55296)",    "(integer->char 1114112)",
      "(buffer-push! \"a\" 1)",     "(string<? \"z\" \"a\" 1)"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
    ot_value value;
    assert(!ot_eval(rt, bad[i], strlen(bad[i]), &value));
    assert(eval(rt, "(= (length b) 503)") == OT_TRUE);
  }
  ot_destroy(rt);
  puts("string and buffer tests passed");
}
