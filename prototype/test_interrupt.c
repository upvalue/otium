#include "internal.h"
#include <assert.h>
#include <string.h>

int main(void) {
  ot_runtime* rt = ot_create(1024 * 1024, 128 * 1024);
  assert(rt);
  assert(ot_load_core(rt, "../lib"));
  volatile sig_atomic_t flag = 0;
  ot_set_interrupt_flag(rt, &flag);
  ot_value value;
  const char* definition = "(def (answer) 42)";
  assert(ot_eval(rt, definition, strlen(definition), &value));
  flag = 1;
  assert(!ot_call(rt, "answer", NULL, 0, &value));
  assert(ot_interrupted(rt) && flag == 0);
  int status;
  assert(!ot_exit_status(rt, &status));
  assert(!rt->cleanup_count && !rt->restart_scope_count);
  assert(ot_call(rt, "answer", NULL, 0, &value) && value == ot_int(42));
  assert(!ot_interrupted(rt));
  flag = 1;
  assert(!ot_eval(rt, "(answer)", 8, &value));
  assert(ot_interrupted(rt) && flag == 0);
  flag = 1;
  assert(!ot_call(rt, "+", NULL, 0, &value));
  assert(ot_interrupted(rt) && flag == 0);
  flag = 1;
  assert(!ot_eval(rt, "", 0, &value));
  assert(ot_interrupted(rt) && flag == 0);
  ot_set_interrupt_flag(rt, NULL);
  flag = 1;
  assert(ot_eval(rt, "(answer)", 8, &value) && value == ot_int(42));
  assert(!ot_interrupted(rt) && flag == 1);
  ot_set_interrupt_flag(rt, &flag);
  assert(!ot_poll_interrupt(rt));
  assert(ot_interrupted(rt) && flag == 0);
  assert(ot_eval(rt, "(answer)", 8, &value) && value == ot_int(42));
  ot_destroy(rt);
  puts("host interrupt flag and recovery checks passed");
}
