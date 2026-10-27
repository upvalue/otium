#include "helpers.h"
#include "internal.h"
#include <stdlib.h>
#include <string.h>

TEST(interrupt_flag_and_recovery) {
  ot_state* rt = new_core_runtime();
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

TEST(interrupt_handler) {
  ot_state* rt = new_core_runtime();
  volatile sig_atomic_t flag = 0;
  ot_set_interrupt_flag(rt, &flag);
  eval(rt, "(def reasons []) (def answer nil)"
           "(def (on-interrupt reason) (push! reasons reason) answer)"
           "(def (spin n) (let loop ((i n)) (if (= i 0) :done (loop (- i 1)))))");
  CHECK(!ot_set_interrupt_handler(rt, "missing-handler"));
  CHECK(ot_set_interrupt_handler(rt, "on-interrupt"));

  /* The handler runs at the next VM safe point, and returning nil resumes. */
  flag = 2;
  check_printed(rt, "(spin 1000)", ":done");
  check_printed(rt, "reasons", "[2]");
  CHECK(flag == 0 && !ot_interrupted(rt));

  /* Calls into the runtime leave the flag for the VM to find. */
  flag = 3;
  CHECK(ot_poll_interrupt(rt) && flag == 3);
  check_printed(rt, "(begin (spin 10) reasons)", "[2 3]");

  /* Returning #t interrupts, running cleanup. */
  eval(rt, "(set! answer #t) (def cleaned #f)"
           "(def (protected) (unwind-protect (spin 1000) (set! cleaned #t)))");
  flag = 1;
  ot_value value;
  CHECK(!ot_call(rt, "protected", NULL, 0, &value) && ot_interrupted(rt));
  check_printed(rt, "cleaned", "#t");

  /* An error from the handler is raised in the interrupted code. */
  eval(rt, "(def (on-interrupt reason) (error \"paused\"))"
           "(def (caught) (try (spin 1000) (catch e (condition-message e))))");
  flag = 1;
  CHECK(ot_call(rt, "caught", NULL, 0, &value));
  char* text = printed(rt, value);
  CHECK(!strcmp(text, "paused"));
  free(text);

  /* Removing the handler restores plain interrupts. */
  CHECK(ot_set_interrupt_handler(rt, NULL));
  flag = 1;
  CHECK(!ot_poll_interrupt(rt) && ot_interrupted(rt));
  ot_destroy(rt);
}

TEST(load_hook) {
  ot_state* rt = new_core_runtime();
  eval(rt, "(def loads []) (def (on-load space path) (push! loads (list space path)))");
  CHECK(ot_set_load_hook(rt, "on-load"));
  check_printed(rt, "(begin (require otium.extra) (car (get loads 0)))", "otium.extra");
  check_true(rt, "(string-contains? (cadr (get loads 0)) \"otium/extra.ot\")");
  eval(rt, "(def (on-load space path) (error \"refused\"))");
  check_error(rt, "(require otium.extra :reload)", "refused");
  CHECK(ot_set_load_hook(rt, NULL));
  check_true(rt, "(begin (require otium.extra :reload) #t)");
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(interrupt_flag_and_recovery),
    TEST_ENTRY(interrupt_handler),
    TEST_ENTRY(load_hook),
};

TEST_SUITE(interrupt, tests);
