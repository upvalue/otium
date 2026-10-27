#include "internal.h"
#include <string.h>

static size_t slots(ot_value layout) {
  return ((oti_object_header*)layout)->fields / LAYOUT_STRIDE;
}

static size_t slot_index(ot_value layout, ot_value key) {
  for (size_t i = 0; i < slots(layout); i++)
    if (object_fields(layout)[LAYOUT_STRIDE * i + LAYOUT_KEY] == key) return i;
  return SIZE_MAX;
}

static bool migrate(ot_state* rt, ot_value* instance, ot_value properties) {
  oti_instance_obj* self = oti_as_instance(*instance);
  ot_value current = oti_as_class(self->type)->layout;
  if (self->layout == current) return true;
  ot_value live[] = {*instance, self->layout, self->data, current,
                     OT_NIL,    OT_NIL,       OT_NIL,     properties};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 8);
  size_t n = slots(live[3]);
  bool installed = false;
  live[4] = oti_alloc(rt, K_ARRAY_DATA, (uint32_t)n, 0);
  bool ok = live[4] != 0;
  if (ok) {
    live[5] = oti_table_new(rt);
    ok = live[5] != 0;
  }
  for (size_t i = 0; ok && i < slots(live[1]); i++) {
    ot_value key = object_fields(live[1])[LAYOUT_STRIDE * i + LAYOUT_KEY];
    ok = oti_table_set(rt, live[5], key, object_fields(live[2])[i]) != 0;
  }
  if (ok) {
    for (size_t i = 0; i < n; i++) {
      ot_value* slot = object_fields(live[3]) + LAYOUT_STRIDE * i;
      size_t old = slot_index(live[1], slot[LAYOUT_KEY]);
      if (live[7] != OT_NIL && slot[LAYOUT_PROPERTY] == OT_FALSE) {
        object_fields(live[4])[i] = object_fields(oti_as_instance(live[7])->data)[i];
        continue;
      }
      object_fields(live[4])[i] =
          old != SIZE_MAX && object_fields(live[1])[LAYOUT_STRIDE * old + LAYOUT_PROPERTY] ==
                                 slot[LAYOUT_PROPERTY]
              ? object_fields(live[2])[old]
              : V_UNBOUND;
    }
    oti_as_instance(live[0])->layout = live[3];
    oti_as_instance(live[0])->data = live[4];
    installed = true;
  }
  for (size_t i = 0; ok && i < n; i++)
    if (object_fields(live[4])[i] == V_UNBOUND) {
      ot_value init = object_fields(live[3])[LAYOUT_STRIDE * i + LAYOUT_INIT];
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
    oti_as_instance(live[0])->layout = live[1];
    oti_as_instance(live[0])->data = live[2];
  }
  *instance = live[0];
  oti_roots_pop(&rt->heap, &roots);
  return ok;
}

static bool migrate_to_current(ot_state* rt, ot_value* instance) {
  return migrate(rt, instance, OT_NIL);
}

ot_value oti_instance_call(ot_state* rt, ot_value instance, size_t first, size_t count) {
  if (count != 1 || !oti_kind(rt->stack[first], K_KEYWORD))
    return oti_fail(rt, "instance call needs one slot keyword");
  oti_roots root;
  oti_roots_push(&rt->heap, &root, &instance, 1);
  ot_value result = 0;
  if (migrate_to_current(rt, &instance)) {
    size_t index = slot_index(oti_as_instance(instance)->layout, rt->stack[first]);
    if (index == SIZE_MAX) oti_fail(rt, "unknown instance slot");
    else {
      result = object_fields(oti_as_instance(instance)->data)[index];
      if (result == V_UNBOUND) result = oti_fail(rt, "uninitialized instance slot");
    }
  }
  oti_roots_pop(&rt->heap, &root);
  return result;
}

