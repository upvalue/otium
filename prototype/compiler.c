#include "internal.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
  const Ast* name;
  uint32_t slot;
} Binding;

typedef struct {
  size_t start, end;
} Lifetime;
typedef struct Compiler Compiler;

struct Compiler {
  Runtime* rt;
  Function* fn;
  Compiler* parent;
  Binding* bindings;
  size_t binding_count, binding_capacity;
  const Ast** upnames;
  size_t upname_capacity;
  Lifetime* lifetimes;
  size_t lifetime_capacity;
  unsigned scope;
  bool definitions;
  Reader* expansion_arena;
  size_t* expansions;
};

typedef struct {
  Opcode load, store;
  uint32_t index;
} Reference;

static bool expression(Compiler* c, const Ast* a, bool tail);
static bool body(Compiler* c, const Ast* forms, bool tail, bool definitions);

static bool error(Compiler* c, const Ast* a, const char* message) {
  if (!c->rt->error[0] && a) c->rt->error_offset = a->offset;
  return rt_error(c->rt, message);
}

static bool same_name(const Ast* a, const Ast* b) {
  return a && b && a->symbol_id == b->symbol_id && a->kind == AST_SYMBOL && b->kind == AST_SYMBOL &&
         a->as.text.size == b->as.text.size &&
         !memcmp(a->as.text.data, b->as.text.data, a->as.text.size);
}

static size_t emit(Compiler* c, Opcode op, uint32_t arg) {
  Function* f = c->fn;
  if (f->count >= UINT32_MAX ||
      !rt_grow(c->rt, (void**)&f->code, &f->capacity, f->count + 1, sizeof(Instruction))) {
    rt_error(c->rt, "function too large");
    return SIZE_MAX;
  }
  size_t i = f->count++;
  f->code[i] = (Instruction){op, arg};
  return i;
}

static bool constant(Compiler* c, Value value) {
  if (!value) return false;
  uint32_t index = rt_constant(c->rt, value);
  return index != UINT32_MAX && emit(c, OP_CONST, index) != SIZE_MAX;
}

static bool patch(Compiler* c, size_t instruction) {
  if (instruction == SIZE_MAX || c->rt->error[0]) return false;
  c->fn->code[instruction].arg = (uint32_t)c->fn->count;
  return true;
}

static uint32_t new_function(Runtime* rt) {
  size_t id = rt->function_free;
  while (id < rt->function_count && rt->functions[id]) id++;
  if (id >= UINT32_MAX ||
      !rt_grow(rt, (void**)&rt->functions, &rt->function_capacity, id + 1, sizeof(Function*))) {
    rt_error(rt, "too many functions");
    return UINT32_MAX;
  }
  Function* f = calloc(1, sizeof(*f));
  if (!f) {
    rt_error(rt, "out of host memory");
    return UINT32_MAX;
  }
  f->namespace = rt->current_namespace;
  if (id == rt->function_count) rt->function_count++;
  rt->function_free = id + 1;
  rt->functions[id] = f;
  return (uint32_t)id;
}

static uint32_t local(Compiler* c, const Ast* name) {
  Function* f = c->fn;
  if (f->locals >= UINT32_MAX ||
      !rt_grow(c->rt, (void**)&f->boxed, &f->locals_capacity, f->locals + 1, sizeof(bool)) ||
      !rt_grow(c->rt, (void**)&c->lifetimes, &c->lifetime_capacity, f->locals + 1,
               sizeof(Lifetime))) {
    rt_error(c->rt, "too many locals");
    return UINT32_MAX;
  }
  uint32_t slot = (uint32_t)f->locals++;
  f->boxed[slot] = false;
  c->lifetimes[slot] = (Lifetime){f->count, SIZE_MAX};
  if (name) {
    if (name->kind != AST_SYMBOL) {
      error(c, name, "binding name must be a symbol");
      return UINT32_MAX;
    }
    if (!rt_grow(c->rt, (void**)&c->bindings, &c->binding_capacity, c->binding_count + 1,
                 sizeof(Binding)))
      return UINT32_MAX;
    c->bindings[c->binding_count++] = (Binding){name, slot};
  }
  return slot;
}

typedef struct {
  size_t end;
  uint32_t slot;
} Slot;

typedef struct {
  Slot* items;
  size_t count;
} SlotPool;

static void slot_push(SlotPool* pool, Slot value) {
  size_t i = pool->count++;
  while (i && pool->items[(i - 1) / 2].end > value.end) {
    pool->items[i] = pool->items[(i - 1) / 2];
    i = (i - 1) / 2;
  }
  pool->items[i] = value;
}

static Slot slot_pop(SlotPool* pool) {
  Slot result = pool->items[0], last = pool->items[--pool->count];
  size_t i = 0;
  while (i * 2 + 1 < pool->count) {
    size_t child = i * 2 + 1;
    if (child + 1 < pool->count && pool->items[child + 1].end < pool->items[child].end) child++;
    if (last.end <= pool->items[child].end) break;
    pool->items[i] = pool->items[child];
    i = child;
  }
  if (pool->count) pool->items[i] = last;
  return result;
}

/* Capture analysis is complete here. Keep box policies separate and leave
 * parameter slots fixed for call entry and optional defaults. */
static bool assign_slots(Compiler* c) {
  Function* f = c->fn;
  size_t parameters = f->arity + f->optional + (f->rest ? 1 : 0);
  if (f->locals == parameters) return true;
  size_t counts[2] = {0, 0};
  for (size_t i = parameters; i < f->locals; i++) counts[f->boxed[i]]++;
  uint32_t* map = malloc(f->locals * sizeof(*map));
  SlotPool pools[2] = {{malloc(counts[0] * sizeof(Slot)), 0},
                       {malloc(counts[1] * sizeof(Slot)), 0}};
  if (!map || (counts[0] && !pools[0].items) || (counts[1] && !pools[1].items)) {
    free(map);
    free(pools[0].items);
    free(pools[1].items);
    return rt_error(c->rt, "out of host memory");
  }
  for (size_t i = 0; i < parameters; i++) map[i] = (uint32_t)i;
  size_t used = parameters;
  for (size_t i = parameters; i < f->locals; i++) {
    bool boxed = f->boxed[i];
    SlotPool* pool = &pools[boxed];
    uint32_t slot = pool->count && pool->items[0].end <= c->lifetimes[i].start ? slot_pop(pool).slot
                                                                               : (uint32_t)used++;
    map[i] = slot;
    f->boxed[slot] = boxed;
    slot_push(pool, (Slot){c->lifetimes[i].end, slot});
  }
  for (size_t i = 0; i < f->count; i++) {
    Instruction* in = &f->code[i];
    switch (in->op) {
      case OP_LOCAL:
      case OP_BIND:
      case OP_SET_LOCAL:
      case OP_PRESENT:
      case OP_CLEAR_LOCAL: in->arg = map[in->arg]; break;
      case OP_CLOSURE: {
        /* Each child has one creation instruction. Its local captures
             * still name this parent's logical bindings. */
        Function* child = c->rt->functions[in->arg];
        for (size_t j = 0; j < child->capture_count; j++)
          if (child->captures[j].local) child->captures[j].index = map[child->captures[j].index];
        break;
      }
      default: break;
    }
  }
  f->locals = used;
  if (used < f->locals_capacity / 2) {
    bool* boxed = realloc(f->boxed, used * sizeof(bool));
    if (boxed) {
      f->boxed = boxed;
      f->locals_capacity = used;
    }
  }
  free(map);
  free(pools[0].items);
  free(pools[1].items);
  return true;
}

static bool find_local(Compiler* c, const Ast* name, uint32_t* slot) {
  for (size_t i = c->binding_count; i > 0; i--)
    if (same_name(c->bindings[i - 1].name, name)) {
      *slot = c->bindings[i - 1].slot;
      return true;
    }
  return false;
}

