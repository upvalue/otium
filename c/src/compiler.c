#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* Single-pass register compiler. Registers are allocated in stack order: a
 * frame's parameters come first, then bindings and temporaries. A captured
 * binding is copied into closures by value unless it is assigned after capture,
 * in which case it lives in a box. Capture and assignment facts can only be
 * known after code referring to the binding is emitted, so a top-level form is
 * compiled again when a later fact changes an earlier decision. Facts are
 * numbered in binding creation order, which is the same on every attempt. */

/* A loop binding names a named let compiled as a loop in the enclosing frame:
 * reg is its first variable and target its first instruction. */
enum { BIND_LOCAL, BIND_SELF, BIND_LOOP };

typedef struct {
  const oti_ast* name;
  uint32_t reg, fact;
  unsigned char kind;
  bool boxed, initialized;
  uint32_t count;
  size_t target;
} oti_binding;

/* escaped is set for a named let whose name is used other than by tail calls
 * from its own body, which needs a real function. */
typedef struct {
  bool captured, assigned, early, escaped;
} oti_fact;

typedef struct {
  const oti_ast* form;
  oti_ast* expansion;
} oti_expansion;

/* State shared by every function compiled for one top-level form. */
typedef struct {
  oti_fact* facts;
  size_t fact_count, fact_capacity, next_fact;
  bool retry;
  oti_expansion* expansions;
  size_t expansion_count, expansion_capacity;
  uint32_t* functions;
  size_t function_count, function_capacity;
} oti_unit;

typedef struct {
  size_t* items;
  size_t count, capacity;
} oti_jumps;

typedef struct oti_compiler oti_compiler;

struct oti_compiler {
  ot_state* rt;
  oti_function* fn;
  oti_compiler* parent;
  oti_unit* unit;
  oti_binding* bindings;
  size_t binding_count, binding_capacity;
  const oti_ast** upnames;
  uint32_t* upfacts;
  size_t upname_capacity, upfact_capacity;
  /* Next free register, and the lowest top that keeps every binding. */
  uint32_t top, pinned;
  /* The highest forward jump target patched so far. */
  size_t label;
  unsigned scope;
  bool definitions;
  oti_arena* expansion_arena;
  /* The source offset of the form being compiled, recorded for the code it
   * emits. Datums are located pairs while reading forms for a macro. The
   * next function compiled takes naming as its name. */
  size_t offset;
  bool locate;
  const oti_ast* naming;
  size_t* expansions;
};

typedef enum { REF_LOCAL, REF_SELF, REF_LOOP, REF_UPVALUE, REF_GLOBAL } oti_ref_kind;

typedef struct {
  oti_ref_kind kind;
  uint32_t index, fact;
  size_t binding;
  bool boxed;
} oti_reference;

static bool expression(oti_compiler* c, const oti_ast* a, uint32_t dst, bool tail);
static bool body(oti_compiler* c, const oti_ast* forms, uint32_t dst, bool tail, bool definitions);

static bool error(oti_compiler* c, const oti_ast* a, const char* message) {
  bool fresh = !oti_failed(c->rt);
  oti_error(c->rt, message);
  if (fresh && a) oti_error_at(c->rt, a->offset);
  return false;
}

/* A misplaced or malformed class or generic declaration. */
static bool declaration_error(oti_compiler* c, const oti_ast* a, const char* message) {
  bool fresh = !oti_failed(c->rt);
  oti_tagged_error(c->rt, "declaration", "invalid-declaration", message, 0, NULL);
  if (fresh && a) oti_error_at(c->rt, a->offset);
  return false;
}

static bool same_name(const oti_ast* a, const oti_ast* b) {
  return a && b && a->symbol_id == b->symbol_id && a->kind == AST_SYMBOL && b->kind == AST_SYMBOL &&
         a->as.text.size == b->as.text.size &&
         !memcmp(a->as.text.data, b->as.text.data, a->as.text.size);
}

static size_t emit_word(oti_compiler* c, oti_instruction ins) {
  oti_function* f = c->fn;
  if (oti_failed(c->rt)) return SIZE_MAX;
  if (f->count >= UINT32_MAX ||
      !oti_grow(c->rt, (void**)&f->code, &f->capacity, f->count + 1, sizeof(oti_instruction))) {
    oti_error(c->rt, "function too large");
    return SIZE_MAX;
  }
  size_t i = f->count++;
  f->code[i] = ins;
  if (!f->point_count || f->points[f->point_count - 1].offset != c->offset) {
    if (!oti_grow(c->rt, (void**)&f->points, &f->point_capacity, f->point_count + 1,
                  sizeof(oti_source_point)))
      return SIZE_MAX;
    f->points[f->point_count++] = (oti_source_point){(uint32_t)i, (uint32_t)c->offset};
  }
  return i;
}

static size_t emit_x(oti_compiler* c, oti_opcode op, unsigned x, uint32_t a, uint32_t b,
                     uint32_t k) {
  if (a > UINT16_MAX || b > UINT16_MAX || k > UINT16_MAX) {
    oti_error(c->rt, "function too large");
    return SIZE_MAX;
  }
  return emit_word(
      c, (oti_instruction){(uint8_t)op, (uint8_t)x, (uint16_t)a, (uint16_t)b, (uint16_t)k});
}

static size_t emit(oti_compiler* c, oti_opcode op, uint32_t a, uint32_t b, uint32_t k) {
  return emit_x(c, op, 0, a, b, k);
}

/* Instructions with a 32-bit operand in b and c. */
static size_t emit_bc(oti_compiler* c, oti_opcode op, uint32_t a, uint32_t bc) {
  return emit(c, op, a, bc & 0xffff, bc >> 16);
}

static bool patch_to(oti_compiler* c, size_t at, size_t target) {
  if (at == SIZE_MAX || oti_failed(c->rt)) return false;
  if (target > UINT32_MAX) return oti_error(c->rt, "function too large");
  c->fn->code[at].b = (uint16_t)(target & 0xffff);
  c->fn->code[at].c = (uint16_t)(target >> 16);
  if (target > c->label) c->label = target;
  return true;
}

static bool patch(oti_compiler* c, size_t at) { return patch_to(c, at, c->fn->count); }

static bool jumps_add(oti_compiler* c, oti_jumps* jumps, size_t at) {
  if (at == SIZE_MAX) return false;
  if (!oti_grow(c->rt, (void**)&jumps->items, &jumps->capacity, jumps->count + 1, sizeof(size_t)))
    return false;
  jumps->items[jumps->count++] = at;
  return true;
}

static bool jumps_patch_to(oti_compiler* c, oti_jumps* jumps, size_t target) {
  bool ok = true;
  for (size_t i = 0; ok && i < jumps->count; i++) ok = patch_to(c, jumps->items[i], target);
  free(jumps->items);
  *jumps = (oti_jumps){0};
  return ok;
}

static bool jumps_patch(oti_compiler* c, oti_jumps* jumps) {
  return jumps_patch_to(c, jumps, c->fn->count);
}

static uint32_t next_register(oti_compiler* c) {
  uint32_t r = c->top;
  if (r >= UINT16_MAX) {
    oti_error(c->rt, "function too large");
    return UINT32_MAX;
  }
  c->top++;
  if (r > c->fn->locals) c->fn->locals = r;
  return r;
}

static void release(oti_compiler* c, uint32_t top) { c->top = top > c->pinned ? top : c->pinned; }

static bool finish(oti_compiler* c, uint32_t dst, bool tail) {
  return !tail || emit(c, OP_RETURN, dst, 0, 0) != SIZE_MAX;
}

static bool move(oti_compiler* c, uint32_t dst, uint32_t src) {
  return dst == src || emit(c, OP_MOVE, dst, src, 0) != SIZE_MAX;
}

static bool immediate(ot_value value) {
  return !value_is_ptr(value) && (ot_value)(intptr_t)(int32_t)value == value;
}

static bool load_constant(oti_compiler* c, ot_value value, uint32_t dst) {
  if (!value) return false;
  if (immediate(value)) return emit_bc(c, OP_IMM, dst, (uint32_t)value) != SIZE_MAX;
  uint32_t index = oti_constant(c->rt, value);
  return index != UINT32_MAX && emit_bc(c, OP_CONST, dst, index) != SIZE_MAX;
}

/* A constant in dst, returned when in tail position. */
static bool constant_result(oti_compiler* c, ot_value value, uint32_t dst, bool tail) {
  if (value && tail && immediate(value))
    return emit_bc(c, OP_RETURN_IMM, 0, (uint32_t)value) != SIZE_MAX;
  return load_constant(c, value, dst) && finish(c, dst, tail);
}

static bool load_builtin(oti_compiler* c, unsigned id, uint32_t dst) {
  return load_constant(c, ((ot_value)id << 8) | TAG_BUILTIN, dst);
}

static uint32_t new_function(ot_state* rt) {
  size_t id = rt->function_free;
  while (id < rt->function_count && rt->functions[id]) id++;
  if (id >= UINT32_MAX || !oti_grow(rt, (void**)&rt->functions, &rt->function_capacity, id + 1,
                                    sizeof(oti_function*))) {
    oti_error(rt, "too many functions");
    return UINT32_MAX;
  }
  oti_function* f = calloc(1, sizeof(*f));
  if (!f) {
    oti_error(rt, "out of host memory");
    return UINT32_MAX;
  }
  f->namespace = rt->current_namespace;
  f->source = rt->source;
  if (id == rt->function_count) rt->function_count++;
  rt->function_free = id + 1;
  rt->functions[id] = f;
  return (uint32_t)id;
}

/* Functions created by an attempt are discarded if the form is compiled again. */
static uint32_t unit_function(oti_compiler* c) {
  uint32_t id = new_function(c->rt);
  oti_unit* u = c->unit;
  if (id == UINT32_MAX || !oti_grow(c->rt, (void**)&u->functions, &u->function_capacity,
                                    u->function_count + 1, sizeof(uint32_t)))
    return UINT32_MAX;
  u->functions[u->function_count++] = id;
  return id;
}

static uint32_t new_fact(oti_compiler* c) {
  oti_unit* u = c->unit;
  uint32_t index = (uint32_t)u->next_fact++;
  size_t old = u->fact_capacity;
  if (!oti_grow(c->rt, (void**)&u->facts, &u->fact_capacity, index + 1, sizeof(oti_fact)))
    return UINT32_MAX;
  if (u->fact_capacity > old)
    memset(u->facts + old, 0, (u->fact_capacity - old) * sizeof(oti_fact));
  if (index >= u->fact_count) u->fact_count = index + 1;
  return index;
}

static oti_fact* fact(oti_compiler* c, uint32_t index) { return &c->unit->facts[index]; }

static bool needs_box(const oti_fact* f) { return f->captured && (f->assigned || f->early); }

static size_t bind_fact(oti_compiler* c, const oti_ast* name, uint32_t reg, bool initialized,
                        uint32_t index) {
  if (!name || name->kind != AST_SYMBOL) {
    error(c, name, "binding name must be a symbol");
    return SIZE_MAX;
  }
  if (index == UINT32_MAX || !oti_grow(c->rt, (void**)&c->bindings, &c->binding_capacity,
                                       c->binding_count + 1, sizeof(oti_binding)))
    return SIZE_MAX;
  bool boxed = needs_box(fact(c, index));
  c->bindings[c->binding_count] =
      (oti_binding){name, reg, index, BIND_LOCAL, boxed, initialized, 0, 0};
  if (reg + 1 > c->pinned) c->pinned = reg + 1;
  return c->binding_count++;
}

static size_t bind(oti_compiler* c, const oti_ast* name, uint32_t reg, bool initialized) {
  return bind_fact(c, name, reg, initialized, new_fact(c));
}

/* A binding created with its value already in its register. */
static bool bind_value(oti_compiler* c, const oti_ast* name, uint32_t reg) {
  size_t b = bind(c, name, reg, true);
  return b != SIZE_MAX && (!c->bindings[b].boxed || emit(c, OP_BOX, reg, 0, 0) != SIZE_MAX);
}

static size_t find_binding(oti_compiler* c, const oti_ast* name) {
  for (size_t i = c->binding_count; i > 0; i--)
    if (same_name(c->bindings[i - 1].name, name)) return i - 1;
  return SIZE_MAX;
}