ot_value oti_instance_copy(ot_state* rt, ot_value instance) {
  ot_value live[] = {instance, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 2);
  if (!migrate_to_current(rt, &live[0])) {
    oti_roots_pop(&rt->heap, &roots);
    return 0;
  }
  size_t n = slots(oti_as_instance(live[0])->layout);
  live[1] = oti_alloc(rt, K_INSTANCE, 3, OTI_BYTES(oti_instance_obj, 3));
  bool ok = live[1] != 0;
  if (ok) {
    oti_as_instance(live[1])->type = oti_as_instance(live[0])->type;
    oti_as_instance(live[1])->layout = oti_as_instance(live[0])->layout;
    ot_value data = oti_alloc(rt, K_ARRAY_DATA, (uint32_t)n, 0);
    ok = data != 0;
    if (ok) {
      if (n)
        memcpy(object_fields(data), object_fields(oti_as_instance(live[0])->data),
               n * sizeof(ot_value));
      oti_as_instance(live[1])->data = data;
    }
  }
  ot_value result = ok ? live[1] : 0;
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

static ot_value instance_define_class(ot_state* rt, size_t first, size_t count) {
  if (!oti_classes_init(rt)) return 0;
  if (count != 5) return oti_fail(rt, "invalid class declaration");
  ot_value name = rt->stack[first], parent = rt->stack[first + 1], abstract = rt->stack[first + 2],
           spec = rt->stack[first + 3];
  if (!oti_kind(name, K_SYMBOL) || !oti_kind(parent, K_CLASS) || !oti_kind(spec, K_ARRAY) ||
      (abstract != OT_TRUE && abstract != OT_FALSE))
    return oti_fail(rt, "invalid class declaration");
  if (!oti_as_class(parent)->abstract) return oti_fail(rt, "class parent must be abstract");
  size_t n = oti_as_array(spec)->length;
  if (n % LAYOUT_STRIDE || (abstract == OT_TRUE && n)) return oti_fail(rt, "invalid class slots");
  /* The spec is laid out like a K_LAYOUT. */
  ot_value* fields = n ? object_fields(oti_as_array(spec)->data) : NULL;
  for (size_t i = 0; i < n; i += LAYOUT_STRIDE) {
    ot_value key = fields[i + LAYOUT_KEY], init = fields[i + LAYOUT_INIT],
             property = fields[i + LAYOUT_PROPERTY];
    if (!oti_kind(key, K_KEYWORD) || (init != OT_FALSE && !oti_kind(init, K_CLOSURE)) ||
        (property != OT_FALSE && property != OT_TRUE) || (property == OT_TRUE && init == OT_FALSE))
      return oti_fail(rt, "invalid slot definition");
    for (size_t j = 0; j < i; j += LAYOUT_STRIDE)
      if (fields[j + LAYOUT_KEY] == key) return oti_fail(rt, "duplicate class slot");
  }
  uint32_t global = oti_global(rt, (char*)oti_bytes_data(name), oti_bytes_length(name));
  if (global == UINT32_MAX) return 0;
  ot_value old = rt->globals[global];
  bool reset = rt->stack[first + 4] == OT_TRUE;
  if (old != V_UNBOUND && !reset) {
    if (!oti_kind(old, K_CLASS) || oti_as_class(old)->builtin ||
        oti_as_class(old)->abstract != (abstract == OT_TRUE))
      return oti_fail(rt, "incompatible class redefinition");
    if (oti_subtype(parent, old)) return oti_fail(rt, "cyclic class ancestry");
  }
  bool same = false;
  if (old != V_UNBOUND && !reset) {
    ot_value* layout = object_fields(oti_as_class(old)->layout);
    same = slots(oti_as_class(old)->layout) * LAYOUT_STRIDE == n;
    for (size_t i = 0; same && i < n; i += LAYOUT_STRIDE)
      same = layout[i + LAYOUT_KEY] == fields[i + LAYOUT_KEY] &&
             layout[i + LAYOUT_PROPERTY] == fields[i + LAYOUT_PROPERTY];
  }
  ot_value live[] = {old, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 2);
  live[1] = same ? oti_as_class(live[0])->layout : oti_alloc(rt, K_LAYOUT, (uint32_t)n, 0);
  bool ok = live[1] != 0;
  if (ok && n)
    memcpy(object_fields(live[1]), object_fields(oti_as_array(rt->stack[first + 3])->data),
           n * sizeof(ot_value));
  if (ok && (old == V_UNBOUND || reset)) {
    live[0] = oti_alloc(rt, K_CLASS, 3, OTI_BYTES(oti_class_obj, 3));
    ok = live[0] != 0;
    if (ok) {
      oti_as_class(live[0])->abstract = abstract == OT_TRUE;
      oti_as_class(live[0])->builtin = false;
    }
  }
  if (ok) {
    oti_class_obj* c = oti_as_class(live[0]);
    c->name = rt->stack[first];
    c->parent = rt->stack[first + 1];
    c->layout = live[1];
    rt->globals[global] = live[0];
    rt->global_info[global].defined = 1;
  }
  ot_value result = ok ? live[0] : 0;
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

/* live[0] is the target instance and live[1] the property description. */
static bool instance_property_check(ot_state* rt, ot_value* live, bool same_class) {
  if (!oti_kind(live[0], K_INSTANCE) || !oti_kind(live[1], K_INSTANCE))
    return oti_error(rt, "property operations require instances");
  if (oti_as_instance(live[1])->layout != oti_as_class(oti_as_instance(live[1])->type)->layout)
    return oti_error(rt, "outdated property description");
  if (same_class && oti_as_instance(live[0])->type != oti_as_instance(live[1])->type)
    return oti_error(rt, "property update requires the same class");
  return true;
}

static ot_value instance_class_mount(ot_state* rt, size_t first, size_t count) {
  if (!oti_classes_init(rt)) return 0;
  if (count != 1) return oti_fail(rt, "wrong number of property operation arguments");
  ot_value live[] = {rt->stack[first], rt->stack[first], OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 3);
  bool ok = instance_property_check(rt, live, false);
  if (ok) {
    live[2] = oti_instance_copy(rt, live[1]);
    ok = live[2] != 0;
    size_t n = ok ? slots(oti_as_instance(live[2])->layout) : 0;
    for (size_t i = 0; ok && i < n; i++) {
      ot_value layout = oti_as_instance(live[2])->layout;
      if (object_fields(layout)[LAYOUT_STRIDE * i + LAYOUT_PROPERTY] == OT_TRUE) {
        ot_value init = object_fields(layout)[LAYOUT_STRIDE * i + LAYOUT_INIT];
        ot_value value = oti_call_list(rt, init, OT_EMPTY);
        ok = value != 0;
        if (ok) object_fields(oti_as_instance(live[2])->data)[i] = value;
      }
    }
  }
  ot_value result = ok ? live[2] : 0;
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

static ot_value instance_class_update(ot_state* rt, size_t first, size_t count) {
  if (!oti_classes_init(rt)) return 0;
  if (count != 2) return oti_fail(rt, "wrong number of property operation arguments");
  ot_value live[] = {rt->stack[first], rt->stack[first + 1]};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 2);
  bool ok = instance_property_check(rt, live, true);
  if (ok) {
    bool stale = oti_as_instance(live[0])->layout != oti_as_instance(live[1])->layout;
    if (stale) ok = migrate(rt, &live[0], live[1]);
    else {
      ot_value layout = oti_as_instance(live[0])->layout;
      ot_value* to = object_fields(oti_as_instance(live[0])->data);
      ot_value* from = object_fields(oti_as_instance(live[1])->data);
      for (size_t i = 0; i < slots(layout); i++)
        if (object_fields(layout)[LAYOUT_STRIDE * i + LAYOUT_PROPERTY] == OT_FALSE) to[i] = from[i];
    }
  }
  ot_value result = ok ? live[0] : 0;
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

static ot_value instance_instance_p(ot_state* rt, size_t first, size_t count) {
  if (!oti_classes_init(rt)) return 0;
  if (count != 1) return oti_fail(rt, "instance? takes one argument");
  return oti_kind(rt->stack[first], K_INSTANCE) ? OT_TRUE : OT_FALSE;
}

static ot_value instance_slot_set(ot_state* rt, size_t first, size_t count) {
  if (!oti_classes_init(rt)) return 0;
  if (count != 3 || !oti_kind(rt->stack[first], K_INSTANCE) ||
      !oti_kind(rt->stack[first + 1], K_KEYWORD))
    return oti_fail(rt, "slot-set! needs an instance, keyword and value");
  ot_value instance = rt->stack[first];
  oti_roots root;
  oti_roots_push(&rt->heap, &root, &instance, 1);
  ot_value result = 0;
  if (migrate_to_current(rt, &instance)) {
    size_t index = slot_index(oti_as_instance(instance)->layout, rt->stack[first + 1]);
    if (index == SIZE_MAX) oti_fail(rt, "unknown instance slot");
    else result = object_fields(oti_as_instance(instance)->data)[index] = rt->stack[first + 2];
  }
  oti_roots_pop(&rt->heap, &root);
  return result;
}

/* make, and class-properties when properties is set: property slots start as
 * nil and may not be passed. */
static ot_value instance_make(ot_state* rt, size_t first, size_t count, bool properties) {
  if (!count || !oti_kind(rt->stack[first], K_CLASS) || !(count % 2))
    return oti_fail(rt, "make needs a class and keyword/value pairs");
  ot_value class = rt->stack[first];
  if (oti_as_class(class)->abstract || oti_as_class(class)->builtin)
    return oti_fail(rt, "class cannot be instantiated");
  ot_value live[] = {class, oti_as_class(class)->layout, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 3);
  bool ok = true;
  size_t n = slots(live[1]);
  for (size_t i = 1; ok && i < count; i += 2) {
    ot_value key = rt->stack[first + i];
    size_t index = slot_index(live[1], key);
    if (!oti_kind(key, K_KEYWORD) || index == SIZE_MAX) {
      oti_fail(rt, "unknown constructor slot");
      ok = false;
    } else if (properties &&
               object_fields(live[1])[LAYOUT_STRIDE * index + LAYOUT_PROPERTY] == OT_TRUE) {
      oti_fail(rt, "state slot cannot be a property argument");
      ok = false;
    }
  }
  if (ok) {
    live[2] = oti_alloc(rt, K_INSTANCE, 3, OTI_BYTES(oti_instance_obj, 3));
    ok = live[2] != 0;
  }
  if (ok) {
    oti_as_instance(live[2])->type = live[0];
    oti_as_instance(live[2])->layout = live[1];
    ot_value data = oti_alloc(rt, K_ARRAY_DATA, (uint32_t)n, 0);
    ok = data != 0;
    if (ok) oti_as_instance(live[2])->data = data;
  }
  for (size_t i = 0; ok && i < n; i++) {
    if (properties && object_fields(live[1])[LAYOUT_STRIDE * i + LAYOUT_PROPERTY] == OT_TRUE) {
      object_fields(oti_as_instance(live[2])->data)[i] = OT_NIL;
      continue;
    }
    ot_value key = object_fields(live[1])[LAYOUT_STRIDE * i + LAYOUT_KEY], value = V_UNBOUND;
    for (size_t j = 1; j < count; j += 2)
      if (rt->stack[first + j] == key) value = rt->stack[first + j + 1];
    if (value == V_UNBOUND) {
      ot_value initializer = object_fields(live[1])[LAYOUT_STRIDE * i + LAYOUT_INIT];
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
    object_fields(oti_as_instance(live[2])->data)[i] = value;
  }
  ot_value result = ok ? live[2] : 0;
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

static ot_value instance_make_builtin(ot_state* rt, size_t first, size_t count) {
  if (!oti_classes_init(rt)) return 0;
  return instance_make(rt, first, count, false);
}

static ot_value instance_class_properties(ot_state* rt, size_t first, size_t count) {
  if (!oti_classes_init(rt)) return 0;
  return instance_make(rt, first, count, true);
}

/* Every instance builtin sets up the builtin classes first. */
const oti_builtin_def oti_instance_builtins[] = {
    {B_CLASS_DEFINE, "%define-class", instance_define_class},
    {B_MAKE, "make", instance_make_builtin},
    {B_INSTANCE_P, "instance?", instance_instance_p},
    {B_SLOT_SET, "slot-set!", instance_slot_set},
    {B_CLASS_PROPERTIES, "class-properties", instance_class_properties},
    {B_CLASS_MOUNT, "class-mount", instance_class_mount},
    {B_CLASS_UPDATE, "class-update-properties!", instance_class_update},
    {0, NULL, NULL},
};