static bool capture(Compiler* c, const Ast* name, uint32_t* index) {
  for (size_t i = 0; i < c->fn->capture_count; i++)
    if (same_name(c->upnames[i], name)) {
      *index = (uint32_t)i;
      return true;
    }
  if (!c->parent) return false;
  uint32_t outer;
  bool is_local = find_local(c->parent, name, &outer);
  if (is_local) c->parent->fn->boxed[outer] = true;
  else if (!capture(c->parent, name, &outer)) return false;
  Function* f = c->fn;
  if (f->capture_count >= UINT32_MAX ||
      !rt_grow(c->rt, (void**)&f->captures, &f->capture_capacity, f->capture_count + 1,
               sizeof(Capture)) ||
      !rt_grow(c->rt, (void**)&c->upnames, &c->upname_capacity, f->capture_count + 1, sizeof(Ast*)))
    return false;
  *index = (uint32_t)f->capture_count++;
  f->captures[*index] = (Capture){outer, is_local};
  c->upnames[*index] = name;
  return true;
}

static bool resolve(Compiler* c, const Ast* name, Reference* ref) {
  if (!name || name->kind != AST_SYMBOL) return error(c, name, "expected a symbol");
  if (find_local(c, name, &ref->index)) {
    ref->load = OP_LOCAL;
    ref->store = OP_SET_LOCAL;
  } else if (capture(c, name, &ref->index)) {
    ref->load = OP_UPVALUE;
    ref->store = OP_SET_UPVALUE;
  } else {
    if (c->rt->error[0]) return false;
    ref->index = rt_resolve_global(c->rt, name->as.text.data, name->as.text.size);
    ref->load = OP_GLOBAL;
    ref->store = OP_SET_GLOBAL;
  }
  return ref->index != UINT32_MAX;
}

static Value datum(Compiler* c, const Ast* a) {
  Runtime* rt = c->rt;
  switch (a->kind) {
    case AST_VALUE: return rt->constants[a->as.integer];
    case AST_NIL: return V_NIL;
    case AST_BOOL: return a->as.integer ? V_TRUE : V_FALSE;
    case AST_CHAR: return ((Value)a->as.integer << 8) | TAG_CHAR;
    case AST_INT:
      if (a->as.integer < INTPTR_MIN / 2 || a->as.integer > INTPTR_MAX / 2) {
        error(c, a, "integer exceeds immediate range");
        return 0;
      }
      return value_int((intptr_t)a->as.integer);
    case AST_FLOAT: return rt_float(rt, a->as.real);
    case AST_STRING: return rt_text(rt, K_STRING, a->as.text.data, a->as.text.size);
    case AST_SYMBOL:
    case AST_KEYWORD: {
      unsigned kind = a->kind == AST_SYMBOL ? K_SYMBOL : K_KEYWORD;
      for (size_t i = 0; i < rt->constant_count; i++) {
        Value v = rt->constants[i];
        if (!rt_kind(v, kind)) continue;
        size_t length;
        unsigned char* bytes = object_bytes(v);
        memcpy(&length, bytes, sizeof(length));
        if ((kind != K_SYMBOL || rt_symbol_id(v) == a->symbol_id) && length == a->as.text.size &&
            !memcmp(bytes + sizeof(length), a->as.text.data, length))
          return v;
      }
      Value v = rt_text(rt, kind, a->as.text.data, a->as.text.size);
      if (v && kind == K_SYMBOL)
        memcpy(object_bytes(v) + sizeof(size_t) + a->as.text.size + 1, &a->symbol_id,
               sizeof(a->symbol_id));
      if (v && rt_constant(rt, v) == UINT32_MAX) return 0;
      return v;
    }
    case AST_LIST: {
      Value slots[] = {V_EMPTY, V_EMPTY, V_NIL};
      Roots roots;
      roots_push(&rt->heap, &roots, slots, 3);
      for (const Ast* item = a->as.list.first; item; item = item->next) {
        slots[2] = datum(c, item);
        if (!slots[2]) break;
        Value pair = rt_alloc(rt, K_PAIR, 2, 0);
        if (!pair) break;
        object_fields(pair)[0] = slots[2];
        object_fields(pair)[1] = V_EMPTY;
        if (slots[0] == V_EMPTY) slots[0] = pair;
        else object_fields(slots[1])[1] = pair;
        slots[1] = pair;
      }
      if (!rt->error[0] && a->as.list.tail) {
        slots[2] = datum(c, a->as.list.tail);
        if (slots[2]) object_fields(slots[1])[1] = slots[2];
      }
      Value result = rt->error[0] ? 0 : slots[0];
      roots_pop(&rt->heap, &roots);
      return result;
    }
  }
  return 0;
}

/* Expanded syntax is copied into a host arena; it never holds heap pointers. */
static Ast* syntax_value(Compiler* c, Value v, size_t offset, unsigned depth) {
  if (depth > 256) {
    error(c, NULL, "macro syntax nesting limit exceeded");
    return NULL;
  }
  Ast* a = reader_allocate(c->expansion_arena, sizeof(*a));
  if (!a) {
    error(c, NULL, "macro syntax memory limit exceeded");
    return NULL;
  }
  a->offset = offset;
  if (v == V_NIL) a->kind = AST_NIL;
  else if (v == V_TRUE || v == V_FALSE) {
    a->kind = AST_BOOL;
    a->as.integer = v == V_TRUE;
  } else if (value_is_int(v)) {
    a->kind = AST_INT;
    a->as.integer = value_to_int(v);
  } else if ((v & 255) == TAG_CHAR) {
    a->kind = AST_CHAR;
    a->as.integer = v >> 8;
  } else if (rt_kind(v, K_FLOAT)) {
    a->kind = AST_FLOAT;
    memcpy(&a->as.real, object_bytes(v), sizeof(double));
  } else if (rt_kind(v, K_STRING) || rt_kind(v, K_SYMBOL) || rt_kind(v, K_KEYWORD)) {
    a->kind = rt_kind(v, K_STRING) ? AST_STRING : rt_kind(v, K_SYMBOL) ? AST_SYMBOL : AST_KEYWORD;
    size_t n;
    memcpy(&n, object_bytes(v), sizeof(n));
    char* copy = reader_allocate(c->expansion_arena, n + 1);
    if (!copy) {
      error(c, NULL, "macro syntax memory limit exceeded");
      return NULL;
    }
    memcpy(copy, object_bytes(v) + sizeof(n), n);
    copy[n] = 0;
    a->as.text.data = copy;
    a->as.text.size = n;
    if (a->kind == AST_SYMBOL) a->symbol_id = rt_symbol_id(v);
  } else if (v == V_EMPTY || rt_kind(v, K_PAIR)) {
    a->kind = AST_LIST;
    Ast** next = &a->as.list.first;
    Value slow = v, fast = v;
    while (rt_kind(v, K_PAIR)) {
      *next = syntax_value(c, object_fields(v)[0], offset, depth + 1);
      if (!*next) return NULL;
      next = &(*next)->next;
      v = object_fields(v)[1];
      if (rt_kind(slow, K_PAIR)) slow = object_fields(slow)[1];
      for (unsigned i = 0; i < 2 && rt_kind(fast, K_PAIR); i++) fast = object_fields(fast)[1];
      if (rt_kind(fast, K_PAIR) && fast == slow) {
        error(c, NULL, "cyclic macro syntax");
        return NULL;
      }
    }
    if (v != V_EMPTY) {
      a->as.list.tail = syntax_value(c, v, offset, depth + 1);
      if (!a->as.list.tail) return NULL;
    }
  } else {
    uint32_t index = rt_constant(c->rt, v);
    if (index == UINT32_MAX) return NULL;
    a->kind = AST_VALUE;
    a->as.integer = index;
  }
  return a;
}

Ast* rt_syntax_value(Runtime* rt, Value value, Reader* arena) {
  Compiler c = {.rt = rt, .expansion_arena = arena};
  return syntax_value(&c, value, 0, 0);
}