static bool shadowed(oti_compiler* c, const oti_ast* name) {
  for (oti_compiler* scope = c; scope; scope = scope->parent)
    if (find_binding(scope, name) != SIZE_MAX) return true;
  return false;
}

static bool capture(oti_compiler* c, const oti_ast* name, uint32_t* index) {
  for (size_t i = 0; i < c->fn->capture_count; i++)
    if (same_name(c->upnames[i], name)) {
      *index = (uint32_t)i;
      return true;
    }
  if (!c->parent) return false;
  oti_compiler* p = c->parent;
  size_t b = find_binding(p, name);
  oti_capture source;
  uint32_t origin;
  if (b != SIZE_MAX) {
    oti_binding* binding = &p->bindings[b];
    origin = binding->fact;
    if (binding->kind == BIND_LOOP) {
      /* The loop needs a function; this attempt is discarded. */
      fact(c, origin)->escaped = true;
      c->unit->retry = true;
      source = (oti_capture){0, true, false};
    } else if (binding->kind == BIND_SELF) source = (oti_capture){0, true, false};
    else {
      oti_fact* f = fact(c, origin);
      f->captured = true;
      if (!binding->initialized) f->early = true;
      if (needs_box(f) && !binding->boxed) c->unit->retry = true;
      source = (oti_capture){binding->reg, true, binding->boxed};
    }
  } else {
    uint32_t outer;
    if (!capture(p, name, &outer)) return false;
    source = (oti_capture){outer, false, p->fn->captures[outer].boxed};
    origin = p->upfacts[outer];
  }
  oti_function* f = c->fn;
  if (f->capture_count >= UINT16_MAX ||
      !oti_grow(c->rt, (void**)&f->captures, &f->capture_capacity, f->capture_count + 1,
                sizeof(oti_capture)) ||
      !oti_grow(c->rt, (void**)&c->upnames, &c->upname_capacity, f->capture_count + 1,
                sizeof(oti_ast*)) ||
      !oti_grow(c->rt, (void**)&c->upfacts, &c->upfact_capacity, f->capture_count + 1,
                sizeof(uint32_t)))
    return oti_error(c->rt, "too many captures");
  *index = (uint32_t)f->capture_count++;
  f->captures[*index] = source;
  c->upnames[*index] = name;
  c->upfacts[*index] = origin;
  return true;
}

static bool resolve(oti_compiler* c, const oti_ast* name, oti_reference* ref) {
  if (!name || name->kind != AST_SYMBOL) return error(c, name, "expected a symbol");
  size_t b = find_binding(c, name);
  if (b != SIZE_MAX) {
    oti_binding* binding = &c->bindings[b];
    if (binding->kind == BIND_LOOP) {
      fact(c, binding->fact)->escaped = true;
      c->unit->retry = true;
      *ref = (oti_reference){REF_LOOP, 0, binding->fact, b, false};
      return true;
    }
    *ref = (oti_reference){binding->kind == BIND_SELF ? REF_SELF : REF_LOCAL, binding->reg,
                           binding->fact, b, binding->boxed};
    return true;
  }
  uint32_t index;
  if (capture(c, name, &index)) {
    *ref = (oti_reference){REF_UPVALUE, index, c->upfacts[index], SIZE_MAX,
                           c->fn->captures[index].boxed};
    return true;
  }
  if (oti_failed(c->rt)) return false;
  index = oti_resolve_global(c->rt, name->as.text.data, name->as.text.size);
  *ref = (oti_reference){REF_GLOBAL, index, UINT32_MAX, SIZE_MAX, false};
  return index != UINT32_MAX;
}

static bool load(oti_compiler* c, const oti_reference* ref, uint32_t dst) {
  switch (ref->kind) {
    case REF_LOCAL:
      if (ref->boxed) return emit(c, OP_UNBOX, dst, ref->index, 0) != SIZE_MAX;
      if (!c->bindings[ref->binding].initialized && emit(c, OP_CHECK, ref->index, 0, 0) == SIZE_MAX)
        return false;
      return move(c, dst, ref->index);
    case REF_SELF: return move(c, dst, 0);
    /* Escaped loops are compiled again as functions. */
    case REF_LOOP: return load_constant(c, OT_NIL, dst);
    case REF_UPVALUE:
      return emit(c, ref->boxed ? OP_UPVALUE_BOX : OP_UPVALUE, dst, ref->index, 0) != SIZE_MAX;
    case REF_GLOBAL: return emit_bc(c, OP_GLOBAL, dst, ref->index) != SIZE_MAX;
  }
  return false;
}

/* Assignment by set!. Assigning a captured unboxed binding means its box
 * decision was wrong, so the form is compiled again. */
static bool assign(oti_compiler* c, const oti_reference* ref, uint32_t src) {
  if (ref->fact != UINT32_MAX) {
    oti_fact* f = fact(c, ref->fact);
    f->assigned = true;
    if (ref->kind == REF_SELF || ref->kind == REF_LOOP || (needs_box(f) && !ref->boxed))
      c->unit->retry = true;
  }
  switch (ref->kind) {
    case REF_LOCAL:
      if (ref->boxed) return emit(c, OP_SET_BOX, ref->index, src, 0) != SIZE_MAX;
      return move(c, ref->index, src);
    case REF_SELF:
    case REF_LOOP: return true;
    case REF_UPVALUE: return emit(c, OP_SET_UPVALUE, src, ref->index, 0) != SIZE_MAX;
    case REF_GLOBAL: return emit_bc(c, OP_SET_GLOBAL, src, ref->index) != SIZE_MAX;
  }
  return false;
}

/* The initializing store of a body definition. A second definition of the same
 * binding is an assignment. */
static bool define_local(oti_compiler* c, size_t b, uint32_t src) {
  oti_binding* binding = &c->bindings[b];
  if (binding->initialized) {
    oti_fact* f = fact(c, binding->fact);
    f->assigned = true;
    if (needs_box(f) && !binding->boxed) c->unit->retry = true;
  }
  binding->initialized = true;
  if (binding->boxed) return emit(c, OP_SET_BOX, binding->reg, src, 0) != SIZE_MAX;
  return move(c, binding->reg, src);
}

/* Bindings created since a scope began are cleared when it ends, so dead
 * values are not retained by live frames. */
typedef struct {
  size_t bindings;
  uint32_t top, pinned;
} oti_scope;

static oti_scope scope_begin(oti_compiler* c) {
  c->scope++;
  return (oti_scope){c->binding_count, c->top, c->pinned};
}

static bool scope_end(oti_compiler* c, oti_scope s, bool clear) {
  bool ok = true;
  if (clear && c->top > s.top) ok = emit(c, OP_CLEAR, s.top, c->top - s.top, 0) != SIZE_MAX;
  c->binding_count = s.bindings;
  c->pinned = s.pinned;
  c->top = s.top;
  c->scope--;
  return ok;
}

static ot_value datum(oti_compiler* c, const oti_ast* a) {
  ot_state* rt = c->rt;
  switch (a->kind) {
    case AST_VALUE: return rt->constants[a->as.integer];
    case AST_NIL: return OT_NIL;
    case AST_BOOL: return a->as.integer ? OT_TRUE : OT_FALSE;
    case AST_CHAR: return ((ot_value)a->as.integer << 8) | TAG_CHAR;
    case AST_INT:
      if (a->as.integer < INTPTR_MIN / 2 || a->as.integer > INTPTR_MAX / 2) {
        error(c, a, "integer exceeds immediate range");
        return 0;
      }
      return ot_int((intptr_t)a->as.integer);
    case AST_FLOAT: return oti_float(rt, a->as.real);
    case AST_STRING: return oti_text(rt, K_STRING, a->as.text.data, a->as.text.size);
    case AST_SYMBOL:
    case AST_KEYWORD: {
      unsigned kind = a->kind == AST_SYMBOL ? K_SYMBOL : K_KEYWORD;
      for (size_t i = 0; i < rt->constant_count; i++) {
        ot_value v = rt->constants[i];
        if (!oti_kind(v, kind)) continue;
        size_t length;
        unsigned char* bytes = object_bytes(v);
        memcpy(&length, bytes, sizeof(length));
        if ((kind != K_SYMBOL || oti_symbol_id(v) == a->symbol_id) && length == a->as.text.size &&
            !memcmp(bytes + sizeof(length), a->as.text.data, length))
          return v;
      }
      ot_value v = oti_text(rt, kind, a->as.text.data, a->as.text.size);
      if (v && kind == K_SYMBOL)
        memcpy(object_bytes(v) + sizeof(size_t) + a->as.text.size + 1, &a->symbol_id,
               sizeof(a->symbol_id));
      if (v && oti_constant(rt, v) == UINT32_MAX) return 0;
      return v;
    }
    case AST_LIST: {
      ot_value slots[] = {OT_EMPTY, OT_EMPTY, OT_NIL};
      oti_roots roots;
      oti_roots_push(&rt->heap, &roots, slots, 3);
      for (const oti_ast* item = a->as.list.first; item; item = item->next) {
        slots[2] = datum(c, item);
        if (!slots[2]) break;
        bool located = c->locate && slots[0] == OT_EMPTY;
        ot_value pair = oti_alloc(rt, K_PAIR, 2, located ? OTI_LOCATED_PAIR_BYTES : 0);
        if (!pair) break;
        if (located) {
          size_t offset = a->offset;
          memcpy(object_bytes(pair), &offset, sizeof(offset));
        }
        object_fields(pair)[0] = slots[2];
        object_fields(pair)[1] = OT_EMPTY;
        if (slots[0] == OT_EMPTY) slots[0] = pair;
        else object_fields(slots[1])[1] = pair;
        slots[1] = pair;
      }
      if (!oti_failed(rt) && a->as.list.tail) {
        slots[2] = datum(c, a->as.list.tail);
        if (slots[2]) object_fields(slots[1])[1] = slots[2];
      }
      ot_value result = oti_failed(rt) ? 0 : slots[0];
      oti_roots_pop(&rt->heap, &roots);
      return result;
    }
  }
  return 0;
}

/* Expanded syntax is copied into a host arena; it never holds heap pointers. */
static oti_ast* syntax_value(oti_compiler* c, ot_value v, size_t offset, unsigned depth) {
  if (depth > 256) {
    error(c, NULL, "macro syntax nesting limit exceeded");
    return NULL;
  }
  oti_ast* a = oti_arena_alloc(c->expansion_arena, sizeof(*a));
  if (!a) {
    error(c, NULL, "macro syntax memory limit exceeded");
    return NULL;
  }
  size_t located;
  if (oti_pair_offset(v, &located)) offset = located;
  a->offset = offset;
  if (v == OT_NIL) a->kind = AST_NIL;
  else if (v == OT_TRUE || v == OT_FALSE) {
    a->kind = AST_BOOL;
    a->as.integer = v == OT_TRUE;
  } else if (ot_is_int(v)) {
    a->kind = AST_INT;
    a->as.integer = ot_to_int(v);
  } else if ((v & 255) == TAG_CHAR) {
    a->kind = AST_CHAR;
    a->as.integer = v >> 8;
  } else if (oti_kind(v, K_FLOAT)) {
    a->kind = AST_FLOAT;
    memcpy(&a->as.real, object_bytes(v), sizeof(double));
  } else if (oti_kind(v, K_STRING) || oti_kind(v, K_SYMBOL) || oti_kind(v, K_KEYWORD)) {
    a->kind = oti_kind(v, K_STRING) ? AST_STRING : oti_kind(v, K_SYMBOL) ? AST_SYMBOL : AST_KEYWORD;
    size_t n;
    memcpy(&n, object_bytes(v), sizeof(n));
    char* copy = oti_arena_alloc(c->expansion_arena, n + 1);
    if (!copy) {
      error(c, NULL, "macro syntax memory limit exceeded");
      return NULL;
    }
    memcpy(copy, object_bytes(v) + sizeof(n), n);
    copy[n] = 0;
    a->as.text.data = copy;
    a->as.text.size = n;
    if (a->kind == AST_SYMBOL) a->symbol_id = oti_symbol_id(v);
  } else if (v == OT_EMPTY || oti_kind(v, K_PAIR)) {
    a->kind = AST_LIST;
    oti_ast** next = &a->as.list.first;
    ot_value slow = v, fast = v;
    while (oti_kind(v, K_PAIR)) {
      *next = syntax_value(c, object_fields(v)[0], offset, depth + 1);
      if (!*next) return NULL;
      next = &(*next)->next;
      v = object_fields(v)[1];
      if (oti_kind(slow, K_PAIR)) slow = object_fields(slow)[1];
      for (unsigned i = 0; i < 2 && oti_kind(fast, K_PAIR); i++) fast = object_fields(fast)[1];
      if (oti_kind(fast, K_PAIR) && fast == slow) {
        error(c, NULL, "cyclic macro syntax");
        return NULL;
      }
    }
    if (v != OT_EMPTY) {
      a->as.list.tail = syntax_value(c, v, offset, depth + 1);
      if (!a->as.list.tail) return NULL;
    }
  } else {
    uint32_t index = oti_constant(c->rt, v);
    if (index == UINT32_MAX) return NULL;
    a->kind = AST_VALUE;
    a->as.integer = index;
  }
  return a;
}

