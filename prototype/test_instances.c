#include "internal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static void eval(Runtime *rt,const char *source,bool check) {
    Value value;
    if(!runtime_eval(rt,source,strlen(source),&value)) { fprintf(stderr,"%s\n%s\n",source,runtime_error(rt)); abort(); }
    if(check) assert(value==V_TRUE);
}
int main(void) {
    Runtime *rt=runtime_create(1024*1024,128*1024); assert(rt);
    assert(runtime_load_core(rt,"../lib")); runtime_gc_stress(rt,true);
    FILE *file=fopen("../tests/classes/checks.ot","rb"); assert(file);
    assert(!fseek(file,0,SEEK_END)); long n=ftell(file); assert(n>=0); rewind(file);
    char *source=malloc((size_t)n+1); assert(source); assert(fread(source,1,(size_t)n,file)==(size_t)n); fclose(file); source[n]=0;
    char *end=strstr(source,"(defgeneric (paint widget))"); assert(end); *end=0;
    eval(rt,source,false); free(source);
    const char *checks[]={
        "(defabstract Other) (def old-widget Widget) (defabstract Widget :is Other) (and (eq? old-widget Widget) (is-a? b Other) (is-a? 42 Object) (not (is-a? nil Number)))",
        "(defclass Moving (a :init 1)) (def m (make Moving)) (def saved-class Moving) (defclass Moving (a :init 2) (b :init [])) (and (eq? Moving saved-class) (= (m :a) 1) (array? (m :b)))",
        "(defmethod (update-instance (m Moving) old) (when (contains? old :a) (slot-set! m :required (get old :a)))) (defclass Moving required) (= (m :required) 1)",
        "(defclass Retry (a :init 1)) (def r (make Retry)) (def tries 0) (defmethod (update-instance (r Retry) old) (set! tries (+ tries 1)) (when (not (= (get old :a) 1)) (error :bad-rollback)) (slot-set! r :a 99) (when (= tries 1) (error :retry))) (defclass Retry (a :init 2) (b :init 3)) (try (r :b) (catch e #t))",
        "(and (= (r :b) 3) (= (r :a) 99) (= tries 2))",
        "(defclass InitRetry (a :init 7)) (def ir (make InitRetry)) (def init-tries 0) (defclass InitRetry (a :init 99) (b :init (begin (set! init-tries (+ init-tries 1)) (if (= init-tries 1) (error :retry) 8)))) (try (ir :b) (catch e #t))",
        "(and (= (ir :b) 8) (= (ir :a) 7) (= init-tries 2))",
        "(defclass Required (a :init 7)) (def rq (make Required)) (defclass Required a b) (try (rq :a) (catch e #t))",
        "(defclass Required (a :init 3)) (= (rq :a) 7)",
        "(def old-moving Moving) (reset-class! Moving (x :init 4)) (and (not (eq? old-moving Moving)) (is-a? m old-moving) (not (is-a? m Moving)) (= ((make Moving) :x) 4))",
        "(defclass State (x :init 1)) (def st (make State :x 9)) (defclass State (x :init 2 :state #t)) (= (st :x) 2)",
        "(equal? (str (make Moving)) \"#<Moving>\")",
        "(defclass Unchanged (x :init 1)) (def unchanged (make Unchanged)) (def hook-count 0) (defmethod (update-instance (x Unchanged) old) (set! hook-count (+ hook-count 1))) (defclass Unchanged (x :init 2)) (and (= (unchanged :x) 1) (= ((make Unchanged) :x) 2) (= hook-count 0))",
        "(def held (make Moving)) (def identity-map {held 42}) (= (get identity-map held) 42)"
    };
    for(size_t i=0;i<sizeof(checks)/sizeof(*checks);i++) eval(rt,checks[i],true);
    runtime_destroy(rt); puts("class construction, slot access and migration checks passed");
}