static Ast* expand_call(Compiler* c, const Ast* form, uint32_t global) {
  Runtime* rt = c->rt;
  if (++*c->expansions > 256) {
    error(c, form, "macro expansion limit exceeded");
    return NULL;
  }
  Ast arguments = {.kind = AST_LIST, .as.list.first = form->as.list.first->next};
  Value args = datum(c, &arguments);
  if (!args) return NULL;
  Value result = rt_call_list(rt, rt->globals[global], args);
  return result ? syntax_value(c, result, form->offset, 0) : NULL;
}

/* Plans use indices and reader-owned ASTs. Managed constants live in a separate
 * root array so host-array growth and heap compaction cannot leave stale pointers. */
typedef enum { QQ_CONSTANT, QQ_EXPRESSION, QQ_CONS, QQ_APPEND } QQKind;

typedef struct {
  QQKind kind;
  size_t left, right;
  const Ast* expression;
} QQNode;

typedef struct {
  Compiler* compiler;
  QQNode* nodes;
  Value* values;
  size_t count, capacity, value_capacity;
  Roots roots;
} QQPlan;

static size_t qq_add(QQPlan* plan, QQKind kind, size_t left, size_t right, const Ast* a,
                     Value value) {
  Runtime* rt = plan->compiler->rt;
  if (plan->count >= 16 * 1024 * 1024 / (sizeof(QQNode) + sizeof(Value))) {
    rt_error(rt, "quasiquote expansion exceeds budget");
    return SIZE_MAX;
  }
  if (!rt_grow(rt, (void**)&plan->nodes, &plan->capacity, plan->count + 1, sizeof(QQNode)))
    return SIZE_MAX;
  if (!rt_grow(rt, (void**)&plan->values, &plan->value_capacity, plan->count + 1, sizeof(Value)))
    return SIZE_MAX;
  plan->roots.slots = plan->values;
  size_t index = plan->count++;
  plan->nodes[index] = (QQNode){kind, left, right, a};
  plan->values[index] = value;
  plan->roots.count = plan->count;
  return index;
}

static size_t qq_constant(QQPlan* plan, Value value) {
  return value ? qq_add(plan, QQ_CONSTANT, 0, 0, NULL, value) : SIZE_MAX;
}

static size_t qq_join(QQPlan* plan, size_t left, size_t right, bool splice) {
  if (left == SIZE_MAX || right == SIZE_MAX) return SIZE_MAX;
  if (splice && plan->nodes[right].kind == QQ_CONSTANT && plan->values[right] == V_EMPTY)
    return left;
  if (!splice && plan->nodes[left].kind == QQ_CONSTANT && plan->nodes[right].kind == QQ_CONSTANT) {
    Value pair = rt_alloc(plan->compiler->rt, K_PAIR, 2, 0);
    if (!pair) return SIZE_MAX;
    object_fields(pair)[0] = plan->values[left];
    object_fields(pair)[1] = plan->values[right];
    return qq_constant(plan, pair);
  }
  return qq_add(plan, splice ? QQ_APPEND : QQ_CONS, left, right, NULL, V_NIL);
}

static int qq_marker(const Ast* a) {
  return ast_symbol(a, "quasiquote")         ? 1
         : ast_symbol(a, "unquote")          ? 2
         : ast_symbol(a, "unquote-splicing") ? 3
                                             : 0;
}

static size_t qq_node(QQPlan* plan, const Ast* a, unsigned depth);

static size_t qq_special(QQPlan* plan, const Ast* head, unsigned depth) {
  int marker = qq_marker(head);
  const Ast* arg = head->next;
  if (!arg) {
    error(plan->compiler, head, "quasiquote marker needs an operand");
    return SIZE_MAX;
  }
  if (marker != 1 && !depth) {
    error(plan->compiler, head, "unquote outside quasiquote");
    return SIZE_MAX;
  }
  if (marker == 2 && depth == 1) return qq_add(plan, QQ_EXPRESSION, 0, 0, arg, V_NIL);
  size_t value = qq_node(plan, arg, marker == 1 ? depth + 1 : depth - 1);
  if (value == SIZE_MAX) return SIZE_MAX;
  size_t empty = qq_constant(plan, V_EMPTY);
  size_t rest = qq_join(plan, value, empty, false);
  if (rest == SIZE_MAX) return SIZE_MAX;
  size_t name = qq_constant(plan, datum(plan->compiler, head));
  return qq_join(plan, name, rest, false);
}

static size_t qq_node(QQPlan* plan, const Ast* a, unsigned depth) {
  if (a->kind != AST_LIST) return qq_constant(plan, datum(plan->compiler, a));

  typedef struct {
    size_t node;
    bool splice;
  } Piece;

  Piece* pieces = NULL;
  size_t count = 0, capacity = 0, rest = SIZE_MAX;
  const Ast* p = a->as.list.first;
  bool ok = true;
  for (; p; p = p->next) {
    /* List tails have the same syntax as explicitly dotted tails. */
    if (qq_marker(p)) {
      rest = qq_special(plan, p, depth);
      break;
    }
    bool splice =
        depth == 1 && p->kind == AST_LIST && ast_symbol(p->as.list.first, "unquote-splicing");
    size_t node;
    if (splice) {
      const Ast* arg = p->as.list.first->next;
      if (!arg) {
        error(plan->compiler, p, "splice needs an operand");
        ok = false;
        break;
      }
      node = qq_add(plan, QQ_EXPRESSION, 0, 0, arg, V_NIL);
    } else node = qq_node(plan, p, depth);
    if (node == SIZE_MAX ||
        !rt_grow(plan->compiler->rt, (void**)&pieces, &capacity, count + 1, sizeof(Piece))) {
      ok = false;
      break;
    }
    pieces[count++] = (Piece){node, splice};
  }
  if (ok && !p)
    rest = a->as.list.tail ? qq_node(plan, a->as.list.tail, depth) : qq_constant(plan, V_EMPTY);
  if (ok)
    for (size_t i = count; i > 0 && rest != SIZE_MAX; i--)
      rest = qq_join(plan, pieces[i - 1].node, rest, pieces[i - 1].splice);
  free(pieces);
  return ok ? rest : SIZE_MAX;
}

static bool quasiquote(Compiler* c, const Ast* a, bool tail) {
  QQPlan plan = {.compiler = c};
  roots_push(&c->rt->heap, &plan.roots, NULL, 0);
  size_t root = qq_node(&plan, a, 1);

  typedef struct {
    size_t node;
    unsigned stage;
  } Pending;

  Pending* pending = NULL;
  size_t count = 0, capacity = 0;
  bool ok = root != SIZE_MAX && rt_grow(c->rt, (void**)&pending, &capacity, 1, sizeof(Pending));
  if (ok) pending[count++] = (Pending){root, 0};
  while (ok && count) {
    size_t index = pending[count - 1].node;
    QQNode node = plan.nodes[index];
    if (node.kind == QQ_CONSTANT) {
      ok = constant(c, plan.values[index]);
      count--;
    } else if (node.kind == QQ_EXPRESSION) {
      ok = expression(c, node.expression, tail && count == 1);
      count--;
    } else {
      unsigned stage = pending[count - 1].stage++;
      if (!stage) {
        unsigned builtin = node.kind == QQ_CONS ? B_CONS : B_QQ_APPEND;
        ok = constant(c, ((Value)builtin << 8) | TAG_BUILTIN);
      } else if (stage < 3) {
        if (!(ok = rt_grow(c->rt, (void**)&pending, &capacity, count + 1, sizeof(Pending)))) break;
        pending[count++] = (Pending){stage == 1 ? node.left : node.right, 0};
      } else {
        ok = emit(c, tail && count == 1 ? OP_TAIL_CALL : OP_CALL, 2) != SIZE_MAX;
        count--;
      }
    }
  }
  free(pending);
  roots_pop(&c->rt->heap, &plan.roots);
  free(plan.nodes);
  free(plan.values);
  return ok;
}

