#include "internal.h"
#include <assert.h>
#include <string.h>

static void check(Runtime* rt, const char* source) {
  Value result;
  if (!runtime_eval(rt, source, strlen(source), &result)) {
    fprintf(stderr, "%s\n", runtime_error(rt));
    assert(false);
  }
  assert(result == V_TRUE);
}

int main(void) {
  Runtime* rt = runtime_create(1024 * 1024, 128 * 1024);
  assert(rt);
  assert(runtime_load_core(rt, "../lib"));
  runtime_gc_stress(rt, true);
  check(rt, "(require otium.process :as p) (and (null? (p/arguments)) (nil? (p/script-name)))");
  char script[] = "script.ot", argument[] = "copied";
  const char* args[] = {argument, "", "λ", "--heap=1"};
  assert(runtime_set_arguments(rt, script, 4, args));
  script[0] = 'X';
  argument[0] = 'X';
  const char* expected = "(and (equal? (p/arguments) '(\"copied\" \"\" \"λ\" \"--heap=1\")) "
                         "(equal? (p/script-name) \"script.ot\"))";
  for (size_t i = 0; i < 100; i++) check(rt, expected);
  check(rt, "(not (eq? (p/arguments) (p/arguments)))");
  assert(!runtime_set_arguments(rt, "bad", 1, NULL));
  check(rt, expected);
  const char* bad[] = {NULL, "allocated before failure"};
  assert(!runtime_set_arguments(rt, "bad", 2, bad));
  check(rt, expected);
  Value result;
  const char* wrong = "(p/arguments 1)";
  assert(!runtime_eval(rt, wrong, strlen(wrong), &result));
  check(rt, expected);
  assert(runtime_set_arguments(rt, NULL, 0, NULL));
  check(rt, "(require otium.process :refer (arguments script-name) :reload) (and (null? "
            "(arguments)) (nil? (script-name)))");
  runtime_destroy(rt);
  puts("process metadata ownership and collection checks passed");
}
