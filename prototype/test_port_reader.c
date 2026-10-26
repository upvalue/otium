#include "internal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static Value eval(Runtime *rt, const char *source) {
    Value v;
    if (!runtime_eval(rt,source,strlen(source),&v)) { fprintf(stderr,"%s\n%s\n",source,runtime_error(rt)); abort(); }
    return v;
}
int main(void) {
    Runtime *rt=runtime_create(1024*1024,128*1024); assert(rt);
    assert(runtime_load_core(rt,"../lib"));
    assert(runtime_load_path(rt,"../tests/reader/data"));
    runtime_gc_stress(rt,true);
    FILE *f=fopen("../tests/library/port-reader.ot","rb"); assert(f);
    assert(!fseek(f,0,SEEK_END)); long n=ftell(f); assert(n>=0); rewind(f);
    char *s=malloc((size_t)n+1); assert(s); assert(fread(s,1,(size_t)n,f)==(size_t)n); fclose(f); s[n]=0;
    eval(rt,s); free(s);
    assert(eval(rt,"(for-all (fn (text) (try (read (open-input-string text)) #f (catch e #t))) (drop 3 datums/negative))") == V_TRUE);
    const char *checks[]={
        "(let ((p (open-input-string \"(a)tail\"))) (and (equal? (read p) '(a)) (eq? (peek-char p) #\\t) (eq? (read p) 'tail)))",
        "(let ((p (open-input-string \"\\\"s\\\"tail\"))) (and (equal? (read p) \"s\") (eq? (read p) 'tail)))",
        "(let ((p (open-input-string \"#\\\\)tail\"))) (and (eq? (read p) #\\)) (eq? (read p) 'tail)))",
        "(let ((p (open-input-string \"#u8(1)tail\"))) (and (equal? (read p) '(bytevector 1)) (eq? (read p) 'tail)))",
        "(let ((p (open-input-string \"' ; c\\n (a . b) :tail\"))) (and (equal? (read p) '(quote (a . b))) (eq? (read p) :tail)))",
        "(let ((p (open-input-string \"; only comment\"))) (eof-object? (read p)))",
        "(let ((p (open-input-string \"   \"))) (eof-object? (read p)))",
        "(let ((p (open-input-string \"[(]) :tail\"))) (try (read p) (catch e #t)))",
        "(let ((p (open-input-string \"'\"))) (try (read p) (catch e #t)))",
        "(let ((p (open-input-string \"\\\"unfinished\"))) (try (read p) (catch e #t)))",
        "(let ((p (open-input-bytevector #u8(40 65 255 41)))) (try (read p) (catch e (and (eq? (condition-type e) 'decode-error) (= (get e :position) 2) (= (read-u8 p) 255)))))",
        "(with-params ((current-input-port (open-input-string \"[1 2]\"))) (equal? (read) '(array 1 2)))",
        "(let ((p (open-input-string \"nil #f ()\"))) (and (nil? (read p)) (eq? (read p) #f) (null? (read p)) (eof-object? (read p))))",
        "(let ((text (string-append \"(\" (string-repeat \"a \" 5000) \") :tail\")) (p (open-input-string text))) (and (= (length (read p)) 5000) (eq? (read p) :tail)))"
    };
    for(size_t i=0;i<sizeof(checks)/sizeof(*checks);i++) if(eval(rt,checks[i])!=V_TRUE) { fprintf(stderr,"failed: %s\n",checks[i]); abort(); }
    runtime_destroy(rt); puts("streaming datum boundary and GC checks passed");
}