static bool lambda(Compiler* c, const Ast* params, const Ast* forms, bool bindings) {
  if (!params || (params->kind != AST_LIST && (bindings || params->kind != AST_SYMBOL)))
    return error(c, params, "expected a parameter list or rest symbol");
  uint32_t id = new_function(c->rt);
  if (id == UINT32_MAX) return false;
  Compiler child = {.rt = c->rt,
                    .fn = c->rt->functions[id],
                    .parent = c,
                    .scope = 1,
                    .expansion_arena = c->expansion_arena,
                    .expansions = c->expansions};
  bool ok = true;
  const Ast* rest = params->kind == AST_SYMBOL ? params : params->as.list.tail;
  const Ast* first = params->kind == AST_LIST ? params->as.list.first : NULL;
  const Ast* optional = NULL;
  for (const Ast* p = first; p; p = p->next) {
    const Ast* name = bindings ? p->as.list.first : p;
    uint32_t ignored;
    if (!bindings && ast_symbol(name, "&optional")) {
      if (rest || !p->next) {
        ok = error(c, p, "optional parameters need defaults and cannot combine with rest");
        break;
      }
      optional = p->next;
      break;
    }
    if (!bindings && ast_symbol(name, "&")) {
      if (rest || !p->next || p->next->next) {
        ok = error(c, p, "rest marker needs one final parameter");
        break;
      }
      rest = p->next;
      break;
    }
    if (!name || name->kind != AST_SYMBOL || ast_symbol(name, "&optional") ||
        ast_symbol(name, "&")) {
      ok = error(c, name, "expected a symbol parameter");
      break;
    }
    if (find_local(&child, name, &ignored)) {
      ok = error(c, name, "duplicate parameter");
      break;
    }
    if (local(&child, name) == UINT32_MAX) {
      ok = false;
      break;
    }
    child.fn->arity++;
  }
  if (ok && rest) {
    uint32_t ignored;
    if (bindings || rest->kind != AST_SYMBOL || ast_symbol(rest, "&") ||
        ast_symbol(rest, "&optional"))
      ok = error(c, rest, "expected a rest parameter symbol");
    else if (find_local(&child, rest, &ignored)) ok = error(c, rest, "duplicate parameter");
    else if (local(&child, rest) == UINT32_MAX) ok = false;
    else child.fn->rest = true;
  }
  for (const Ast* p = optional; ok && p; p = p->next) {
    const Ast* name = p->kind == AST_LIST ? p->as.list.first : NULL;
    uint32_t ignored;
    if (!name || name->kind != AST_SYMBOL || !name->next || name->next->next || p->as.list.tail ||
        ast_symbol(name, "&") || ast_symbol(name, "&optional")) {
      ok = error(c, p, "expected (name default) after &optional");
      break;
    }
    if (find_local(&child, name, &ignored)) {
      ok = error(c, name, "duplicate parameter");
      break;
    }
    for (const Ast* previous = optional; previous != p; previous = previous->next)
      if (same_name(previous->as.list.first, name)) {
        ok = error(c, name, "duplicate parameter");
        break;
      }
    if (!ok || local(&child, NULL) == UINT32_MAX) {
      ok = false;
      break;
    }
    child.fn->optional++;
  }
  uint32_t optional_slot = (uint32_t)child.fn->arity;
  for (const Ast* p = optional; ok && p; p = p->next, optional_slot++) {
    const Ast* name = p->as.list.first;
    ok = emit(&child, OP_PRESENT, optional_slot) != SIZE_MAX;
    size_t missing = ok ? emit(&child, OP_JFALSE, 0) : SIZE_MAX;
    size_t done = missing != SIZE_MAX ? emit(&child, OP_JUMP, 0) : SIZE_MAX;
    ok = done != SIZE_MAX && patch(&child, missing) && expression(&child, name->next, false) &&
         emit(&child, OP_SET_LOCAL, optional_slot) != SIZE_MAX && patch(&child, done);
    if (ok)
      ok = rt_grow(c->rt, (void**)&child.bindings, &child.binding_capacity, child.binding_count + 1,
                   sizeof(Binding));
    if (ok) child.bindings[child.binding_count++] = (Binding){name, optional_slot};
  }
  if (forms && forms->kind == AST_STRING && forms->next) forms = forms->next;
  if (ok)
    ok = body(&child, forms, true, true) && emit(&child, OP_RETURN, 0) != SIZE_MAX &&
         assign_slots(&child);
  free(child.bindings);
  free(child.upnames);
  free(child.lifetimes);
  return ok && emit(c, OP_CLOSURE, id) != SIZE_MAX;
}

static const Ast* definition_name(const Ast* a) {
  if (!a || a->kind != AST_LIST) return NULL;
  const Ast* head = a->as.list.first;
  if (!ast_symbol(head, "def") && !ast_symbol(head, "def-")) return NULL;
  const Ast* name = head->next;
  if (name && name->kind == AST_LIST) name = name->as.list.first;
  return name;
}

static bool body(Compiler* c, const Ast* forms, bool tail, bool definitions) {
  if (c->scope && definitions) {
    for (const Ast* a = forms; a; a = a->next) {
      const Ast* name = definition_name(a);
      if (!name) continue;
      uint32_t slot = local(c, name);
      if (slot == UINT32_MAX || !constant(c, V_UNBOUND) || emit(c, OP_BIND, slot) == SIZE_MAX)
        return false;
    }
  }
  if (!forms) return constant(c, V_NIL);
  for (const Ast* a = forms; a; a = a->next) {
    c->definitions = definitions;
    if (!expression(c, a, tail && !a->next)) return false;
    if (a->next && emit(c, OP_POP, 0) == SIZE_MAX) return false;
  }
  return true;
}

static bool clear_bindings(Compiler* c, size_t first) {
  for (size_t i = first; i < c->binding_count; i++)
    if (emit(c, OP_CLEAR_LOCAL, c->bindings[i].slot) == SIZE_MAX) return false;
  return true;
}

static bool let_form(Compiler* c, const Ast* args, bool tail) {
  const Ast* name = args && args->kind == AST_SYMBOL ? args : NULL;
  const Ast* bindings = name ? args->next : args;
  if (!bindings || bindings->kind != AST_LIST || bindings->as.list.tail)
    return error(c, bindings, "let needs a binding list");
  size_t saved = c->binding_count;
  c->scope++;
  size_t count = 0, slot_capacity = 0;
  uint32_t* slots = NULL;
  bool ok = true;
  for (const Ast* b = bindings->as.list.first; b; b = b->next) {
    const Ast* var = b->kind == AST_LIST ? b->as.list.first : NULL;
    if (!var || var->kind != AST_SYMBOL || !var->next || var->next->next || b->as.list.tail) {
      ok = error(c, b, "let binding needs a name and initializer");
      break;
    }
    if (!expression(c, var->next, false)) {
      ok = false;
      break;
    }
    uint32_t slot = local(c, name ? NULL : var);
    if (slot == UINT32_MAX || emit(c, OP_BIND, slot) == SIZE_MAX) {
      ok = false;
      break;
    }
    if (!rt_grow(c->rt, (void**)&slots, &slot_capacity, count + 1, sizeof(uint32_t))) {
      ok = false;
      break;
    }
    slots[count] = slot;
    count++;
  }
  if (ok && name) {
    uint32_t slot = local(c, name);
    ok = slot != UINT32_MAX && constant(c, V_UNBOUND) && emit(c, OP_BIND, slot) != SIZE_MAX &&
         lambda(c, bindings, bindings->next, true) && emit(c, OP_SET_LOCAL, slot) != SIZE_MAX &&
         emit(c, OP_LOCAL, slot) != SIZE_MAX;
    for (size_t i = 0; ok && i < count; i++) ok = emit(c, OP_LOCAL, slots[i]) != SIZE_MAX;
    if (ok) ok = emit(c, tail ? OP_TAIL_CALL : OP_CALL, (uint32_t)count) != SIZE_MAX;
  } else if (ok) ok = body(c, bindings->next, tail, true);
  if (ok && !tail) {
    ok = clear_bindings(c, saved);
    if (name)
      for (size_t i = 0; ok && i < count; i++) ok = emit(c, OP_CLEAR_LOCAL, slots[i]) != SIZE_MAX;
  }
  for (size_t i = saved; i < c->binding_count; i++)
    c->lifetimes[c->bindings[i].slot].end = c->fn->count;
  if (name)
    for (size_t i = 0; i < count; i++) c->lifetimes[slots[i]].end = c->fn->count;
  c->scope--;
  c->binding_count = saved;
  free(slots);
  return ok;
}

