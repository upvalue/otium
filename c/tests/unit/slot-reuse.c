#include "helpers.h"
#include "internal.h"
#include <string.h>

static oti_function* function(ot_state* rt, const char* name) {
  for (size_t i = 0; i < rt->global_count; i++)
    if (!strcmp(rt->global_info[i].name, name)) {
      CHECK(oti_kind(rt->globals[i], K_CLOSURE));
      uint32_t id;
      memcpy(&id, object_bytes(rt->globals[i]), sizeof(id));
      return rt->functions[id];
    }
  test_fail(__FILE__, __LINE__, "no global named %s", name);
}

TEST(sequential_scopes_share_slots) {
  char source[8192] = "(def (many) ";
  for (size_t i = 0; i < 256; i++) strcat(source, "(let ((x 42)) x) ");
  strcat(source, "42) (many)");
  ot_state* rt = new_runtime(16384, 32);
  ot_gc_stress(rt, true);
  check_int(rt, source, 42);
  /* Sequential scopes reuse registers: result, discarded value and binding. */
  CHECK_EQ_INT(function(rt, "many")->locals, 3);
  ot_destroy(rt);
}

TEST(captured_and_nested_scopes) {
  ot_state* rt = new_runtime(16384, 32);
  ot_gc_stress(rt, true);
  check_int(rt,
            "(def (mixed) (let ((a nil) (b nil) (c 0)) "
            "(let ((x 10)) (set! a (fn () (set! x (+ x 1)) x))) "
            "(let ((x 20)) (set! b (fn () (set! x (+ x 1)) x))) "
            "(let ((x (cons 30 nil))) (set! c (car x))) "
            "(+ (a) (a) (b) c))) (mixed)",
            74);
  oti_function* f = function(rt, "mixed");
  CHECK_EQ_INT(f->locals, 9);
  check_int(rt,
            "(def (nested) (let ((left (let ((x 40)) (fn () (fn () x)))) "
            "(right (let ((x 2)) (fn () x)))) (+ ((left)) (right)))) (nested)",
            42);
  check_int(rt,
            "((fn (a &optional (b (let ((t 1)) (+ a t))) "
            "(c (let ((t 2)) (+ b t)))) (+ a b c)) 10)",
            34);
  check_int(rt, "((fn (a . xs) (let ((x 1)) x) (+ a (car xs))) 40 2)", 42);
  check_int(rt,
            "(let ((result 0) (i 0)) (while (< i 20) "
            "(def x (+ i 1)) (if (< i 10) (let ((x 1)) (set! result (+ result x))) "
            "(let ((x 2)) (set! result (+ result x)))) (set! i x)) "
            "(let ((x 12)) (+ result x)))",
            42);
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(sequential_scopes_share_slots),
    TEST_ENTRY(captured_and_nested_scopes),
};

TEST_SUITE(slot_reuse, tests);
