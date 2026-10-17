/* Effective method chains and per-generic dispatch caches. */
static otv method_qualifier(otv method) { return as_pair(as_pair(method)->cdr)->car; }
static otv method_function(otv method) { return as_pair(as_pair(method)->cdr)->cdr; }
static otv method_field(otv context, size_t index) {
  return as_slots(as_array(context)->slots)->values[index];
}
static void invalidate_generic(ots* state, otv value) {
  ot_generic_obj* generic = as_generic(value);
  ot_store(state, generic, &generic->cache, ot_nil);
  ot_store(state, generic, &generic->positions, ot_nil);
  generic->epoch = state->class_epoch;
}
static otv effective_method(ots* state, otv generic, otv methods, otv* args, size_t argc);

static otv prepare_method_frame(ots* state, otv context, otv* args, size_t argc) {
  if (method_field(context, 3) == ot_nil) return ot_nil;
  OT_FRAME_SCOPED(state, &context);
  otv arguments = make_descriptor(state, args, argc);
  otv fields[] = {method_field(context, 3), arguments, method_field(context, 2), method_field(context, 0)};
  return make_descriptor(state, fields, 4);
}

static otv invoke_method_chain(ots* state, otv context, otv* args, int argc) {
  otv generic = method_field(context, 0), function = method_field(context, 1);
  otv remaining = method_field(context, 2), next = method_field(context, 3);
  otv saved = state->next_method, frame = ot_nil;
  OT_FRAME_SCOPED(state, &context, &generic, &function, &remaining, &next, &saved, &frame);
  if (function == ot_false) {
    buf message = {0};
    buf_cstr(&message, remaining == ot_null ? "no applicable method for " : "ambiguous methods for ");
    render_value(state, &message, generic, false, 0);
    for (otv p = remaining; p != ot_null; p = as_pair(p)->cdr) {
      buf_cstr(&message, " ");
      render_value(state, &message, as_pair(as_pair(p)->car)->car, false, 0);
    }
    buf_byte(&message, '\0');
    otv result = generic_error(state, remaining == ot_null ? "missing-method" : "ambiguous-method",
                               message.failed ? "method selection failed" : message.data);
    buf_free(&message);
    return result;
  }
  frame = prepare_method_frame(state, context, args, (size_t)argc);
  state->next_method = frame;
  otv result = apply_value(state, function, args, (size_t)argc, false);
  state->next_method = saved;
  return result;
}

static otv effective_method(ots* state, otv generic, otv methods, otv* args, size_t argc) {
  otv applicable = ot_null, best = ot_nil, remaining = ot_null, next = ot_nil;
  otv cursor = methods, function = ot_false;
  OT_FRAME_SCOPED(state, &generic, &methods, &applicable, &best, &remaining, &next, &cursor, &function);
  bool around = false;
  while (cursor != ot_null) {
    otv method = as_pair(cursor)->car;
    if (signature_applies(state, as_pair(method)->car, args, argc)) {
      if (method_qualifier(method) == ot_true) around = true;
      applicable = ot_cons(state, method, applicable);
    }
    cursor = as_pair(cursor)->cdr;
  }
  size_t count = 0;
  for (otv p = applicable; p != ot_null; p = as_pair(p)->cdr) {
    otv method = as_pair(p)->car;
    if ((method_qualifier(method) == ot_true) != around) continue;
    bool dominated = false;
    for (otv q = applicable; q != ot_null; q = as_pair(q)->cdr) {
      otv other = as_pair(q)->car;
      if (method_qualifier(other) == method_qualifier(method) &&
          signature_dominates(as_pair(other)->car, as_pair(method)->car)) {
        dominated = true;
        break;
      }
    }
    if (!dominated) { best = method; count++; }
  }
  if (count == 1) {
    function = method_function(best);
    cursor = applicable;
    while (cursor != ot_null) {
      otv method = as_pair(cursor)->car;
      if (method != best) remaining = ot_cons(state, method, remaining);
      cursor = as_pair(cursor)->cdr;
    }
    if (remaining != ot_null) next = effective_method(state, generic, remaining, args, argc);
  } else remaining = applicable;
  otv fields[] = {generic, function, remaining, next};
  otv context = make_descriptor(state, fields, 4);
  return ot_make_native(state, "effective method", invoke_method_chain, context);
}