static bool try_form(Compiler* c, const Ast* forms, bool tail) {
  const Ast* clause = forms;
  Ast* protected = NULL, ** next = &protected;
  while (clause && !(clause->kind == AST_LIST && ast_symbol(clause->as.list.first, "catch"))) {
    Ast* copy = reader_allocate(c->expansion_arena, sizeof(*copy));
    if (!copy) return error(c, clause, "compiler syntax memory limit");
    *copy = *clause;
    copy->next = NULL;
    *next = copy;
    next = &copy->next;
    clause = clause->next;
  }
  if (!clause) return body(c, forms, tail, false);
  size_t handler = emit(c, OP_TRY, 0);
  if (handler == SIZE_MAX || !body(c, protected, false, false) ||
      emit(c, OP_END_TRY, 0) == SIZE_MAX)
    return false;
  size_t normal = emit(c, OP_JUMP, 0);
  if (!patch(c, handler)) return false;
  uint32_t condition = local(c, NULL);
  if (condition == UINT32_MAX || emit(c, OP_BIND, condition) == SIZE_MAX) return false;
  size_t *jumps = NULL, count = 0, capacity = 0;
  bool ok = true;
  for (; clause && ok; clause = clause->next) {
    const Ast* head = clause->kind == AST_LIST ? clause->as.list.first : NULL;
    const Ast* guard = head ? head->next : NULL;
    if (!ast_symbol(head, "catch") || !guard || clause->as.list.tail) {
      ok = error(c, clause, "invalid catch clause");
      break;
    }
    const Ast *name = guard, *predicate = NULL;
    if (guard->kind == AST_LIST) {
      predicate = guard->as.list.first;
      name = predicate ? predicate->next : NULL;
      if (guard->as.list.tail || !name || name->next) {
        ok = error(c, guard, "invalid catch guard");
        break;
      }
    }
    if (name->kind != AST_SYMBOL) {
      ok = error(c, guard, "catch needs a variable");
      break;
    }
    size_t saved = c->binding_count;
    c->scope++;
    size_t skip = SIZE_MAX;
    if (predicate) {
      ok = expression(c, predicate, false) && emit(c, OP_LOCAL, condition) != SIZE_MAX &&
           emit(c, OP_CALL, 1) != SIZE_MAX;
      if (ok) skip = emit(c, OP_JFALSE, 0);
    }
    uint32_t slot = local(c, name);
    if (slot == UINT32_MAX || emit(c, OP_LOCAL, condition) == SIZE_MAX ||
        emit(c, OP_BIND, slot) == SIZE_MAX) {
      ok = false;
      break;
    }
    if (ok) ok = body(c, guard->next, tail, true) && clear_bindings(c, saved);
    if (ok) ok = rt_grow(c->rt, (void**)&jumps, &capacity, count + 1, sizeof(size_t));
    if (ok) jumps[count++] = emit(c, OP_JUMP, 0);
    if (ok && skip != SIZE_MAX) ok = patch(c, skip) && clear_bindings(c, saved);
    for (size_t i = saved; i < c->binding_count; i++)
      c->lifetimes[c->bindings[i].slot].end = c->fn->count;
    c->binding_count = saved;
    c->scope--;
  }
  if (ok)
    ok = emit(c, OP_LOCAL, condition) != SIZE_MAX && emit(c, OP_RAISE, 0) != SIZE_MAX &&
         patch(c, normal);
  for (size_t i = 0; ok && i < count; i++) ok = patch(c, jumps[i]);
  if (ok) ok = emit(c, OP_CLEAR_LOCAL, condition) != SIZE_MAX;
  c->lifetimes[condition].end = c->fn->count;
  free(jumps);
  return ok;
}

static bool class_key(const Ast* a, const char* key) {
  return a && a->kind == AST_KEYWORD && a->as.text.size == strlen(key) &&
         !memcmp(a->as.text.data, key, strlen(key));
}

static Value slot_key(Compiler* c, const Ast* name) {
  size_t n = name->as.text.size;
  char* text = malloc(n + 2);
  if (!text) {
    error(c, name, "out of host memory");
    return 0;
  }
  text[0] = ':';
  memcpy(text + 1, name->as.text.data, n);
  text[n + 1] = 0;
  Value key = rt_intern(c->rt, K_KEYWORD, text, n + 1);
  free(text);
  return key;
}

static bool accessor(Compiler* c, const Ast* class, const Ast* slot, const Ast* name) {
  Ast param = {.kind = AST_SYMBOL, .as.text = {"%instance", 9}}, generic = *name;
  generic.next = &param;
  Ast sig = {.kind = AST_LIST, .as.list = {.first = &generic}};
  Ast head = {.kind = AST_SYMBOL, .as.text = {"defgeneric", 10}, .next = &sig};
  Ast form = {.kind = AST_LIST, .as.list = {.first = &head}};
  c->definitions = true;
  if (!expression(c, &form, false) || emit(c, OP_POP, 0) == SIZE_MAX) return false;
  Ast type = *class;
  type.next = NULL;
  param.next = &type;
  Ast specialized = {.kind = AST_LIST, .as.list = {.first = &param}};
  generic.next = &specialized;
  Value key = slot_key(c, slot);
  if (!key) return false;
  uint32_t constant_index = rt_constant(c->rt, key);
  if (constant_index == UINT32_MAX) return false;
  Ast key_ast = {.kind = AST_VALUE, .as.integer = constant_index};
  Ast receiver = param;
  receiver.next = &key_ast;
  Ast read = {.kind = AST_LIST, .as.list = {.first = &receiver}};
  sig.next = &read;
  head.as.text.data = "defmethod";
  head.as.text.size = 9;
  c->definitions = true;
  return expression(c, &form, false) && emit(c, OP_POP, 0) != SIZE_MAX;
}

static bool class_declaration(Compiler* c, const Ast* a, const Ast* head, const Ast* name,
                              bool definitions) {
  if (c->scope || !definitions || !name || name->kind != AST_SYMBOL)
    return error(c, a, "class declaration requires a top-level name");
  if (!rt_classes_init(c->rt)) return false;
  bool abstract = ast_symbol(head, "defabstract"), reset = ast_symbol(head, "reset-class!");
  const Ast *fields = name->next, *parent = NULL;
  if (class_key(fields, ":is")) {
    parent = fields->next;
    if (!parent) return error(c, a, "missing class parent");
    fields = parent->next;
  }
  if (abstract && fields) return error(c, a, "abstract classes cannot have slots");
  if (!constant(c, ((Value)B_CLASS_DEFINE << 8) | TAG_BUILTIN) || !constant(c, datum(c, name)) ||
      !(parent ? expression(c, parent, false) : constant(c, c->rt->classes[C_OBJECT])) ||
      !constant(c, abstract ? V_TRUE : V_FALSE) ||
      !constant(c, ((Value)B_ARRAY << 8) | TAG_BUILTIN))
    return false;
  size_t count = 0;
  for (const Ast* p = fields; p; p = p->next) {
    const Ast* slot = p->kind == AST_LIST ? p->as.list.first : p;
    if (!slot || slot->kind != AST_SYMBOL || (p->kind == AST_LIST && p->as.list.tail))
      return error(c, p, "invalid slot declaration");
    const Ast *init = NULL, *state = NULL, *getter = NULL;
    for (const Ast* option = p->kind == AST_LIST ? slot->next : NULL; option;) {
      const Ast* value = option->next;
      if (!value) return error(c, option, "missing slot option value");
      if (class_key(option, ":init") && !init) init = value;
      else if (class_key(option, ":state") && !state) state = value;
      else if (class_key(option, ":accessor") && !getter && value->kind == AST_SYMBOL)
        getter = value;
      else return error(c, option, "invalid or duplicate slot option");
      option = value->next;
    }
    if (!constant(c, slot_key(c, slot))) return false;
    if (init) {
      Ast params = {.kind = AST_LIST}, value = *init;
      value.next = NULL;
      if (!lambda(c, &params, &value, false)) return false;
    } else if (!constant(c, V_FALSE)) return false;
    if (!(state ? expression(c, state, false) : constant(c, V_FALSE))) return false;
    count += 3;
  }
  if (count > UINT32_MAX || emit(c, OP_CALL, (uint32_t)count) == SIZE_MAX ||
      !constant(c, reset ? V_TRUE : V_FALSE) || emit(c, OP_CALL, 5) == SIZE_MAX ||
      emit(c, OP_POP, 0) == SIZE_MAX)
    return false;
  for (const Ast* p = fields; p; p = p->next)
    if (p->kind == AST_LIST) {
      const Ast* slot = p->as.list.first;
      for (const Ast* o = slot->next; o; o = o->next->next)
        if (class_key(o, ":accessor") && !accessor(c, name, slot, o->next)) return false;
    }
  return expression(c, name, false);
}

