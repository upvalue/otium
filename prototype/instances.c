#include "internal.h"
#include <string.h>

static Value fail(Runtime* rt, const char* message) {
  rt_error(rt, message);
  return 0;
}

static size_t slots(Value layout) { return ((HeapObject*)layout)->fields / 3; }

static size_t slot_index(Value layout, Value key) {
  for (size_t i = 0; i < slots(layout); i++)
    if (object_fields(layout)[3 * i] == key) return i;
  return SIZE_MAX;
}

static Value pair(Runtime* rt, Value car, Value cdr) {
  Value live[] = {car, cdr};
  Roots roots;
  roots_push(&rt->heap, &roots, live, 2);
  Value result = rt_alloc(rt, K_PAIR, 2, 0);
  if (result) {
    object_fields(result)[0] = live[0];
    object_fields(result)[1] = live[1];
  }
  roots_pop(&rt->heap, &roots);
  return result;
}

static bool migrate(Runtime* rt, Value* instance, Value properties) {
  if (object_fields(*instance)[1] == object_fields(object_fields(*instance)[0])[2]) return true;
  Value live[] = {*instance,
                  object_fields(*instance)[1],
                  object_fields(*instance)[2],
                  object_fields(object_fields(*instance)[0])[2],
                  V_NIL,
                  V_NIL,
                  V_NIL,
                  properties};
  Roots roots;
  roots_push(&rt->heap, &roots, live, 8);
  size_t n = slots(live[3]);
  bool installed = false;
  live[4] = rt_alloc(rt, K_ARRAY_DATA, (uint32_t)n, 0);
  bool ok = live[4] != 0;
  if (ok) {
    live[5] = rt_table_builtin(rt, B_TABLE, 0, 0);
    ok = live[5] != 0;
  }
  for (size_t i = 0; ok && i < slots(live[1]); i++) {
    ok = rt_table_set(rt, live[5], object_fields(live[1])[3 * i], object_fields(live[2])[i]) != 0;
  }
  if (ok) {
    for (size_t i = 0; i < n; i++) {
      size_t old = slot_index(live[1], object_fields(live[3])[3 * i]);
      if (live[7] != V_NIL && object_fields(live[3])[3 * i + 2] == V_FALSE) {
        object_fields(live[4])[i] = object_fields(object_fields(live[7])[2])[i];
        continue;
      }
      object_fields(live[4])[i] = old != SIZE_MAX && object_fields(live[1])[old * 3 + 2] ==
                                                         object_fields(live[3])[i * 3 + 2]
                                      ? object_fields(live[2])[old]
                                      : V_UNBOUND;
    }
    object_fields(live[0])[1] = live[3];
    object_fields(live[0])[2] = live[4];
    installed = true;
  }
  for (size_t i = 0; ok && i < n; i++)
    if (object_fields(live[4])[i] == V_UNBOUND) {
      Value init = object_fields(live[3])[i * 3 + 1];
      if (init != V_FALSE) {
        Value value = rt_call_list(rt, init, V_EMPTY);
        ok = value != 0;
        if (ok) object_fields(live[4])[i] = value;
      }
    }
  if (ok) {
    live[6] = pair(rt, live[5], V_EMPTY);
    ok = live[6] != 0;
    if (ok) {
      live[6] = pair(rt, live[0], live[6]);
      ok = live[6] != 0;
    }
    uint32_t hook = rt_binding_global(rt, 0, "update-instance", 15, false);
    if (ok && hook != UINT32_MAX && rt->globals[hook] != V_UNBOUND)
      ok = rt_call_list(rt, rt->globals[hook], live[6]) != 0;
  }
  for (size_t i = 0; ok && i < n; i++)
    if (object_fields(live[4])[i] == V_UNBOUND) {
      fail(rt, "missing required slot after migration");
      ok = false;
    }
  if (!ok && installed) {
    object_fields(live[0])[1] = live[1];
    object_fields(live[0])[2] = live[2];
  }
  *instance = live[0];
  roots_pop(&rt->heap, &roots);
  return ok;
}