oti_ast* oti_syntax_value(ot_state* rt, ot_value value, oti_arena* arena) {
  oti_compiler c = {.rt = rt, .expansion_arena = arena};
  return syntax_value(&c, value, 0, 0);
}

static oti_ast* expand_call(oti_compiler* c, const oti_ast* form, uint32_t global) {
  ot_state* rt = c->rt;
  if (++*c->expansions > 256) {
    error(c, form, "macro expansion limit exceeded");
    return NULL;
  }
  oti_ast arguments = {.kind = AST_LIST, .as.list.first = form->as.list.first->next};
  bool locate = c->locate;
  c->locate = true;
  ot_value args = datum(c, &arguments);
  c->locate = locate;
  if (!args) return NULL;
  ot_value result = oti_call_list(rt, rt->globals[global], args);
  return result ? syntax_value(c, result, form->offset, 0) : NULL;
}

/* Plans use indices and reader-owned ASTs. Managed constants live in a separate
 * root array so host-array growth and heap compaction cannot leave stale pointers. */
typedef enum { QQ_CONSTANT, QQ_EXPRESSION, QQ_CONS, QQ_APPEND } oti_qq_kind;

typedef struct {
  oti_qq_kind kind;
  size_t left, right;
  const oti_ast* expression;
} oti_qq_node;

typedef struct {
  oti_compiler* compiler;
  oti_qq_node* nodes;
  ot_value* values;
  size_t count, capacity, value_capacity;
  oti_roots roots;
} oti_qq_plan;

static size_t qq_add(oti_qq_plan* plan, oti_qq_kind kind, size_t left, size_t right,
                     const oti_ast* a, ot_value value) {
  ot_state* rt = plan->compiler->rt;
  if (plan->count >= 16 * 1024 * 1024 / (sizeof(oti_qq_node) + sizeof(ot_value))) {
    oti_error(rt, "quasiquote expansion exceeds budget");
    return SIZE_MAX;
  }
  if (!oti_grow(rt, (void**)&plan->nodes, &plan->capacity, plan->count + 1, sizeof(oti_qq_node)))
    return SIZE_MAX;
  if (!oti_grow(rt, (void**)&plan->values, &plan->value_capacity, plan->count + 1,
                sizeof(ot_value)))
    return SIZE_MAX;
  plan->roots.slots = plan->values;
  size_t index = plan->count++;
  plan->nodes[index] = (oti_qq_node){kind, left, right, a};
  plan->values[index] = value;
  plan->roots.count = plan->count;
  return index;
}

static size_t qq_constant(oti_qq_plan* plan, ot_value value) {
  return value ? qq_add(plan, QQ_CONSTANT, 0, 0, NULL, value) : SIZE_MAX;
}

static size_t qq_join(oti_qq_plan* plan, size_t left, size_t right, bool splice) {
  if (left == SIZE_MAX || right == SIZE_MAX) return SIZE_MAX;
  if (splice && plan->nodes[right].kind == QQ_CONSTANT && plan->values[right] == OT_EMPTY)
    return left;
  if (!splice && plan->nodes[left].kind == QQ_CONSTANT && plan->nodes[right].kind == QQ_CONSTANT) {
    ot_value pair = oti_alloc(plan->compiler->rt, K_PAIR, 2, 0);
    if (!pair) return SIZE_MAX;
    object_fields(pair)[0] = plan->values[left];
    object_fields(pair)[1] = plan->values[right];
    return qq_constant(plan, pair);
  }
  return qq_add(plan, splice ? QQ_APPEND : QQ_CONS, left, right, NULL, OT_NIL);
}

static int qq_marker(const oti_ast* a) {
  return oti_ast_symbol(a, "quasiquote")         ? 1
         : oti_ast_symbol(a, "unquote")          ? 2
         : oti_ast_symbol(a, "unquote-splicing") ? 3
                                                 : 0;
}

static size_t qq_node(oti_qq_plan* plan, const oti_ast* a, unsigned depth);

static size_t qq_special(oti_qq_plan* plan, const oti_ast* head, unsigned depth) {
  int marker = qq_marker(head);
  const oti_ast* arg = head->next;
  if (!arg) {
    error(plan->compiler, head, "quasiquote marker needs an operand");
    return SIZE_MAX;
  }
  if (marker != 1 && !depth) {
    error(plan->compiler, head, "unquote outside quasiquote");
    return SIZE_MAX;
  }
  if (marker == 2 && depth == 1) return qq_add(plan, QQ_EXPRESSION, 0, 0, arg, OT_NIL);
  size_t value = qq_node(plan, arg, marker == 1 ? depth + 1 : depth - 1);
  if (value == SIZE_MAX) return SIZE_MAX;
  size_t empty = qq_constant(plan, OT_EMPTY);
  size_t rest = qq_join(plan, value, empty, false);
  if (rest == SIZE_MAX) return SIZE_MAX;
  size_t name = qq_constant(plan, datum(plan->compiler, head));
  return qq_join(plan, name, rest, false);
}

static size_t qq_node(oti_qq_plan* plan, const oti_ast* a, unsigned depth) {
  if (a->kind != AST_LIST) return qq_constant(plan, datum(plan->compiler, a));

  typedef struct {
    size_t node;
    bool splice;
  } oti_qq_piece;

  oti_qq_piece* pieces = NULL;
  size_t count = 0, capacity = 0, rest = SIZE_MAX;
  const oti_ast* p = a->as.list.first;
  bool ok = true;
  for (; p; p = p->next) {
    /* List tails have the same syntax as explicitly dotted tails. */
    if (qq_marker(p)) {
      rest = qq_special(plan, p, depth);
      break;
    }
    bool splice =
        depth == 1 && p->kind == AST_LIST && oti_ast_symbol(p->as.list.first, "unquote-splicing");
    size_t node;
    if (splice) {
      const oti_ast* arg = p->as.list.first->next;
      if (!arg) {
        error(plan->compiler, p, "splice needs an operand");
        ok = false;
        break;
      }
      node = qq_add(plan, QQ_EXPRESSION, 0, 0, arg, OT_NIL);
    } else node = qq_node(plan, p, depth);
    if (node == SIZE_MAX || !oti_grow(plan->compiler->rt, (void**)&pieces, &capacity, count + 1,
                                      sizeof(oti_qq_piece))) {
      ok = false;
      break;
    }
    pieces[count++] = (oti_qq_piece){node, splice};
  }
  if (ok && !p)
    rest = a->as.list.tail ? qq_node(plan, a->as.list.tail, depth) : qq_constant(plan, OT_EMPTY);
  if (ok)
    for (size_t i = count; i > 0 && rest != SIZE_MAX; i--)
      rest = qq_join(plan, pieces[i - 1].node, rest, pieces[i - 1].splice);
  free(pieces);
  return ok ? rest : SIZE_MAX;
}

/* Expansions are cached by form so that compiling a form again does not run
 * its macros again. */
static oti_ast* expand(oti_compiler* c, const oti_ast* form, uint32_t global) {
  oti_unit* u = c->unit;
  if (u)
    for (size_t i = 0; i < u->expansion_count; i++)
      if (u->expansions[i].form == form) return u->expansions[i].expansion;
  oti_ast* expanded = expand_call(c, form, global);
  if (expanded && u &&
      oti_grow(c->rt, (void**)&u->expansions, &u->expansion_capacity, u->expansion_count + 1,
               sizeof(oti_expansion)))
    u->expansions[u->expansion_count++] = (oti_expansion){form, expanded};
  return expanded;
}

/* The expansion of a call to a global macro not shadowed by a local, or the
 * form itself. NULL reports an expansion error. */
static const oti_ast* expand_head(oti_compiler* c, const oti_ast* a) {
  while (a->kind == AST_LIST && !a->as.list.tail && a->as.list.first &&
         a->as.list.first->kind == AST_SYMBOL && !shadowed(c, a->as.list.first)) {
    const oti_ast* head = a->as.list.first;
    uint32_t global = oti_lookup_global(c->rt, head->as.text.data, head->as.text.size);
    if (global == UINT32_MAX || !c->rt->global_info[global].macro) break;
    a = expand(c, a, global);
    if (!a) return NULL;
  }
  return a;
}

/* Drop a final store of a constant or copy into the unused register t, made
 * since start, unless a jump lands after it. */
static void drop_unused(oti_compiler* c, size_t start, uint32_t t) {
  oti_function* f = c->fn;
  if (f->count <= start || c->label >= f->count) return;
  oti_instruction last = f->code[f->count - 1];
  if ((last.op == OP_IMM || last.op == OP_CONST || last.op == OP_MOVE) && last.a == t) f->count--;
}

/* Evaluate an expression for its effects only. */
static bool effect(oti_compiler* c, const oti_ast* a) {
  uint32_t saved = c->top, t = next_register(c);
  size_t start = c->fn->count;
  bool ok = expression(c, a, t, false);
  if (ok) drop_unused(c, start, t);
  release(c, saved);
  return ok;
}

/* Calls place the callee in the topmost register so every register above it
 * is free when the callee's frame starts there. A destination that is itself
 * the topmost register is used directly. */
static uint32_t call_begin(oti_compiler* c, uint32_t dst, bool tail, uint32_t* saved) {
  *saved = c->top;
  return !tail && dst + 1 == c->top ? dst : next_register(c);
}

static bool call_end(oti_compiler* c, uint32_t base, size_t count, uint32_t saved, uint32_t dst,
                     bool tail) {
  if (count > UINT16_MAX) return oti_error(c->rt, "too many arguments");
  if (emit(c, tail ? OP_TAIL_CALL : OP_CALL, base, (uint32_t)count, 0) == SIZE_MAX) return false;
  release(c, saved);
  return tail || move(c, dst, base);
}

static bool argument(oti_compiler* c, const oti_ast* a) {
  return expression(c, a, next_register(c), false);
}

static bool quasiquote(oti_compiler* c, const oti_ast* a, uint32_t dst, bool tail) {
  oti_qq_plan plan = {.compiler = c};
  oti_roots_push(&c->rt->heap, &plan.roots, NULL, 0);
  size_t root = qq_node(&plan, a, 1);

  /* Explicit work list: long quoted lists would otherwise recurse deeply. */
  typedef struct {
    size_t node;
    unsigned stage;
    uint32_t dst, base, saved;
  } oti_qq_pending;

  oti_qq_pending* pending = NULL;
  size_t count = 0, capacity = 0;
  bool ok =
      root != SIZE_MAX && oti_grow(c->rt, (void**)&pending, &capacity, 1, sizeof(oti_qq_pending));
  if (ok) pending[count++] = (oti_qq_pending){root, 0, dst, 0, 0};
  while (ok && count) {
    oti_qq_pending* p = &pending[count - 1];
    oti_qq_node node = plan.nodes[p->node];
    if (node.kind == QQ_CONSTANT) {
      ok = load_constant(c, plan.values[p->node], p->dst);
      count--;
    } else if (node.kind == QQ_EXPRESSION) {
      /* A template that is a single splice is in tail position. */
      bool whole = tail && count == 1;
      ok = expression(c, node.expression, p->dst, whole);
      tail &= !whole;
      count--;
    } else if (p->stage == 0) {
      p->base = call_begin(c, p->dst, false, &p->saved);
      ok = load_builtin(c, node.kind == QQ_CONS ? B_CONS : B_QQ_APPEND, p->base);
      p->stage = 1;
    } else if (p->stage < 3) {
      size_t child = p->stage == 1 ? node.left : node.right;
      p->stage++;
      uint32_t r = next_register(c);
      if (!(ok = oti_grow(c->rt, (void**)&pending, &capacity, count + 1, sizeof(oti_qq_pending))))
        break;
      pending[count++] = (oti_qq_pending){child, 0, r, 0, 0};
    } else {
      ok = call_end(c, p->base, 2, p->saved, p->dst, false);
      count--;
    }
  }
  free(pending);
  oti_roots_pop(&c->rt->heap, &plan.roots);
  free(plan.nodes);
  free(plan.values);
  return ok && finish(c, dst, tail);
}

