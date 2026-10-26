#include "internal.h"
#include <stdlib.h>
#include <string.h>

void rt_free_function(Function *fn) {
    if (!fn) return;
    free(fn->code); free(fn->boxed); free(fn->captures); free(fn);
}

typedef struct { Value value; bool function; } Task;
typedef struct {
    Runtime *rt;
    unsigned char *objects, *functions, *constants;
    Task *work;
    size_t count, capacity;
} Mark;

static bool enqueue(Mark *m, Value value, bool function) {
    if (!rt_grow(m->rt, (void **)&m->work, &m->capacity, m->count + 1, sizeof(Task))) return false;
    m->work[m->count++] = (Task){value, function};
    return true;
}

static bool mark_function(Mark *m, uint32_t id) {
    if (id >= m->rt->function_count || !m->rt->functions[id]) return rt_error(m->rt, "invalid live function");
    if (m->functions[id]) return true;
    m->functions[id] = 1;
    return enqueue(m, id, true);
}

static size_t object_index(Mark *m, Value value) {
    return (value - (Value)m->rt->heap.space[m->rt->heap.active]) / 8;
}

static bool mark_value(Mark *m, Value value) {
    if (!value_is_ptr(value)) return true;
    uintptr_t start = (uintptr_t)m->rt->heap.space[m->rt->heap.active];
    if (value < start || value - start >= m->rt->heap.used) return rt_error(m->rt, "invalid live object");
    size_t bit = object_index(m, value);
    if (m->objects[bit / 8] & (1u << (bit % 8))) return true;
    m->objects[bit / 8] |= (unsigned char)(1u << (bit % 8));
    return enqueue(m, value, false);
}

static bool mark_constant(Mark *m, uint32_t id) {
    if (id >= m->rt->constant_count || !m->rt->constant_state[id]) return rt_error(m->rt, "invalid live constant");
    m->constants[id] = 1;
    return mark_value(m, m->rt->constants[id]);
}

bool rt_reclaim(Runtime *rt) {
    if (rt->frame_count || rt->compiling || rt->evaluating || rt->eval_depth) return rt_error(rt, "code reclamation requires an evaluation boundary");
    Mark m = {.rt = rt};
    m.objects = calloc(rt->heap.used / 64 + 1, 1);
    m.functions = calloc(rt->function_count + 1, 1);
    m.constants = calloc(rt->constant_count + 1, 1);
    bool ok = m.objects && m.functions && m.constants;
    if (!ok) rt_error(rt, "out of host memory");
    for (Roots *root = rt->heap.roots; ok && root; root = root->previous) {
        if (root == &rt->constant_roots) continue;
        for (size_t i = 0; ok && i < root->count; i++) ok = mark_value(&m, root->slots[i]);
    }
    for (size_t i = 0; ok && i < rt->permanent_constants; i++) ok = mark_constant(&m, (uint32_t)i);
    while (ok && m.count) {
        Task task = m.work[--m.count];
        if (task.function) {
            Function *fn = rt->functions[task.value];
            for (size_t i = 0; ok && i < fn->count; i++) {
                Instruction in = fn->code[i];
                if (in.op == OP_CONST) ok = mark_constant(&m, in.arg);
                else if (in.op == OP_CLOSURE) ok = mark_function(&m, in.arg);
            }
        } else {
            if (rt_kind(task.value, K_CLOSURE)) {
                uint32_t id;
                memcpy(&id, object_bytes(task.value), sizeof(id));
                ok = mark_function(&m, id);
            }
            HeapObject *object = (HeapObject *)task.value;
            Value *fields = object_fields(task.value);
            for (size_t i = 0; ok && i < object->fields; i++) ok = mark_value(&m, fields[i]);
        }
    }
    if (ok) {
        for (size_t i = 0; i < rt->constant_count; i++) {
            Value value = rt->constants[i];
            /* The intern table is weak: retain entries for symbols reachable
             * through user data as well as those referenced by live code. */
            if (rt_kind(value, K_SYMBOL) || rt_kind(value, K_KEYWORD) || rt_kind(value, K_CLOSURE)) {
                size_t bit = object_index(&m, value);
                if (m.objects[bit / 8] & (1u << (bit % 8))) m.constants[i] = 1;
            }
            if (!m.constants[i]) { rt->constants[i] = V_NIL; rt->constant_state[i] = 0; }
        }
        for (size_t i = 0; i < rt->function_count; i++) if (!m.functions[i]) {
            rt_free_function(rt->functions[i]); rt->functions[i] = NULL;
        }
        while (rt->function_count && !rt->functions[rt->function_count - 1]) rt->function_count--;
        while (rt->constant_count && !rt->constant_state[rt->constant_count - 1]) rt->constant_count--;
        rt->constant_roots.count = rt->constant_count;
        rt->constant_free = rt->function_free = 0;
        heap_collect(&rt->heap);
        rt_foreign_collect(rt);
    }
    free(m.objects); free(m.functions); free(m.constants); free(m.work);
    return ok;
}
