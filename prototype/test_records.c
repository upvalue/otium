#include "internal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static void eval(Runtime *rt,const char *source,bool check) {
    Value result;
    if(!runtime_eval(rt,source,strlen(source),&result)) { fprintf(stderr,"%s\n%s\n",source,runtime_error(rt)); abort(); }
    if(check) assert(result==V_TRUE);
}
int main(void) {
    Runtime *rt=runtime_create(1024*1024,128*1024); assert(rt);
    assert(runtime_load_path(rt,"../tests/records/fixtures"));
    assert(runtime_load_core(rt,"../lib")); runtime_gc_stress(rt,true);
    FILE *file=fopen("../tests/records/checks.ot","rb"); assert(file);
    assert(!fseek(file,0,SEEK_END)); long n=ftell(file); assert(n>=0); rewind(file);
    char *source=malloc((size_t)n+1); assert(source);
    assert(fread(source,1,(size_t)n,file)==(size_t)n); fclose(file); source[n]=0;
    eval(rt,source,false); free(source);
    const char *checks[]={
        "(def names (list 'a 'b)) (def copied-fields (%make-record-type 'Copied names #f)) (set-car! names 'changed) (def copied (record-new copied-fields 1 2)) (and (= (copied :a) 1) (= (copied :b) 2))",
        "(def loop-fields (list 'a)) (set-cdr! loop-fields loop-fields) (try (%make-record-type 'Cycle loop-fields #f) (catch e #t))",
        "(try (%make-record-type 'Improper '(a . b) #f) (catch e #t))",
        "(try (%make-record-type 'Invalid '(a) :yes) (catch e #t))",
        "(defrecord Retained (item next)) (def first-record (record-new Retained [] nil)) (def record-index {first-record :found}) (let loop ((i 500) (previous first-record)) (if (= i 0) (begin (record-set! first-record :next previous) #t) (loop (- i 1) (record-new Retained [i] previous))))",
        "(push! (first-record :item) 42) (and (eq? (get record-index first-record) :found) (= (get (first-record :item) 0) 42) (record? (first-record :next)))",
        "(defrecord Frozen (item) :immutable #t) (def frozen (record-new Frozen [])) (push! (frozen :item) 1) (and (= (get (frozen :item) 0) 1) (try (set! (frozen :item) []) (catch e #t)))",
        "(try (copied :a :b) (catch e #t))",
        "(try (record-ref copied 'a) (catch e #t))",
        "(and (= (copied :a) 1) (eq? (get record-index first-record) :found))"
    };
    for(size_t i=0;i<sizeof(checks)/sizeof(*checks);i++) eval(rt,checks[i],true);
    runtime_destroy(rt); puts("record layouts, retention, immutability and identity checks passed");
}
