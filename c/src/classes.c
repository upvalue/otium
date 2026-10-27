#include "internal.h"
#include <stdlib.h>
#include <string.h>

static ot_value cons(ot_state* rt, ot_value a, ot_value b);

/* A K_NEXT carries the argument count of the call that made it. */
static size_t arity(ot_value generic) {
  return oti_kind(generic, K_GENERIC) ? oti_as_generic(generic)->arity
                                      : oti_as_next(generic)->count;
}

bool oti_subtype(ot_value child, ot_value parent) {
  while (oti_kind(child, K_CLASS)) {
    if (child == parent) return true;
    child = oti_as_class(child)->parent;
  }
  return false;
}

bool oti_classes_init(ot_state* rt) {
  if (rt->classes_ready) return true;
  static const char* names[C_COUNT] = {
      "Object",  "Number",  "Boolean",    "Character", "String",    "Symbol",         "Null",
      "Pair",    "Vector",  "Bytevector", "Hashtable", "Procedure", "TypeDescriptor", "Nil",
      "Keyword", "Pointer", "Port",       "CPointer",  "Other"};
  uint32_t saved = rt->current_namespace;
  rt->current_namespace = 0;
  bool ok = true;
  for (size_t i = 0; i < C_COUNT; i++) {
    if (rt->classes[i] == OT_NIL) {
      rt->classes[i] = oti_alloc(rt, K_CLASS, 4, OTI_BYTES(oti_class_obj, 4));
      if (!rt->classes[i]) {
        rt->classes[i] = OT_NIL;
        ok = false;
        break;
      }
      oti_class_obj* c = oti_as_class(rt->classes[i]);
      c->name = OT_NIL;
      c->parent = i ? rt->classes[C_OBJECT] : OT_NIL;
      c->layout = OT_NIL;
      c->owner = OT_NIL;
      c->abstract = i == C_OBJECT;
      c->builtin = true;
    }
    ot_value name = oti_intern(rt, K_SYMBOL, names[i], strlen(names[i]));
    if (!name) {
      ok = false;
      break;
    }
    oti_as_class(rt->classes[i])->name = name;
    ot_value owner = oti_intern(rt, K_SYMBOL, "otium.generic", 13);
    if (!owner) {
      ok = false;
      break;
    }
    oti_as_class(rt->classes[i])->owner = owner;
    uint32_t global = oti_global(rt, names[i], strlen(names[i]));
    if (global == UINT32_MAX) {
      ok = false;
      break;
    }
    rt->globals[global] = rt->classes[i];
  }
  rt->current_namespace = saved;
  rt->classes_ready = ok;
  return ok;
}

ot_value oti_dispatch_type(ot_state* rt, ot_value v) {
  if (oti_kind(v, K_INSTANCE)) return oti_as_instance(v)->type;
  unsigned kind = C_OTHER;
  if (ot_is_int(v) || oti_kind(v, K_FLOAT)) kind = C_NUMBER;
  else if (v == OT_TRUE || v == OT_FALSE) kind = C_BOOLEAN;
  else if (v == OT_NIL) kind = C_NIL;
  else if (v == OT_EMPTY) kind = C_NULL;
  else if ((v & 255) == TAG_CHAR) kind = C_CHARACTER;
  else if ((v & 255) == TAG_BUILTIN || oti_kind(v, K_PARAM) || oti_kind(v, K_CLOSURE) ||
           oti_kind(v, K_NEXT) || oti_kind(v, K_GENERIC) || oti_kind(v, K_MACRO) ||
           oti_kind(v, K_FOREIGN_FUNCTION) || oti_kind(v, K_NATIVE))
    kind = C_PROCEDURE;
  else if (oti_kind(v, K_STRING)) kind = C_STRING;
  else if (oti_kind(v, K_SYMBOL)) kind = C_SYMBOL;
  else if (oti_kind(v, K_KEYWORD)) kind = C_KEYWORD;
  else if (oti_kind(v, K_PORT)) kind = C_PORT;
  else if (oti_kind(v, K_BYTEVECTOR)) kind = C_BYTEVECTOR;
  else if (oti_kind(v, K_ARRAY)) kind = C_VECTOR;
  else if (oti_kind(v, K_TABLE)) kind = C_TABLE;
  else if (oti_kind(v, K_PAIR)) kind = C_PAIR;
  else if (oti_kind(v, K_POINTER)) kind = oti_foreign_owned(v) ? C_CPOINTER : C_POINTER;
  else if (oti_kind(v, K_CLASS) || oti_kind(v, K_RECORD_TYPE) || oti_kind(v, K_FOREIGN_LAYOUT))
    kind = C_TYPE;
  return rt->classes[kind];
}

