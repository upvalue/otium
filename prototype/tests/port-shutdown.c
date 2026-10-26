#include "helpers.h"

static ot_runtime* shutdown_runtime(void) {
  ot_runtime* rt = new_core_runtime();
  ot_gc_stress(rt, true);
  return rt;
}

TEST(host_flush_retries_failed_write) {
  ot_runtime* rt = shutdown_runtime();
  eval(rt, "(def sent []) (def attempts 0) (def p (make-output-port \"retry\" {:write (fn (b s e) "
           "(set! attempts (+ attempts 1)) (when (= attempts 2) (error :retry)) (push! sent (get b "
           "s)) 1)})) (write-bytevector #u8(7 8 9) p)");
  CHECK(!ot_flush_ports(rt));
  CHECK(ot_flush_ports(rt));
  check_true(rt, "(equal? sent [7 8 9])");
  eval(rt, "(close-port p)");
  ot_destroy(rt);
}

TEST(host_flush_after_exit_keeps_status) {
  ot_runtime* rt = shutdown_runtime();
  eval(rt, "(def flushes 0) (def p (make-output-port \"flush\" {:write (fn (b s e) "
           "(- e s)) :flush (fn () (set! flushes (+ flushes 1)))})) (write-u8 1 p)");
  ot_value ignored;
  CHECK(!ot_eval(rt, "(exit 7)", 8, &ignored));
  int status;
  CHECK(ot_exit_status(rt, &status) && status == 7);
  CHECK(ot_flush_ports(rt));
  CHECK(ot_exit_status(rt, &status) && status == 7);
  check_true(rt, "(= flushes 1)");
  eval(rt, "(close-port p)");
  ot_destroy(rt);
}

TEST(host_flush_survives_port_closed_by_flush) {
  ot_runtime* rt = shutdown_runtime();
  eval(rt, "(def first nil) (def second nil) (set! first (make-output-port "
           "\"first\" {:write (fn (b s e) (- e s)) :flush (fn () (close-port second))})) (set! "
           "second (make-output-port \"second\" {:write (fn (b s e) (- e s))}))");
  CHECK(ot_flush_ports(rt));
  check_true(rt, "(not (output-port-open? second))");
  eval(rt, "(close-port first)");
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(host_flush_retries_failed_write),
    TEST_ENTRY(host_flush_after_exit_keeps_status),
    TEST_ENTRY(host_flush_survives_port_closed_by_flush),
};

TEST_MAIN(tests)