/* Compile a function into register dst. A self name refers to the closure
 * itself inside its own body, unless the binding it names is ever assigned. */
static bool lambda(oti_compiler* c, const oti_ast* params, const oti_ast* forms, bool bindings,
                   uint32_t dst, const oti_ast* self, uint32_t self_fact) {
  if (!params || (params->kind != AST_LIST && (bindings || params->kind != AST_SYMBOL)))
    return error(c, params, "expected a parameter list or rest symbol");
  uint32_t id = unit_function(c);
  if (id == UINT32_MAX) return false;
  oti_compiler child = {.rt = c->rt,
                        .fn = c->rt->functions[id],
                        .parent = c,
                        .unit = c->unit,
                        .top = 1,
                        .scope = 1,
                        .expansion_arena = c->expansion_arena,
                        .expansions = c->expansions,
                        .offset = c->offset};
  bool ok = true;
  const oti_ast* label = self ? self : c->naming;
  c->naming = NULL;
  if (label && label->kind == AST_SYMBOL) {
    child.fn->name = malloc(label->as.text.size + 1);
    if (!child.fn->name) return oti_error(c->rt, "out of host memory");
    memcpy(child.fn->name, label->as.text.data, label->as.text.size);
    child.fn->name[label->as.text.size] = 0;
  }
  if (self && !fact(c, self_fact)->assigned) {
    ok = oti_grow(c->rt, (void**)&child.bindings, &child.binding_capacity, 1, sizeof(oti_binding));
    if (ok)
      child.bindings[child.binding_count++] =
          (oti_binding){self, 0, self_fact, BIND_SELF, false, true, 0, 0};
  }
  const oti_ast* rest = params->kind == AST_SYMBOL ? params : params->as.list.tail;
  const oti_ast* first = params->kind == AST_LIST ? params->as.list.first : NULL;
  const oti_ast* optional = NULL;
  for (const oti_ast* p = first; ok && p; p = p->next) {
    const oti_ast* name = bindings ? p->as.list.first : p;
    if (!bindings && oti_ast_symbol(name, "&optional")) {
      if (rest || !p->next) {
        ok = error(c, p, "optional parameters need defaults and cannot combine with rest");
        break;
      }
      optional = p->next;
      break;
    }
    if (!bindings && oti_ast_symbol(name, "&")) {
      if (rest || !p->next || p->next->next) {
        ok = error(c, p, "rest marker needs one final parameter");
        break;
      }
      rest = p->next;
      break;
    }
    if (!name || name->kind != AST_SYMBOL || oti_ast_symbol(name, "&optional") ||
        oti_ast_symbol(name, "&")) {
      ok = error(c, name, "expected a symbol parameter");
      break;
    }
    size_t prior = find_binding(&child, name);
    if (prior != SIZE_MAX && child.bindings[prior].kind != BIND_SELF) {
      ok = error(c, name, "duplicate parameter");
      break;
    }
    if (bind(&child, name, next_register(&child), true) == SIZE_MAX) {
      ok = false;
      break;
    }
    child.fn->arity++;
  }
  if (ok && rest) {
    size_t prior = find_binding(&child, rest);
    if (bindings || rest->kind != AST_SYMBOL || oti_ast_symbol(rest, "&") ||
        oti_ast_symbol(rest, "&optional"))
      ok = error(c, rest, "expected a rest parameter symbol");
    else if (prior != SIZE_MAX && child.bindings[prior].kind != BIND_SELF)
      ok = error(c, rest, "duplicate parameter");
    else if (bind(&child, rest, next_register(&child), true) == SIZE_MAX) ok = false;
    else child.fn->rest = true;
  }
  for (const oti_ast* p = optional; ok && p; p = p->next) {
    const oti_ast* name = p->kind == AST_LIST ? p->as.list.first : NULL;
    if (!name || name->kind != AST_SYMBOL || !name->next || name->next->next || p->as.list.tail ||
        oti_ast_symbol(name, "&") || oti_ast_symbol(name, "&optional")) {
      ok = error(c, p, "expected (name default) after &optional");
      break;
    }
    size_t prior = find_binding(&child, name);
    if (prior != SIZE_MAX && child.bindings[prior].kind != BIND_SELF) {
      ok = error(c, name, "duplicate parameter");
      break;
    }
    for (const oti_ast* previous = optional; previous != p; previous = previous->next)
      if (same_name(previous->as.list.first, name)) {
        ok = error(c, name, "duplicate parameter");
        break;
      }
    if (!ok || next_register(&child) == UINT32_MAX) {
      ok = false;
      break;
    }
    child.fn->optional++;
  }
  /* Captured, assigned parameters are boxed on entry. A self tail call jumps
   * back here, so each iteration gets fresh boxes. */
  for (size_t i = 0; ok && i < child.binding_count; i++)
    if (child.bindings[i].boxed) ok = emit(&child, OP_BOX, child.bindings[i].reg, 0, 0) != SIZE_MAX;
  uint32_t optional_slot = (uint32_t)child.fn->arity + 1;
  for (const oti_ast* p = optional; ok && p; p = p->next, optional_slot++) {
    const oti_ast* name = p->as.list.first;
    size_t present = emit_bc(&child, OP_JBOUND, optional_slot, 0);
    ok = present != SIZE_MAX && expression(&child, name->next, optional_slot, false) &&
         patch(&child, present) && bind_value(&child, name, optional_slot);
  }
  child.fn->variadic = child.fn->rest || child.fn->optional;
  if (forms && forms->kind == AST_STRING && forms->next) forms = forms->next;
  if (ok) ok = body(&child, forms, next_register(&child), true, true);
  free(child.bindings);
  free(child.upnames);
  free(child.upfacts);
  return ok && emit_bc(c, OP_CLOSURE, dst, id) != SIZE_MAX;
}

static const oti_ast* definition_name(const oti_ast* a) {
  if (!a || a->kind != AST_LIST) return NULL;
  const oti_ast* head = a->as.list.first;
  if (!oti_ast_symbol(head, "def") && !oti_ast_symbol(head, "def-")) return NULL;
  const oti_ast* name = head->next;
  if (name && name->kind == AST_LIST) name = name->as.list.first;
  return name;
}

static bool body(oti_compiler* c, const oti_ast* forms, uint32_t dst, bool tail, bool definitions) {
  if (c->scope && definitions) {
    for (const oti_ast* a = forms; a; a = a->next) {
      const oti_ast* name = definition_name(a);
      if (!name) continue;
      uint32_t reg = next_register(c);
      size_t b = bind(c, name, reg, false);
      if (b == SIZE_MAX || !load_constant(c, V_UNBOUND, reg) ||
          (c->bindings[b].boxed && emit(c, OP_BOX, reg, 0, 0) == SIZE_MAX))
        return false;
    }
  }
  if (!forms) return constant_result(c, OT_NIL, dst, tail);
  for (const oti_ast* a = forms; a; a = a->next) {
    c->definitions = definitions;
    if (!(a->next ? effect(c, a) : expression(c, a, dst, tail))) return false;
  }
  return true;
}

/* A named let in tail position whose name is only called in tail position
 * from its body is a loop in the enclosing frame. Its variables are bound
 * after the initializers are evaluated, as a call would bind them. Facts are
 * created in the same order as for the function it otherwise becomes. */
static bool inline_loop(oti_compiler* c, const oti_ast* name, const oti_ast* bindings, uint32_t dst,
                        uint32_t index, size_t count) {
  oti_scope s = scope_begin(c);
  uint32_t first = c->top;
  for (const oti_ast* b = bindings->as.list.first; b; b = b->next)
    if (!argument(c, b->as.list.first->next)) return false;
  size_t target = c->fn->count;
  uint32_t reg = first;
  for (const oti_ast* b = bindings->as.list.first; b; b = b->next, reg++) {
    const oti_ast* var = b->as.list.first;
    for (const oti_ast* q = bindings->as.list.first; q != b; q = q->next)
      if (same_name(q->as.list.first, var)) return error(c, var, "duplicate parameter");
    if (!bind_value(c, var, reg)) return false;
  }
  if (!oti_grow(c->rt, (void**)&c->bindings, &c->binding_capacity, c->binding_count + 1,
                sizeof(oti_binding)))
    return false;
  c->bindings[c->binding_count++] =
      (oti_binding){name, first, index, BIND_LOOP, false, false, (uint32_t)count, target};
  const oti_ast* forms = bindings->next;
  if (forms && forms->kind == AST_STRING && forms->next) forms = forms->next;
  return body(c, forms, dst, true, true) && scope_end(c, s, false);
}

static bool named_let(oti_compiler* c, const oti_ast* name, const oti_ast* bindings, uint32_t dst,
                      bool tail) {
  uint32_t index = new_fact(c);
  if (index == UINT32_MAX) return false;
  size_t count = 0;
  for (const oti_ast* b = bindings->as.list.first; b; b = b->next) count++;
  if (tail && !fact(c, index)->assigned && !fact(c, index)->escaped && count <= UINT16_MAX)
    return inline_loop(c, name, bindings, dst, index, count);
  if (!fact(c, index)->assigned) {
    /* The loop function refers to itself as register 0. */
    uint32_t saved, base = call_begin(c, dst, tail, &saved);
    for (const oti_ast* b = bindings->as.list.first; b; b = b->next)
      if (!argument(c, b->as.list.first->next)) return false;
    return lambda(c, bindings, bindings->next, true, base, name, index) &&
           call_end(c, base, count, saved, dst, tail);
  }
  /* An assigned loop name needs a real binding, which the loop captures. */
  oti_scope s = scope_begin(c);
  uint32_t first = c->top;
  for (const oti_ast* b = bindings->as.list.first; b; b = b->next)
    if (!argument(c, b->as.list.first->next)) return false;
  uint32_t slot = next_register(c);
  size_t binding = bind_fact(c, name, slot, false, index);
  if (binding == SIZE_MAX || !load_constant(c, V_UNBOUND, slot) ||
      (c->bindings[binding].boxed && emit(c, OP_BOX, slot, 0, 0) == SIZE_MAX))
    return false;
  uint32_t t = next_register(c);
  if (!lambda(c, bindings, bindings->next, true, t, NULL, 0) || !define_local(c, binding, t))
    return false;
  release(c, t);
  uint32_t saved, base = call_begin(c, dst, tail, &saved);
  oti_reference ref = {REF_LOCAL, slot, index, binding, c->bindings[binding].boxed};
  if (!load(c, &ref, base)) return false;
  for (size_t i = 0; i < count; i++)
    if (!move(c, next_register(c), first + (uint32_t)i)) return false;
  if (!call_end(c, base, count, saved, dst, tail)) return false;
  return scope_end(c, s, !tail);
}

static bool let_form(oti_compiler* c, const oti_ast* args, uint32_t dst, bool tail) {
  const oti_ast* name = args && args->kind == AST_SYMBOL ? args : NULL;
  const oti_ast* bindings = name ? args->next : args;
  if (!bindings || bindings->kind != AST_LIST || bindings->as.list.tail)
    return error(c, bindings, "let needs a binding list");
  for (const oti_ast* b = bindings->as.list.first; b; b = b->next) {
    const oti_ast* var = b->kind == AST_LIST ? b->as.list.first : NULL;
    if (!var || var->kind != AST_SYMBOL || !var->next || var->next->next || b->as.list.tail)
      return error(c, b, "let binding needs a name and initializer");
  }
  if (name) return named_let(c, name, bindings, dst, tail);
  oti_scope s = scope_begin(c);
  for (const oti_ast* b = bindings->as.list.first; b; b = b->next) {
    const oti_ast* var = b->as.list.first;
    uint32_t reg = next_register(c);
    if (!expression(c, var->next, reg, false) || !bind_value(c, var, reg)) return false;
  }
  return body(c, bindings->next, dst, tail, true) && scope_end(c, s, !tail);
}

