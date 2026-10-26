#include "helpers.h"
#include "internal.h"
#include <string.h>

TEST(interrupt_flag_and_recovery) {
  ot_runtime* rt = new_core_runtime();
  volatile sig_atomic_t flag = 0;
  ot_set_interrupt_flag(rt, &flag);
  ot_value value;
  eval(rt, "(def (answer) 42)");

  /* A raised flag stops a call, is cleared, and leaves no cleanup state behind. */
  flag = 1;
  CHECK(!ot_call(rt, "answer", NULL, 0, &value));
  CHECK(ot_interrupted(rt) && flag == 0);
  int status;
  CHECK(!ot_exit_status(rt, &status));
  CHECK(!rt->cleanup_count && !rt->restart_scope_count);
  CHECK(ot_call(rt, "answer", NULL, 0, &value) && value == ot_int(42));
  CHECK(!ot_interrupted(rt));

  flag = 1;
  CHECK(!ot_eval(rt, "(answer)", 8, &value));
  CHECK(ot_interrupted(rt) && flag == 0);
  flag = 1;
  CHECK(!ot_call(rt, "+", NULL, 0, &value));
  CHECK(ot_interrupted(rt) && flag == 0);
  flag = 1;
  CHECK(!ot_eval(rt, "", 0, &value));
  CHECK(ot_interrupted(rt) && flag == 0);

  /* Without a flag, a raised host variable is ignored. */
  ot_set_interrupt_flag(rt, NULL);
  flag = 1;
  CHECK(ot_eval(rt, "(answer)", 8, &value) && value == ot_int(42));
  CHECK(!ot_interrupted(rt) && flag == 1);

  ot_set_interrupt_flag(rt, &flag);
  CHECK(!ot_poll_interrupt(rt));
  CHECK(ot_interrupted(rt) && flag == 0);
  CHECK(ot_eval(rt, "(answer)", 8, &value) && value == ot_int(42));
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(interrupt_flag_and_recovery),
};

TEST_MAIN(tests)
