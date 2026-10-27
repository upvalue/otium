/* A cut-down copy of the runtime's declarations, enough for the fixtures. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef uintptr_t ot_value;
enum { OT_NIL = 2, OT_FALSE = 6, OT_TRUE = 10, OT_EMPTY = 14 };

typedef struct oti_roots oti_roots;
struct oti_roots {
  oti_roots* previous;
  ot_value* slots;
  size_t count;
};

typedef struct {
  oti_roots* roots;
} oti_heap;

typedef struct ot_state {
  oti_heap heap;
  ot_value* stack;
  size_t sp;
  size_t* frames;
  size_t frame_count;
  ot_value (*hook)(struct ot_state*);
} ot_state;

typedef struct {
  size_t size;
  uint32_t fields, kind;
} oti_object_header;

typedef struct {
  oti_object_header header;
  ot_value car, cdr;
} oti_pair_obj;

void oti_roots_push(oti_heap* h, oti_roots* r, ot_value* slots, size_t count);
void oti_roots_pop(oti_heap* h, oti_roots* r);
ot_value oti_heap_alloc(oti_heap* h, uint32_t kind, uint32_t fields, size_t bytes);
bool oti_stack(ot_state* rt, size_t needed);
bool oti_error(ot_state* rt, const char* message);

static inline ot_value oti_alloc(ot_state* rt, uint32_t kind, uint32_t fields, size_t bytes) {
  return oti_heap_alloc(&rt->heap, kind, fields, bytes);
}

static inline ot_value* object_fields(ot_value object) {
  return (ot_value*)((oti_object_header*)object + 1);
}

static inline void* object_bytes(ot_value object) { return object_fields(object) + 2; }

static inline oti_pair_obj* oti_as_pair(ot_value v) { return (oti_pair_obj*)v; }

static inline bool ot_is_int(ot_value v) { return (v & 1) != 0; }
static inline intptr_t ot_to_int(ot_value v) { return (intptr_t)v >> 1; }
static inline ot_value ot_int(intptr_t n) { return ((uintptr_t)n << 1) | 1; }

static inline ot_value oti_cons(ot_state* rt, ot_value car, ot_value cdr) {
  ot_value v[] = {car, cdr};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, v, 2);
  ot_value pair = oti_alloc(rt, 1, 2, 0);
  if (pair) {
    oti_as_pair(pair)->car = v[0];
    oti_as_pair(pair)->cdr = v[1];
  }
  oti_roots_pop(&rt->heap, &roots);
  return pair;
}