static bool try_form(oti_compiler* c, const oti_ast* forms, uint32_t dst, bool tail) {
  const oti_ast* clause = forms;
  oti_ast* protected = NULL, ** next = &protected;
  while (clause && !(clause->kind == AST_LIST && oti_ast_symbol(clause->as.list.first, "catch"))) {
    oti_ast* copy = oti_arena_alloc(c->expansion_arena, sizeof(*copy));
    if (!copy) return error(c, clause, "compiler syntax memory limit");
    *copy = *clause;
    copy->next = NULL;
    *next = copy;
    next = &copy->next;
    clause = clause->next;
  }
  if (!clause) return body(c, forms, dst, tail, false);
  uint32_t saved = c->top, condition = next_register(c);
  size_t handler = emit_bc(c, OP_TRY, condition, 0);
  if (handler == SIZE_MAX || !body(c, protected, dst, false, false) ||
      emit(c, OP_END_TRY, 0, 0, 0) == SIZE_MAX)
    return false;
  size_t normal = emit_bc(c, OP_JUMP, 0, 0);
  if (!patch(c, handler)) return false;
  oti_jumps done = {0};
  bool ok = true;
  for (; clause && ok; clause = clause->next) {
    const oti_ast* head = clause->kind == AST_LIST ? clause->as.list.first : NULL;
    const oti_ast* guard = head ? head->next : NULL;
    if (!oti_ast_symbol(head, "catch") || !guard || clause->as.list.tail) {
      ok = error(c, clause, "invalid catch clause");
      break;
    }
    const oti_ast *name = guard, *predicate = NULL;
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
    oti_scope s = scope_begin(c);
    size_t skip = SIZE_MAX;
    if (predicate) {
      uint32_t call_saved, base = call_begin(c, UINT32_MAX, false, &call_saved);
      ok = expression(c, predicate, base, false) && move(c, next_register(c), condition) &&
           call_end(c, base, 1, call_saved, base, false);
      if (ok) skip = emit_bc(c, OP_JFALSE, base, 0);
      release(c, call_saved);
    }
    uint32_t slot = next_register(c);
    ok = ok && move(c, slot, condition) && bind_value(c, name, slot) &&
         body(c, guard->next, dst, tail, true);
    ok = scope_end(c, s, ok && !tail) && ok;
    if (ok) ok = jumps_add(c, &done, emit_bc(c, OP_JUMP, 0, 0));
    if (ok && skip != SIZE_MAX) ok = patch(c, skip);
  }
  if (ok)
    ok = emit(c, OP_RAISE, condition, 0, 0) != SIZE_MAX && patch(c, normal) &&
         jumps_patch(c, &done) && emit(c, OP_CLEAR, condition, 1, 0) != SIZE_MAX;
  free(done.items);
  release(c, saved);
  return ok && finish(c, dst, tail);
}

static bool class_key(const oti_ast* a, const char* key) {
  return a && a->kind == AST_KEYWORD && a->as.text.size == strlen(key) &&
         !memcmp(a->as.text.data, key, strlen(key));
}

static ot_value slot_key(oti_compiler* c, const oti_ast* name) {
  size_t n = name->as.text.size;
  char* text = malloc(n + 2);
  if (!text) {
    error(c, name, "out of host memory");
    return 0;
  }
  text[0] = ':';
  memcpy(text + 1, name->as.text.data, n);
  text[n + 1] = 0;
  ot_value key = oti_intern(c->rt, K_KEYWORD, text, n + 1);
  free(text);
  return key;
}

static bool accessor(oti_compiler* c, const oti_ast* class, const oti_ast* slot,
                     const oti_ast* name) {
  oti_ast param = {.kind = AST_SYMBOL, .as.text = {"%instance", 9}}, generic = *name;
  generic.next = &param;
  oti_ast sig = {.kind = AST_LIST, .as.list = {.first = &generic}};
  oti_ast head = {.kind = AST_SYMBOL, .as.text = {"defgeneric", 10}, .next = &sig};
  oti_ast form = {.kind = AST_LIST, .as.list = {.first = &head}};
  c->definitions = true;
  if (!effect(c, &form)) return false;
  oti_ast type = *class;
  type.next = NULL;
  param.next = &type;
  oti_ast specialized = {.kind = AST_LIST, .as.list = {.first = &param}};
  generic.next = &specialized;
  ot_value key = slot_key(c, slot);
  if (!key) return false;
  uint32_t constant_index = oti_constant(c->rt, key);
  if (constant_index == UINT32_MAX) return false;
  oti_ast key_ast = {.kind = AST_VALUE, .as.integer = constant_index};
  oti_ast receiver = param;
  receiver.next = &key_ast;
  oti_ast read = {.kind = AST_LIST, .as.list = {.first = &receiver}};
  sig.next = &read;
  head.as.text.data = "defmethod";
  head.as.text.size = 9;
  c->definitions = true;
  return effect(c, &form);
}

static bool class_declaration(oti_compiler* c, const oti_ast* a, const oti_ast* head,
                              const oti_ast* name, bool definitions, uint32_t dst, bool tail) {
  if (c->scope || !definitions || !name || name->kind != AST_SYMBOL)
    return declaration_error(c, a, "class declaration requires a top-level name");
  if (!oti_classes_init(c->rt)) return false;
  bool abstract = oti_ast_symbol(head, "defabstract"), reset = oti_ast_symbol(head, "reset-class!");
  const oti_ast *fields = name->next, *parent = NULL;
  if (class_key(fields, ":is")) {
    parent = fields->next;
    if (!parent) return error(c, a, "missing class parent");
    fields = parent->next;
  }
  if (abstract && fields) return error(c, a, "abstract classes cannot have slots");
  uint32_t saved = c->top, base = next_register(c);
  if (!load_builtin(c, B_CLASS_DEFINE, base) ||
      !load_constant(c, datum(c, name), next_register(c)) ||
      !(parent ? argument(c, parent)
               : load_constant(c, c->rt->classes[C_OBJECT], next_register(c))) ||
      !load_constant(c, abstract ? OT_TRUE : OT_FALSE, next_register(c)))
    return false;
  uint32_t array = next_register(c);
  if (!load_builtin(c, B_ARRAY, array)) return false;
  size_t count = 0;
  for (const oti_ast* p = fields; p; p = p->next) {
    const oti_ast* slot = p->kind == AST_LIST ? p->as.list.first : p;
    if (!slot || slot->kind != AST_SYMBOL || (p->kind == AST_LIST && p->as.list.tail))
      return error(c, p, "invalid slot declaration");
    const oti_ast *init = NULL, *state = NULL, *getter = NULL;
    for (const oti_ast* option = p->kind == AST_LIST ? slot->next : NULL; option;) {
      const oti_ast* value = option->next;
      if (!value) return error(c, option, "missing slot option value");
      if (class_key(option, ":init") && !init) init = value;
      else if (class_key(option, ":state") && !state) state = value;
      else if (class_key(option, ":accessor") && !getter && value->kind == AST_SYMBOL)
        getter = value;
      else return error(c, option, "invalid or duplicate slot option");
      option = value->next;
    }
    if (!load_constant(c, slot_key(c, slot), next_register(c))) return false;
    if (init) {
      oti_ast params = {.kind = AST_LIST}, value = *init;
      value.next = NULL;
      if (!lambda(c, &params, &value, false, next_register(c), NULL, 0)) return false;
    } else if (!load_constant(c, OT_FALSE, next_register(c))) return false;
    if (!(state ? argument(c, state) : load_constant(c, OT_FALSE, next_register(c)))) return false;
    count += 3;
  }
  if (!call_end(c, array, count, array + 1, array, false) ||
      !load_constant(c, reset ? OT_TRUE : OT_FALSE, next_register(c)) ||
      !call_end(c, base, 5, saved, base, false))
    return false;
  release(c, saved);
  for (const oti_ast* p = fields; p; p = p->next)
    if (p->kind == AST_LIST) {
      const oti_ast* slot = p->as.list.first;
      for (const oti_ast* o = slot->next; o; o = o->next->next)
        if (class_key(o, ":accessor") && !accessor(c, name, slot, o->next)) return false;
    }
  return expression(c, name, dst, tail);
}

static bool generic_form(oti_compiler* c, const oti_ast* a, const oti_ast* head,
                         const oti_ast* args, bool definitions, uint32_t dst, bool tail) {
  bool method = oti_ast_symbol(head, "defmethod");
  if (c->scope || !definitions)
    return declaration_error(c, a, "generic declarations require top level");
  if (!oti_classes_init(c->rt)) return false;
  const oti_ast* signature = args;
  bool around = method && class_key(signature, ":around");
  if (around) signature = signature->next;
  if (!signature || signature->kind != AST_LIST || signature->as.list.tail ||
      !signature->as.list.first)
    return declaration_error(c, a, "expected a generic signature");
  const oti_ast* name = signature->as.list.first;
  if (name->kind != AST_SYMBOL) return declaration_error(c, name, "generic name must be a symbol");
  size_t count = 0;
  oti_ast params = {.kind = AST_LIST}, **next = &params.as.list.first;
  uint32_t saved, base = call_begin(c, dst, tail, &saved), types = 0;
  if (!load_builtin(c, method ? B_GENERIC_METHOD : B_GENERIC_DEFINE, base)) return false;
  if (method) {
    if (!argument(c, name)) return false;
    types = next_register(c);
    if (!load_builtin(c, B_LIST, types)) return false;
  } else if (!load_constant(c, datum(c, name), next_register(c))) return false;
  for (const oti_ast* p = name->next; p; p = p->next) {
    const oti_ast *binding = p, *type = NULL;
    if (method && p->kind == AST_LIST) {
      binding = p->as.list.first;
      type = binding ? binding->next : NULL;
      if (p->as.list.tail || !type || type->next)
        return declaration_error(c, p, "invalid method parameter");
    }
    if (!binding || binding->kind != AST_SYMBOL || oti_ast_symbol(binding, "&") ||
        oti_ast_symbol(binding, "&optional"))
      return declaration_error(c, p, "generics need fixed parameters");
    for (const oti_ast* prior = params.as.list.first; prior; prior = prior->next)
      if (same_name(prior, binding)) return declaration_error(c, p, "duplicate generic parameter");
    oti_ast* copy = oti_arena_alloc(c->expansion_arena, sizeof(*copy));
    if (!copy) return error(c, p, "compiler syntax memory limit");
    *copy = *binding;
    copy->next = NULL;
    *next = copy;
    next = &copy->next;
    if (method &&
        !(type ? argument(c, type) : load_constant(c, c->rt->classes[C_OBJECT], next_register(c))))
      return false;
    count++;
  }
  if (method) {
    const oti_ast* forms = signature->next;
    bool has_next = class_key(forms, ":next");
    if (has_next) {
      const oti_ast* binding = forms->next;
      if (!binding || binding->kind != AST_SYMBOL ||
          memchr(binding->as.text.data, '/', binding->as.text.size) ||
          oti_ast_symbol(binding, "&") || oti_ast_symbol(binding, "&optional"))
        return error(c, forms, "next method needs a local binding name");
      if (!binding->next) return error(c, forms, "next method needs a body");
      for (const oti_ast* p = params.as.list.first; p; p = p->next)
        if (same_name(p, binding))
          return error(c, binding, "next binding conflicts with parameter");
      oti_ast* copy = oti_arena_alloc(c->expansion_arena, sizeof(*copy));
      if (!copy) return error(c, binding, "compiler syntax memory limit");
      *copy = *binding;
      copy->next = params.as.list.first;
      params.as.list.first = copy;
      forms = binding->next;
    }
    return call_end(c, types, count, types + 1, types, false) &&
           lambda(c, &params, forms, false, next_register(c), NULL, 0) &&
           load_constant(c, around ? OT_TRUE : OT_FALSE, next_register(c)) &&
           load_constant(c, has_next ? OT_TRUE : OT_FALSE, next_register(c)) &&
           call_end(c, base, 5, saved, dst, tail);
  }
  if (signature->next && (signature->next->kind != AST_STRING || signature->next->next))
    return error(c, a, "defgeneric accepts only an optional docstring");
  return load_constant(c, ot_int((intptr_t)count), next_register(c)) &&
         load_constant(c, oti_ast_symbol(head, "reset-generic!") ? OT_TRUE : OT_FALSE,
                       next_register(c)) &&
         call_end(c, base, 3, saved, dst, tail);
}

