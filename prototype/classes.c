#include "internal.h"
#include <string.h>

typedef struct {
  bool around, next;
} MethodInfo;

static MethodInfo* method_info(Value method) { return object_bytes(method); }

static Value cons(Runtime* rt, Value a, Value b);

static Value fail(Runtime* rt, const char* message) {
  rt_error(rt, message);
  return 0;
}

static size_t arity(Value generic) {
  size_t n;
  memcpy(&n, object_bytes(generic), sizeof(n));
  return n;
}

bool rt_subtype(Value child, Value parent) {
  while (rt_kind(child, K_CLASS)) {
    if (child == parent) return true;
    child = object_fields(child)[1];
  }
  return false;
}

bool rt_classes_init(Runtime* rt) {
  if (rt->classes_ready) return true;
  static const char* names[C_COUNT] = {
      "Object",         "Number", "Boolean", "Character",  "String",    "Symbol",
      "Null",           "Pair",   "Vector",  "Bytevector", "Hashtable", "Procedure",
      "TypeDescriptor", "Nil",    "Keyword", "Pointer",    "Port",      "CPointer"};
  uint32_t saved = rt->current_namespace;
  rt->current_namespace = 0;
  bool ok = true;
  for (size_t i = 0; i < C_COUNT; i++) {
    if (rt->classes[i] == V_NIL) {
      rt->classes[i] = rt_alloc(rt, K_CLASS, 3, sizeof(ClassInfo));
      if (!rt->classes[i]) {
        rt->classes[i] = V_NIL;
        ok = false;
        break;
      }
      object_fields(rt->classes[i])[0] = V_NIL;
      object_fields(rt->classes[i])[1] = i ? rt->classes[C_OBJECT] : V_NIL;
      object_fields(rt->classes[i])[2] = V_NIL;
      *(ClassInfo*)object_bytes(rt->classes[i]) = (ClassInfo){i == C_OBJECT, true};
    }
    Value name = rt_intern(rt, K_SYMBOL, names[i], strlen(names[i]));
    if (!name) {
      ok = false;
      break;
    }
    object_fields(rt->classes[i])[0] = name;
    uint32_t global = rt_global(rt, names[i], strlen(names[i]));
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

Value rt_dispatch_type(Runtime* rt, Value v) {
  if (rt_kind(v, K_INSTANCE)) return object_fields(v)[0];
  unsigned kind = C_OBJECT;
  if (value_is_int(v) || rt_kind(v, K_FLOAT)) kind = C_NUMBER;
  else if (v == V_TRUE || v == V_FALSE) kind = C_BOOLEAN;
  else if (v == V_NIL) kind = C_NIL;
  else if (v == V_EMPTY) kind = C_NULL;
  else if ((v & 255) == TAG_CHAR) kind = C_CHARACTER;
  else if ((v & 255) == TAG_BUILTIN || rt_kind(v, K_PARAM) || rt_kind(v, K_CLOSURE) ||
           rt_kind(v, K_NEXT) || rt_kind(v, K_GENERIC) || rt_kind(v, K_MACRO) ||
           rt_kind(v, K_FOREIGN_FUNCTION))
    kind = C_PROCEDURE;
  else if (rt_kind(v, K_STRING)) kind = C_STRING;
  else if (rt_kind(v, K_SYMBOL)) kind = C_SYMBOL;
  else if (rt_kind(v, K_KEYWORD)) kind = C_KEYWORD;
  else if (rt_kind(v, K_PORT)) kind = C_PORT;
  else if (rt_kind(v, K_BYTEVECTOR)) kind = C_BYTEVECTOR;
  else if (rt_kind(v, K_ARRAY)) kind = C_VECTOR;
  else if (rt_kind(v, K_TABLE)) kind = C_TABLE;
  else if (rt_kind(v, K_PAIR)) kind = C_PAIR;
  else if (rt_kind(v, K_POINTER)) kind = rt_foreign_owned(v) ? C_CPOINTER : C_POINTER;
  else if (rt_kind(v, K_CLASS) || rt_kind(v, K_RECORD_TYPE) || rt_kind(v, K_FOREIGN_LAYOUT))
    kind = C_TYPE;
  return rt->classes[kind];
}

static bool applicable(Runtime* rt, Value signature, size_t first) {
  for (size_t i = 0; signature != V_EMPTY; i++, signature = object_fields(signature)[1])
    if (!rt_subtype(rt_dispatch_type(rt, rt->stack[first + i]), object_fields(signature)[0]))
      return false;
  return true;
}

static bool more_specific(Value a, Value b) {
  bool strict = false;
  while (a != V_EMPTY) {
    Value x = object_fields(a)[0], y = object_fields(b)[0];
    if (!rt_subtype(x, y)) return false;
    if (x != y) strict = true;
    a = object_fields(a)[1];
    b = object_fields(b)[1];
  }
  return strict;
}

bool rt_generic_prepare(Runtime* rt, size_t call, size_t* count) {
  Value callable = rt->stack[call];
  if (*count != arity(callable)) return rt_error(rt, "wrong number of generic arguments");
  bool initial = rt_kind(callable, K_GENERIC);
  Value methods = object_fields(callable)[initial ? 1 : 0], selected = V_NIL;
  bool around = false;
  for (Value p = methods; p != V_EMPTY; p = object_fields(p)[1]) {
    Value m = object_fields(p)[0];
    if (method_info(m)->around && applicable(rt, object_fields(m)[0], call + 1)) {
      around = true;
      break;
    }
  }
  for (Value p = methods; p != V_EMPTY; p = object_fields(p)[1]) {
    Value m = object_fields(p)[0], signature = object_fields(m)[0];
    if (method_info(m)->around != around || !applicable(rt, signature, call + 1)) continue;
    bool dominated = false;
    for (Value q = methods; q != V_EMPTY; q = object_fields(q)[1]) {
      Value other = object_fields(q)[0];
      if (other != m && method_info(other)->around == around &&
          applicable(rt, object_fields(other)[0], call + 1) &&
          more_specific(object_fields(other)[0], signature)) {
        dominated = true;
        break;
      }
    }
    if (dominated) continue;
    if (selected != V_NIL) return rt_error(rt, "ambiguous generic call");
    selected = m;
  }
  if (selected == V_NIL) return rt_error(rt, "no applicable method");
  if (!method_info(selected)->next) {
    rt->stack[call] = object_fields(selected)[1];
    return true;
  }
  Value live[] = {methods, selected, V_EMPTY, V_FALSE};
  Roots roots;
  roots_push(&rt->heap, &roots, live, 4);
  bool ok = true;
  while (ok && live[0] != V_EMPTY) {
    Value m = object_fields(live[0])[0];
    if (m != live[1] && (around || !method_info(m)->around) &&
        (!initial || applicable(rt, object_fields(m)[0], call + 1))) {
      Value node = cons(rt, m, live[2]);
      ok = node != 0;
      if (ok) live[2] = node;
    }
    live[0] = object_fields(live[0])[1];
  }
  if (ok && live[2] != V_EMPTY) {
    live[3] = rt_alloc(rt, K_NEXT, 1, sizeof(size_t));
    ok = live[3] != 0;
    if (ok) {
      object_fields(live[3])[0] = live[2];
      memcpy(object_bytes(live[3]), count, sizeof(*count));
    }
  }
  if (ok) {
    if (rt->sp >= rt->stack_limit || !rt_stack(rt, rt->sp + 1))
      ok = rt_error(rt, "VM stack limit exceeded");
    else {
      memmove(rt->stack + call + 2, rt->stack + call + 1, *count * sizeof(Value));
      rt->stack[call] = object_fields(live[1])[1];
      rt->stack[call + 1] = live[3];
      (*count)++;
      rt->sp++;
      rt->stack_roots.count = rt->sp;
    }
  }
  roots_pop(&rt->heap, &roots);
  return ok;
}

static Value cons(Runtime* rt, Value a, Value b) {
  Value slots[] = {a, b};
  Roots roots;
  roots_push(&rt->heap, &roots, slots, 2);
  Value pair = rt_alloc(rt, K_PAIR, 2, 0);
  if (pair) {
    object_fields(pair)[0] = slots[0];
    object_fields(pair)[1] = slots[1];
  }
  roots_pop(&rt->heap, &roots);
  return pair;
}

static Value copy_signature(Runtime* rt, Value source) {
  Value slots[] = {source, V_EMPTY, V_EMPTY};
  Roots roots;
  roots_push(&rt->heap, &roots, slots, 3);
  bool ok = true;
  while (slots[0] != V_EMPTY) {
    Value item = object_fields(slots[0])[0];
    Value node = cons(rt, item == V_FALSE ? rt->classes[C_OBJECT] : item, V_EMPTY);
    if (!node) {
      ok = false;
      break;
    }
    if (slots[1] == V_EMPTY) slots[1] = node;
    else object_fields(slots[2])[1] = node;
    slots[2] = node;
    slots[0] = object_fields(slots[0])[1];
  }
  Value result = ok ? slots[1] : 0;
  roots_pop(&rt->heap, &roots);
  return result;
}

Value rt_class_builtin(Runtime* rt, unsigned id, size_t first, size_t count) {
  if (id >= B_MAKE_RECORD_TYPE) return rt_record_builtin(rt, id, first, count);
  if (id >= B_CLASS_DEFINE) return rt_instance_builtin(rt, id, first, count);
  size_t needed = id == B_GENERIC_METHOD                           ? 5
                  : id == B_GENERIC_DEFINE                         ? 3
                  : id == B_CLASS_SUBTYPE || id == B_REMOVE_METHOD ? 2
                                                                   : 1;
  if (count != needed && !(id == B_REMOVE_METHOD && count == 3))
    return fail(rt, "wrong number of class arguments");
  if (!rt_classes_init(rt)) return 0;
  Value v = rt->stack[first];
  if (id == B_CLASS_P) return rt_kind(v, K_CLASS) ? V_TRUE : V_FALSE;
  if (id == B_GENERIC_P) return rt_kind(v, K_GENERIC) ? V_TRUE : V_FALSE;
  if (id == B_DISPATCH_TYPE) return rt_dispatch_type(rt, v);
  if (id == B_CLASS_SUBTYPE) {
    if (!rt_kind(v, K_CLASS) || !rt_kind(rt->stack[first + 1], K_CLASS))
      return fail(rt, "expected class descriptors");
    return rt_subtype(v, rt->stack[first + 1]) ? V_TRUE : V_FALSE;
  }
  if (id == B_GENERIC_DEFINE) {
    Value n = rt->stack[first + 1];
    if (!rt_kind(v, K_SYMBOL) || !value_is_int(n) || value_to_int(n) < 0)
      return fail(rt, "invalid generic declaration");
    size_t length;
    memcpy(&length, object_bytes(v), sizeof(length));
    uint32_t global = rt_global(rt, (const char*)object_bytes(v) + sizeof(length), length);
    if (global == UINT32_MAX) return 0;
    Value existing = rt->globals[global];
    size_t wanted = (size_t)value_to_int(n);
    if (existing != V_UNBOUND && rt->stack[first + 2] != V_TRUE) {
      if (!rt_kind(existing, K_GENERIC) || arity(existing) != wanted)
        return fail(rt, "incompatible generic redeclaration");
      return existing;
    }
    Value generic = rt_alloc(rt, K_GENERIC, 2, sizeof(size_t));
    if (!generic) return 0;
    object_fields(generic)[0] = rt->stack[first];
    object_fields(generic)[1] = V_EMPTY;
    memcpy(object_bytes(generic), &wanted, sizeof(wanted));
    rt->globals[global] = generic;
    rt->global_defined[global] = 1;
    return generic;
  }
  if (!rt_kind(v, K_GENERIC)) return fail(rt, "expected generic function");
  Value slots[] = {v, V_NIL, V_NIL, V_NIL, V_EMPTY};
  Roots roots;
  roots_push(&rt->heap, &roots, slots, 5);
  Value result = 0;
  if (id == B_GENERIC_METHOD || id == B_REMOVE_METHOD) {
    Value sig = rt->stack[first + 1];
    size_t n = 0;
    for (Value p = sig; rt_kind(p, K_PAIR); p = object_fields(p)[1]) {
      if (++n > arity(slots[0])) {
        fail(rt, "method signature arity");
        goto done;
      }
      Value type = object_fields(p)[0];
      if (type != V_FALSE && !rt_kind(type, K_CLASS)) {
        fail(rt, "method specializer must be a class");
        goto done;
      }
    }
    Value end = sig;
    for (size_t i = 0; i < n; i++) end = object_fields(end)[1];
    if (end != V_EMPTY || n != arity(slots[0]) ||
        (id == B_GENERIC_METHOD && !rt_kind(rt->stack[first + 2], K_CLOSURE))) {
      fail(rt, "invalid method signature or body");
      goto done;
    }
    slots[1] = copy_signature(rt, sig);
    if (!slots[1]) goto done;
    bool around = false;
    if (id == B_REMOVE_METHOD && count == 3) {
      Value qualifier = rt->stack[first + 2];
      if (rt_kind(qualifier, K_KEYWORD) && !strcmp((char*)rt_bytes_data(qualifier), ":around"))
        around = true;
      else if (!rt_kind(qualifier, K_SYMBOL) ||
               strcmp((char*)rt_bytes_data(qualifier), "primary")) {
        fail(rt, "invalid method qualifier");
        goto done;
      }
    } else if (id == B_GENERIC_METHOD) around = rt->stack[first + 3] == V_TRUE;
    if (id == B_REMOVE_METHOD) {
      Value previous = V_NIL;
      for (Value p = object_fields(slots[0])[1]; p != V_EMPTY; p = object_fields(p)[1]) {
        Value m = object_fields(p)[0], a = object_fields(m)[0], b = slots[1];
        while (a != V_EMPTY && object_fields(a)[0] == object_fields(b)[0]) {
          a = object_fields(a)[1];
          b = object_fields(b)[1];
        }
        if (a == V_EMPTY && method_info(m)->around == around) {
          if (previous == V_NIL) object_fields(slots[0])[1] = object_fields(p)[1];
          else object_fields(previous)[1] = object_fields(p)[1];
          result = V_TRUE;
          goto done;
        }
        previous = p;
      }
      result = V_FALSE;
      goto done;
    }
    slots[2] = rt_alloc(rt, K_METHOD, 2, sizeof(MethodInfo));
    if (!slots[2]) goto done;
    *method_info(slots[2]) = (MethodInfo){around, rt->stack[first + 4] == V_TRUE};
    object_fields(slots[2])[0] = slots[1];
    object_fields(slots[2])[1] = rt->stack[first + 2];
    for (Value p = object_fields(slots[0])[1]; p != V_EMPTY; p = object_fields(p)[1]) {
      Value a = object_fields(object_fields(p)[0])[0], b = slots[1];
      while (a != V_EMPTY && object_fields(a)[0] == object_fields(b)[0]) {
        a = object_fields(a)[1];
        b = object_fields(b)[1];
      }
      if (a == V_EMPTY && method_info(object_fields(p)[0])->around == around) {
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
    while (slots[1] != V_EMPTY) {
      slots[2] = copy_signature(rt, object_fields(object_fields(slots[1])[0])[0]);
      if (!slots[2]) goto done;
      slots[2] = cons(rt, slots[2], V_EMPTY);
      if (!slots[2]) goto done;
      bool around = method_info(object_fields(slots[1])[0])->around;
      slots[3] = rt_intern(rt, around ? K_KEYWORD : K_SYMBOL, around ? ":around" : "primary", 7);
      if (!slots[3]) goto done;
      slots[2] = cons(rt, slots[3], slots[2]);
      if (!slots[2]) goto done;
      slots[4] = cons(rt, slots[2], slots[4]);
      if (!slots[4]) goto done;
      slots[1] = object_fields(slots[1])[1];
    }
    result = slots[4];
  } else fail(rt, "unknown class operation");
done:
  roots_pop(&rt->heap, &roots);
  return result;
}
