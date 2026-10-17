/* Class storage and declarations. Included after the generic registry helpers. */
static otv array_item(otv array, size_t index) {
  return as_slots(as_array(array)->slots)->values[index];
}

static bool class_subtype(otv child, otv parent) {
  while (is_type(child, OBJ_DISPATCH_TYPE)) {
    if (child == parent) return true;
    child = as_dispatch_type(child)->parent;
  }
  return false;
}

static otv class_slot_error(ots* state, const char* code, const char* text, otv type, otv key) {
  buf message = {0};
  buf_cstr(&message, text);
  buf_cstr(&message, ": ");
  render_value(state, &message, type, false, 0);
  buf_cstr(&message, " ");
  render_value(state, &message, key, false, 0);
  buf_byte(&message, '\0');
  otv result = generic_error(state, code, message.failed ? text : message.data);
  buf_free(&message);
  return result;
}

static otv allocate_instance(ots* state, otv layout) {
  OT_FRAME_SCOPED(state, &layout);
  size_t count = as_array(as_layout(layout)->keys)->length;
  ot_instance_obj* object = must_alloc(state, sizeof(*object) + count * sizeof(otv), OBJ_INSTANCE);
  ot_store(state, object, &object->layout, layout);
  object->stable_id = ++state->next_stable_id;
  for (size_t i = 0; i < count; i++) ot_store(state, object, &object->values[i], OT_UNDEFINED);
  return ot_from_obj(object);
}

static otv check_instance_slots(ots* state, otv object) {
  otv layout = as_instance(object)->layout;
  for (size_t i = 0; i < as_array(as_layout(layout)->keys)->length; i++)
    if (as_instance(object)->values[i] == OT_UNDEFINED)
      return class_slot_error(state, "uninitialized-slot", "required slot has no value",
                              as_layout(layout)->type, array_item(as_layout(layout)->keys, i));
  return object;
}

static otv instance_current_properties(ots* state, otv original, otv properties) {
  if (!is_type(original, OBJ_INSTANCE))
    return generic_error(state, "invalid-slot-operation", "expected instance");
  otv object = instance_payload(original);
  otv old_layout = as_instance(object)->layout;
  otv type = as_layout(old_layout)->type;
  otv layout = as_dispatch_type(type)->layout;
  if (old_layout == layout) return object;
  otv fresh = ot_nil, old_slots = ot_nil, result = ot_nil;
  OT_FRAME_SCOPED(state, &original, &object, &old_layout, &type, &layout, &fresh, &old_slots,
                  &result, &properties);
  old_slots = ot_table_new(state, as_array(as_layout(old_layout)->keys)->length);
  for (size_t i = 0; i < as_array(as_layout(old_layout)->keys)->length; i++) {
    result = ot_table_put(state, old_slots, array_item(as_layout(old_layout)->keys, i),
                          as_instance(object)->values[i]);
    if (result == OT_UNWIND) return result;
  }
  fresh = allocate_instance(state, layout);
  /* Publish the new layout before defaults and update-instance can access this object. */
  ot_instance_obj* previous = as_instance(object);
  ot_store(state, previous, &previous->layout, fresh);
  for (size_t i = 0; i < as_array(as_layout(layout)->keys)->length; i++) {
    otv index = ot_table_get(state, as_layout(old_layout)->indices,
                             array_item(as_layout(layout)->keys, i), OT_UNDEFINED);
    if (properties != ot_nil && array_item(as_layout(layout)->states, i) == ot_false)
      result = as_instance(properties)->values[i];
    else if (index != OT_UNDEFINED &&
             array_item(as_layout(old_layout)->states, ot_get_int(index)) ==
                 array_item(as_layout(layout)->states, i))
      result = as_instance(object)->values[ot_get_int(index)];
    else {
      otv initializer = array_item(as_layout(layout)->defaults, i);
      result = initializer == ot_false ? OT_UNDEFINED : apply_value(state, initializer, NULL, 0, false);
      if (result == OT_UNWIND) goto failed;
    }
    ot_instance_obj* target = as_instance(fresh);
    ot_store(state, target, &target->values[i], result);
  }
  if (state->update_instance != ot_nil) {
    otv args[] = {original, old_slots};
    result = apply_value(state, state->update_instance, args, 2, false);
    if (result == OT_UNWIND) goto failed;
  }
  result = check_instance_slots(state, fresh);
  if (result == OT_UNWIND) goto failed;
  for (size_t i = 0; i < as_array(as_layout(old_layout)->keys)->length; i++) {
    ot_instance_obj* stale = as_instance(object);
    ot_store(state, stale, &stale->values[i], ot_nil);
  }
  return fresh;
failed:;
  ot_instance_obj* rollback = as_instance(object);
  ot_store(state, rollback, &rollback->layout, old_layout);
  return OT_UNWIND;
}

