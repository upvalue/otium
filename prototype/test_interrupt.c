#include "internal.h"
#include <assert.h>
#include <string.h>

int main(void) {
  Runtime* rt = runtime_create(1024 * 1024, 128 * 1024);
  assert(rt);
  assert(runtime_load_core(rt, "../lib"));
  volatile sig_atomic_t flag = 0;
  runtime_interrupt_flag(rt, &flag);
  Value value;
  const char* definition = "(def (answer) 42)";
  assert(runtime_eval(rt, definition, strlen(definition), &value));
  flag = 1;
  assert(!runtime_call(rt, "answer", NULL, 0, &value));
  assert(runtime_interrupted(rt) && flag == 0);
  int status;
  assert(!runtime_exit_status(rt, &status));
  assert(!rt->cleanup_count && !rt->restart_scope_count);
  assert(runtime_call(rt, "answer", NULL, 0, &value) && value == value_int(42));
  assert(!runtime_interrupted(rt));
  flag = 1;
  assert(!runtime_eval(rt, "(answer)", 8, &value));
  assert(runtime_interrupted(rt) && flag == 0);
  flag = 1;
  assert(!runtime_call(rt, "+", NULL, 0, &value));
  assert(runtime_interrupted(rt) && flag == 0);
  flag = 1;
  assert(!runtime_eval(rt, "", 0, &value));
  assert(runtime_interrupted(rt) && flag == 0);
  runtime_interrupt_flag(rt, NULL);
  flag = 1;
  assert(runtime_eval(rt, "(answer)", 8, &value) && value == value_int(42));
  assert(!runtime_interrupted(rt) && flag == 1);
  runtime_interrupt_flag(rt, &flag);
  assert(!runtime_poll_interrupt(rt));
  assert(runtime_interrupted(rt) && flag == 0);
  assert(runtime_eval(rt, "(answer)", 8, &value) && value == value_int(42));
  runtime_destroy(rt);
  puts("host interrupt flag and recovery checks passed");
}
