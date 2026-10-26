#include "internal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    Runtime *rt=runtime_create(4*1024*1024,256*1024); assert(rt);
    assert(runtime_load_path(rt,"../tests/reader/data"));
    assert(runtime_load_core(rt,"../lib")); runtime_gc_stress(rt,true);
    FILE *file=fopen("../tests/reader/tokens.ot","rb"); assert(file);
    assert(!fseek(file,0,SEEK_END)); long n=ftell(file); assert(n>=0); rewind(file);
    char *source=malloc((size_t)n+1); assert(source);
    assert(fread(source,1,(size_t)n,file)==(size_t)n); fclose(file); source[n]=0;
    Value result;
    if(!runtime_eval(rt,source,(size_t)n,&result)) { fprintf(stderr,"%s\n",runtime_error(rt)); abort(); }
    free(source);
    const char *large="(require otium.reader :as reader) (= (length (reader/tokens (string-repeat \"λ ; comment\\r\\n\" 1000))) 2000)";
    assert(runtime_eval(rt,large,strlen(large),&result) && result==V_TRUE);
    const char *retained="(def saved-tokens (reader/tokens \"(a . b)\")) (reader/tokens \"[]\") (equal? saved-tokens '((:open \"(\" 0 0 0) (:atom \"a\" 0 1 0) (:atom \".\" 0 3 0) (:atom \"b\" 0 5 0) (:close \")\" 0 6 0)))";
    assert(runtime_eval(rt,retained,strlen(retained),&result) && result==V_TRUE);
    runtime_destroy(rt); puts("token source preservation, collection and position checks passed");
}