static otv instance_current(ots* state, otv original) {
  return instance_current_properties(state, original, ot_nil);
}

static otv instance_index(ots* state, otv object, otv key) {
  otv layout = as_instance(object)->layout;
  otv index = is_type(key, OBJ_KEYWORD)
                 ? ot_table_get(state, as_layout(layout)->indices, key, OT_UNDEFINED) : OT_UNDEFINED;
  if (index == OT_UNDEFINED)
    return class_slot_error(state, "missing-slot", "class has no such slot", as_layout(layout)->type, key);
  return index;
}

static otv instance_read(ots* state, otv object, otv key) {
  OT_FRAME_SCOPED(state, &object, &key);
  object = instance_current(state, object);
  if (object == OT_UNWIND) return object;
  otv index = instance_index(state, object, key);
  if (index == OT_UNWIND) return index;
  otv value = as_instance(object)->values[ot_get_int(index)];
  if (value == OT_UNDEFINED)
    return class_slot_error(state, "uninitialized-slot", "slot has no value",
                            as_layout(as_instance(object)->layout)->type, key);
  return value;
}

static otv instance_write(ots* state, otv object, otv key, otv value) {
  OT_FRAME_SCOPED(state, &object, &key, &value);
  object = instance_current(state, object);
  if (object == OT_UNWIND) return object;
  otv index = instance_index(state, object, key);
  if (index == OT_UNWIND) return index;
  ot_instance_obj* target = as_instance(object);
  ot_store(state, target, &target->values[ot_get_int(index)], value);
  return value;
}

static otv instance_copy(ots* state, otv object) {
  OT_FRAME_SCOPED(state, &object);
  object = instance_current(state, object);
  if (object == OT_UNWIND) return object;
  otv copy = allocate_instance(state, as_instance(object)->layout);
  size_t count = as_array(as_layout(as_instance(object)->layout)->keys)->length;
  ot_instance_obj* target = as_instance(copy);
  for (size_t i = 0; i < count; i++)
    ot_store(state, target, &target->values[i], as_instance(object)->values[i]);
  return copy;
}

static otv make_instance_properties(ots* state, otv* args, int argc, bool properties_only) {
  if (argc < 1 || !is_type(args[0], OBJ_DISPATCH_TYPE) ||
      as_dispatch_type(args[0])->builtin || as_dispatch_type(args[0])->abstract)
    return generic_error(state, "invalid-class", "make expects a concrete user class");
  if (argc % 2 != 1)
    return generic_error(state, "invalid-slot-operation", "make expects keyword/value pairs");
  otv layout = as_dispatch_type(args[0])->layout;
  otv object = ot_nil, result = ot_nil;
  OT_FRAME_SCOPED(state, &layout, &object, &result);
  object = allocate_instance(state, layout);
  for (int i = 1; i < argc; i += 2) {
    otv index = instance_index(state, object, args[i]);
    if (index == OT_UNWIND) return index;
    if (properties_only && array_item(as_layout(layout)->states, ot_get_int(index)) == ot_true)
      return generic_error(state, "invalid-slot-operation", "state slot cannot be supplied as a property");
    ot_instance_obj* target = as_instance(object);
    ot_store(state, target, &target->values[ot_get_int(index)], args[i + 1]);
  }
  for (size_t i = 0; i < as_array(as_layout(layout)->keys)->length; i++) {
    if (properties_only && array_item(as_layout(layout)->states, i) == ot_true) {
      ot_instance_obj* target = as_instance(object);
      ot_store(state, target, &target->values[i], ot_nil);
      continue;
    }
    if (as_instance(object)->values[i] != OT_UNDEFINED) continue;
    otv initializer = array_item(as_layout(layout)->defaults, i);
    if (initializer == ot_false) continue;
    result = apply_value(state, initializer, NULL, 0, false);
    if (result == OT_UNWIND) return result;
    ot_instance_obj* target = as_instance(object);
    ot_store(state, target, &target->values[i], result);
  }
  return check_instance_slots(state, object);
}