static bool applicable(ot_state* rt, ot_value signature, size_t first) {
  for (size_t i = 0; signature != OT_EMPTY; i++, signature = oti_cdr(signature))
    if (!oti_subtype(oti_dispatch_type(rt, rt->stack[first + i]), oti_car(signature))) return false;
  return true;
}

static bool more_specific(ot_value a, ot_value b) {
  bool strict = false;
  while (a != OT_EMPTY) {
    ot_value x = oti_car(a), y = oti_car(b);
    if (!oti_subtype(x, y)) return false;
    if (x != y) strict = true;
    a = oti_cdr(a);
    b = oti_cdr(b);
  }
  return strict;
}

bool oti_generic_prepare(ot_state* rt, size_t call, size_t* count) {
  ot_value callable = rt->stack[call];
  if (*count != arity(callable))
    return oti_kind(callable, K_GENERIC) ? oti_tagged_error(rt, "generic-call", "wrong-arity",
                                                            "incorrect generic arity", 0, NULL)
                                         : oti_tagged_error(rt, "next-method", "wrong-arity",
                                                            "incorrect next method arity", 0, NULL);
  bool initial = oti_kind(callable, K_GENERIC);
  ot_value methods = initial ? oti_as_generic(callable)->methods : oti_as_next(callable)->methods;
  ot_value selected = OT_NIL;
  bool around = false;
  for (ot_value p = methods; p != OT_EMPTY; p = oti_cdr(p)) {
    ot_value m = oti_car(p);
    if (oti_as_method(m)->around && applicable(rt, oti_as_method(m)->signature, call + 1)) {
      around = true;
      break;
    }
  }
  for (ot_value p = methods; p != OT_EMPTY; p = oti_cdr(p)) {
    ot_value m = oti_car(p), signature = oti_as_method(m)->signature;
    if (oti_as_method(m)->around != around || !applicable(rt, signature, call + 1)) continue;
    bool dominated = false;
    for (ot_value q = methods; q != OT_EMPTY; q = oti_cdr(q)) {
      ot_value other = oti_car(q);
      if (other != m && oti_as_method(other)->around == around &&
          applicable(rt, oti_as_method(other)->signature, call + 1) &&
          more_specific(oti_as_method(other)->signature, signature)) {
        dominated = true;
        break;
      }
    }
    if (dominated) continue;
    if (selected != OT_NIL)
      return oti_tagged_error(rt, "generic-call", "ambiguous-method", "ambiguous methods", 0, NULL);
    selected = m;
  }
  if (selected == OT_NIL)
    return oti_tagged_error(rt, "generic-call", "missing-method", "no applicable method", 0, NULL);
  if (!oti_as_method(selected)->next) {
    rt->stack[call] = oti_as_method(selected)->procedure;
    return true;
  }
  ot_value live[] = {methods, selected, OT_EMPTY, OT_FALSE};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 4);
  bool ok = true;
  while (ok && live[0] != OT_EMPTY) {
    ot_value m = oti_car(live[0]);
    if (m != live[1] && (around || !oti_as_method(m)->around) &&
        (!initial || applicable(rt, oti_as_method(m)->signature, call + 1))) {
      ot_value node = cons(rt, m, live[2]);
      ok = node != 0;
      if (ok) live[2] = node;
    }
    live[0] = oti_cdr(live[0]);
  }
  if (ok && live[2] != OT_EMPTY) {
    live[3] = oti_alloc(rt, K_NEXT, 1, OTI_BYTES(oti_next_obj, 1));
    ok = live[3] != 0;
    if (ok) {
      oti_as_next(live[3])->methods = live[2];
      oti_as_next(live[3])->count = *count;
    }
  }
  if (ok) {
    /* Arguments need not end at the stack top; registers above them are dead. */
    size_t end = call + 2 + *count;
    if (end > rt->stack_limit || !oti_stack(rt, end)) ok = oti_error(rt, "VM stack limit exceeded");
    else {
      memmove(rt->stack + call + 2, rt->stack + call + 1, *count * sizeof(ot_value));
      rt->stack[call] = oti_as_method(live[1])->procedure;
      rt->stack[call + 1] = live[3];
      (*count)++;
      if (rt->sp < end) rt->sp = rt->stack_roots.count = end;
    }
  }
  oti_roots_pop(&rt->heap, &roots);
  return ok;
}

