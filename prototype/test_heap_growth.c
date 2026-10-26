#include "heap.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    Heap h; assert(heap_init(&h,256)); assert(h.capacity==h.limit);
    Value live[]={V_NIL,V_NIL,V_NIL}; Roots roots; roots_push(&h,&roots,live,3);
    live[0]=heap_alloc(&h,1,2,0); assert(live[0]);
    object_fields(live[0])[0]=live[0]; object_fields(live[0])[1]=value_int(42);
    live[1]=live[0]; uint32_t identity=object_identity(live[0]);
    assert(!heap_alloc(&h,2,0,300)); assert(h.capacity==256);
    assert(heap_set_limit(&h,4096));
    live[2]=heap_alloc(&h,2,0,1000); assert(live[2]);
    assert(h.capacity==2048 && live[0]==live[1]);
    assert(object_fields(live[0])[0]==live[0] && object_identity(live[0])==identity);
    memset(object_bytes(live[2]),0x55,1000);
    object_fields(live[0])[1]=live[2];
    live[2]=heap_alloc(&h,3,0,2000); assert(live[2]); assert(h.capacity==4096);
    assert(live[0]==live[1] && object_fields(live[0])[0]==live[0]);
    assert(object_identity(live[0])==identity);
    unsigned char *bytes=object_bytes(object_fields(live[0])[1]);
    for(size_t i=0;i<1000;i++) assert(bytes[i]==0x55);
    assert(!heap_set_limit(&h,2048)); assert(!heap_set_limit(&h,SIZE_MAX)); assert(h.limit==4096);
    assert(!heap_alloc(&h,4,0,2000)); assert(h.capacity==4096);
    assert(live[0]==live[1] && object_fields(live[0])[0]==live[0]);
    live[2]=V_NIL; assert(heap_alloc(&h,4,0,2000));
    roots_pop(&h,&roots); heap_destroy(&h);
    assert(heap_init(&h,256)); assert(heap_set_limit(&h,4096));
    for(size_t i=0;i<1000;i++) assert(heap_alloc(&h,1,0,128));
    assert(h.capacity==256); heap_destroy(&h);
    puts("bounded heap growth, cycles, aliases and exhaustion recovery passed");
}