static otv nat_make_instance(ots* state, otv* args, int argc) {
  return make_instance_properties(state, args, argc, false);
}

static otv nat_class_properties(ots* state, otv* args, int argc) {
  return make_instance_properties(state, args, argc, true);
}

/* Descriptions must be rebuilt after class redefinition, never lazily migrated. */
static otv current_properties(ots* state, otv object) {
  if (!is_type(object, OBJ_INSTANCE))
    return generic_error(state, "invalid-slot-operation", "expected property instance");
  object = instance_payload(object);
  otv layout = as_instance(object)->layout;
  if (layout != as_dispatch_type(as_layout(layout)->type)->layout)
    return generic_error(state, "stale-properties", "rebuild description after class redefinition");
  return object;
}

static otv nat_class_mount(ots* state, otv* args, int argc) {
  if (need_arity(state, "class-mount", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  otv source = current_properties(state, args[0]);
  if (source == OT_UNWIND) return source;
  otv object = ot_nil, layout = as_instance(source)->layout, result = ot_nil;
  OT_FRAME_SCOPED(state, &source, &object, &layout, &result);
  object = allocate_instance(state, layout);
  for (size_t i = 0; i < as_array(as_layout(layout)->keys)->length; i++) {
    result = as_instance(source)->values[i];
    if (array_item(as_layout(layout)->states, i) == ot_true)
      result = apply_value(state, array_item(as_layout(layout)->defaults, i), NULL, 0, false);
    if (result == OT_UNWIND) return result;
    ot_instance_obj* target = as_instance(object);
    ot_store(state, target, &target->values[i], result);
  }
  return check_instance_slots(state, object);
}

static otv nat_class_update_properties(ots* state, otv* args, int argc) {
  if (need_arity(state, "class-update-properties!", argc, 2, 2) == OT_UNWIND) return OT_UNWIND;
  otv source = current_properties(state, args[1]), object = args[0];
  if (source == OT_UNWIND) return source;
  if (!is_type(object, OBJ_INSTANCE) ||
      as_layout(as_instance(instance_payload(object))->layout)->type !=
          as_layout(as_instance(source)->layout)->type)
    return generic_error(state, "invalid-class", "properties must have the same class as the instance");
  OT_FRAME_SCOPED(state, &source, &object);
  object = instance_current_properties(state, object, source);
  if (object == OT_UNWIND) return object;
  otv layout = as_instance(object)->layout;
  for (size_t i = 0; i < as_array(as_layout(layout)->keys)->length; i++) {
    if (array_item(as_layout(layout)->states, i) == ot_false) {
      ot_instance_obj* target = as_instance(object);
      ot_store(state, target, &target->values[i], as_instance(source)->values[i]);
    }
  }
  return args[0];
}

static otv nat_class_subtype_p(ots* state, otv* args, int argc) {
  if (need_arity(state, "class-subtype?", argc, 2, 2) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_DISPATCH_TYPE) || !is_type(args[1], OBJ_DISPATCH_TYPE))
    return generic_error(state, "invalid-class", "class-subtype? expects classes");
  return class_subtype(args[0], args[1]) ? ot_true : ot_false;
}

static otv nat_instance_p(ots* state, otv* args, int argc) {
  if (need_arity(state, "instance?", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  return is_type(args[0], OBJ_INSTANCE) ? ot_true : ot_false;
}

static otv nat_slot_set(ots* state, otv* args, int argc) {
  if (need_arity(state, "slot-set!", argc, 3, 3) == OT_UNWIND) return OT_UNWIND;
  return instance_write(state, args[0], args[1], args[2]);
}

static bool class_option(otv value, const char* name) {
  return is_type(value, OBJ_KEYWORD) && as_name(value)->length == strlen(name) &&
         memcmp(as_name(value)->bytes, name, strlen(name)) == 0;
}

static bool local_class_name(otv name) {
  return is_type(name, OBJ_SYMBOL) && !as_name(name)->special_form &&
         !memchr(as_name(name)->bytes, '/', as_name(name)->length);
}

static bool compile_class_declaration(ots* state, bytecode_compiler* compiler, otv form) {
  otv kind = as_pair(form)->car, name = as_pair(as_pair(form)->cdr)->car;
  otv rest = list_tail(form, 2), parent = ot_false, specs = ot_nil;
  OT_FRAME_SCOPED(state, &form, &kind, &name, &rest, &parent, &specs);
  if (!local_class_name(name)) goto invalid;
  if (is_type(rest, OBJ_PAIR) && class_option(as_pair(rest)->car, "is")) {
    if (!list_nth(rest, 1, &parent) || !is_type(parent, OBJ_SYMBOL)) goto invalid;
    rest = list_tail(rest, 2);
  }
  if (symbol_is(kind, "defabstract") && rest != ot_null) goto invalid;
  specs = ot_array_new(state, list_length(rest));
  while (rest != ot_null) {
    otv slot = as_pair(rest)->car, options = ot_null, key = ot_nil;
    otv init = ot_false, accessor = ot_false, state_slot = ot_false;
    OT_FRAME_SCOPED(state, &slot, &options, &key, &init, &accessor, &state_slot);
    if (is_type(slot, OBJ_PAIR)) {
      options = as_pair(slot)->cdr;
      slot = as_pair(slot)->car;
    }
    if (!is_type(slot, OBJ_SYMBOL) || !proper_list(options) || list_length(options) % 2 != 0)
      goto invalid;
    key = coerce_name(state, slot, true, "defclass");
    for (size_t i = 0; i < as_array(specs)->length; i++)
      if (array_item(array_item(specs, i), 0) == key) goto invalid;
    bool seen_init = false, seen_accessor = false, seen_state = false;
    while (options != ot_null) {
      otv option = as_pair(options)->car;
      otv value = as_pair(as_pair(options)->cdr)->car;
      OT_FRAME_SCOPED(state, &option, &value);
      if (class_option(option, "init") && !seen_init) {
        seen_init = true;
        otv fn = ot_nil, expanded = ot_nil;
        OT_FRAME_SCOPED(state, &fn, &expanded);
        fn = ot_cons(state, value, ot_null);
        fn = ot_cons(state, ot_null, fn);
        otv fn_name = ot_intern(state, "fn", 2, false);
        fn = ot_cons(state, fn_name, fn);
        expanded = apply_value(state, state->expander, &fn, 1, false);
        if (expanded == OT_UNWIND) return false;
        init = compile_function_code(state, ot_null, list_tail(expanded, 2), ot_nil,
                                     state->vm.current_namespace, slot);
        if (init == OT_UNWIND) return false;
      } else if (class_option(option, "accessor") && !seen_accessor && local_class_name(value)) {
        seen_accessor = true;
        accessor = value;
      } else if (class_option(option, "state") && !seen_state &&
                 (value == ot_true || value == ot_false)) {
        seen_state = true;
        state_slot = value;
      } else goto invalid;
      options = list_tail(options, 2);
    }
    if (state_slot == ot_true && !seen_init) goto invalid;
    otv fields[] = {key, init, accessor, state_slot};
    otv spec = make_descriptor(state, fields, 4);
    array_push(state, &specs, spec);
    rest = as_pair(rest)->cdr;
  }
  otv fields[] = {kind, name, parent, specs};
  otv descriptor = make_descriptor(state, fields, 4);
  bytecode_emit_constant(state, compiler, BC_GENERIC_DECL, descriptor);
  return !compiler->bytes.failed;
invalid:
  generic_error(state, "invalid-class-definition", "invalid class name, parent, slot, or slot options");
  return false;
}

static otv new_generic(ots* state, otv owner, otv name, size_t arity) {
  OT_FRAME_SCOPED(state, &owner, &name);
  ot_generic_obj* generic = must_alloc(state, sizeof(*generic), OBJ_GENERIC);
  ot_store(state, generic, &generic->owner, owner);
  ot_store(state, generic, &generic->name, name);
  ot_store(state, generic, &generic->methods, ot_null);
  ot_store(state, generic, &generic->cache, ot_nil);
  ot_store(state, generic, &generic->positions, ot_nil);
  generic->epoch = state->class_epoch;
  generic->arity = arity;
  generic->stable_id = ++state->next_stable_id;
  return ot_from_obj(generic);
}

static otv slot_accessor(ots* state, otv key, otv* args, int argc) {
  if (need_arity(state, "slot accessor", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  return instance_read(state, args[0], key);
}

static otv execute_class_declaration(ots* state, otv declaration) {
  otv kind = array_item(declaration, 0), name = array_item(declaration, 1);
  otv parent = array_item(declaration, 2), specs = array_item(declaration, 3);
  otv space = state->vm.current_namespace;
  otv owner = ((ot_namespace_obj*)ot_as_obj(space))->name;
  otv type = ot_nil, existing = ot_nil, layout = ot_nil;
  otv keys = ot_nil, defaults = ot_nil, states = ot_nil, indices = ot_nil;
  OT_FRAME_SCOPED(state, &declaration, &kind, &name, &parent, &specs, &space, &owner,
                  &type, &existing, &layout, &keys, &defaults, &states, &indices);
  bool abstract = symbol_is(kind, "defabstract");
  bool reset = symbol_is(kind, "reset-class!");
  existing = namespace_var(state, space, name, false);
  otv referred = namespace_var(state, space, name, true);
  if (symbol_is(owner, "otium.generic") || (existing == ot_nil && referred != ot_nil) ||
      (reset && existing == ot_nil))
    return generic_error(state, "binding-conflict", "class declaration conflicts with binding");
  if (existing != ot_nil && !reset) {
    type = ((ot_var_obj*)ot_as_obj(existing))->value;
    if (!is_type(type, OBJ_DISPATCH_TYPE) || as_dispatch_type(type)->owner != owner ||
        as_dispatch_type(type)->name != name || as_dispatch_type(type)->abstract != abstract)
      return generic_error(state, "binding-conflict", "incompatible class declaration");
  }
  if (parent == ot_false) parent = state->object_class;
  else {
    otv var = resolve_var(state, space, parent, false);
    parent = is_type(var, OBJ_VAR) ? ((ot_var_obj*)ot_as_obj(var))->value : ot_nil;
  }
  if (!is_type(parent, OBJ_DISPATCH_TYPE) || !as_dispatch_type(parent)->abstract ||
      (type != ot_nil && class_subtype(parent, type)))
    return generic_error(state, "invalid-class-definition", "expected acyclic abstract parent");
  for (size_t i = 0; i < as_array(specs)->length; i++) {
    otv accessor = array_item(array_item(specs, i), 2);
    if (accessor == ot_false) continue;
    otv var = namespace_var(state, space, accessor, false);
    otv imported = namespace_var(state, space, accessor, true);
    if (accessor == name || (var == ot_nil && imported != ot_nil))
      return generic_error(state, "binding-conflict", "accessor conflicts with binding");
    if (var != ot_nil) {
      otv value = ((ot_var_obj*)ot_as_obj(var))->value;
      if (!is_type(value, OBJ_GENERIC) || as_generic(value)->arity != 1 ||
          as_generic(value)->owner != owner || as_generic(value)->name != accessor)
        return generic_error(state, "binding-conflict", "accessor conflicts with binding");
    }
  }
  if (type == ot_nil) type = new_dispatch_type(state, owner, name, false);
  size_t count = as_array(specs)->length;
  keys = ot_array_new(state, count);
  defaults = ot_array_new(state, count);
  states = ot_array_new(state, count);
  indices = ot_table_new(state, count);
  for (size_t i = 0; i < count; i++) {
    otv spec = array_item(specs, i), init = array_item(spec, 1);
    OT_FRAME_SCOPED(state, &spec, &init);
    array_push(state, &keys, array_item(spec, 0));
    if (init != ot_false) init = make_bytecode_function(state, init, ot_nil, space, name);
    array_push(state, &defaults, init);
    array_push(state, &states, array_item(spec, 3));
    otv result = ot_table_put(state, indices, array_item(spec, 0), ot_make_int((intptr_t)i));
    if (result == OT_UNWIND) return result;
  }
  ot_layout_obj* made = must_alloc(state, sizeof(*made), OBJ_LAYOUT);
  ot_store(state, made, &made->type, type);
  ot_store(state, made, &made->keys, keys);
  ot_store(state, made, &made->defaults, defaults);
  ot_store(state, made, &made->states, states);
  ot_store(state, made, &made->indices, indices);
  layout = ot_from_obj(made);
  ot_dispatch_type_obj* descriptor = as_dispatch_type(type);
  descriptor->abstract = abstract;
  ot_store(state, descriptor, &descriptor->parent, parent);
  ot_store(state, descriptor, &descriptor->layout, layout);
  state->class_epoch++;
  define_var(state, space, name, type, ot_nil, false);
  for (size_t i = 0; i < count; i++) {
    otv accessor = array_item(array_item(specs, i), 2);
    if (accessor == ot_false) continue;
    otv generic = ot_nil, function = ot_nil, signature = ot_nil;
    OT_FRAME_SCOPED(state, &accessor, &generic, &function, &signature);
    otv var = namespace_var(state, space, accessor, false);
    generic = var != ot_nil ? ((ot_var_obj*)ot_as_obj(var))->value : new_generic(state, owner, accessor, 1);
    function = ot_make_native(state, "slot accessor", slot_accessor, array_item(array_item(specs, i), 0));
    signature = ot_array_new(state, 1);
    array_push(state, &signature, type);
    register_method(state, generic, signature, function, ot_false);
    define_var(state, space, accessor, generic, ot_nil, false);
  }
  return ot_nil;
}

static otv cached_instance_index(ots* state, otv cache, otv object, otv key) {
  otv layout = as_instance(object)->layout;
  if (as_slots(cache)->values[0] == layout && as_slots(cache)->values[1] == key)
    return as_slots(cache)->values[2];
  otv index = instance_index(state, object, key);
  if (index == OT_UNWIND) return index;
  ot_slots_obj* slots = as_slots(cache);
  ot_store(state, slots, &slots->values[0], layout);
  ot_store(state, slots, &slots->values[1], key);
  ot_store(state, slots, &slots->values[2], index);
  return index;
}

static otv cached_read_call(ots* state, otv cache, otv* args, int argc) {
  if (need_arity(state, "cached slot read", argc, 2, 2) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_INSTANCE)) return apply_value(state, args[0], args + 1, 1, false);
  OT_FRAME_SCOPED(state, &cache);
  otv object = instance_current(state, args[0]);
  if (object == OT_UNWIND) return object;
  otv index = cached_instance_index(state, cache, object, args[1]);
  if (index == OT_UNWIND) return index;
  otv value = as_instance(object)->values[ot_get_int(index)];
  if (value == OT_UNDEFINED)
    return class_slot_error(state, "uninitialized-slot", "slot has no value",
                            as_layout(as_instance(object)->layout)->type, args[1]);
  return value;
}

static otv cached_write_call(ots* state, otv cache, otv* args, int argc) {
  if (need_arity(state, "cached slot assignment", argc, 4, 4) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_NAT) || ((ot_nat_obj*)ot_as_obj(args[0]))->function != nat_set_applied ||
      !is_type(args[1], OBJ_INSTANCE)) return apply_value(state, args[0], args + 1, 3, false);
  OT_FRAME_SCOPED(state, &cache);
  otv object = instance_current(state, args[1]);
  if (object == OT_UNWIND) return object;
  otv index = cached_instance_index(state, cache, object, args[2]);
  if (index == OT_UNWIND) return index;
  ot_instance_obj* target = as_instance(object);
  ot_store(state, target, &target->values[ot_get_int(index)], args[3]);
  return args[3];
}
