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
    assert(runtime_load_core(rt,"../lib")); runtime_gc_stress(rt,true);
    FILE *file=fopen("../tests/classes/checks.ot","rb"); assert(file);
    assert(!fseek(file,0,SEEK_END)); long n=ftell(file); assert(n>=0); rewind(file);
    char *source=malloc((size_t)n+1); assert(source);
    assert(fread(source,1,(size_t)n,file)==(size_t)n); fclose(file); source[n]=0;
    eval(rt,source,false); free(source);
    const char *checks[]={
        "(defgeneric (snapshot x)) (defmethod (snapshot x) :old) (defmethod (snapshot (x String)) :excluded) (defmethod (snapshot (x Number)) :next next next) (def saved-next (snapshot 1)) (and (procedure? saved-next) (eq? (type saved-next) :function) (is-a? saved-next Procedure) (not (generic? saved-next)))",
        "(remove-method! snapshot (list Object)) (defmethod (snapshot x) :new) (and (eq? (saved-next 2) :old) (eq? (saved-next \"string\") :old) (eq? ((snapshot 1) 2) :new))",
        "(def old-snapshot snapshot) (reset-generic! (snapshot x)) (defmethod (snapshot x) :reset) (and (not (eq? snapshot old-snapshot)) (eq? (snapshot 2) :reset) (eq? ((old-snapshot 1) 2) :new) (eq? (saved-next 2) :old))",
        "(defgeneric (tail-next n)) (defmethod (tail-next n) (if (= n 0) #t (tail-next (- n 1)))) (defmethod (tail-next (n Number)) :next next (next n)) (tail-next 2000)",
        "(defgeneric (around-next x)) (defmethod (around-next x) x) (defmethod :around (around-next (x Number)) :next next (next (+ x 1))) (= (around-next 41) 42)",
        "(try (saved-next) (catch e #t))",
        "(try (saved-next 1 2) (catch e #t))",
        "(eq? (saved-next 2) :old)"
    };
    for(size_t i=0;i<sizeof(checks)/sizeof(*checks);i++) eval(rt,checks[i],true);
    runtime_destroy(rt); puts("method snapshots, replacement arguments and tail calls passed");
}