static bool current(Runtime* rt, Value* instance) { return migrate(rt, instance, V_NIL); }

Value rt_instance_call(Runtime* rt, Value instance, size_t first, size_t count) {
  if (count != 1 || !rt_kind(rt->stack[first], K_KEYWORD))
    return fail(rt, "instance call needs one slot keyword");
  Roots root;
  roots_push(&rt->heap, &root, &instance, 1);
  Value result = 0;
  if (current(rt, &instance)) {
    size_t index = slot_index(object_fields(instance)[1], rt->stack[first]);
    if (index == SIZE_MAX) fail(rt, "unknown instance slot");
    else {
      result = object_fields(object_fields(instance)[2])[index];
      if (result == V_UNBOUND) result = fail(rt, "uninitialized instance slot");
    }
  }
  roots_pop(&rt->heap, &root);
  return result;
}

Value rt_instance_copy(Runtime* rt, Value instance) {
  Value live[] = {instance, V_NIL};
  Roots roots;
  roots_push(&rt->heap, &roots, live, 2);
  if (!current(rt, &live[0])) {
    roots_pop(&rt->heap, &roots);
    return 0;
  }
  size_t n = slots(object_fields(live[0])[1]);
  live[1] = rt_alloc(rt, K_INSTANCE, 3, 0);
  bool ok = live[1] != 0;
  if (ok) {
    object_fields(live[1])[0] = object_fields(live[0])[0];
    object_fields(live[1])[1] = object_fields(live[0])[1];
    Value data = rt_alloc(rt, K_ARRAY_DATA, (uint32_t)n, 0);
    ok = data != 0;
    if (ok) {
      if (n)
        memcpy(object_fields(data), object_fields(object_fields(live[0])[2]), n * sizeof(Value));
      object_fields(live[1])[2] = data;
    }
  }
  Value result = ok ? live[1] : 0;
  roots_pop(&rt->heap, &roots);
  return result;
}

static Value define_class(Runtime* rt, size_t first, size_t count) {
  if (count != 5) return fail(rt, "invalid class declaration");
  Value name = rt->stack[first], parent = rt->stack[first + 1], abstract = rt->stack[first + 2],
        spec = rt->stack[first + 3];
  if (!rt_kind(name, K_SYMBOL) || !rt_kind(parent, K_CLASS) || !rt_kind(spec, K_ARRAY) ||
      (abstract != V_TRUE && abstract != V_FALSE))
    return fail(rt, "invalid class declaration");
  if (!((ClassInfo*)object_bytes(parent))->abstract)
    return fail(rt, "class parent must be abstract");
  size_t n = ((ArrayInfo*)object_bytes(spec))->length;
  if (n % 3 || (abstract == V_TRUE && n)) return fail(rt, "invalid class slots");
  Value* fields = n ? object_fields(object_fields(spec)[0]) : NULL;
  for (size_t i = 0; i < n; i += 3) {
    if (!rt_kind(fields[i], K_KEYWORD) ||
        (fields[i + 1] != V_FALSE && !rt_kind(fields[i + 1], K_CLOSURE)) ||
        (fields[i + 2] != V_FALSE && fields[i + 2] != V_TRUE) ||
        (fields[i + 2] == V_TRUE && fields[i + 1] == V_FALSE))
      return fail(rt, "invalid slot definition");
    for (size_t j = 0; j < i; j += 3)
      if (fields[j] == fields[i]) return fail(rt, "duplicate class slot");
  }
  uint32_t global = rt_global(rt, (char*)rt_bytes_data(name), rt_bytes_length(name));
  if (global == UINT32_MAX) return 0;
  Value old = rt->globals[global];
  bool reset = rt->stack[first + 4] == V_TRUE;
  if (old != V_UNBOUND && !reset) {
    if (!rt_kind(old, K_CLASS) || ((ClassInfo*)object_bytes(old))->builtin ||
        ((ClassInfo*)object_bytes(old))->abstract != (abstract == V_TRUE))
      return fail(rt, "incompatible class redefinition");
    if (rt_subtype(parent, old)) return fail(rt, "cyclic class ancestry");
  }
  bool same = false;
  if (old != V_UNBOUND && !reset) {
    Value layout = object_fields(old)[2];
    same = slots(layout) * 3 == n;
    for (size_t i = 0; same && i < n; i += 3)
      same = object_fields(layout)[i] == fields[i] && object_fields(layout)[i + 2] == fields[i + 2];
  }
  Value live[] = {old, V_NIL};
  Roots roots;
  roots_push(&rt->heap, &roots, live, 2);
  live[1] = same ? object_fields(live[0])[2] : rt_alloc(rt, K_LAYOUT, (uint32_t)n, 0);
  bool ok = live[1] != 0;
  if (ok && n)
    memcpy(object_fields(live[1]), object_fields(object_fields(rt->stack[first + 3])[0]),
           n * sizeof(Value));
  if (ok && (old == V_UNBOUND || reset)) {
    live[0] = rt_alloc(rt, K_CLASS, 3, sizeof(ClassInfo));
    ok = live[0] != 0;
    if (ok) *(ClassInfo*)object_bytes(live[0]) = (ClassInfo){abstract == V_TRUE, false};
  }
  if (ok) {
    object_fields(live[0])[0] = rt->stack[first];
    object_fields(live[0])[1] = rt->stack[first + 1];
    object_fields(live[0])[2] = live[1];
    rt->globals[global] = live[0];
    rt->global_defined[global] = 1;
  }
  Value result = ok ? live[0] : 0;
  roots_pop(&rt->heap, &roots);
  return result;
}

