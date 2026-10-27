/* Each line that should be reported ends in a "warn: KIND" comment. */
#include "runtime.h"

ot_value value_across_allocation(ot_state* rt, ot_value car) {
  ot_value pair = oti_alloc(rt, 1, 2, 0);
  if (!pair) return 0;
  oti_as_pair(pair)->car = car; /* warn: stale-value */
  return pair;
}

ot_value rooted_value(ot_state* rt, ot_value car) {
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, &car, 1);
  ot_value pair = oti_alloc(rt, 1, 2, 0);
  if (pair) oti_as_pair(pair)->car = car;
  oti_roots_pop(&rt->heap, &roots);
  return pair;
}

ot_value rooted_array(ot_state* rt, ot_value car, ot_value cdr) {
  ot_value live[] = {car, cdr};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 2);
  ot_value pair = oti_alloc(rt, 1, 2, 0);
  if (pair) {
    oti_as_pair(pair)->car = live[0];
    oti_as_pair(pair)->cdr = cdr; /* warn: stale-value */
  }
  oti_roots_pop(&rt->heap, &roots);
  return pair;
}

ot_value used_after_pop(ot_state* rt, ot_value car) {
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, &car, 1);
  ot_value first = oti_alloc(rt, 1, 2, 0);
  oti_roots_pop(&rt->heap, &roots);
  if (!first) return 0;
  ot_value second = oti_alloc(rt, 1, 2, 0);
  if (second) oti_as_pair(second)->car = car; /* warn: stale-value */
  return first; /* warn: stale-value */
}

/* Values re-read from rooted places are fresh, and immediates never move. */
ot_value reread(ot_state* rt, size_t slot) {
  ot_value v = rt->stack[slot];
  ot_value n = ot_int(3);
  if (!oti_alloc(rt, 1, 2, 0)) return 0;
  v = rt->stack[slot];
  return v + n;
}

/* Tag tests read only the value's bits, which a move doesn't change. */
intptr_t tag_tests(ot_state* rt, ot_value v) {
  if (!oti_alloc(rt, 1, 2, 0)) return 0;
  if (v == OT_NIL || !v) return 0;
  if (ot_is_int(v)) return ot_to_int(v);
  return (v & 7) == 2;
}

static ot_value helper(ot_state* rt) { return oti_alloc(rt, 1, 2, 0); }
static ot_value outer_helper(ot_state* rt) { return helper(rt); }

/* Effects carry up the call graph. */
ot_value transitive(ot_state* rt, ot_value car) {
  if (!outer_helper(rt)) return 0;
  return car; /* warn: stale-value */
}

ot_value indirect(ot_state* rt, ot_value car) {
  rt->hook(rt);
  return car; /* warn: stale-value */
}

ot_value object_pointer(ot_state* rt, ot_value pair) {
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, &pair, 1);
  oti_pair_obj* p = oti_as_pair(pair);
  ot_value fresh = oti_alloc(rt, 1, 2, 0);
  p->car = fresh; /* warn: stale-object */
  p = oti_as_pair(pair);
  p->cdr = fresh;
  oti_roots_pop(&rt->heap, &roots);
  return pair;
}

ot_value field_pointer(ot_state* rt, ot_value pair) {
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, &pair, 1);
  ot_value* fields = object_fields(pair);
  ot_value fresh = oti_alloc(rt, 1, 2, 0);
  fields[0] = fresh; /* warn: stale-object */
  oti_roots_pop(&rt->heap, &roots);
  return pair;
}

bool stack_pointer(ot_state* rt, size_t first) {
  ot_value* args = rt->stack + first;
  ot_value* also = &rt->stack[first];
  if (!oti_stack(rt, rt->sp + 8)) return false;
  args[0] = OT_NIL; /* warn: stale-array */
  also[0] = OT_NIL; /* warn: stale-array */
  ot_value* fresh = rt->stack + first;
  fresh[0] = OT_NIL;
  return true;
}

/* Growing any ot_state array moves it, which the tool works out from the
 * assignment. */
void* realloc(void* p, size_t n);

static bool push_frame(ot_state* rt) {
  size_t* frames = realloc(rt->frames, (rt->frame_count + 1) * sizeof(size_t));
  if (!frames) return false;
  rt->frames = frames;
  rt->frames[rt->frame_count++] = 0;
  return true;
}

static bool enter(ot_state* rt) { return push_frame(rt); }

bool frame_pointer(ot_state* rt) {
  size_t* top = &rt->frames[rt->frame_count - 1];
  ot_value* args = rt->stack;
  if (!enter(rt)) return false;
  args[0] = OT_NIL;
  return *top == 0; /* warn: stale-array */
}

/* A collection doesn't move the stack, and stack growth doesn't move objects. */
bool separate_effects(ot_state* rt, ot_value pair, size_t first) {
  ot_value* args = rt->stack + first;
  oti_pair_obj* p = oti_as_pair(pair);
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, &pair, 1);
  oti_heap_alloc(&rt->heap, 1, 2, 0);
  oti_roots_pop(&rt->heap, &roots);
  args[0] = pair;
  p = oti_as_pair(pair);
  if (!oti_stack(rt, 8)) return false;
  return p->car == OT_NIL;
}