static ot_value cons(ot_state* rt, ot_value a, ot_value b) {
  ot_value slots[] = {a, b};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, slots, 2);
  ot_value pair = oti_alloc(rt, K_PAIR, 2, OTI_BYTES(oti_pair_obj, 2));
  if (pair) {
    oti_as_pair(pair)->car = slots[0];
    oti_as_pair(pair)->cdr = slots[1];
  }
  oti_roots_pop(&rt->heap, &roots);
  return pair;
}

static ot_value copy_signature(ot_state* rt, ot_value source) {
  ot_value slots[] = {source, OT_EMPTY, OT_EMPTY};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, slots, 3);
  bool ok = true;
  while (slots[0] != OT_EMPTY) {
    ot_value item = oti_car(slots[0]);
    ot_value node = cons(rt, item == OT_FALSE ? rt->classes[C_OBJECT] : item, OT_EMPTY);
    if (!node) {
      ok = false;
      break;
    }
    if (slots[1] == OT_EMPTY) slots[1] = node;
    else oti_as_pair(slots[2])->cdr = node;
    slots[2] = node;
    slots[0] = oti_cdr(slots[0]);
  }
  ot_value result = ok ? slots[1] : 0;
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

static bool class_args(ot_state* rt, size_t count, size_t needed) {
  if (count != needed) return oti_error(rt, "wrong number of class arguments");
  return oti_classes_init(rt);
}

static ot_value class_class_p(ot_state* rt, size_t first, size_t count) {
  if (!class_args(rt, count, 1)) return 0;
  return oti_kind(rt->stack[first], K_CLASS) ? OT_TRUE : OT_FALSE;
}

static ot_value class_generic_p(ot_state* rt, size_t first, size_t count) {
  if (!class_args(rt, count, 1)) return 0;
  return oti_kind(rt->stack[first], K_GENERIC) ? OT_TRUE : OT_FALSE;
}

static ot_value class_dispatch_type(ot_state* rt, size_t first, size_t count) {
  if (!class_args(rt, count, 1)) return 0;
  return oti_dispatch_type(rt, rt->stack[first]);
}

static ot_value class_subtype_p(ot_state* rt, size_t first, size_t count) {
  if (!class_args(rt, count, 2)) return 0;
  ot_value child = rt->stack[first], parent = rt->stack[first + 1];
  if (!oti_kind(child, K_CLASS) || !oti_kind(parent, K_CLASS))
    return oti_fail(rt, "expected class descriptors");
  return oti_subtype(child, parent) ? OT_TRUE : OT_FALSE;
}

static ot_value class_define_generic(ot_state* rt, size_t first, size_t count) {
  if (!class_args(rt, count, 3)) return 0;
  ot_value v = rt->stack[first], n = rt->stack[first + 1];
  if (!oti_kind(v, K_SYMBOL) || !ot_is_int(n) || ot_to_int(n) < 0)
    return oti_fail(rt, "invalid generic declaration");
  size_t length;
  memcpy(&length, object_bytes(v), sizeof(length));
  const char* text = (const char*)object_bytes(v) + sizeof(length);
  if (oti_import_global(rt, text, length) != UINT32_MAX) {
    oti_tagged_error(rt, "defgeneric", "binding-conflict",
                     "declaration would replace another binding", 1, &v);
    return 0;
  }
  uint32_t global = oti_global(rt, text, length);
  if (global == UINT32_MAX) return 0;
  ot_value existing = rt->globals[global];
  size_t wanted = (size_t)ot_to_int(n);
  if (existing != V_UNBOUND && rt->stack[first + 2] != OT_TRUE) {
    if (!oti_kind(existing, K_GENERIC) || arity(existing) != wanted) {
      oti_tagged_error(rt, "defgeneric", "binding-conflict",
                       "declaration would replace another binding", 0, NULL);
      return 0;
    }
    return existing;
  }
  ot_value generic = oti_alloc(rt, K_GENERIC, 2, OTI_BYTES(oti_generic_obj, 2));
  if (!generic) return 0;
  oti_generic_obj* g = oti_as_generic(generic);
  g->name = rt->stack[first];
  g->methods = OT_EMPTY;
  g->arity = wanted;
  rt->globals[global] = generic;
  rt->global_info[global].defined = 1;
  return generic;
}

