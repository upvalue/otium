#include "internal.h"
#include <stdlib.h>
#include <string.h>

void oti_free_function(oti_function* fn) {
  if (!fn) return;
  free(fn->code);
  free(fn->captures);
  free(fn->points);
  free(fn->name);
  free(fn);
}

typedef struct {
  ot_value value;
  bool function;
} oti_reclaim_task;

typedef struct {
  ot_state* rt;
  unsigned char *functions, *constants;
  oti_reclaim_task* work;
  size_t count, capacity;
} oti_reclaim_mark;

static bool enqueue(oti_reclaim_mark* m, ot_value value, bool function) {
  if (!oti_grow(m->rt, (void**)&m->work, &m->capacity, m->count + 1, sizeof(oti_reclaim_task)))
    return false;
  m->work[m->count++] = (oti_reclaim_task){value, function};
  return true;
}

static bool mark_function(oti_reclaim_mark* m, uint32_t id) {
  if (id >= m->rt->function_count || !m->rt->functions[id])
    return oti_error(m->rt, "invalid live function");
  if (m->functions[id]) return true;
  m->functions[id] = 1;
  return enqueue(m, id, true);
}

static bool reached(ot_value value) {
  return (((oti_object_header*)value)->gc & OTI_GC_RECLAIM) != 0;
}

static bool mark_value(oti_reclaim_mark* m, ot_value value) {
  if (!value_is_ptr(value)) return true;
#ifdef OTIUM_CHECK_KINDS
  /* A walk of every chunk and large object, so only in checking builds. */
  if (!oti_heap_contains(&m->rt->heap, value)) return oti_error(m->rt, "invalid live object");
#endif
  if (reached(value)) return true;
  ((oti_object_header*)value)->gc |= OTI_GC_RECLAIM;
  return enqueue(m, value, false);
}

static bool mark_constant(oti_reclaim_mark* m, uint32_t id) {
  if (id >= m->rt->constant_count || !m->rt->constant_state[id])
    return oti_error(m->rt, "invalid live constant");
  m->constants[id] = 1;
  return mark_value(m, m->rt->constants[id]);
}

typedef struct {
  oti_reclaim_mark* mark;
  bool ok;
} oti_reclaim_visit;

#ifdef OTIUM_NATIVE
static bool mark_code_constant(void* mark, uint32_t id) { return mark_constant(mark, id); }
#endif

static void mark_root(void* context, ot_value* slot) {
  oti_reclaim_visit* v = context;
  if (v->ok) v->ok = mark_value(v->mark, *slot);
}

bool oti_reclaim(ot_state* rt) {
  if (rt->frame_count || rt->compiling || rt->evaluating || rt->eval_depth)
    return oti_error(rt, "code reclamation requires an evaluation boundary");
  /* Other threads may be running code, or compiling it, that only their
   * frames reach, so code is reclaimed only while one thread is left. */
  if (oti_thread_count(rt) > 1) return true;
  oti_reclaim_mark m = {.rt = rt};
  m.functions = calloc(rt->function_count + 1, 1);
  m.constants = calloc(rt->constant_count + 1, 1);
  bool ok = m.functions && m.constants;
  if (!ok) oti_error(rt, "out of host memory");
  for (oti_roots* root = rt->heap.roots; ok && root; root = root->previous) {
    if (root == &rt->constant_roots) continue;
    for (size_t i = 0; ok && i < root->count; i++) ok = mark_value(&m, root->slots[i]);
  }
  if (ok && rt->heap.hook) {
    oti_reclaim_visit visit = {&m, true};
    rt->heap.hook(rt->heap.hook_data, mark_root, &visit);
    ok = visit.ok;
  }
  for (size_t i = 0; ok && i < rt->permanent_constants; i++) ok = mark_constant(&m, (uint32_t)i);
  while (ok && m.count) {
    oti_reclaim_task task = m.work[--m.count];
    if (task.function) {
      oti_function* fn = rt->functions[task.value];
      for (size_t i = 0; ok && i < fn->count; i++) {
        oti_instruction in = fn->code[i];
        if (in.op == OP_CONST || in.op == OP_BR_EQK) ok = mark_constant(&m, oti_instruction_bc(in));
        else if (in.op == OP_CLOSURE) ok = mark_function(&m, oti_instruction_bc(in));
      }
    } else {
      if (oti_kind(task.value, K_CLOSURE)) {
#ifdef OTIUM_NATIVE
        if (rt->native_mode) ok = oti_native_mark_closure(rt, task.value, mark_code_constant, &m);
        else
#endif
          ok = mark_function(&m, oti_closure_function(task.value));
      }
      oti_object_header* object = (oti_object_header*)task.value;
      ot_value* fields = object_fields(task.value);
      for (size_t i = 0; ok && i < object->fields; i++) ok = mark_value(&m, fields[i]);
    }
  }
  if (ok) {
    for (size_t i = 0; i < rt->constant_count; i++) {
      ot_value value = rt->constants[i];
      /* The intern table is weak: retain entries for symbols reachable
             * through user data as well as those referenced by live code. */
      if ((oti_kind(value, K_SYMBOL) || oti_kind(value, K_KEYWORD) || oti_kind(value, K_CLOSURE)) &&
          reached(value))
        m.constants[i] = 1;
      if (!m.constants[i]) {
        rt->constants[i] = OT_NIL;
        rt->constant_state[i] = 0;
      }
    }
#ifdef OTIUM_NATIVE
    if (rt->native_mode) oti_native_sweep(rt);
    else
#endif
    for (size_t i = 0; i < rt->function_count; i++)
      if (!m.functions[i]) {
        oti_free_function(rt->functions[i]);
        rt->functions[i] = NULL;
      }
    while (rt->function_count && !rt->functions[rt->function_count - 1]) rt->function_count--;
    while (rt->constant_count && !rt->constant_state[rt->constant_count - 1]) rt->constant_count--;
    rt->constant_roots.count = rt->constant_count;
    rt->constant_free = rt->function_free = 0;
  }
  /* The collection also clears the reclaim marks, including those left by
   * a mark that failed partway. */
  oti_heap_collect(&rt->heap);
  if (rt->stack_stale > rt->sp) rt->stack_stale = rt->sp;
  oti_foreign_collect(rt);
  free(m.functions);
  free(m.constants);
  free(m.work);
  return ok;
}
