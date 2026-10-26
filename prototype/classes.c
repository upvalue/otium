#include "internal.h"
#include <string.h>

typedef struct {
  bool around, next;
} oti_method_info;

static oti_method_info* method_info(ot_value method) { return object_bytes(method); }

static ot_value cons(ot_runtime* rt, ot_value a, ot_value b);

static size_t arity(ot_value generic) {
  size_t n;
  memcpy(&n, object_bytes(generic), sizeof(n));
  return n;
}

bool oti_subtype(ot_value child, ot_value parent) {
  while (oti_kind(child, K_CLASS)) {
    if (child == parent) return true;
    child = object_fields(child)[1];
  }
  return false;
}

bool oti_classes_init(ot_runtime* rt) {
  if (rt->classes_ready) return true;
  static const char* names[C_COUNT] = {
      "Object",         "Number", "Boolean", "Character",  "String",    "Symbol",
      "Null",           "Pair",   "Vector",  "Bytevector", "Hashtable", "Procedure",
      "TypeDescriptor", "Nil",    "Keyword", "Pointer",    "Port",      "CPointer"};
  uint32_t saved = rt->current_namespace;
  rt->current_namespace = 0;
  bool ok = true;
  for (size_t i = 0; i < C_COUNT; i++) {
    if (rt->classes[i] == OT_NIL) {
      rt->classes[i] = oti_alloc(rt, K_CLASS, 3, sizeof(oti_class_info));
      if (!rt->classes[i]) {
        rt->classes[i] = OT_NIL;
        ok = false;
        break;
      }
      object_fields(rt->classes[i])[0] = OT_NIL;
      object_fields(rt->classes[i])[1] = i ? rt->classes[C_OBJECT] : OT_NIL;
      object_fields(rt->classes[i])[2] = OT_NIL;
      *(oti_class_info*)object_bytes(rt->classes[i]) = (oti_class_info){i == C_OBJECT, true};
    }
    ot_value name = oti_intern(rt, K_SYMBOL, names[i], strlen(names[i]));
    if (!name) {
      ok = false;
      break;
    }
    object_fields(rt->classes[i])[0] = name;
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

ot_value oti_dispatch_type(ot_runtime* rt, ot_value v) {
  if (oti_kind(v, K_INSTANCE)) return object_fields(v)[0];
  unsigned kind = C_OBJECT;
  if (ot_is_int(v) || oti_kind(v, K_FLOAT)) kind = C_NUMBER;
  else if (v == OT_TRUE || v == OT_FALSE) kind = C_BOOLEAN;
  else if (v == OT_NIL) kind = C_NIL;
  else if (v == OT_EMPTY) kind = C_NULL;
  else if ((v & 255) == TAG_CHAR) kind = C_CHARACTER;
  else if ((v & 255) == TAG_BUILTIN || oti_kind(v, K_PARAM) || oti_kind(v, K_CLOSURE) ||
           oti_kind(v, K_NEXT) || oti_kind(v, K_GENERIC) || oti_kind(v, K_MACRO) ||
           oti_kind(v, K_FOREIGN_FUNCTION))
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

static bool applicable(ot_runtime* rt, ot_value signature, size_t first) {
  for (size_t i = 0; signature != OT_EMPTY; i++, signature = object_fields(signature)[1])
    if (!oti_subtype(oti_dispatch_type(rt, rt->stack[first + i]), object_fields(signature)[0]))
      return false;
  return true;
}

static bool more_specific(ot_value a, ot_value b) {
  bool strict = false;
  while (a != OT_EMPTY) {
    ot_value x = object_fields(a)[0], y = object_fields(b)[0];
    if (!oti_subtype(x, y)) return false;
    if (x != y) strict = true;
    a = object_fields(a)[1];
    b = object_fields(b)[1];
  }
  return strict;
}

bool oti_generic_prepare(ot_runtime* rt, size_t call, size_t* count) {
  ot_value callable = rt->stack[call];
  if (*count != arity(callable)) return oti_error(rt, "wrong number of generic arguments");
  bool initial = oti_kind(callable, K_GENERIC);
  ot_value methods = object_fields(callable)[initial ? 1 : 0], selected = OT_NIL;
  bool around = false;
  for (ot_value p = methods; p != OT_EMPTY; p = object_fields(p)[1]) {
    ot_value m = object_fields(p)[0];
    if (method_info(m)->around && applicable(rt, object_fields(m)[0], call + 1)) {
      around = true;
      break;
    }
  }
  for (ot_value p = methods; p != OT_EMPTY; p = object_fields(p)[1]) {
    ot_value m = object_fields(p)[0], signature = object_fields(m)[0];
    if (method_info(m)->around != around || !applicable(rt, signature, call + 1)) continue;
    bool dominated = false;
    for (ot_value q = methods; q != OT_EMPTY; q = object_fields(q)[1]) {
      ot_value other = object_fields(q)[0];
      if (other != m && method_info(other)->around == around &&
          applicable(rt, object_fields(other)[0], call + 1) &&
          more_specific(object_fields(other)[0], signature)) {
        dominated = true;
        break;
      }
    }
    if (dominated) continue;
    if (selected != OT_NIL) return oti_error(rt, "ambiguous generic call");
    selected = m;
  }
  if (selected == OT_NIL) return oti_error(rt, "no applicable method");
  if (!method_info(selected)->next) {
    rt->stack[call] = object_fields(selected)[1];
    return true;
  }
  ot_value live[] = {methods, selected, OT_EMPTY, OT_FALSE};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 4);
  bool ok = true;
  while (ok && live[0] != OT_EMPTY) {
    ot_value m = object_fields(live[0])[0];
    if (m != live[1] && (around || !method_info(m)->around) &&
        (!initial || applicable(rt, object_fields(m)[0], call + 1))) {
      ot_value node = cons(rt, m, live[2]);
      ok = node != 0;
      if (ok) live[2] = node;
    }
    live[0] = object_fields(live[0])[1];
  }
  if (ok && live[2] != OT_EMPTY) {
    live[3] = oti_alloc(rt, K_NEXT, 1, sizeof(size_t));
    ok = live[3] != 0;
    if (ok) {
      object_fields(live[3])[0] = live[2];
      memcpy(object_bytes(live[3]), count, sizeof(*count));
    }
  }
  if (ok) {
    if (rt->sp >= rt->stack_limit || !oti_stack(rt, rt->sp + 1))
      ok = oti_error(rt, "VM stack limit exceeded");
    else {
      memmove(rt->stack + call + 2, rt->stack + call + 1, *count * sizeof(ot_value));
      rt->stack[call] = object_fields(live[1])[1];
      rt->stack[call + 1] = live[3];
      (*count)++;
      rt->sp++;
      rt->stack_roots.count = rt->sp;
    }
  }
  oti_roots_pop(&rt->heap, &roots);
  return ok;
}

static ot_value cons(ot_runtime* rt, ot_value a, ot_value b) {
  ot_value slots[] = {a, b};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, slots, 2);
  ot_value pair = oti_alloc(rt, K_PAIR, 2, 0);
  if (pair) {
    object_fields(pair)[0] = slots[0];
    object_fields(pair)[1] = slots[1];
  }
  oti_roots_pop(&rt->heap, &roots);
  return pair;
}

static ot_value copy_signature(ot_runtime* rt, ot_value source) {
  ot_value slots[] = {source, OT_EMPTY, OT_EMPTY};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, slots, 3);
  bool ok = true;
  while (slots[0] != OT_EMPTY) {
    ot_value item = object_fields(slots[0])[0];
    ot_value node = cons(rt, item == OT_FALSE ? rt->classes[C_OBJECT] : item, OT_EMPTY);
    if (!node) {
      ok = false;
      break;
    }
    if (slots[1] == OT_EMPTY) slots[1] = node;
    else object_fields(slots[2])[1] = node;
    slots[2] = node;
    slots[0] = object_fields(slots[0])[1];
  }
  ot_value result = ok ? slots[1] : 0;
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

ot_value oti_class_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count) {
  if (id >= B_MAKE_RECORD_TYPE) return oti_record_builtin(rt, id, first, count);
  if (id >= B_CLASS_DEFINE) return oti_instance_builtin(rt, id, first, count);
  size_t needed = id == B_GENERIC_METHOD                           ? 5
                  : id == B_GENERIC_DEFINE                         ? 3
                  : id == B_CLASS_SUBTYPE || id == B_REMOVE_METHOD ? 2
                                                                   : 1;
  if (count != needed && !(id == B_REMOVE_METHOD && count == 3))
    return oti_fail(rt, "wrong number of class arguments");
  if (!oti_classes_init(rt)) return 0;
  ot_value v = rt->stack[first];
  if (id == B_CLASS_P) return oti_kind(v, K_CLASS) ? OT_TRUE : OT_FALSE;
  if (id == B_GENERIC_P) return oti_kind(v, K_GENERIC) ? OT_TRUE : OT_FALSE;
  if (id == B_DISPATCH_TYPE) return oti_dispatch_type(rt, v);
  if (id == B_CLASS_SUBTYPE) {
    if (!oti_kind(v, K_CLASS) || !oti_kind(rt->stack[first + 1], K_CLASS))
      return oti_fail(rt, "expected class descriptors");
    return oti_subtype(v, rt->stack[first + 1]) ? OT_TRUE : OT_FALSE;
  }
  if (id == B_GENERIC_DEFINE) {
    ot_value n = rt->stack[first + 1];
    if (!oti_kind(v, K_SYMBOL) || !ot_is_int(n) || ot_to_int(n) < 0)
      return oti_fail(rt, "invalid generic declaration");
    size_t length;
    memcpy(&length, object_bytes(v), sizeof(length));
    uint32_t global = oti_global(rt, (const char*)object_bytes(v) + sizeof(length), length);
    if (global == UINT32_MAX) return 0;
    ot_value existing = rt->globals[global];
    size_t wanted = (size_t)ot_to_int(n);
    if (existing != V_UNBOUND && rt->stack[first + 2] != OT_TRUE) {
      if (!oti_kind(existing, K_GENERIC) || arity(existing) != wanted)
        return oti_fail(rt, "incompatible generic redeclaration");
      return existing;
    }
    ot_value generic = oti_alloc(rt, K_GENERIC, 2, sizeof(size_t));
    if (!generic) return 0;
    object_fields(generic)[0] = rt->stack[first];
    object_fields(generic)[1] = OT_EMPTY;
    memcpy(object_bytes(generic), &wanted, sizeof(wanted));
    rt->globals[global] = generic;
    rt->global_defined[global] = 1;
    return generic;
  }
  if (!oti_kind(v, K_GENERIC)) return oti_fail(rt, "expected generic function");
  ot_value slots[] = {v, OT_NIL, OT_NIL, OT_NIL, OT_EMPTY};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, slots, 5);
  ot_value result = 0;
  if (id == B_GENERIC_METHOD || id == B_REMOVE_METHOD) {
    ot_value sig = rt->stack[first + 1];
    size_t n = 0;
    for (ot_value p = sig; oti_kind(p, K_PAIR); p = object_fields(p)[1]) {
      if (++n > arity(slots[0])) {
        oti_fail(rt, "method signature arity");
        goto done;
      }
      ot_value type = object_fields(p)[0];
      if (type != OT_FALSE && !oti_kind(type, K_CLASS)) {
        oti_fail(rt, "method specializer must be a class");
        goto done;
      }
    }
    ot_value end = sig;
    for (size_t i = 0; i < n; i++) end = object_fields(end)[1];
    if (end != OT_EMPTY || n != arity(slots[0]) ||
        (id == B_GENERIC_METHOD && !oti_kind(rt->stack[first + 2], K_CLOSURE))) {
      oti_fail(rt, "invalid method signature or body");
      goto done;
    }
    slots[1] = copy_signature(rt, sig);
    if (!slots[1]) goto done;
    bool around = false;
    if (id == B_REMOVE_METHOD && count == 3) {
      ot_value qualifier = rt->stack[first + 2];
      if (oti_kind(qualifier, K_KEYWORD) && !strcmp((char*)oti_bytes_data(qualifier), ":around"))
        around = true;
      else if (!oti_kind(qualifier, K_SYMBOL) ||
               strcmp((char*)oti_bytes_data(qualifier), "primary")) {
        oti_fail(rt, "invalid method qualifier");
        goto done;
      }
    } else if (id == B_GENERIC_METHOD) around = rt->stack[first + 3] == OT_TRUE;
    if (id == B_REMOVE_METHOD) {
      ot_value previous = OT_NIL;
      for (ot_value p = object_fields(slots[0])[1]; p != OT_EMPTY; p = object_fields(p)[1]) {
        ot_value m = object_fields(p)[0], a = object_fields(m)[0], b = slots[1];
        while (a != OT_EMPTY && object_fields(a)[0] == object_fields(b)[0]) {
          a = object_fields(a)[1];
          b = object_fields(b)[1];
        }
        if (a == OT_EMPTY && method_info(m)->around == around) {
          if (previous == OT_NIL) object_fields(slots[0])[1] = object_fields(p)[1];
          else object_fields(previous)[1] = object_fields(p)[1];
          result = OT_TRUE;
          goto done;
        }
        previous = p;
      }
      result = OT_FALSE;
      goto done;
    }
    slots[2] = oti_alloc(rt, K_METHOD, 2, sizeof(oti_method_info));
    if (!slots[2]) goto done;
    *method_info(slots[2]) = (oti_method_info){around, rt->stack[first + 4] == OT_TRUE};
    object_fields(slots[2])[0] = slots[1];
    object_fields(slots[2])[1] = rt->stack[first + 2];
    for (ot_value p = object_fields(slots[0])[1]; p != OT_EMPTY; p = object_fields(p)[1]) {
      ot_value a = object_fields(object_fields(p)[0])[0], b = slots[1];
      while (a != OT_EMPTY && object_fields(a)[0] == object_fields(b)[0]) {
        a = object_fields(a)[1];
        b = object_fields(b)[1];
      }
      if (a == OT_EMPTY && method_info(object_fields(p)[0])->around == around) {
        object_fields(p)[0] = slots[2];
        result = slots[0];
        goto done;
      }
    }
    slots[3] = cons(rt, slots[2], object_fields(slots[0])[1]);
    if (!slots[3]) goto done;
    object_fields(slots[0])[1] = slots[3];
    result = slots[0];
  } else if (id == B_GENERIC_METHODS) {
    slots[1] = object_fields(slots[0])[1];
    while (slots[1] != OT_EMPTY) {
      slots[2] = copy_signature(rt, object_fields(object_fields(slots[1])[0])[0]);
      if (!slots[2]) goto done;
      slots[2] = cons(rt, slots[2], OT_EMPTY);
      if (!slots[2]) goto done;
      bool around = method_info(object_fields(slots[1])[0])->around;
      slots[3] = oti_intern(rt, around ? K_KEYWORD : K_SYMBOL, around ? ":around" : "primary", 7);
      if (!slots[3]) goto done;
      slots[2] = cons(rt, slots[3], slots[2]);
      if (!slots[2]) goto done;
      slots[4] = cons(rt, slots[2], slots[4]);
      if (!slots[4]) goto done;
      slots[1] = object_fields(slots[1])[1];
    }
    result = slots[4];
  } else oti_fail(rt, "unknown class operation");
done:
  oti_roots_pop(&rt->heap, &roots);
  return result;
}