static Value property_operation(Runtime* rt, unsigned id, size_t first, size_t count) {
  size_t wanted = id == B_CLASS_MOUNT ? 1 : 2;
  if (count != wanted) return fail(rt, "wrong number of property operation arguments");
  Value live[] = {rt->stack[first], rt->stack[first + count - 1], V_NIL};
  Roots roots;
  roots_push(&rt->heap, &roots, live, 3);
  bool ok = true;
  if (!rt_kind(live[0], K_INSTANCE) || !rt_kind(live[1], K_INSTANCE)) {
    fail(rt, "property operations require instances");
    ok = false;
  }
  if (ok && object_fields(live[1])[1] != object_fields(object_fields(live[1])[0])[2]) {
    fail(rt, "outdated property description");
    ok = false;
  }
  if (ok && id == B_CLASS_UPDATE && object_fields(live[0])[0] != object_fields(live[1])[0]) {
    fail(rt, "property update requires the same class");
    ok = false;
  }
  if (ok && id == B_CLASS_MOUNT) {
    live[2] = rt_instance_copy(rt, live[1]);
    ok = live[2] != 0;
    size_t n = ok ? slots(object_fields(live[2])[1]) : 0;
    for (size_t i = 0; ok && i < n; i++) {
      Value layout = object_fields(live[2])[1];
      if (object_fields(layout)[i * 3 + 2] == V_TRUE) {
        Value value = rt_call_list(rt, object_fields(layout)[i * 3 + 1], V_EMPTY);
        ok = value != 0;
        if (ok) object_fields(object_fields(live[2])[2])[i] = value;
      }
    }
  } else if (ok) {
    bool stale = object_fields(live[0])[1] != object_fields(live[1])[1];
    if (stale) ok = migrate(rt, &live[0], live[1]);
    else {
      Value layout = object_fields(live[0])[1];
      for (size_t i = 0; i < slots(layout); i++)
        if (object_fields(layout)[3 * i + 2] == V_FALSE)
          object_fields(object_fields(live[0])[2])[i] = object_fields(object_fields(live[1])[2])[i];
    }
    if (ok) live[2] = live[0];
  }
  Value result = ok ? live[2] : 0;
  roots_pop(&rt->heap, &roots);
  return result;
}