/* Callees that keep using a pointer argument after they collect. */
static size_t copy_after(ot_state* rt, const char* text, size_t length) {
  ot_value s = oti_alloc(rt, 5, 0, length);
  if (!s) return 0;
  memcpy(object_bytes(s), text, length);
  return length;
}

static size_t copy_before(ot_state* rt, const char* text, size_t length) {
  char buffer[16];
  memcpy(buffer, text, length < 16 ? length : 16);
  return oti_alloc(rt, 5, 0, length) ? length : 0;
}

static size_t forward(ot_state* rt, const char* text, size_t length) {
  return copy_after(rt, text, length);
}

size_t managed_argument(ot_state* rt, ot_value string) {
  const char* text = object_bytes(string);
  size_t n = copy_before(rt, text, 4);
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, &string, 1);
  n += copy_after(rt, object_bytes(string), 4); /* warn: stale-argument */
  n += forward(rt, object_bytes(string), 4); /* warn: stale-argument */
  oti_roots_pop(&rt->heap, &roots);
  return n;
}

/* An out parameter that the callee writes after it collects. */
static bool make(ot_state* rt, ot_value* out) {
  ot_value v = oti_alloc(rt, 1, 2, 0);
  *out = v;
  return v != 0;
}

/* A slot the callee reads after it collects; it must be rooted. */
static bool fill(ot_state* rt, ot_value* pair) {
  ot_value v = oti_alloc(rt, 1, 2, 0);
  if (!v) return false;
  oti_as_pair(*pair)->car = v;
  return true;
}

/* A slot the callee roots itself. */
static bool fill_rooted(ot_state* rt, ot_value* pair) {
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, pair, 1);
  ot_value v = oti_alloc(rt, 1, 2, 0);
  if (v) oti_as_pair(*pair)->car = v;
  oti_roots_pop(&rt->heap, &roots);
  return v != 0;
}

ot_value slots(ot_state* rt) {
  ot_value pair;
  if (!make(rt, &pair)) return 0;
  if (!fill_rooted(rt, &pair)) return 0;
  if (!fill(rt, &pair)) return 0; /* warn: stale-argument */
  /* fill doesn't store a fresh value, so this one is stale too. */
  return pair; /* warn: stale-value */
}

ot_value unsequenced(ot_state* rt, ot_value car) {
  ot_value cdr = rt->stack[0];
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, &cdr, 1);
  ot_value pair = oti_cons(rt, car, oti_cons(rt, cdr, OT_EMPTY)); /* warn: unsequenced */
  oti_roots_pop(&rt->heap, &roots);
  return pair;
}

/* The fast path writes v only when the whole condition holds. */
static inline bool add(ot_value a, ot_value b, ot_value* out) {
  *out = a + b - 1;
  return true;
}

ot_value conjunction(ot_state* rt, ot_value* registers) {
  ot_value v = registers[0];
  if (!oti_alloc(rt, 1, 2, 0)) return 0;
  ot_value x = registers[1], y = registers[2];
  if (__builtin_expect(!!(ot_is_int(x) && ot_is_int(y) && add(x, y, &v)), 1)) return v;
  return v; /* warn: stale-value */
}

/* Invalidation reaches a use around loops. */
ot_value loop(ot_state* rt, ot_value car, int n) {
  ot_value result = OT_NIL;
  for (int i = 0; i < n; i++) {
    if (i) result = car; /* warn: stale-value */
    if (!oti_alloc(rt, 1, 2, 0)) return 0;
  }
  return result; /* warn: stale-value */
}

/* gc-lint.conf in this directory accepts this one. */
ot_value accepted(ot_state* rt, ot_value pinned) {
  if (!oti_alloc(rt, 1, 2, 0)) return 0;
  return pinned;
}

int choice(void);

/* Two invalidations circulate through overlapping cycles; the analysis must
 * still settle. */
void cycles(ot_state* rt, ot_value value) {
  goto use;
use:
  rt->stack[0] = value; /* warn: stale-value */
  if (choice()) goto left;
  else goto branch;
left:
  oti_alloc(rt, 1, 2, 0);
  if (choice()) goto back;
  else goto left;
right:
  oti_stack(rt, 8);
  oti_heap_alloc(&rt->heap, 1, 2, 0);
  if (choice()) goto join;
  else goto back;
join:
  if (choice()) goto use;
  else goto left;
branch:
  if (choice()) goto right;
  else goto back;
back:
  if (choice()) goto use;
  else goto branch;
}

/* An unrooted array goes stale as a whole; storing one element refreshes only
 * that element. */
ot_value array(ot_state* rt, ot_value car, ot_value cdr) {
  ot_value pair[2] = {car, cdr};
  if (!oti_alloc(rt, 1, 2, 0)) return 0;
  pair[0] = rt->stack[0];
  return pair[1]; /* warn: stale-value */
}
