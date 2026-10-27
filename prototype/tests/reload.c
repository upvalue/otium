#include "helpers.h"
#include "internal.h"

TEST(redefinition_reclaims_functions_and_constants) {
  ot_state* rt = new_runtime(8192, 4096);
  ot_gc_stress(rt, true);
  eval(rt, "(def stable 'stable) (def tag :tag) (def held {}) (put! held :self held) "
           "(def current (let ((x 42)) (fn () (fn () (list x 'stable \"kept\"))))) "
           "(put! held :old current)");
  for (unsigned i = 0; i < 2000u; i++) {
    char source[512];
    snprintf(source, sizeof(source),
             "(def current (let ((x %u)) (fn () (fn () (list x 'stable \"new-%u\"))))) "
             "(car ((current)))",
             i, i);
    check_int(rt, source, i);
    check_true(rt, "(eq? stable 'stable)");
    check_true(rt, "(eq? tag :tag)");
    check_int(rt, "(car (((held :old))))", 42);
    check_error(rt, "(fn () (list \"discarded\" (fn (duplicate duplicate) duplicate)))",
                "duplicate");
    check_int(rt, "(car (((held :old))))", 42);
    CHECK(rt->function_count < 32 && rt->constant_count < 64);
  }
  eval(rt, "(put! held :old nil) (set! current nil) (set! held nil)");
  eval(rt, "nil");
  CHECK(rt->function_count < 4);
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(redefinition_reclaims_functions_and_constants),
};

TEST_MAIN(tests)