Value rt_instance_builtin(Runtime* rt, unsigned id, size_t first, size_t count) {
  if (!rt_classes_init(rt)) return 0;
  if (id == B_CLASS_DEFINE) return define_class(rt, first, count);
  if (id == B_CLASS_MOUNT || id == B_CLASS_UPDATE) return property_operation(rt, id, first, count);
  if (id == B_INSTANCE_P) {
    if (count != 1) return fail(rt, "instance? takes one argument");
    return rt_kind(rt->stack[first], K_INSTANCE) ? V_TRUE : V_FALSE;
  }
  if (id == B_SLOT_SET) {
    if (count != 3 || !rt_kind(rt->stack[first], K_INSTANCE) ||
        !rt_kind(rt->stack[first + 1], K_KEYWORD))
      return fail(rt, "slot-set! needs an instance, keyword and value");
    Value instance = rt->stack[first];
    Roots root;
    roots_push(&rt->heap, &root, &instance, 1);
    Value result = 0;
    if (current(rt, &instance)) {
      size_t index = slot_index(object_fields(instance)[1], rt->stack[first + 1]);
      if (index == SIZE_MAX) fail(rt, "unknown instance slot");
      else result = object_fields(object_fields(instance)[2])[index] = rt->stack[first + 2];
    }
    roots_pop(&rt->heap, &root);
    return result;
  }
  if (!count || !rt_kind(rt->stack[first], K_CLASS) || !(count % 2))
    return fail(rt, "make needs a class and keyword/value pairs");
  Value class = rt->stack[first];
  ClassInfo* info = object_bytes(class);
  if (info->abstract || info->builtin) return fail(rt, "class cannot be instantiated");
  Value live[] = {class, object_fields(class)[2], V_NIL};
  Roots roots;
  roots_push(&rt->heap, &roots, live, 3);
  bool properties = id == B_CLASS_PROPERTIES;
  bool ok = true;
  size_t n = slots(live[1]);
  for (size_t i = 1; ok && i < count; i += 2) {
    Value key = rt->stack[first + i];
    size_t index = slot_index(live[1], key);
    if (!rt_kind(key, K_KEYWORD) || index == SIZE_MAX) {
      fail(rt, "unknown constructor slot");
      ok = false;
    } else if (properties && object_fields(live[1])[3 * index + 2] == V_TRUE) {
      fail(rt, "state slot cannot be a property argument");
      ok = false;
    }
  }
  if (ok) {
    live[2] = rt_alloc(rt, K_INSTANCE, 3, 0);
    ok = live[2] != 0;
  }
  if (ok) {
    object_fields(live[2])[0] = live[0];
    object_fields(live[2])[1] = live[1];
    Value data = rt_alloc(rt, K_ARRAY_DATA, (uint32_t)n, 0);
    ok = data != 0;
    if (ok) object_fields(live[2])[2] = data;
  }
  for (size_t i = 0; ok && i < n; i++) {
    if (properties && object_fields(live[1])[i * 3 + 2] == V_TRUE) {
      object_fields(object_fields(live[2])[2])[i] = V_NIL;
      continue;
    }
    Value key = object_fields(live[1])[i * 3], value = V_UNBOUND;
    for (size_t j = 1; j < count; j += 2)
      if (rt->stack[first + j] == key) value = rt->stack[first + j + 1];
    if (value == V_UNBOUND) {
      Value initializer = object_fields(live[1])[i * 3 + 1];
      if (initializer == V_FALSE) {
        fail(rt, "missing required slot");
        ok = false;
        break;
      }
      value = rt_call_list(rt, initializer, V_EMPTY);
      if (!value) {
        ok = false;
        break;
      }
    }
    object_fields(object_fields(live[2])[2])[i] = value;
  }
  Value result = ok ? live[2] : 0;
  roots_pop(&rt->heap, &roots);
  return result;
}