/* Builtins compiled to instructions. Builtins cannot be redefined, so a call
 * is compiled this way when its head names one and no local shadows it. */
typedef struct {
  unsigned builtin;
  oti_opcode op, branch;
  unsigned arity;
} oti_primitive;

static const oti_primitive primitives[] = {
    {B_ADD, OP_ADD, OP_MOVE, 2},       {B_SUB, OP_SUB, OP_MOVE, 2},
    {B_MUL, OP_MUL, OP_MOVE, 2},       {B_LT, OP_LT, OP_BR_LT, 2},
    {B_LE, OP_LE, OP_BR_LE, 2},        {B_GT, OP_GT, OP_BR_GT, 2},
    {B_GE, OP_GE, OP_BR_GE, 2},        {B_NUMEQ, OP_NUMEQ, OP_BR_NUMEQ, 2},
    {B_EQ, OP_EQ, OP_BR_EQ, 2},        {B_CONS, OP_CONS, OP_MOVE, 2},
    {B_CAR, OP_CAR, OP_MOVE, 1},       {B_CDR, OP_CDR, OP_MOVE, 1},
    {B_NOT, OP_NOT, OP_MOVE, 1},       {B_NULL, OP_NULLP, OP_BR_NULL, 1},
    {B_PAIR, OP_PAIRP, OP_BR_PAIR, 1},
};

static const oti_primitive* primitive(oti_compiler* c, const oti_ast* a) {
  if (a->kind != AST_LIST || a->as.list.tail) return NULL;
  const oti_ast* head = a->as.list.first;
  if (!head || head->kind != AST_SYMBOL || shadowed(c, head)) return NULL;
  uint32_t global = oti_lookup_global(c->rt, head->as.text.data, head->as.text.size);
  if (global == UINT32_MAX) return NULL;
  ot_value value = c->rt->globals[global];
  /* Only the builtin's own cell is immutable; other globals may hold it. */
  if ((value & 255) != TAG_BUILTIN || c->rt->builtin_cells[value >> 8] != global) return NULL;
  unsigned count = 0;
  for (const oti_ast* p = head->next; p; p = p->next) count++;
  for (size_t i = 0; i < sizeof(primitives) / sizeof(primitives[0]); i++)
    if (primitives[i].builtin == (unsigned)(value >> 8))
      return primitives[i].arity == count ? &primitives[i] : NULL;
  return NULL;
}

static bool leaf(const oti_ast* a) {
  return a->kind != AST_LIST || (a->as.list.first && oti_ast_symbol(a->as.list.first, "quote"));
}

static bool small_integer(const oti_ast* a, int32_t* value) {
  if (a->kind != AST_INT || a->as.integer < INT16_MIN || a->as.integer > INT16_MAX) return false;
  *value = (int32_t)a->as.integer;
  return true;
}

/* An operand's register. A bound local is used in place when the operands
 * after it cannot assign it. */
static bool operand(oti_compiler* c, const oti_ast* a, bool later_leaves, uint32_t* reg) {
  if (later_leaves && a->kind == AST_SYMBOL) {
    size_t b = find_binding(c, a);
    if (b != SIZE_MAX) {
      oti_binding* binding = &c->bindings[b];
      if (binding->kind == BIND_SELF) {
        *reg = 0;
        return true;
      }
      if (!binding->boxed && binding->initialized) {
        *reg = binding->reg;
        return true;
      }
    }
  }
  *reg = next_register(c);
  return expression(c, a, *reg, false);
}

static bool operands(oti_compiler* c, const oti_ast* args, unsigned count, uint32_t* regs) {
  for (unsigned i = 0; i < count; i++, args = args->next) {
    bool later = true;
    for (const oti_ast* p = args->next; p; p = p->next) later &= leaf(p);
    if (!operand(c, args, later, &regs[i])) return false;
  }
  return true;
}

static bool primitive_call(oti_compiler* c, const oti_primitive* p, const oti_ast* args,
                           uint32_t dst, bool tail) {
  uint32_t saved = c->top, regs[2];
  int32_t k;
  bool ok;
  if (p->op == OP_ADD && small_integer(args, &k)) {
    ok =
        operands(c, args->next, 1, regs) && emit(c, OP_ADDI, dst, regs[0], (uint16_t)k) != SIZE_MAX;
  } else if ((p->op == OP_ADD || p->op == OP_SUB) && small_integer(args->next, &k) &&
             (p->op == OP_ADD || (k != 0 && k != INT16_MIN))) {
    /* x - k is x + -k; zero is excluded because builtin addition turns -0.0
     * into 0.0. */
    if (p->op == OP_SUB) k = -k;
    ok = operands(c, args, 1, regs) && emit(c, OP_ADDI, dst, regs[0], (uint16_t)k) != SIZE_MAX;
  } else {
    ok = operands(c, args, p->arity, regs) &&
         emit(c, p->op, dst, regs[0], p->arity == 2 ? regs[1] : 0) != SIZE_MAX;
  }
  release(c, saved);
  return ok && finish(c, dst, tail);
}

static bool branch(oti_compiler* c, const oti_ast* a, bool when, oti_jumps* out);

/* Branch to a target patched later when the test's truth equals when. */
static bool branch_word(oti_compiler* c, oti_opcode op, bool when, uint32_t a, uint32_t b,
                        uint32_t k, oti_jumps* out) {
  return emit_x(c, op, when, a, b, k) != SIZE_MAX && jumps_add(c, out, emit_bc(c, OP_JUMP, 0, 0));
}

/* The syntax of a literal or quoted datum, or NULL. */
static const oti_ast* constant_syntax(const oti_ast* a) {
  if (a->kind != AST_LIST) return a->kind == AST_SYMBOL ? NULL : a;
  const oti_ast* head = a->as.list.first;
  return !a->as.list.tail && oti_ast_symbol(head, "quote") && head->next && !head->next->next
             ? head->next
             : NULL;
}

static bool branch_primitive(oti_compiler* c, const oti_primitive* p, const oti_ast* args,
                             bool when, oti_jumps* out) {
  uint32_t saved = c->top, regs[2];
  int32_t k;
  bool ok;
  oti_opcode op = p->branch;
  const oti_ast *constant = NULL, *other = NULL;
  if (op == OP_BR_EQ) {
    if ((constant = constant_syntax(args->next))) other = args;
    else if ((constant = constant_syntax(args))) other = args->next;
  }
  if (constant) {
    /* The constant has no effects, so the other operand is evaluated alone. */
    ot_value value = datum(c, constant);
    uint32_t index = value ? oti_constant(c->rt, value) : UINT32_MAX;
    ok = index != UINT32_MAX && operands(c, other, 1, regs) &&
         branch_word(c, OP_BR_EQK, when, regs[0], index & 0xffff, index >> 16, out);
  } else if (op <= OP_BR_NUMEQ && small_integer(args->next, &k)) {
    ok = operands(c, args, 1, regs) && branch_word(c, (oti_opcode)(op + OP_BR_LTI - OP_BR_LT), when,
                                                   regs[0], 0, (uint16_t)k, out);
  } else if (op <= OP_BR_NUMEQ && small_integer(args, &k)) {
    oti_opcode swapped = op == OP_BR_LT   ? OP_BR_GT
                         : op == OP_BR_LE ? OP_BR_GE
                         : op == OP_BR_GT ? OP_BR_LT
                         : op == OP_BR_GE ? OP_BR_LE
                                          : op;
    ok = operands(c, args->next, 1, regs) &&
         branch_word(c, (oti_opcode)(swapped + OP_BR_LTI - OP_BR_LT), when, regs[0], 0, (uint16_t)k,
                     out);
  } else {
    ok = operands(c, args, p->arity, regs) &&
         branch_word(c, op, when, regs[0], p->arity == 2 ? regs[1] : 0, 0, out);
  }
  release(c, saved);
  return ok;
}

static bool branch_form(oti_compiler* c, const oti_ast* a, bool when, oti_jumps* out);

static bool branch(oti_compiler* c, const oti_ast* a, bool when, oti_jumps* out) {
  size_t outer = c->offset;
  c->offset = a->offset;
  bool ok = branch_form(c, a, when, out);
  c->offset = outer;
  return ok;
}

static bool branch_form(oti_compiler* c, const oti_ast* a, bool when, oti_jumps* out) {
  if (oti_failed(c->rt)) return false;
  c->definitions = false;
  a = expand_head(c, a);
  if (!a) return false;
  if (a->kind == AST_LIST && !a->as.list.tail && a->as.list.first) {
    const oti_ast *head = a->as.list.first, *args = head->next;
    bool conjunction = oti_ast_symbol(head, "and");
    if (conjunction || oti_ast_symbol(head, "or")) {
      if (!args) {
        /* (and) is true and (or) is false. */
        return conjunction != when || jumps_add(c, out, emit_bc(c, OP_JUMP, 0, 0));
      }
      /* (and ...) jumps when false as soon as one test is false; it jumps
       * when true only if the last test is reached and true. */
      bool early = !conjunction;
      if (when == early) {
        for (const oti_ast* p = args; p; p = p->next)
          if (!branch(c, p, when, out)) return false;
        return true;
      }
      oti_jumps skip = {0};
      for (const oti_ast* p = args; p->next; p = p->next)
        if (!branch(c, p, early, &skip)) {
          free(skip.items);
          return false;
        }
      const oti_ast* last = args;
      while (last->next) last = last->next;
      bool ok = branch(c, last, when, out);
      return jumps_patch(c, &skip) && ok;
    }
    const oti_primitive* p = primitive(c, a);
    if (p && p->op == OP_NOT) return branch(c, args, !when, out);
    if (p && p->branch != OP_MOVE) return branch_primitive(c, p, args, when, out);
  }
  uint32_t saved = c->top, reg;
  bool ok = operand(c, a, true, &reg) &&
            jumps_add(c, out, emit_bc(c, when ? OP_JTRUE : OP_JFALSE, reg, 0));
  release(c, saved);
  return ok;
}

/* A tail call to the enclosing function with its exact arity, or to a loop,
 * becomes moves into the parameters and a jump to the start. Parameters start
 * at *first and the start is *target. */
static bool self_tail_call(oti_compiler* c, const oti_ast* head, size_t count, uint32_t* first,
                           size_t* target) {
  size_t b = find_binding(c, head);
  if (b == SIZE_MAX) return false;
  oti_binding* binding = &c->bindings[b];
  if (binding->kind == BIND_LOOP && count == binding->count) {
    *first = binding->reg;
    *target = binding->target;
    return true;
  }
  if (binding->kind != BIND_SELF || count != c->fn->arity || c->fn->optional || c->fn->rest)
    return false;
  *first = 1;
  *target = 0;
  return true;
}

/* True when argument a of a self call is the parameter in register reg itself. */
static bool same_parameter(oti_compiler* c, const oti_ast* a, uint32_t reg) {
  if (a->kind != AST_SYMBOL) return false;
  size_t b = find_binding(c, a);
  return b != SIZE_MAX && c->bindings[b].kind == BIND_LOCAL && c->bindings[b].reg == reg &&
         !c->bindings[b].boxed;
}

/* Arguments go to temporaries and are then moved into the parameters. One that
 * passes its parameter through needs neither. The last can be computed in place
 * when it reads its operands before writing, as a primitive or a leaf does. */
static bool self_jump(oti_compiler* c, const oti_ast* args, uint32_t params, size_t target) {
  uint32_t saved = c->top, first = c->top, i = 0;
  const oti_ast* last = args;
  while (last && last->next) last = last->next;
  bool in_place = last && (leaf(last) || primitive(c, last));
  const oti_ast* p;
  for (p = args; p; p = p->next, i++) {
    if (same_parameter(c, p, params + i)) continue;
    if (p == last && in_place) {
      if (!expression(c, p, params + i, false)) return false;
    } else if (!argument(c, p)) return false;
  }
  uint32_t temp = first;
  for (p = args, i = 0; p; p = p->next, i++) {
    if (same_parameter(c, p, params + i) || (p == last && in_place)) continue;
    if (!move(c, params + i, temp++)) return false;
  }
  release(c, saved);
  return emit_bc(c, OP_JUMP, 0, (uint32_t)target) != SIZE_MAX;
}

