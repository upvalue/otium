typedef unsigned long otv;
typedef struct ot_state ots;

typedef struct ot_string_obj {
  unsigned long header;
  otv bytes;
  unsigned long length;
} ot_string_obj;

#define OT_GC_MAY_ALLOCATE __attribute__((annotate("otium.gc.may_allocate")))
#define OT_GC_ALLOW_STALE_BORROW(REASON)                                                           \
  __attribute__((annotate("otium.gc.allow_stale_borrow:" REASON)))

OT_GC_MAY_ALLOCATE void grow(ots* state);
void consume(unsigned long length);

void unsafe(ots* state, otv value) {
  ot_string_obj* string = (ot_string_obj*)value;
  grow(state);
  consume(string->length);
}

void safe(ots* state, otv value) {
  ot_string_obj* string = (ot_string_obj*)value;
  grow(state);
  string = (ot_string_obj*)value;
  consume(string->length);
}

void escaped(ots* state, otv value) {
  ot_string_obj* string OT_GC_ALLOW_STALE_BORROW("pinned by the embedding host") =
      (ot_string_obj*)value;
  grow(state);
  consume(string->length);
}

void empty_escape(ots* state, otv value) {
  ot_string_obj* string OT_GC_ALLOW_STALE_BORROW("") = (ot_string_obj*)value;
  grow(state);
  consume(string->length);
}
