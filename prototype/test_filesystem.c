#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static void eval(Runtime *rt,const char *source,bool check) {
    Value value;
    if(!runtime_eval(rt,source,strlen(source),&value)) {
        fprintf(stderr,"%s\n%s\n",source,runtime_error(rt)); abort();
    }
    if(check) assert(value==V_TRUE);
}
int main(void) {
    char root[]="/tmp/otium-fs-XXXXXX"; assert(mkdtemp(root));
    Runtime *rt=runtime_create(1024*1024,128*1024); assert(rt);
    assert(runtime_load_core(rt,"../lib")); runtime_gc_stress(rt,true);
    const char *args[]={root}; assert(runtime_set_arguments(rt,NULL,1,args));
    FILE *file=fopen("../tests/library/port-files.ot","rb"); assert(file);
    assert(!fseek(file,0,SEEK_END)); long length=ftell(file); assert(length>=0); rewind(file);
    char *source=malloc((size_t)length+1); assert(source);
    assert(fread(source,1,(size_t)length,file)==(size_t)length); fclose(file); source[length]=0;
    eval(rt,source,false); free(source);
    char link[256],target[256];
    snprintf(link,sizeof(link),"%s/link",root); assert(!symlink("directory/a",link));
    snprintf(target,sizeof(target),"%s/dangling",root); assert(!symlink("missing",target));
    eval(rt,"(and (file-regular? (str root \"/link\")) (file-symbolic-link? (str root \"/link\")) (file-symbolic-link? (str root \"/dangling\")) (not (file-exists? (str root \"/dangling\"))) (not (file-symbolic-link? (str root \"/absent\"))))",true);
    eval(rt,"(and (path-absolute? (current-directory)) (equal? (list-directory root) [\"dangling\" \"directory\" \"link\"]))",true);
    eval(rt,"(try (file-info (str root \"/absent\")) (catch e (and (condition-of-type? e 'file-error) (eq? (get e :op) 'file-info) (eq? (get e :reason) :not-found))))",true);
    eval(rt,"(delete-file (str root \"/link\")) (file-exists? (str root \"/directory/a\"))",true);
    eval(rt,"(try (rename-file (str root \"/absent\") (str root \"/target\")) (catch e (eq? (get e :reason) :not-found)))",true);
    eval(rt,"(def nul-path (string-append root (str (integer->char 0)) \"/child\")) (try (file-exists? nul-path) (catch e (equal? (condition-message e) \"expected path without NUL\")))",true);
    assert(!unlink(target));
    snprintf(target,sizeof(target),"%s/directory/a",root); assert(!unlink(target));
    snprintf(target,sizeof(target),"%s/directory",root); assert(!rmdir(target));
    runtime_destroy(rt); assert(!rmdir(root));
    puts("filesystem metadata, symlinks and public file checks passed");
}
