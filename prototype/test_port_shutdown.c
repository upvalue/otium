#include "internal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static Value eval(Runtime *rt, const char *s) {
    Value result;
    if(!runtime_eval(rt,s,strlen(s),&result)) { fprintf(stderr,"%s\n%s\n",s,runtime_error(rt)); abort(); }
    return result;
}
int main(void) {
    Runtime *rt=runtime_create(1024*1024,128*1024); assert(rt);
    assert(runtime_load_core(rt,"../lib")); runtime_gc_stress(rt,true);
    eval(rt,"(def sent []) (def attempts 0) (def p (make-output-port \"retry\" {:write (fn (b s e) (set! attempts (+ attempts 1)) (when (= attempts 2) (error :retry)) (push! sent (get b s)) 1)})) (write-bytevector #u8(7 8 9) p)");
    assert(!runtime_flush_ports(rt));
    assert(runtime_flush_ports(rt));
    assert(eval(rt,"(equal? sent [7 8 9])")==V_TRUE);
    eval(rt,"(close-port p) (def flushes 0) (def p (make-output-port \"flush\" {:write (fn (b s e) (- e s)) :flush (fn () (set! flushes (+ flushes 1)))})) (write-u8 1 p)");
    Value ignored; assert(!runtime_eval(rt,"(exit 7)",8,&ignored));
    int status; assert(runtime_exit_status(rt,&status) && status==7);
    assert(runtime_flush_ports(rt));
    assert(runtime_exit_status(rt,&status) && status==7);
    assert(eval(rt,"(= flushes 1)")==V_TRUE);
    eval(rt,"(close-port p) (def first nil) (def second nil) (set! first (make-output-port \"first\" {:write (fn (b s e) (- e s)) :flush (fn () (close-port second))})) (set! second (make-output-port \"second\" {:write (fn (b s e) (- e s))}))");
    assert(runtime_flush_ports(rt));
    assert(eval(rt,"(not (output-port-open? second))")==V_TRUE);
    eval(rt,"(close-port first)");
    runtime_destroy(rt); puts("host shutdown flush and retry checks passed");
}