/* Validate a method signature against the generic's arity and return a copy
 * with #f specializers replaced by Object. A method definition passes its
 * body's validity; other callers pass NULL. */
static ot_value class_method_signature(ot_state* rt, const char* who, size_t wanted, ot_value sig,
                                       const bool* body_ok) {
  size_t n = 0;
  ot_value end = sig;
  for (; oti_kind(end, K_PAIR) && n <= wanted; end = oti_cdr(end), n++) {
    ot_value type = oti_car(end);
    if (type != OT_FALSE && !oti_kind(type, K_CLASS)) {
      oti_tagged_error(rt, who, "invalid-specializer", "expected class", 0, NULL);
      return 0;
    }
  }
  if (end == OT_EMPTY && n != wanted && body_ok) {
    oti_tagged_error(rt, who, "wrong-arity", "method arity differs from generic", 0, NULL);
    return 0;
  }
  if (end != OT_EMPTY || n != wanted || (body_ok && !*body_ok)) {
    oti_tagged_error(rt, who, "invalid-signature", "expected class/wildcard signature", 0, NULL);
    return 0;
  }
  return copy_signature(rt, sig);
}

static ot_value not_generic(ot_state* rt, const char* who) {
  oti_tagged_error(rt, who, "not-generic", "expected generic procedure", 0, NULL);
  return 0;
}

static bool class_same_method(ot_value method, ot_value signature, bool around) {
  ot_value a = oti_as_method(method)->signature, b = signature;
  while (a != OT_EMPTY && oti_car(a) == oti_car(b)) {
    a = oti_cdr(a);
    b = oti_cdr(b);
  }
  return a == OT_EMPTY && oti_as_method(method)->around == around;
}