/* An expression that runs no user code and leaves globals alone: a bound
 * local, a constant, or an inline primitive of those. */
static bool simple(oti_compiler* c, const oti_ast* a) {
  if (a->kind == AST_SYMBOL) {
    size_t b = find_binding(c, a);
    return b != SIZE_MAX && (c->bindings[b].kind == BIND_SELF ||
                             (c->bindings[b].kind == BIND_LOCAL && c->bindings[b].initialized));
  }
  if (constant_syntax(a)) return true;
  if (!primitive(c, a)) return false;
  for (const oti_ast* q = a->as.list.first->next; q; q = q->next)
    if (!simple(c, q)) return false;
  return true;
}

static bool expression_form(oti_compiler* c, const oti_ast* a, uint32_t dst, bool tail);

/* Code compiled for a form records the form's source offset. */
static bool expression(oti_compiler* c, const oti_ast* a, uint32_t dst, bool tail) {
  size_t outer = c->offset;
  c->offset = a->offset;
  bool ok = expression_form(c, a, dst, tail);
  c->offset = outer;
  return ok;
}

static bool expression_form(oti_compiler* c, const oti_ast* a, uint32_t dst, bool tail) {
  if (oti_failed(c->rt)) return false;
  bool definitions = c->definitions;
  c->definitions = false;
  if (a->kind == AST_SYMBOL) {
    oti_reference ref;
    if (!resolve(c, a, &ref)) return false;
    /* A bound local in tail position is returned from its own register. */
    if (tail && ref.kind == REF_LOCAL && !ref.boxed && c->bindings[ref.binding].initialized)
      return finish(c, ref.index, tail);
    return load(c, &ref, dst) && finish(c, dst, tail);
  }
  if (a->kind != AST_LIST) return constant_result(c, datum(c, a), dst, tail);
  if (a->as.list.tail) return error(c, a, "cannot call an improper list");
  const oti_ast* head = a->as.list.first;
  if (!head) return constant_result(c, OT_EMPTY, dst, tail);
  const oti_ast* args = head->next;
  const oti_ast* expanded = expand_head(c, a);
  if (!expanded) return false;
  if (expanded != a) {
    c->definitions = definitions;
    return expression(c, expanded, dst, tail);
  }
  if (oti_ast_symbol(head, "defclass") || oti_ast_symbol(head, "defabstract") ||
      oti_ast_symbol(head, "reset-class!"))
    return class_declaration(c, a, head, args, definitions, dst, tail);
  if (oti_ast_symbol(head, "defgeneric") || oti_ast_symbol(head, "reset-generic!") ||
      oti_ast_symbol(head, "defmethod"))
    return generic_form(c, a, head, args, definitions, dst, tail);
  if (oti_ast_symbol(head, "defparam")) {
    if (c->scope || !definitions) return error(c, a, "defparam requires top level");
    if (!args || args->kind != AST_SYMBOL || !args->next)
      return error(c, a, "defparam needs a name and default");
    const oti_ast* value = args->next;
    if (value->kind == AST_STRING && value->next) value = value->next;
    if (value->next) return error(c, a, "defparam needs one default");
    uint32_t global = oti_global(c->rt, args->as.text.data, args->as.text.size);
    uint32_t saved = c->top, name = next_register(c), initial = next_register(c);
    bool ok = global != UINT32_MAX && load_constant(c, datum(c, args), name) &&
              expression(c, value, initial, false) &&
              emit(c, OP_MAKE_PARAM, name, name, initial) != SIZE_MAX &&
              emit_bc(c, OP_DEFINE_GLOBAL, name, global) != SIZE_MAX;
    release(c, saved);
    return ok && constant_result(c, OT_NIL, dst, tail);
  }
  if (oti_ast_symbol(head, "unwind-protect") || oti_ast_symbol(head, "defer")) {
    if (!args) return error(c, a, "unwind-protect needs an expression");
    oti_ast params = {.kind = AST_LIST};
    uint32_t saved = c->top, cleanup = next_register(c);
    bool ok = lambda(c, &params, args->next, false, cleanup, NULL, 0) &&
              emit(c, OP_CLEANUP, cleanup, 0, 0) != SIZE_MAX;
    release(c, saved);
    return ok && expression(c, args, dst, false) && emit(c, OP_RUN_CLEANUP, 0, 0, 0) != SIZE_MAX &&
           finish(c, dst, tail);
  }
  if (oti_ast_symbol(head, "%call-with-cleanup")) {
    if (!args || !args->next || args->next->next) return error(c, a, "cleanup needs two callables");
    uint32_t saved, base = call_begin(c, dst, false, &saved);
    uint32_t cleanup = base + 1;
    if (!expression(c, args, base, false) || next_register(c) != cleanup ||
        !expression(c, args->next, cleanup, false) ||
        emit_x(c, OP_CLEANUP, 1, cleanup, 0, 0) == SIZE_MAX)
      return false;
    release(c, cleanup);
    return call_end(c, base, 0, saved, dst, false) &&
           emit(c, OP_RUN_CLEANUP, 0, 0, 0) != SIZE_MAX && finish(c, dst, tail);
  }
  if (oti_ast_symbol(head, "restart-case")) {
    if (!args) return error(c, a, "restart-case needs an expression");
    uint32_t saved = c->top, first = c->top;
    size_t count = 0;
    for (const oti_ast* clause = args->next; clause; clause = clause->next) {
      const oti_ast* name = clause->kind == AST_LIST ? clause->as.list.first : NULL;
      if (!name || name->kind != AST_SYMBOL || clause->as.list.tail || !name->next)
        return error(c, clause, "invalid restart clause");
      const oti_ast *params = name->next, *description = NULL;
      if (params->kind == AST_STRING && params->next) {
        description = params;
        params = params->next;
      }
      if (!load_constant(c, datum(c, name), next_register(c)) ||
          !load_constant(c, description ? datum(c, description) : OT_NIL, next_register(c)) ||
          !lambda(c, params, params->next, false, next_register(c), NULL, 0))
        return false;
      if (++count > UINT16_MAX) return error(c, clause, "too many restarts");
    }
    if (emit(c, OP_RESTARTS, first, (uint32_t)count, 0) == SIZE_MAX) return false;
    size_t target = emit_bc(c, OP_JUMP, 0, 0);
    release(c, saved);
    if (target == SIZE_MAX || !expression(c, args, dst, false)) return false;
    size_t done = emit_bc(c, OP_JUMP, 0, 0);
    return patch(c, target) && emit(c, OP_RESULT, dst, 0, 0) != SIZE_MAX && patch(c, done) &&
           emit(c, OP_END_RESTARTS, 0, 0, 0) != SIZE_MAX && finish(c, dst, tail);
  }
  if (oti_ast_symbol(head, "handler-bind")) {
    if (!args || args->kind != AST_LIST || args->as.list.tail)
      return error(c, a, "handler-bind needs a binding list");
    uint32_t saved = c->top, first = c->top;
    size_t count = 0;
    for (const oti_ast* binding = args->as.list.first; binding; binding = binding->next) {
      const oti_ast* predicate = binding->kind == AST_LIST ? binding->as.list.first : NULL;
      if (!predicate || binding->as.list.tail || !predicate->next || predicate->next->next)
        return error(c, binding, "handler binding needs a predicate and handler");
      if (!argument(c, predicate) || !argument(c, predicate->next)) return false;
      if (++count > UINT16_MAX) return error(c, binding, "too many handler bindings");
    }
    if (emit(c, OP_BIND_HANDLERS, first, (uint32_t)count, 0) == SIZE_MAX) return false;
    release(c, saved);
    return body(c, args->next, dst, false, false) &&
           emit(c, OP_UNBIND_HANDLERS, 0, (uint32_t)count, 0) != SIZE_MAX && finish(c, dst, tail);
  }
  if (oti_ast_symbol(head, "with-params")) {
    if (!args || args->kind != AST_LIST || args->as.list.tail)
      return error(c, a, "with-params needs a binding list");
    size_t count = 0;
    for (const oti_ast* binding = args->as.list.first; binding; binding = binding->next) {
      const oti_ast* param = binding->kind == AST_LIST ? binding->as.list.first : NULL;
      if (!param || binding->as.list.tail || !param->next || param->next->next)
        return error(c, binding, "parameter binding needs a param and value");
      uint32_t saved = c->top, p = next_register(c), value = next_register(c);
      if (!expression(c, param, p, false) || !expression(c, param->next, value, false) ||
          emit(c, OP_BIND_PARAM, p, value, 0) == SIZE_MAX)
        return false;
      release(c, saved);
      if (++count > UINT16_MAX) return error(c, binding, "too many parameter bindings");
    }
    return body(c, args->next, dst, false, false) &&
           emit(c, OP_UNBIND_PARAMS, 0, (uint32_t)count, 0) != SIZE_MAX && finish(c, dst, tail);
  }
  if (oti_ast_symbol(head, "try")) return try_form(c, args, dst, tail);
  if (oti_ast_symbol(head, "defonce")) return error(c, a, "defonce requires top-level context");
  if (oti_ast_symbol(head, "quote")) {
    if (!args || args->next) return error(c, a, "quote needs one datum");
    return constant_result(c, datum(c, args), dst, tail);
  }
  if (oti_ast_symbol(head, "quasiquote")) {
    if (!args || args->next) return error(c, a, "quasiquote needs one datum");
    return quasiquote(c, args, dst, tail);
  }
  if (oti_ast_symbol(head, "unquote") || oti_ast_symbol(head, "unquote-splicing"))
    return error(c, a, "unquote outside quasiquote");
  if (oti_ast_symbol(head, "if")) {
    if (!args || !args->next || (args->next->next && args->next->next->next))
      return error(c, a, "if needs two or three operands");
    oti_jumps otherwise = {0};
    if (!branch(c, args, false, &otherwise) || !expression(c, args->next, dst, tail)) {
      free(otherwise.items);
      return false;
    }
    size_t done = tail ? SIZE_MAX : emit_bc(c, OP_JUMP, 0, 0);
    if (!jumps_patch(c, &otherwise)) return false;
    if (!(args->next->next ? expression(c, args->next->next, dst, tail)
                           : constant_result(c, OT_NIL, dst, tail)))
      return false;
    return tail || patch(c, done);
  }
  if (oti_ast_symbol(head, "cond")) {
    oti_jumps done = {0};
    bool ok = true, otherwise = false;
    for (const oti_ast* clause = args; clause && ok; clause = clause->next) {
      const oti_ast* test = clause->kind == AST_LIST ? clause->as.list.first : NULL;
      if (!test || clause->as.list.tail) {
        ok = error(c, clause, "cond needs proper nonempty clauses");
        break;
      }
      if (oti_ast_symbol(test, "else")) {
        if (clause->next) {
          ok = error(c, clause, "cond else must be last");
          break;
        }
        otherwise = true;
        ok = body(c, test->next, dst, tail, false);
        break;
      }
      if (!test->next) {
        ok = expression(c, test, dst, false) && jumps_add(c, &done, emit_bc(c, OP_JTRUE, dst, 0));
        continue;
      }
      oti_jumps skip = {0};
      ok = branch(c, test, false, &skip) && body(c, test->next, dst, tail, false) &&
           (tail || jumps_add(c, &done, emit_bc(c, OP_JUMP, 0, 0)));
      ok = jumps_patch(c, &skip) && ok;
    }
    if (ok && !otherwise) ok = load_constant(c, OT_NIL, dst);
    ok = jumps_patch(c, &done) && ok;
    return ok && finish(c, dst, tail);
  }
  if (oti_ast_symbol(head, "when") || oti_ast_symbol(head, "unless")) {
    if (!args) return error(c, a, "conditional body needs a condition");
    oti_jumps skip = {0};
    if (!branch(c, args, oti_ast_symbol(head, "unless"), &skip) ||
        !body(c, args->next, dst, tail, false)) {
      free(skip.items);
      return false;
    }
    size_t done = tail ? SIZE_MAX : emit_bc(c, OP_JUMP, 0, 0);
    return jumps_patch(c, &skip) && constant_result(c, OT_NIL, dst, tail) &&
           (tail || patch(c, done));
  }
  if (oti_ast_symbol(head, "begin")) return body(c, args, dst, tail, definitions);
  if (oti_ast_symbol(head, "fn"))
    return lambda(c, args, args ? args->next : NULL, false, dst, NULL, 0) && finish(c, dst, tail);
  if (oti_ast_symbol(head, "let")) return let_form(c, args, dst, tail);
  if (oti_ast_symbol(head, "set!")) {
    if (!args || !args->next || args->next->next) return error(c, a, "set! needs a name and value");
    oti_reference ref;
    if (!resolve(c, args, &ref)) return false;
    uint32_t saved = c->top;
    /* A primitive writes its destination only after reading its operands. */
    uint32_t value = ref.kind == REF_LOCAL && !ref.boxed && primitive(c, args->next)
                         ? ref.index
                         : next_register(c);
    bool ok = expression(c, args->next, value, false) && assign(c, &ref, value);
    release(c, saved);
    return ok && constant_result(c, OT_NIL, dst, tail);
  }
  if (oti_ast_symbol(head, "def") || oti_ast_symbol(head, "def-")) {
    if (!definitions) return error(c, a, "definition is only allowed at top level or in a body");
    const oti_ast* name = definition_name(a);
    if (!name || name->kind != AST_SYMBOL || !args->next) return error(c, a, "invalid definition");
    const oti_ast* value = NULL;
    if (args->kind != AST_LIST) {
      value = args->next;
      if (value->kind == AST_STRING && value->next) value = value->next;
      if (value->next) return error(c, a, "definition needs one value");
    }
    if (!c->scope) {
      uint32_t global = oti_global(c->rt, name->as.text.data, name->as.text.size);
      if (global == UINT32_MAX) return false;
      bool ok;
      if (value) {
        if (value->kind == AST_LIST && oti_ast_symbol(value->as.list.first, "fn")) c->naming = name;
        ok = expression(c, value, dst, false);
        c->naming = NULL;
      } else {
        oti_ast params = *args;
        params.as.list.first = name->next;
        c->naming = name;
        ok = lambda(c, &params, args->next, false, dst, NULL, 0);
      }
      return ok && emit_bc(c, OP_DEFINE_GLOBAL, dst, global) != SIZE_MAX && finish(c, dst, tail);
    }
    oti_reference ref;
    if (!resolve(c, name, &ref)) return false;
    uint32_t saved = c->top;
    bool ok;
    if (ref.kind != REF_LOCAL) {
      /* A definition that is not in a body's binding list assigns the binding
       * it resolves to, as before. */
      uint32_t t = next_register(c);
      if (value) ok = expression(c, value, t, false);
      else {
        oti_ast params = *args;
        params.as.list.first = name->next;
        ok = lambda(c, &params, args->next, false, t, NULL, 0);
      }
      ok = ok && assign(c, &ref, t) && move(c, dst, t);
    } else if (value) {
      uint32_t t = next_register(c);
      ok = expression(c, value, t, false) && define_local(c, ref.binding, t) && move(c, dst, t);
    } else {
      oti_ast params = *args;
      params.as.list.first = name->next;
      oti_binding* binding = &c->bindings[ref.binding];
      uint32_t target = binding->boxed || binding->initialized ? next_register(c) : binding->reg;
      ok = lambda(c, &params, args->next, false, target, name, binding->fact) &&
           define_local(c, ref.binding, target) && move(c, dst, target);
    }
    release(c, saved);
    return ok && finish(c, dst, tail);
  }
  if (oti_ast_symbol(head, "while")) {
    if (!args) return error(c, a, "while needs a condition");
    /* The test follows the body, so an iteration takes one branch. */
    uint32_t saved = c->top, scratch = next_register(c);
    size_t entry = emit_bc(c, OP_JUMP, 0, 0), start = c->fn->count;
    oti_scope s = scope_begin(c);
    bool ok = entry != SIZE_MAX && body(c, args->next, scratch, false, true);
    if (ok) drop_unused(c, start, scratch);
    ok = scope_end(c, s, true) && ok;
    oti_jumps again = {0};
    ok = ok && patch(c, entry) && branch(c, args, true, &again);
    ok = jumps_patch_to(c, &again, start) && ok;
    release(c, saved);
    return ok && constant_result(c, OT_NIL, dst, tail);
  }
  if (oti_ast_symbol(head, "and") || oti_ast_symbol(head, "or")) {
    bool conjunction = oti_ast_symbol(head, "and");
    if (!args) return constant_result(c, conjunction ? OT_TRUE : OT_FALSE, dst, tail);
    /* A test that yields #t or #f branches to the result it decides, so it is
     * not stored and tested again. Other operands leave their own value. */
    oti_jumps done = {0}, decided = {0};
    bool ok = true;
    for (const oti_ast* p = args; ok && p; p = p->next) {
      const oti_primitive* test = p->next ? primitive(c, p) : NULL;
      if (test && (test->branch != OP_MOVE || test->op == OP_NOT)) {
        ok = branch(c, p, !conjunction, &decided);
        continue;
      }
      ok = expression(c, p, dst, tail && !p->next);
      if (ok && p->next)
        ok = jumps_add(c, &done, emit_bc(c, conjunction ? OP_JFALSE : OP_JTRUE, dst, 0));
    }
    if (ok && decided.count) {
      size_t over = tail ? SIZE_MAX : emit_bc(c, OP_JUMP, 0, 0);
      ok = jumps_patch(c, &decided) &&
           constant_result(c, conjunction ? OT_FALSE : OT_TRUE, dst, tail) &&
           (tail || patch(c, over));
    }
    free(decided.items);
    /* In tail position every path has returned unless an operand jumps here. */
    if (tail && !done.count) return ok;
    ok = jumps_patch(c, &done) && ok;
    return ok && finish(c, dst, tail);
  }
  if (oti_ast_symbol(head, "syntax-quote") || oti_ast_symbol(head, "defmacro") ||
      oti_ast_symbol(head, "ns") || oti_ast_symbol(head, "require"))
    return error(c, a, "form not implemented by the compiler");
  const oti_primitive* p = primitive(c, a);
  if (p) return primitive_call(c, p, args, dst, tail);
  size_t count = 0;
  for (const oti_ast* q = args; q; q = q->next) count++;
  uint32_t params;
  size_t target;
  if (tail && head->kind == AST_SYMBOL && self_tail_call(c, head, count, &params, &target))
    return self_jump(c, args, params, target);
  /* A global callee is read by the call instruction itself when reading it
   * after the arguments cannot be told apart from reading it before. */
  oti_reference ref = {REF_LOCAL, 0, 0, 0, false};
  if (head->kind == AST_SYMBOL && !resolve(c, head, &ref)) return false;
  bool global = head->kind == AST_SYMBOL && ref.kind == REF_GLOBAL && count <= UINT8_MAX;
  for (const oti_ast* q = args; global && q; q = q->next) global = simple(c, q);
  uint32_t saved, base = call_begin(c, dst, tail, &saved);
  if (!global &&
      !(head->kind == AST_SYMBOL ? load(c, &ref, base) : expression(c, head, base, false)))
    return false;
  for (const oti_ast* q = args; q; q = q->next)
    if (!argument(c, q)) return false;
  if (!global) return call_end(c, base, count, saved, dst, tail);
  if (emit_x(c, tail ? OP_TAIL_CALL_GLOBAL : OP_CALL_GLOBAL, (unsigned)count, base,
             ref.index & 0xffff, ref.index >> 16) == SIZE_MAX)
    return false;
  release(c, saved);
  return tail || move(c, dst, base);
}