static bool expression(Compiler* c, const Ast* a, bool tail) {
  if (c->rt->error[0]) return false;
  bool definitions = c->definitions;
  c->definitions = false;
  if (a->kind == AST_SYMBOL) {
    Reference ref;
    return resolve(c, a, &ref) && emit(c, ref.load, ref.index) != SIZE_MAX;
  }
  if (a->kind != AST_LIST) return constant(c, datum(c, a));
  if (a->as.list.tail) return error(c, a, "cannot call an improper list");
  const Ast* head = a->as.list.first;
  if (!head) return constant(c, V_EMPTY);
  const Ast* args = head->next;
  if (head->kind == AST_SYMBOL) {
    uint32_t ignored;
    bool shadowed = false;
    for (Compiler* scope = c; scope; scope = scope->parent)
      if (find_local(scope, head, &ignored)) {
        shadowed = true;
        break;
      }
    if (!shadowed) {
      uint32_t global = rt_lookup_global(c->rt, head->as.text.data, head->as.text.size);
      if (global != UINT32_MAX && c->rt->global_macro[global]) {
        Ast* expanded = expand_call(c, a, global);
        c->definitions = definitions;
        return expanded && expression(c, expanded, tail);
      }
    }
  }
  if (ast_symbol(head, "defclass") || ast_symbol(head, "defabstract") ||
      ast_symbol(head, "reset-class!"))
    return class_declaration(c, a, head, args, definitions);
  if (ast_symbol(head, "defgeneric") || ast_symbol(head, "reset-generic!") ||
      ast_symbol(head, "defmethod")) {
    bool method = ast_symbol(head, "defmethod");
    if (c->scope || !definitions) return error(c, a, "generic declarations require top level");
    if (!rt_classes_init(c->rt)) return false;
    const Ast* signature = args;
    bool around = method && class_key(signature, ":around");
    if (around) signature = signature->next;
    if (!signature || signature->kind != AST_LIST || signature->as.list.tail ||
        !signature->as.list.first)
      return error(c, a, "expected a generic signature");
    const Ast* name = signature->as.list.first;
    if (name->kind != AST_SYMBOL) return error(c, name, "generic name must be a symbol");
    size_t count = 0;
    Ast params = {.kind = AST_LIST}, **next = &params.as.list.first;
    if (!constant(c, ((Value)(method ? B_GENERIC_METHOD : B_GENERIC_DEFINE) << 8) | TAG_BUILTIN))
      return false;
    if (method) {
      if (!expression(c, name, false) || !constant(c, ((Value)B_LIST << 8) | TAG_BUILTIN))
        return false;
    } else if (!constant(c, datum(c, name))) return false;
    for (const Ast* p = name->next; p; p = p->next) {
      const Ast *binding = p, *type = NULL;
      if (method && p->kind == AST_LIST) {
        binding = p->as.list.first;
        type = binding ? binding->next : NULL;
        if (p->as.list.tail || !type || type->next) return error(c, p, "invalid method parameter");
      }
      if (!binding || binding->kind != AST_SYMBOL || ast_symbol(binding, "&") ||
          ast_symbol(binding, "&optional"))
        return error(c, p, "generics need fixed parameters");
      for (const Ast* prior = params.as.list.first; prior; prior = prior->next)
        if (same_name(prior, binding)) return error(c, p, "duplicate generic parameter");
      Ast* copy = reader_allocate(c->expansion_arena, sizeof(*copy));
      if (!copy) return error(c, p, "compiler syntax memory limit");
      *copy = *binding;
      copy->next = NULL;
      *next = copy;
      next = &copy->next;
      if (method && !(type ? expression(c, type, false) : constant(c, c->rt->classes[C_OBJECT])))
        return false;
      count++;
    }
    if (method) {
      const Ast* forms = signature->next;
      bool has_next = class_key(forms, ":next");
      if (has_next) {
        const Ast* binding = forms->next;
        if (!binding || binding->kind != AST_SYMBOL ||
            memchr(binding->as.text.data, '/', binding->as.text.size) || ast_symbol(binding, "&") ||
            ast_symbol(binding, "&optional"))
          return error(c, forms, "next method needs a local binding name");
        if (!binding->next) return error(c, forms, "next method needs a body");
        for (const Ast* p = params.as.list.first; p; p = p->next)
          if (same_name(p, binding))
            return error(c, binding, "next binding conflicts with parameter");
        Ast* copy = reader_allocate(c->expansion_arena, sizeof(*copy));
        if (!copy) return error(c, binding, "compiler syntax memory limit");
        *copy = *binding;
        copy->next = params.as.list.first;
        params.as.list.first = copy;
        forms = binding->next;
      }
      return emit(c, OP_CALL, (uint32_t)count) != SIZE_MAX && lambda(c, &params, forms, false) &&
             constant(c, around ? V_TRUE : V_FALSE) && constant(c, has_next ? V_TRUE : V_FALSE) &&
             emit(c, OP_CALL, 5) != SIZE_MAX;
    }
    if (signature->next && (signature->next->kind != AST_STRING || signature->next->next))
      return error(c, a, "defgeneric accepts only an optional docstring");
    return constant(c, value_int((intptr_t)count)) &&
           constant(c, ast_symbol(head, "reset-generic!") ? V_TRUE : V_FALSE) &&
           emit(c, OP_CALL, 3) != SIZE_MAX;
  }
  if (ast_symbol(head, "defparam")) {
    if (c->scope || !definitions) return error(c, a, "defparam requires top level");
    if (!args || args->kind != AST_SYMBOL || !args->next)
      return error(c, a, "defparam needs a name and default");
    const Ast* value = args->next;
    if (value->kind == AST_STRING && value->next) value = value->next;
    if (value->next) return error(c, a, "defparam needs one default");
    uint32_t global = rt_global(c->rt, args->as.text.data, args->as.text.size);
    return global != UINT32_MAX && constant(c, datum(c, args)) && expression(c, value, false) &&
           emit(c, OP_MAKE_PARAM, 0) != SIZE_MAX && emit(c, OP_DEFINE_GLOBAL, global) != SIZE_MAX &&
           constant(c, V_NIL);
  }
  if (ast_symbol(head, "unwind-protect") || ast_symbol(head, "defer")) {
    if (!args) return error(c, a, "unwind-protect needs an expression");
    Ast params = {.kind = AST_LIST};
    return lambda(c, &params, args->next, false) && emit(c, OP_CLEANUP, 0) != SIZE_MAX &&
           expression(c, args, false) && emit(c, OP_RUN_CLEANUP, 0) != SIZE_MAX;
  }
  if (ast_symbol(head, "%call-with-cleanup")) {
    if (!args || !args->next || args->next->next) return error(c, a, "cleanup needs two callables");
    return expression(c, args, false) && expression(c, args->next, false) &&
           emit(c, OP_CLEANUP, 1) != SIZE_MAX && emit(c, OP_CALL, 0) != SIZE_MAX &&
           emit(c, OP_RUN_CLEANUP, 0) != SIZE_MAX;
  }
  if (ast_symbol(head, "restart-case")) {
    if (!args) return error(c, a, "restart-case needs an expression");
    size_t count = 0;
    for (const Ast* clause = args->next; clause; clause = clause->next) {
      const Ast* name = clause->kind == AST_LIST ? clause->as.list.first : NULL;
      if (!name || name->kind != AST_SYMBOL || clause->as.list.tail || !name->next)
        return error(c, clause, "invalid restart clause");
      const Ast *params = name->next, *description = NULL;
      if (params->kind == AST_STRING && params->next) {
        description = params;
        params = params->next;
      }
      if (!constant(c, datum(c, name)) ||
          !constant(c, description ? datum(c, description) : V_NIL) ||
          !lambda(c, params, params->next, false))
        return false;
      if (++count > UINT32_MAX) return error(c, clause, "too many restarts");
    }
    if (emit(c, OP_RESTARTS, (uint32_t)count) == SIZE_MAX) return false;
    size_t target = emit(c, OP_JUMP, 0);
    return target != SIZE_MAX && expression(c, args, false) && patch(c, target) &&
           emit(c, OP_END_RESTARTS, 0) != SIZE_MAX;
  }
  if (ast_symbol(head, "handler-bind")) {
    if (!args || args->kind != AST_LIST || args->as.list.tail)
      return error(c, a, "handler-bind needs a binding list");
    size_t count = 0;
    for (const Ast* binding = args->as.list.first; binding; binding = binding->next) {
      const Ast* predicate = binding->kind == AST_LIST ? binding->as.list.first : NULL;
      if (!predicate || binding->as.list.tail || !predicate->next || predicate->next->next)
        return error(c, binding, "handler binding needs a predicate and handler");
      if (!expression(c, predicate, false) || !expression(c, predicate->next, false)) return false;
      if (++count > UINT32_MAX) return error(c, binding, "too many handler bindings");
    }
    return emit(c, OP_BIND_HANDLERS, (uint32_t)count) != SIZE_MAX &&
           body(c, args->next, false, false) &&
           emit(c, OP_UNBIND_HANDLERS, (uint32_t)count) != SIZE_MAX;
  }
  if (ast_symbol(head, "with-params")) {
    if (!args || args->kind != AST_LIST || args->as.list.tail)
      return error(c, a, "with-params needs a binding list");
    size_t count = 0;
    for (const Ast* binding = args->as.list.first; binding; binding = binding->next) {
      const Ast* param = binding->kind == AST_LIST ? binding->as.list.first : NULL;
      if (!param || binding->as.list.tail || !param->next || param->next->next)
        return error(c, binding, "parameter binding needs a param and value");
      if (!expression(c, param, false) || !expression(c, param->next, false) ||
          emit(c, OP_BIND_PARAM, 0) == SIZE_MAX)
        return false;
      if (++count > UINT32_MAX) return error(c, binding, "too many parameter bindings");
    }
    return body(c, args->next, false, false) &&
           emit(c, OP_UNBIND_PARAMS, (uint32_t)count) != SIZE_MAX;
  }
  if (ast_symbol(head, "try")) return try_form(c, args, tail);
  if (ast_symbol(head, "defonce")) return error(c, a, "defonce requires top-level context");
  if (ast_symbol(head, "quote")) {
    if (!args || args->next) return error(c, a, "quote needs one datum");
    return constant(c, datum(c, args));
  }
  if (ast_symbol(head, "quasiquote")) {
    if (!args || args->next) return error(c, a, "quasiquote needs one datum");
    return quasiquote(c, args, tail);
  }
  if (ast_symbol(head, "unquote") || ast_symbol(head, "unquote-splicing"))
    return error(c, a, "unquote outside quasiquote");
  if (ast_symbol(head, "if")) {
    if (!args || !args->next || (args->next->next && args->next->next->next))
      return error(c, a, "if needs two or three operands");
    if (!expression(c, args, false)) return false;
    size_t branch = emit(c, OP_JFALSE, 0);
    if (!expression(c, args->next, tail)) return false;
    size_t done = emit(c, OP_JUMP, 0);
    if (!patch(c, branch)) return false;
    if (!(args->next->next ? expression(c, args->next->next, tail) : constant(c, V_NIL)))
      return false;
    return patch(c, done);
  }
  if (ast_symbol(head, "cond")) {
    size_t *jumps = NULL, count = 0, capacity = 0;
    bool ok = true, otherwise = false;
    for (const Ast* clause = args; clause && ok; clause = clause->next) {
      const Ast* test = clause->kind == AST_LIST ? clause->as.list.first : NULL;
      if (!test || clause->as.list.tail) {
        ok = error(c, clause, "cond needs proper nonempty clauses");
        break;
      }
      if (ast_symbol(test, "else")) {
        if (clause->next) {
          ok = error(c, clause, "cond else must be last");
          break;
        }
        otherwise = true;
        ok = body(c, test->next, tail, false);
        break;
      }
      if (!expression(c, test, false)) {
        ok = false;
        break;
      }
      if (!test->next && emit(c, OP_DUP, 0) == SIZE_MAX) {
        ok = false;
        break;
      }
      size_t skip = emit(c, OP_JFALSE, 0);
      if (test->next && !body(c, test->next, tail, false)) {
        ok = false;
        break;
      }
      if (!rt_grow(c->rt, (void**)&jumps, &capacity, count + 1, sizeof(size_t))) {
        ok = false;
        break;
      }
      jumps[count++] = emit(c, OP_JUMP, 0);
      if (!patch(c, skip) || (!test->next && emit(c, OP_POP, 0) == SIZE_MAX)) {
        ok = false;
        break;
      }
    }
    if (ok && !otherwise) ok = constant(c, V_NIL);
    for (size_t i = 0; ok && i < count; i++) ok = patch(c, jumps[i]);
    free(jumps);
    return ok;
  }
  if (ast_symbol(head, "when") || ast_symbol(head, "unless")) {
    if (!args) return error(c, a, "conditional body needs a condition");
    if (!expression(c, args, false)) return false;
    size_t branch = emit(c, OP_JFALSE, 0);
    if (ast_symbol(head, "unless")) {
      if (!constant(c, V_NIL)) return false;
      size_t done = emit(c, OP_JUMP, 0);
      return patch(c, branch) && body(c, args->next, tail, false) && patch(c, done);
    }
    if (!body(c, args->next, tail, false)) return false;
    size_t done = emit(c, OP_JUMP, 0);
    return patch(c, branch) && constant(c, V_NIL) && patch(c, done);
  }
  if (ast_symbol(head, "begin")) return body(c, args, tail, definitions);
  if (ast_symbol(head, "fn")) return lambda(c, args, args ? args->next : NULL, false);
  if (ast_symbol(head, "let")) return let_form(c, args, tail);
  if (ast_symbol(head, "set!")) {
    if (!args || !args->next || args->next->next) return error(c, a, "set! needs a name and value");
    Reference ref;
    return resolve(c, args, &ref) && expression(c, args->next, false) &&
           emit(c, ref.store, ref.index) != SIZE_MAX && constant(c, V_NIL);
  }
  if (ast_symbol(head, "def") || ast_symbol(head, "def-")) {
    if (!definitions) return error(c, a, "definition is only allowed at top level or in a body");
    const Ast* name = definition_name(a);
    if (!name || name->kind != AST_SYMBOL || !args->next) return error(c, a, "invalid definition");
    Reference ref;
    if (!c->scope) {
      ref.load = OP_GLOBAL;
      ref.store = OP_SET_GLOBAL;
      ref.index = rt_global(c->rt, name->as.text.data, name->as.text.size);
      if (ref.index == UINT32_MAX) return false;
    } else if (!resolve(c, name, &ref)) return false;
    if (args->kind == AST_LIST) {
      Ast params = *args;
      params.as.list.first = name->next;
      if (!lambda(c, &params, args->next, false)) return false;
    } else {
      const Ast* value = args->next;
      if (value->kind == AST_STRING && value->next) value = value->next;
      if (value->next) return error(c, a, "definition needs one value");
      if (!expression(c, value, false)) return false;
    }
    return emit(c, OP_DUP, 0) != SIZE_MAX &&
           emit(c, ref.store == OP_SET_GLOBAL ? OP_DEFINE_GLOBAL : ref.store, ref.index) !=
               SIZE_MAX;
  }
  if (ast_symbol(head, "while")) {
    if (!args) return error(c, a, "while needs a condition");
    uint32_t start = (uint32_t)c->fn->count;
    if (!expression(c, args, false)) return false;
    size_t done = emit(c, OP_JFALSE, 0);
    size_t saved = c->binding_count;
    c->scope++;
    bool ok = body(c, args->next, false, true) && clear_bindings(c, saved);
    for (size_t i = saved; i < c->binding_count; i++)
      c->lifetimes[c->bindings[i].slot].end = c->fn->count;
    c->scope--;
    c->binding_count = saved;
    if (!ok || emit(c, OP_POP, 0) == SIZE_MAX || emit(c, OP_JUMP, start) == SIZE_MAX ||
        !patch(c, done))
      return false;
    return constant(c, V_NIL);
  }
  if (ast_symbol(head, "and") || ast_symbol(head, "or")) {
    bool conjunction = ast_symbol(head, "and");
    if (!args) return constant(c, conjunction ? V_TRUE : V_FALSE);
    /* Recursive expansion would make long flat forms consume C stack. */
    size_t *jumps = NULL, count = 0, capacity = 0;
    bool ok = true;
    for (const Ast* p = args; p; p = p->next) {
      if (!expression(c, p, tail && !p->next)) {
        ok = false;
        break;
      }
      if (!p->next) break;
      if (emit(c, OP_DUP, 0) == SIZE_MAX) {
        ok = false;
        break;
      }
      size_t branch = emit(c, OP_JFALSE, 0);
      if (!conjunction) {
        size_t done = emit(c, OP_JUMP, 0);
        if (!patch(c, branch)) {
          ok = false;
          break;
        }
        branch = done;
      }
      if (!rt_grow(c->rt, (void**)&jumps, &capacity, count + 1, sizeof(size_t))) {
        ok = false;
        break;
      }
      jumps[count++] = branch;
      if (emit(c, OP_POP, 0) == SIZE_MAX) {
        ok = false;
        break;
      }
    }
    for (size_t i = 0; ok && i < count; i++) ok = patch(c, jumps[i]);
    free(jumps);
    return ok;
  }
  if (ast_symbol(head, "syntax-quote") || ast_symbol(head, "defmacro") || ast_symbol(head, "ns") ||
      ast_symbol(head, "require"))
    return error(c, a, "form not implemented in the prototype compiler");
  if (!expression(c, head, false)) return false;
  size_t count = 0;
  for (const Ast* p = args; p; p = p->next) {
    if (!expression(c, p, false)) return false;
    count++;
  }
  if (count > UINT32_MAX) return error(c, a, "too many arguments");
  return emit(c, tail ? OP_TAIL_CALL : OP_CALL, (uint32_t)count) != SIZE_MAX;
}