static ot_value class_define_method(ot_state* rt, size_t first, size_t count) {
  if (!class_args(rt, count, 5)) return 0;
  if (!oti_kind(rt->stack[first], K_GENERIC)) return not_generic(rt, "defmethod");
  ot_value slots[] = {rt->stack[first], OT_NIL, OT_NIL, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, slots, 4);
  ot_value result = 0;
  slots[1] = class_method_signature(rt, "defmethod", arity(slots[0]), rt->stack[first + 1],
                                    &(bool){oti_kind(rt->stack[first + 2], K_CLOSURE)});
  if (!slots[1]) goto done;
  bool around = rt->stack[first + 3] == OT_TRUE;
  slots[2] = oti_alloc(rt, K_METHOD, 2, OTI_BYTES(oti_method_obj, 2));
  if (!slots[2]) goto done;
  oti_method_obj* method = oti_as_method(slots[2]);
  method->around = around;
  method->next = rt->stack[first + 4] == OT_TRUE;
  method->signature = slots[1];
  method->procedure = rt->stack[first + 2];
  for (ot_value p = oti_as_generic(slots[0])->methods; p != OT_EMPTY; p = oti_cdr(p))
    if (class_same_method(oti_car(p), slots[1], around)) {
      oti_as_pair(p)->car = slots[2];
      result = slots[0];
      goto done;
    }
  slots[3] = cons(rt, slots[2], oti_as_generic(slots[0])->methods);
  if (!slots[3]) goto done;
  oti_as_generic(slots[0])->methods = slots[3];
  result = slots[0];
done:
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

static ot_value class_remove_method(ot_state* rt, size_t first, size_t count) {
  if (count != 2 && count != 3) return oti_fail(rt, "wrong number of class arguments");
  if (!oti_classes_init(rt)) return 0;
  if (!oti_kind(rt->stack[first], K_GENERIC)) return not_generic(rt, "remove-method!");
  ot_value slots[] = {rt->stack[first], OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, slots, 2);
  ot_value result = 0;
  slots[1] =
      class_method_signature(rt, "remove-method!", arity(slots[0]), rt->stack[first + 1], NULL);
  if (!slots[1]) goto done;
  bool around = false;
  if (count == 3) {
    ot_value qualifier = rt->stack[first + 2];
    if (oti_kind(qualifier, K_KEYWORD) && !strcmp((char*)oti_bytes_data(qualifier), ":around"))
      around = true;
    else if (!oti_kind(qualifier, K_SYMBOL) ||
             strcmp((char*)oti_bytes_data(qualifier), "primary")) {
      oti_fail(rt, "invalid method qualifier");
      goto done;
    }
  }
  ot_value previous = OT_NIL;
  result = OT_FALSE;
  for (ot_value p = oti_as_generic(slots[0])->methods; p != OT_EMPTY; p = oti_cdr(p)) {
    if (class_same_method(oti_car(p), slots[1], around)) {
      if (previous == OT_NIL) oti_as_generic(slots[0])->methods = oti_cdr(p);
      else oti_as_pair(previous)->cdr = oti_cdr(p);
      result = OT_TRUE;
      break;
    }
    previous = p;
  }
done:
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

static ot_value class_generic_methods(ot_state* rt, size_t first, size_t count) {
  if (!class_args(rt, count, 1)) return 0;
  if (!oti_kind(rt->stack[first], K_GENERIC)) return not_generic(rt, "generic-methods");
  ot_value slots[] = {OT_NIL, OT_NIL, OT_NIL, OT_EMPTY};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, slots, 4);
  ot_value result = 0;
  slots[0] = oti_as_generic(rt->stack[first])->methods;
  while (slots[0] != OT_EMPTY) {
    slots[1] = copy_signature(rt, oti_as_method(oti_car(slots[0]))->signature);
    if (!slots[1]) goto done;
    slots[1] = cons(rt, slots[1], OT_EMPTY);
    if (!slots[1]) goto done;
    bool around = oti_as_method(oti_car(slots[0]))->around;
    slots[2] = oti_intern(rt, around ? K_KEYWORD : K_SYMBOL, around ? ":around" : "primary", 7);
    if (!slots[2]) goto done;
    slots[1] = cons(rt, slots[2], slots[1]);
    if (!slots[1]) goto done;
    slots[3] = cons(rt, slots[1], slots[3]);
    if (!slots[3]) goto done;
    slots[0] = oti_cdr(slots[0]);
  }
  result = slots[3];
done:
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

/* The class's name qualified by its owner, as in user/Point. */
static ot_value class_type_name(ot_state* rt, size_t first, size_t count) {
  if (count != 1 || !oti_kind(rt->stack[first], K_CLASS))
    return oti_fail(rt, "`type-name` expected a type descriptor");
  oti_class_obj* c = oti_as_class(rt->stack[first]);
  size_t owner = oti_bytes_length(c->owner), name = oti_bytes_length(c->name);
  char* text = malloc(owner + 1 + name);
  if (!text) return oti_fail(rt, "out of host memory");
  memcpy(text, oti_bytes_data(c->owner), owner);
  text[owner] = '/';
  memcpy(text + owner + 1, oti_bytes_data(c->name), name);
  ot_value result = oti_intern(rt, K_SYMBOL, text, owner + 1 + name);
  free(text);
  return result;
}

const oti_builtin_def oti_class_builtins[] = {
    {B_CLASS_P, "class?", class_class_p},
    {B_DISPATCH_TYPE, "dispatch-type", class_dispatch_type},
    {B_CLASS_SUBTYPE, "class-subtype?", class_subtype_p},
    {B_GENERIC_P, "generic?", class_generic_p},
    {B_GENERIC_METHODS, "generic-methods", class_generic_methods},
    {B_GENERIC_DEFINE, "%define-generic", class_define_generic},
    {B_GENERIC_METHOD, "%define-method", class_define_method},
    {B_REMOVE_METHOD, "remove-method!", class_remove_method},
    {B_TYPE_NAME, "type-name", class_type_name},
    {B_TYPE_DESCRIPTOR_P, "type-descriptor?", class_class_p},
    {0, NULL, NULL},
};
