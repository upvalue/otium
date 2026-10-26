#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static Value fail(Runtime *rt,const char *message) { rt_error(rt,message); return 0; }
static bool path_valid(Value path) {
    return rt_kind(path,K_STRING) && !memchr(rt_bytes_data(path),0,rt_bytes_length(path));
}
static int compare_names(const void *a,const void *b) {
    return strcmp(*(const char *const *)a,*(const char *const *)b);
}
static Value directory(Runtime *rt,Value path) {
    DIR *dir=opendir((char *)rt_bytes_data(path));
    if(!dir) return rt_file_error(rt,"list-directory",path,errno);
    char **names=NULL; size_t count=0,capacity=0; int error=0; bool ok=true;
    while(ok) {
        if(!runtime_poll_interrupt(rt)) { ok=false; break; }
        errno=0; struct dirent *entry=readdir(dir);
        if(!entry) { error=errno; break; }
        if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")) continue;
        if(count==UINT32_MAX) { fail(rt,"directory too large"); ok=false; break; }
        if(!rt_grow(rt,(void **)&names,&capacity,count+1,sizeof(char *))) { ok=false; break; }
        names[count]=strdup(entry->d_name);
        if(!names[count]) { fail(rt,"out of host memory"); ok=false; break; }
        count++;
    }
    if(closedir(dir) && !error) error=errno;
    Value live[]={path,V_NIL,V_NIL}; Roots roots;
    roots_push(&rt->heap,&roots,live,3);
    if(error) { rt_file_error(rt,"list-directory",live[0],error); ok=false; }
    if(ok) {
        if(count>1) qsort(names,count,sizeof(char *),compare_names);
        live[1]=rt_alloc(rt,K_ARRAY,1,sizeof(ArrayInfo)); ok=live[1]!=0;
    }
    if(ok && count) {
        Value storage=rt_alloc(rt,K_ARRAY_DATA,(uint32_t)count,0); ok=storage!=0;
        if(ok) {
            object_fields(live[1])[0]=storage;
            ((ArrayInfo *)object_bytes(live[1]))->capacity=count;
        }
    }
    for(size_t i=0;ok && i<count;i++) {
        live[2]=rt_text(rt,K_STRING,names[i],strlen(names[i])); ok=live[2]!=0;
        if(ok) {
            object_fields(object_fields(live[1])[0])[i]=live[2];
            ((ArrayInfo *)object_bytes(live[1]))->length=i+1;
        }
    }
    for(size_t i=0;i<count;i++) free(names[i]);
    free(names); Value result=ok ? live[1] : 0;
    roots_pop(&rt->heap,&roots); return result;
}
static Value metadata(Runtime *rt,const struct stat *st) {
    if(st->st_size<0 || (uintmax_t)st->st_size>(uintmax_t)(INTPTR_MAX/2)) return fail(rt,"file size exceeds integer range");
    Value live[]={V_NIL,V_NIL}; Roots roots; roots_push(&rt->heap,&roots,live,2);
    live[0]=rt_table_builtin(rt,B_TABLE,0,0); bool ok=live[0]!=0;
    const char *keys[]={":type",":size",":modified"};
    for(size_t i=0;ok && i<3;i++) {
        if(!i) {
            const char *type=S_ISREG(st->st_mode) ? ":file" : S_ISDIR(st->st_mode) ? ":directory" : ":other";
            live[1]=rt_intern(rt,K_KEYWORD,type,strlen(type));
        } else if(i==1) live[1]=value_int((intptr_t)st->st_size);
        else live[1]=rt_float(rt,(double)st->st_mtime);
        Value key=rt_intern(rt,K_KEYWORD,keys[i],strlen(keys[i]));
        ok=live[1] && key && rt_table_set(rt,live[0],key,live[1]);
    }
    Value result=ok ? live[0] : 0; roots_pop(&rt->heap,&roots); return result;
}
Value rt_filesystem_builtin(Runtime *rt,unsigned id,size_t first,size_t count) {
    if(id==B_CURRENT_DIRECTORY) {
        if(count) return fail(rt,"current-directory takes no arguments");
        size_t size=256; char *buffer=NULL;
        for(;;) {
            buffer=malloc(size); if(!buffer) return fail(rt,"out of host memory");
            if(getcwd(buffer,size)) break;
            int error=errno; free(buffer);
            if(error!=ERANGE) return rt_file_error(rt,"current-directory",V_NIL,error);
            if(size>SIZE_MAX/2) return fail(rt,"directory path too long");
            size*=2;
        }
        Value result=rt_text(rt,K_STRING,buffer,strlen(buffer)); free(buffer); return result;
    }
    if(count!=(id==B_RENAME_FILE ? 2u : 1u)) return fail(rt,"wrong number of filesystem arguments");
    for(size_t i=0;i<count;i++) if(!path_valid(rt->stack[first+i])) return fail(rt,"expected path without NUL");
    Value path=rt->stack[first]; const char *name=(char *)rt_bytes_data(path);
    if(id==B_LIST_DIRECTORY) return directory(rt,path);
    const char *op=id==B_FILE_EXISTS ? "file-exists?" : id==B_DELETE_FILE ? "delete-file" :
        id==B_RENAME_FILE ? "rename-file" : id==B_FILE_INFO ? "file-info" :
        id==B_FILE_LINK ? "file-symbolic-link?" : "make-directory";
    struct stat st; int result;
    if(id==B_DELETE_FILE) result=unlink(name);
    else if(id==B_RENAME_FILE) result=rename(name,(char *)rt_bytes_data(rt->stack[first+1]));
    else if(id==B_MAKE_DIRECTORY) result=mkdir(name,0777);
    else result=id==B_FILE_LINK ? lstat(name,&st) : stat(name,&st);
    if(result) {
        int error=errno;
        if((id==B_FILE_EXISTS || id==B_FILE_LINK) && (error==ENOENT || error==ENOTDIR)) return V_FALSE;
        return rt_file_error(rt,op,path,error);
    }
    if(id==B_FILE_EXISTS) return V_TRUE;
    if(id==B_FILE_LINK) return S_ISLNK(st.st_mode) ? V_TRUE : V_FALSE;
    if(id==B_FILE_INFO) return metadata(rt,&st);
    return V_NIL;
}