bool rt_compile(Runtime* rt, const Ast* form, uint32_t* function) {
  uint32_t id = new_function(rt);
  if (id == UINT32_MAX) return false;
  Reader arena;
  reader_init(&arena, "", 0, 16 * 1024 * 1024);
  size_t expansions = 0;
  Compiler c = {.rt = rt,
                .fn = rt->functions[id],
                .definitions = true,
                .expansion_arena = &arena,
                .expansions = &expansions};
  bool ok = expression(&c, form, false);
  if (ok) ok = emit(&c, OP_RETURN, 0) != SIZE_MAX && assign_slots(&c);
  free(c.bindings);
  free(c.upnames);
  free(c.lifetimes);
  *function = id;
  reader_destroy(&arena);
  return ok;
}

Ast* rt_expand_form(Runtime* rt, const Ast* form, Reader* arena) {
  Compiler c = {.rt = rt, .expansion_arena = arena};
  Value values[2] = {V_NIL, V_EMPTY};
  Roots roots;
  roots_push(&rt->heap, &roots, values, 2);
  values[0] = datum(&c, form);
  if (values[0]) {
    Value pair = rt_alloc(rt, K_PAIR, 2, 0);
    if (pair) {
      object_fields(pair)[0] = values[0];
      object_fields(pair)[1] = V_EMPTY;
      values[1] = pair;
    } else values[0] = 0;
  }
  Ast* result = NULL;
  uint32_t global = rt_binding_global(rt, 0, "*expander*", 10, false);
  if (values[0] && global != UINT32_MAX) {
    bool expanding = rt->expanding;
    rt->expanding = true;
    if (!expanding) rt->expansion_calls = 0;
    Value expanded = rt_call_list(rt, rt->globals[global], values[1]);
    rt->expanding = expanding;
    if (expanded) result = syntax_value(&c, expanded, form->offset, 0);
  }
  roots_pop(&rt->heap, &roots);
  return result;
}

