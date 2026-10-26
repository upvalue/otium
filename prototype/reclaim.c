#include "internal.h"
#include <stdlib.h>
#include <string.h>

void oti_free_function(oti_function* fn) {
  if (!fn) return;
  free(fn->code);
  free(fn->boxed);
  free(fn->captures);
  free(fn);
}

typedef struct {
  ot_value value;
  bool function;
} oti_reclaim_task;

typedef struct {
  ot_runtime* rt;
  unsigned char *objects, *functions, *constants;
  oti_reclaim_task* work;
  size_t count, capacity;
} oti_reclaim_mark;

static bool enqueue(oti_reclaim_mark* m, ot_value value, bool function) {
  if (!oti_grow(m->rt, (void**)&m->work, &m->capacity, m->count + 1, sizeof(oti_reclaim_task))) return false;
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

static size_t object_index(oti_reclaim_mark* m, ot_value value) {
  return (value - (ot_value)m->rt->heap.space[m->rt->heap.active]) / 8;
}

static bool mark_value(oti_reclaim_mark* m, ot_value value) {
  if (!value_is_ptr(value)) return true;
  uintptr_t start = (uintptr_t)m->rt->heap.space[m->rt->heap.active];
  if (value < start || value - start >= m->rt->heap.used)
    return oti_error(m->rt, "invalid live object");
  size_t bit = object_index(m, value);
  if (m->objects[bit / 8] & (1u << (bit % 8))) return true;
  m->objects[bit / 8] |= (unsigned char)(1u << (bit % 8));
  return enqueue(m, value, false);
}

static bool mark_constant(oti_reclaim_mark* m, uint32_t id) {
  if (id >= m->rt->constant_count || !m->rt->constant_state[id])
    return oti_error(m->rt, "invalid live constant");
  m->constants[id] = 1;
  return mark_value(m, m->rt->constants[id]);
}

bool oti_reclaim(ot_runtime* rt) {
  if (rt->frame_count || rt->compiling || rt->evaluating || rt->eval_depth)
    return oti_error(rt, "code reclamation requires an evaluation boundary");
  oti_reclaim_mark m = {.rt = rt};
  m.objects = calloc(rt->heap.used / 64 + 1, 1);
  m.functions = calloc(rt->function_count + 1, 1);
  m.constants = calloc(rt->constant_count + 1, 1);
  bool ok = m.objects && m.functions && m.constants;
  if (!ok) oti_error(rt, "out of host memory");
  for (oti_roots* root = rt->heap.roots; ok && root; root = root->previous) {
    if (root == &rt->constant_roots) continue;
    for (size_t i = 0; ok && i < root->count; i++) ok = mark_value(&m, root->slots[i]);
  }
  for (size_t i = 0; ok && i < rt->permanent_constants; i++) ok = mark_constant(&m, (uint32_t)i);
  while (ok && m.count) {
    oti_reclaim_task task = m.work[--m.count];
    if (task.function) {
      oti_function* fn = rt->functions[task.value];
      for (size_t i = 0; ok && i < fn->count; i++) {
        oti_instruction in = fn->code[i];
        if (in.op == OP_CONST) ok = mark_constant(&m, in.arg);
        else if (in.op == OP_CLOSURE) ok = mark_function(&m, in.arg);
      }
    } else {
      if (oti_kind(task.value, K_CLOSURE)) {
        uint32_t id;
        memcpy(&id, object_bytes(task.value), sizeof(id));
        ok = mark_function(&m, id);
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
      if (oti_kind(value, K_SYMBOL) || oti_kind(value, K_KEYWORD) || oti_kind(value, K_CLOSURE)) {
        size_t bit = object_index(&m, value);
        if (m.objects[bit / 8] & (1u << (bit % 8))) m.constants[i] = 1;
      }
      if (!m.constants[i]) {
        rt->constants[i] = OT_NIL;
        rt->constant_state[i] = 0;
      }
    }
    for (size_t i = 0; i < rt->function_count; i++)
      if (!m.functions[i]) {
        oti_free_function(rt->functions[i]);
        rt->functions[i] = NULL;
      }
    while (rt->function_count && !rt->functions[rt->function_count - 1]) rt->function_count--;
    while (rt->constant_count && !rt->constant_state[rt->constant_count - 1]) rt->constant_count--;
    rt->constant_roots.count = rt->constant_count;
    rt->constant_free = rt->function_free = 0;
    oti_heap_collect(&rt->heap);
    oti_foreign_collect(rt);
  }
  free(m.objects);
  free(m.functions);
  free(m.constants);
  free(m.work);
  return ok;
}
