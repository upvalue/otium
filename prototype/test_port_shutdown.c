#include "internal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static ot_value eval(ot_runtime* rt, const char* s) {
  ot_value result;
  if (!ot_eval(rt, s, strlen(s), &result)) {
    fprintf(stderr, "%s\n%s\n", s, ot_error(rt));
    abort();
  }
  return result;
}

int main(void) {
  ot_runtime* rt = ot_create(1024 * 1024, 128 * 1024);
  assert(rt);
  assert(ot_load_core(rt, "../lib"));
  ot_gc_stress(rt, true);
  eval(rt, "(def sent []) (def attempts 0) (def p (make-output-port \"retry\" {:write (fn (b s e) "
           "(set! attempts (+ attempts 1)) (when (= attempts 2) (error :retry)) (push! sent (get b "
           "s)) 1)})) (write-bytevector #u8(7 8 9) p)");
  assert(!ot_flush_ports(rt));
  assert(ot_flush_ports(rt));
  assert(eval(rt, "(equal? sent [7 8 9])") == OT_TRUE);
  eval(rt, "(close-port p) (def flushes 0) (def p (make-output-port \"flush\" {:write (fn (b s e) "
           "(- e s)) :flush (fn () (set! flushes (+ flushes 1)))})) (write-u8 1 p)");
  ot_value ignored;
  assert(!ot_eval(rt, "(exit 7)", 8, &ignored));
  int status;
  assert(ot_exit_status(rt, &status) && status == 7);
  assert(ot_flush_ports(rt));
  assert(ot_exit_status(rt, &status) && status == 7);
  assert(eval(rt, "(= flushes 1)") == OT_TRUE);
  eval(rt, "(close-port p) (def first nil) (def second nil) (set! first (make-output-port "
           "\"first\" {:write (fn (b s e) (- e s)) :flush (fn () (close-port second))})) (set! "
           "second (make-output-port \"second\" {:write (fn (b s e) (- e s))}))");
  assert(ot_flush_ports(rt));
  assert(eval(rt, "(not (output-port-open? second))") == OT_TRUE);
  eval(rt, "(close-port first)");
  ot_destroy(rt);
  puts("host shutdown flush and retry checks passed");
}