Value rt_read_string(Runtime* rt, Value string) {
  if (!rt_kind(string, K_STRING)) {
    rt_error(rt, "read-string expects a string");
    return 0;
  }
  size_t n;
  memcpy(&n, object_bytes(string), sizeof(n));
  char* copy = malloc(n + 1);
  if (!copy) {
    rt_error(rt, "out of host memory");
    return 0;
  }
  memcpy(copy, object_bytes(string) + sizeof(n), n);
  copy[n] = 0;
  Value result = rt_read_source(rt, copy, n);
  free(copy);
  return result;
}

Value rt_read_source(Runtime* rt, const char* source, size_t length) {
  Reader reader;
  reader_init(&reader, source, length, 16 * 1024 * 1024);
  Ast* form = reader_one(&reader);
  Compiler c = {.rt = rt};
  Value result = form ? datum(&c, form) : 0;
  if (reader.error) {
    rt_coded_error(rt, reader.error_code, reader.error);
    rt->error_offset = reader.error_offset;
    result = 0;
  }
  reader_destroy(&reader);
  return result;
}

/* Eval values retain the namespace where they were obtained. */
Value rt_evaluator(Runtime* rt, uint32_t space) {
  for (size_t i = 0; i < rt->constant_count; i++) {
    Value v = rt->constants[i];
    if (!rt_kind(v, K_CLOSURE)) continue;
    uint32_t id;
    memcpy(&id, object_bytes(v), sizeof(id));
    Function* fn = rt->functions[id];
    if (fn->evaluator && fn->namespace == space) return v;
  }
  uint32_t id = new_function(rt);
  if (id == UINT32_MAX) return 0;
  Function* fn = rt->functions[id];
  fn->namespace = space;
  fn->evaluator = true;
  fn->arity = fn->locals = fn->locals_capacity = 1;
  fn->boxed = calloc(1, sizeof(bool));
  if (!fn->boxed) {
    rt_error(rt, "out of host memory");
    return 0;
  }
  Compiler c = {.rt = rt, .fn = fn};
  if (!constant(&c, ((Value)B_EVAL_IN_NAMESPACE << 8) | TAG_BUILTIN) ||
      !constant(&c, value_int(space)) || emit(&c, OP_LOCAL, 0) == SIZE_MAX ||
      emit(&c, OP_TAIL_CALL, 2) == SIZE_MAX)
    return 0;
  Value value = rt_alloc(rt, K_CLOSURE, 0, sizeof(id));
  if (!value) return 0;
  memcpy(object_bytes(value), &id, sizeof(id));
  return rt_constant(rt, value) == UINT32_MAX ? 0 : value;
}