static otv generic_select(ots* state, otv generic, otv* args, size_t argc) {
  if (argc != as_generic(generic)->arity)
    return generic_error(state, "wrong-arity", "generic called with incorrect arity");
  OT_FRAME_SCOPED(state, &generic);
  if (as_generic(generic)->epoch != state->class_epoch) invalidate_generic(state, generic);
  if (as_generic(generic)->cache == ot_nil) {
    otv cache = ot_nil, positions = ot_nil;
    OT_FRAME_SCOPED(state, &cache, &positions);
    cache = ot_table_new(state, 8);
    positions = ot_array_new(state, argc);
    for (size_t i = 0; i < argc; i++) {
      bool specialized = false;
      for (otv p = as_generic(generic)->methods; p != ot_null; p = as_pair(p)->cdr)
        if (method_field(as_pair(as_pair(p)->car)->car, i) != ot_false) { specialized = true; break; }
      if (specialized) array_push(state, &positions, ot_make_int((intptr_t)i));
    }
    ot_generic_obj* target = as_generic(generic);
    ot_store(state, target, &target->cache, cache);
    ot_store(state, target, &target->positions, positions);
  }
  otv cache = as_generic(generic)->cache, positions = as_generic(generic)->positions;
  otv key = ot_false, found = ot_nil;
  OT_FRAME_SCOPED(state, &cache, &positions, &key, &found);
  size_t count = as_array(positions)->length;
  for (size_t i = 0; i < (count == 0 ? 1 : count); i++) {
    key = count == 0 ? state->object_class : dispatch_type(state, args[ot_get_int(method_field(positions, i))]);
    found = ot_table_get(state, cache, key, ot_nil);
    if (i + 1 >= count) {
      if (found == ot_nil) {
        found = effective_method(state, generic, as_generic(generic)->methods, args, argc);
        if (found == OT_UNWIND) return found;
        if (ot_table_put(state, cache, key, found) == OT_UNWIND) return OT_UNWIND;
      }
      return found;
    }
    if (found == ot_nil) {
      found = ot_table_new(state, 4);
      if (ot_table_put(state, cache, key, found) == OT_UNWIND) return OT_UNWIND;
    }
    cache = found;
  }
  return OT_UNWIND;
}

static otv nat_next_method_p(ots* state, otv* args, int argc) {
  (void)args;
  if (need_arity(state, "next-method?", argc, 0, 0) == OT_UNWIND) return OT_UNWIND;
  return state->next_method != ot_nil && method_field(state->next_method, 0) != ot_nil ? ot_true : ot_false;
}

static otv nat_call_next_method(ots* state, otv* args, int argc) {
  if (state->next_method == ot_nil || method_field(state->next_method, 0) == ot_nil)
    return generic_error(state, "no-next-method", "no next method");
  otv frame = state->next_method, next = method_field(frame, 0);
  otv generic = method_field(frame, 3);
  OT_FRAME_SCOPED(state, &frame, &next, &generic);
  if (argc != 0) {
    if ((size_t)argc != as_generic(generic)->arity)
      return generic_error(state, "wrong-arity", "incorrect next method arity");
    next = effective_method(state, generic, method_field(frame, 2), args, (size_t)argc);
    return apply_value(state, next, args, (size_t)argc, false);
  }
  size_t count = as_array(method_field(frame, 1))->length;
  otv values[count == 0 ? 1 : count];
  for (size_t i = 0; i < count; i++) values[i] = method_field(method_field(frame, 1), i);
  return apply_value(state, next, values, count, false);
}

static otv default_update_instance(ots* state, otv context, otv* args, int argc) {
  (void)state; (void)context; (void)args; (void)argc;
  return ot_nil;
}
