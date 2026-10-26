#include "internal.h"
#include <stdlib.h>
#include <string.h>
static Value fail(Runtime *rt,const char *message) { rt_error(rt,message); return 0; }
static size_t field_count(Value type) { return ((HeapObject *)type)->fields-1; }
static size_t field_index(Value type,Value key) {
    if(!rt_kind(key,K_KEYWORD)) return SIZE_MAX;
    for(size_t i=0;i<field_count(type);i++) if(object_fields(type)[i+1]==key) return i+1;
    return SIZE_MAX;
}
Value rt_record_call(Runtime *rt,Value record,size_t first,size_t count) {
    if(count!=1) return fail(rt,"record call needs one field keyword");
    size_t index=field_index(object_fields(record)[0],rt->stack[first]);
    return index==SIZE_MAX ? fail(rt,"unknown record field") : object_fields(record)[index];
}
static Value make_type(Runtime *rt,size_t first,size_t count) {
    if(count!=3 || !rt_kind(rt->stack[first],K_SYMBOL) ||
       (rt->stack[first+2]!=V_TRUE && rt->stack[first+2]!=V_FALSE)) return fail(rt,"invalid record type declaration");
    Value cursor=rt->stack[first+1],slow=cursor,fast=cursor; size_t n=0;
    while(rt_kind(cursor,K_PAIR)) {
        if(!rt_kind(object_fields(cursor)[0],K_SYMBOL)) return fail(rt,"record fields must be symbols");
        if(n==UINT32_MAX-1) return fail(rt,"too many record fields");
        n++; cursor=object_fields(cursor)[1];
        if(rt_kind(slow,K_PAIR)) slow=object_fields(slow)[1];
        for(unsigned i=0;i<2 && rt_kind(fast,K_PAIR);i++) fast=object_fields(fast)[1];
        if(rt_kind(fast,K_PAIR) && slow==fast) return fail(rt,"cyclic record field list");
    }
    if(cursor!=V_EMPTY) return fail(rt,"record fields must form a proper list");
    Value live[]={V_NIL,rt->stack[first+1]}; Roots roots; roots_push(&rt->heap,&roots,live,2);
    live[0]=rt_alloc(rt,K_RECORD_TYPE,(uint32_t)n+1,sizeof(bool)); bool ok=live[0]!=0;
    if(ok) {
        object_fields(live[0])[0]=rt->stack[first];
        *(bool *)object_bytes(live[0])=rt->stack[first+2]==V_TRUE;
    }
    for(size_t i=0;ok && i<n;i++) {
        Value name=object_fields(live[1])[0]; size_t length=rt_bytes_length(name);
        char *text=malloc(length+2);
        if(!text) { fail(rt,"out of host memory"); ok=false; break; }
        text[0]=':'; memcpy(text+1,rt_bytes_data(name),length); text[length+1]=0;
        Value key=rt_intern(rt,K_KEYWORD,text,length+1); free(text); ok=key!=0;
        if(ok) {
            for(size_t j=0;j<i;j++) if(object_fields(live[0])[j+1]==key) { fail(rt,"duplicate record field"); ok=false; break; }
            object_fields(live[0])[i+1]=key;
        }
        live[1]=object_fields(live[1])[1];
    }
    Value result=ok ? live[0] : 0; roots_pop(&rt->heap,&roots); return result;
}
Value rt_record_builtin(Runtime *rt,unsigned id,size_t first,size_t count) {
    if(id==B_MAKE_RECORD_TYPE) return make_type(rt,first,count);
    size_t wanted=id==B_RECORD_REF ? 2 : id==B_RECORD_SET ? 3 : 1;
    if(id!=B_RECORD_NEW && count!=wanted) return fail(rt,"wrong number of record arguments");
    if(!count) return fail(rt,"record-new needs a record type");
    Value v=rt->stack[first];
    if(id==B_RECORD_P) return rt_kind(v,K_RECORD) ? V_TRUE : V_FALSE;
    if(id==B_RECORD_TYPE_P) return rt_kind(v,K_RECORD_TYPE) ? V_TRUE : V_FALSE;
    if(id==B_RECORD_NEW) {
        if(!rt_kind(v,K_RECORD_TYPE)) return fail(rt,"expected record type");
        if(count-1!=field_count(v)) return fail(rt,"wrong number of record fields");
        Value record=rt_alloc(rt,K_RECORD,(uint32_t)count,0);
        if(record) memcpy(object_fields(record),rt->stack+first,count*sizeof(Value));
        return record;
    }
    if(!rt_kind(v,K_RECORD)) return fail(rt,"expected record instance");
    Value type=object_fields(v)[0];
    if(id==B_RECORD_TYPE) return type;
    size_t index=field_index(type,rt->stack[first+1]);
    if(index==SIZE_MAX) return fail(rt,"unknown record field");
    if(id==B_RECORD_REF) return object_fields(v)[index];
    if(*(bool *)object_bytes(type)) return fail(rt,"cannot mutate immutable record");
    return object_fields(v)[index]=rt->stack[first+2];
}
