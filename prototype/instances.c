#include "internal.h"
#include <string.h>

static size_t slots(ot_value layout) { return ((oti_object_header*)layout)->fields / 3; }

static size_t slot_index(ot_value layout, ot_value key) {
  for (size_t i = 0; i < slots(layout); i++)
    if (object_fields(layout)[3 * i] == key) return i;
  return SIZE_MAX;
}

static bool migrate(ot_runtime* rt, ot_value* instance, ot_value properties) {
  if (object_fields(*instance)[1] == object_fields(object_fields(*instance)[0])[2]) return true;
  ot_value live[] = {*instance,
                  object_fields(*instance)[1],
                  object_fields(*instance)[2],
                  object_fields(object_fields(*instance)[0])[2],
                  OT_NIL,
                  OT_NIL,
                  OT_NIL,
                  properties};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 8);
  size_t n = slots(live[3]);
  bool installed = false;
  live[4] = oti_alloc(rt, K_ARRAY_DATA, (uint32_t)n, 0);
  bool ok = live[4] != 0;
  if (ok) {
    live[5] = oti_table_builtin(rt, B_TABLE, 0, 0);
    ok = live[5] != 0;
  }
  for (size_t i = 0; ok && i < slots(live[1]); i++) {
    ok = oti_table_set(rt, live[5], object_fields(live[1])[3 * i], object_fields(live[2])[i]) != 0;
  }
  if (ok) {
    for (size_t i = 0; i < n; i++) {
      size_t old = slot_index(live[1], object_fields(live[3])[3 * i]);
      if (live[7] != OT_NIL && object_fields(live[3])[3 * i + 2] == OT_FALSE) {
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
      ot_value init = object_fields(live[3])[i * 3 + 1];
      if (init != OT_FALSE) {
        ot_value value = oti_call_list(rt, init, OT_EMPTY);
        ok = value != 0;
        if (ok) object_fields(live[4])[i] = value;
      }
    }
  if (ok) {
    live[6] = oti_cons(rt, live[5], OT_EMPTY);
    ok = live[6] != 0;
    if (ok) {
      live[6] = oti_cons(rt, live[0], live[6]);
      ok = live[6] != 0;
    }
    uint32_t hook = oti_binding_global(rt, 0, "update-instance", 15, false);
    if (ok && hook != UINT32_MAX && rt->globals[hook] != V_UNBOUND)
      ok = oti_call_list(rt, rt->globals[hook], live[6]) != 0;
  }
  for (size_t i = 0; ok && i < n; i++)
    if (object_fields(live[4])[i] == V_UNBOUND) {
      oti_fail(rt, "missing required slot after migration");
      ok = false;
    }
  if (!ok && installed) {
    object_fields(live[0])[1] = live[1];
    object_fields(live[0])[2] = live[2];
  }
  *instance = live[0];
  oti_roots_pop(&rt->heap, &roots);
  return ok;
}

static bool migrate_to_current(ot_runtime* rt, ot_value* instance) { return migrate(rt, instance, OT_NIL); }

ot_value oti_instance_call(ot_runtime* rt, ot_value instance, size_t first, size_t count) {
  if (count != 1 || !oti_kind(rt->stack[first], K_KEYWORD))
    return oti_fail(rt, "instance call needs one slot keyword");
  oti_roots root;
  oti_roots_push(&rt->heap, &root, &instance, 1);
  ot_value result = 0;
  if (migrate_to_current(rt, &instance)) {
    size_t index = slot_index(object_fields(instance)[1], rt->stack[first]);
    if (index == SIZE_MAX) oti_fail(rt, "unknown instance slot");
    else {
      result = object_fields(object_fields(instance)[2])[index];
      if (result == V_UNBOUND) result = oti_fail(rt, "uninitialized instance slot");
    }
  }
  oti_roots_pop(&rt->heap, &root);
  return result;
}

ot_value oti_instance_copy(ot_runtime* rt, ot_value instance) {
  ot_value live[] = {instance, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 2);
  if (!migrate_to_current(rt, &live[0])) {
    oti_roots_pop(&rt->heap, &roots);
    return 0;
  }
  size_t n = slots(object_fields(live[0])[1]);
  live[1] = oti_alloc(rt, K_INSTANCE, 3, 0);
  bool ok = live[1] != 0;
  if (ok) {
    object_fields(live[1])[0] = object_fields(live[0])[0];
    object_fields(live[1])[1] = object_fields(live[0])[1];
    ot_value data = oti_alloc(rt, K_ARRAY_DATA, (uint32_t)n, 0);
    ok = data != 0;
    if (ok) {
      if (n)
        memcpy(object_fields(data), object_fields(object_fields(live[0])[2]), n * sizeof(ot_value));
      object_fields(live[1])[2] = data;
    }
  }
  ot_value result = ok ? live[1] : 0;
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

static ot_value define_class(ot_runtime* rt, size_t first, size_t count) {
  if (count != 5) return oti_fail(rt, "invalid class declaration");
  ot_value name = rt->stack[first], parent = rt->stack[first + 1], abstract = rt->stack[first + 2],
        spec = rt->stack[first + 3];
  if (!oti_kind(name, K_SYMBOL) || !oti_kind(parent, K_CLASS) || !oti_kind(spec, K_ARRAY) ||
      (abstract != OT_TRUE && abstract != OT_FALSE))
    return oti_fail(rt, "invalid class declaration");
  if (!((oti_class_info*)object_bytes(parent))->abstract)
    return oti_fail(rt, "class parent must be abstract");
  size_t n = ((oti_array_info*)object_bytes(spec))->length;
  if (n % 3 || (abstract == OT_TRUE && n)) return oti_fail(rt, "invalid class slots");
  ot_value* fields = n ? object_fields(object_fields(spec)[0]) : NULL;
  for (size_t i = 0; i < n; i += 3) {
    if (!oti_kind(fields[i], K_KEYWORD) ||
        (fields[i + 1] != OT_FALSE && !oti_kind(fields[i + 1], K_CLOSURE)) ||
        (fields[i + 2] != OT_FALSE && fields[i + 2] != OT_TRUE) ||
        (fields[i + 2] == OT_TRUE && fields[i + 1] == OT_FALSE))
      return oti_fail(rt, "invalid slot definition");
    for (size_t j = 0; j < i; j += 3)
      if (fields[j] == fields[i]) return oti_fail(rt, "duplicate class slot");
  }
  uint32_t global = oti_global(rt, (char*)oti_bytes_data(name), oti_bytes_length(name));
  if (global == UINT32_MAX) return 0;
  ot_value old = rt->globals[global];
  bool reset = rt->stack[first + 4] == OT_TRUE;
  if (old != V_UNBOUND && !reset) {
    if (!oti_kind(old, K_CLASS) || ((oti_class_info*)object_bytes(old))->builtin ||
        ((oti_class_info*)object_bytes(old))->abstract != (abstract == OT_TRUE))
      return oti_fail(rt, "incompatible class redefinition");
    if (oti_subtype(parent, old)) return oti_fail(rt, "cyclic class ancestry");
  }
  bool same = false;
  if (old != V_UNBOUND && !reset) {
    ot_value layout = object_fields(old)[2];
    same = slots(layout) * 3 == n;
    for (size_t i = 0; same && i < n; i += 3)
      same = object_fields(layout)[i] == fields[i] && object_fields(layout)[i + 2] == fields[i + 2];
  }
  ot_value live[] = {old, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 2);
  live[1] = same ? object_fields(live[0])[2] : oti_alloc(rt, K_LAYOUT, (uint32_t)n, 0);
  bool ok = live[1] != 0;
  if (ok && n)
    memcpy(object_fields(live[1]), object_fields(object_fields(rt->stack[first + 3])[0]),
           n * sizeof(ot_value));
  if (ok && (old == V_UNBOUND || reset)) {
    live[0] = oti_alloc(rt, K_CLASS, 3, sizeof(oti_class_info));
    ok = live[0] != 0;
    if (ok) *(oti_class_info*)object_bytes(live[0]) = (oti_class_info){abstract == OT_TRUE, false};
  }
  if (ok) {
    object_fields(live[0])[0] = rt->stack[first];
    object_fields(live[0])[1] = rt->stack[first + 1];
    object_fields(live[0])[2] = live[1];
    rt->globals[global] = live[0];
    rt->global_info[global].defined = 1;
  }
  ot_value result = ok ? live[0] : 0;
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

static ot_value property_operation(ot_runtime* rt, unsigned id, size_t first, size_t count) {
  size_t wanted = id == B_CLASS_MOUNT ? 1 : 2;
  if (count != wanted) return oti_fail(rt, "wrong number of property operation arguments");
  ot_value live[] = {rt->stack[first], rt->stack[first + count - 1], OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 3);
  bool ok = true;
  if (!oti_kind(live[0], K_INSTANCE) || !oti_kind(live[1], K_INSTANCE)) {
    oti_fail(rt, "property operations require instances");
    ok = false;
  }
  if (ok && object_fields(live[1])[1] != object_fields(object_fields(live[1])[0])[2]) {
    oti_fail(rt, "outdated property description");
    ok = false;
  }
  if (ok && id == B_CLASS_UPDATE && object_fields(live[0])[0] != object_fields(live[1])[0]) {
    oti_fail(rt, "property update requires the same class");
    ok = false;
  }
  if (ok && id == B_CLASS_MOUNT) {
    live[2] = oti_instance_copy(rt, live[1]);
    ok = live[2] != 0;
    size_t n = ok ? slots(object_fields(live[2])[1]) : 0;
    for (size_t i = 0; ok && i < n; i++) {
      ot_value layout = object_fields(live[2])[1];
      if (object_fields(layout)[i * 3 + 2] == OT_TRUE) {
        ot_value value = oti_call_list(rt, object_fields(layout)[i * 3 + 1], OT_EMPTY);
        ok = value != 0;
        if (ok) object_fields(object_fields(live[2])[2])[i] = value;
      }
    }
  } else if (ok) {
    bool stale = object_fields(live[0])[1] != object_fields(live[1])[1];
    if (stale) ok = migrate(rt, &live[0], live[1]);
    else {
      ot_value layout = object_fields(live[0])[1];
      for (size_t i = 0; i < slots(layout); i++)
        if (object_fields(layout)[3 * i + 2] == OT_FALSE)
          object_fields(object_fields(live[0])[2])[i] = object_fields(object_fields(live[1])[2])[i];
    }
    if (ok) live[2] = live[0];
  }
  ot_value result = ok ? live[2] : 0;
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

ot_value oti_instance_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count) {
  if (!oti_classes_init(rt)) return 0;
  if (id == B_CLASS_DEFINE) return define_class(rt, first, count);
  if (id == B_CLASS_MOUNT || id == B_CLASS_UPDATE) return property_operation(rt, id, first, count);
  if (id == B_INSTANCE_P) {
    if (count != 1) return oti_fail(rt, "instance? takes one argument");
    return oti_kind(rt->stack[first], K_INSTANCE) ? OT_TRUE : OT_FALSE;
  }
  if (id == B_SLOT_SET) {
    if (count != 3 || !oti_kind(rt->stack[first], K_INSTANCE) ||
        !oti_kind(rt->stack[first + 1], K_KEYWORD))
      return oti_fail(rt, "slot-set! needs an instance, keyword and value");
    ot_value instance = rt->stack[first];
    oti_roots root;
    oti_roots_push(&rt->heap, &root, &instance, 1);
    ot_value result = 0;
    if (migrate_to_current(rt, &instance)) {
      size_t index = slot_index(object_fields(instance)[1], rt->stack[first + 1]);
      if (index == SIZE_MAX) oti_fail(rt, "unknown instance slot");
      else result = object_fields(object_fields(instance)[2])[index] = rt->stack[first + 2];
    }
    oti_roots_pop(&rt->heap, &root);
    return result;
  }
  if (!count || !oti_kind(rt->stack[first], K_CLASS) || !(count % 2))
    return oti_fail(rt, "make needs a class and keyword/value pairs");
  ot_value class = rt->stack[first];
  oti_class_info* info = object_bytes(class);
  if (info->abstract || info->builtin) return oti_fail(rt, "class cannot be instantiated");
  ot_value live[] = {class, object_fields(class)[2], OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 3);
  bool properties = id == B_CLASS_PROPERTIES;
  bool ok = true;
  size_t n = slots(live[1]);
  for (size_t i = 1; ok && i < count; i += 2) {
    ot_value key = rt->stack[first + i];
    size_t index = slot_index(live[1], key);
    if (!oti_kind(key, K_KEYWORD) || index == SIZE_MAX) {
      oti_fail(rt, "unknown constructor slot");
      ok = false;
    } else if (properties && object_fields(live[1])[3 * index + 2] == OT_TRUE) {
      oti_fail(rt, "state slot cannot be a property argument");
      ok = false;
    }
  }
  if (ok) {
    live[2] = oti_alloc(rt, K_INSTANCE, 3, 0);
    ok = live[2] != 0;
  }
  if (ok) {
    object_fields(live[2])[0] = live[0];
    object_fields(live[2])[1] = live[1];
    ot_value data = oti_alloc(rt, K_ARRAY_DATA, (uint32_t)n, 0);
    ok = data != 0;
    if (ok) object_fields(live[2])[2] = data;
  }
  for (size_t i = 0; ok && i < n; i++) {
    if (properties && object_fields(live[1])[i * 3 + 2] == OT_TRUE) {
      object_fields(object_fields(live[2])[2])[i] = OT_NIL;
      continue;
    }
    ot_value key = object_fields(live[1])[i * 3], value = V_UNBOUND;
    for (size_t j = 1; j < count; j += 2)
      if (rt->stack[first + j] == key) value = rt->stack[first + j + 1];
    if (value == V_UNBOUND) {
      ot_value initializer = object_fields(live[1])[i * 3 + 1];
      if (initializer == OT_FALSE) {
        oti_fail(rt, "missing required slot");
        ok = false;
        break;
      }
      value = oti_call_list(rt, initializer, OT_EMPTY);
      if (!value) {
        ok = false;
        break;
      }
    }
    object_fields(object_fields(live[2])[2])[i] = value;
  }
  ot_value result = ok ? live[2] : 0;
  oti_roots_pop(&rt->heap, &roots);
  return result;
}