static void discard_functions(ot_state* rt, oti_unit* unit) {
  for (size_t i = 0; i < unit->function_count; i++) {
    uint32_t id = unit->functions[i];
    oti_free_function(rt->functions[id]);
    rt->functions[id] = NULL;
    if (id < rt->function_free) rt->function_free = id;
  }
  unit->function_count = 0;
}

bool oti_compile(ot_state* rt, const oti_ast* form, uint32_t* function) {
  oti_arena arena;
  oti_arena_init(&arena, OTI_SOURCE_LIMIT);
  size_t expansions = 0;
  oti_unit unit = {0};
  bool ok = false;
  for (unsigned attempt = 0;; attempt++) {
    unit.next_fact = 0;
    unit.retry = false;
    oti_compiler c = {.rt = rt,
                      .unit = &unit,
                      .top = 1,
                      .definitions = true,
                      .expansion_arena = &arena,
                      .expansions = &expansions};
    uint32_t id = unit_function(&c);
    if (id == UINT32_MAX) break;
    c.fn = rt->functions[id];
    ok = expression(&c, form, next_register(&c), true);
    free(c.bindings);
    free(c.upnames);
    free(c.upfacts);
    *function = id;
    if (!ok || !unit.retry) break;
    if (attempt == 8) {
      ok = oti_error(rt, "compiler binding analysis did not settle");
      break;
    }
    discard_functions(rt, &unit);
  }
  free(unit.facts);
  free(unit.expansions);
  free(unit.functions);
  oti_arena_destroy(&arena);
  return ok;
}

oti_ast* oti_expand_form(ot_state* rt, const oti_ast* form, oti_arena* arena) {
  oti_compiler c = {.rt = rt, .expansion_arena = arena, .locate = true};
  ot_value values[2] = {OT_NIL, OT_EMPTY};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, values, 2);
  values[0] = datum(&c, form);
  if (values[0]) {
    ot_value pair = oti_alloc(rt, K_PAIR, 2, 0);
    if (pair) {
      object_fields(pair)[0] = values[0];
      object_fields(pair)[1] = OT_EMPTY;
      values[1] = pair;
    } else values[0] = 0;
  }
  oti_ast* result = NULL;
  uint32_t global = oti_binding_global(rt, 0, "*expander*", 10, false);
  if (values[0] && global != UINT32_MAX) {
    bool expanding = rt->expanding;
    rt->expanding = true;
    if (!expanding) rt->expansion_calls = 0;
    ot_value expanded = oti_call_list(rt, rt->globals[global], values[1]);
    rt->expanding = expanding;
    if (expanded) result = syntax_value(&c, expanded, form->offset, 0);
  }
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

ot_value oti_read_string(ot_state* rt, ot_value string) {
  if (!oti_kind(string, K_STRING)) {
    oti_error(rt, "read-string expects a string");
    return 0;
  }
  size_t n;
  memcpy(&n, object_bytes(string), sizeof(n));
  char* copy = malloc(n + 1);
  if (!copy) {
    oti_error(rt, "out of host memory");
    return 0;
  }
  memcpy(copy, object_bytes(string) + sizeof(n), n);
  copy[n] = 0;
  ot_value result = oti_read_source(rt, copy, n);
  free(copy);
  return result;
}

ot_value oti_read_source(ot_state* rt, const char* source, size_t length) {
  oti_reader reader;
  oti_reader_init(&reader, source, length, OTI_SOURCE_LIMIT);
  oti_ast* form = oti_reader_one(&reader);
  oti_compiler c = {.rt = rt};
  ot_value result = form ? datum(&c, form) : 0;
  if (reader.error) {
    oti_reader_error(rt, &reader);
    result = 0;
  }
  oti_reader_destroy(&reader);
  return result;
}

/* Eval values retain the namespace where they were obtained. */
ot_value oti_evaluator(ot_state* rt, uint32_t space) {
  for (size_t i = 0; i < rt->constant_count; i++) {
    ot_value v = rt->constants[i];
    if (!oti_kind(v, K_CLOSURE)) continue;
    uint32_t id;
    memcpy(&id, object_bytes(v), sizeof(id));
    oti_function* fn = rt->functions[id];
    if (fn->evaluator && fn->namespace == space) return v;
  }
  uint32_t id = new_function(rt);
  if (id == UINT32_MAX) return 0;
  oti_function* fn = rt->functions[id];
  fn->namespace = space;
  fn->evaluator = true;
  fn->arity = 1;
  fn->locals = 4;
  oti_compiler c = {.rt = rt, .fn = fn};
  if (!load_builtin(&c, B_EVAL_IN_NAMESPACE, 2) || !load_constant(&c, ot_int(space), 3) ||
      !move(&c, 4, 1) || emit(&c, OP_TAIL_CALL, 2, 2, 0) == SIZE_MAX)
    return 0;
  ot_value value = oti_alloc(rt, K_CLOSURE, 0, sizeof(id));
  if (!value) return 0;
  memcpy(object_bytes(value), &id, sizeof(id));
  return oti_constant(rt, value) == UINT32_MAX ? 0 : value;
}
