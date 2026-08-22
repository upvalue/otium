/* Intentionally has no include guard.  A collector supplies three function
 * names and a direct slot operation, then may instantiate this walker for
 * evacuation, marking, pointer rewriting, or validation. */
#if !defined(OT_GC_TRACE_OBJECT) || !defined(OT_GC_TRACE_ROOTS) || !defined(OT_GC_TRACE_VM) ||     \
    !defined(OT_GC_TRACE_SLOT)
#error "define the OT_GC_TRACE_* macros before including ot-gc-trace.inc"
#endif

static void OT_GC_TRACE_OBJECT(ots* state, ot_obj* object) {
  otv value = ot_from_obj(object);
  switch (ot_object_type(value)) {
    case OBJ_FLOAT:
    case OBJ_BYTES:
    case OBJ_EXT:
    case OBJ_FREE: break;
    case OBJ_SYMBOL:
    case OBJ_KEYWORD: {
      ot_name_obj* name = (ot_name_obj*)object;
      OT_GC_TRACE_SLOT(state, &name->next);
      OT_GC_TRACE_SLOT(state, &name->cache_namespace);
      OT_GC_TRACE_SLOT(state, &name->cache_var);
      break;
    }
    case OBJ_STRING: {
      ot_string_obj* string = (ot_string_obj*)object;
      OT_GC_TRACE_SLOT(state, &string->bytes);
      break;
    }
    case OBJ_PAIR: {
      ot_pair_obj* pair = (ot_pair_obj*)object;
      OT_GC_TRACE_SLOT(state, &pair->car);
      OT_GC_TRACE_SLOT(state, &pair->cdr);
      break;
    }
    case OBJ_SLOTS: {
      ot_slots_obj* slots = (ot_slots_obj*)object;
      for (size_t i = 0; i < slots->capacity; i++) OT_GC_TRACE_SLOT(state, &slots->values[i]);
      break;
    }
    case OBJ_ARRAY: {
      ot_array_obj* array = (ot_array_obj*)object;
      OT_GC_TRACE_SLOT(state, &array->slots);
      break;
    }
    case OBJ_ENTRIES: {
      ot_entries_obj* entries = (ot_entries_obj*)object;
      for (size_t i = 0; i < entries->capacity; i++) {
        if (!entries->values[i].live) continue;
        OT_GC_TRACE_SLOT(state, &entries->values[i].key);
        OT_GC_TRACE_SLOT(state, &entries->values[i].value);
      }
      break;
    }
    case OBJ_TABLE: {
      ot_table_obj* table = (ot_table_obj*)object;
      OT_GC_TRACE_SLOT(state, &table->entries);
      OT_GC_TRACE_SLOT(state, &table->index);
      break;
    }
    case OBJ_BUFFER: {
      ot_buffer_obj* buffer = (ot_buffer_obj*)object;
      OT_GC_TRACE_SLOT(state, &buffer->bytes);
      break;
    }
    case OBJ_BINDING: {
      ot_binding_obj* binding = (ot_binding_obj*)object;
      OT_GC_TRACE_SLOT(state, &binding->name);
      OT_GC_TRACE_SLOT(state, &binding->value);
      OT_GC_TRACE_SLOT(state, &binding->next);
      break;
    }
    case OBJ_ENV: {
      ot_env_obj* env = (ot_env_obj*)object;
      OT_GC_TRACE_SLOT(state, &env->parent);
      OT_GC_TRACE_SLOT(state, &env->bindings);
      OT_GC_TRACE_SLOT(state, &env->namespace_value);
      break;
    }
    case OBJ_VAR: {
      ot_var_obj* var = (ot_var_obj*)object;
      OT_GC_TRACE_SLOT(state, &var->name);
      OT_GC_TRACE_SLOT(state, &var->value);
      OT_GC_TRACE_SLOT(state, &var->doc);
      OT_GC_TRACE_SLOT(state, &var->next);
      break;
    }
    case OBJ_ALIAS: {
      ot_alias_obj* alias = (ot_alias_obj*)object;
      OT_GC_TRACE_SLOT(state, &alias->name);
      OT_GC_TRACE_SLOT(state, &alias->value);
      OT_GC_TRACE_SLOT(state, &alias->next);
      break;
    }
    case OBJ_NAMESPACE: {
      ot_namespace_obj* space = (ot_namespace_obj*)object;
      OT_GC_TRACE_SLOT(state, &space->name);
      OT_GC_TRACE_SLOT(state, &space->vars);
      OT_GC_TRACE_SLOT(state, &space->var_index);
      OT_GC_TRACE_SLOT(state, &space->refers);
      OT_GC_TRACE_SLOT(state, &space->refer_index);
      OT_GC_TRACE_SLOT(state, &space->aliases);
      OT_GC_TRACE_SLOT(state, &space->next);
      break;
    }
    case OBJ_CODE: {
      ot_code_obj* code = (ot_code_obj*)object;
      OT_GC_TRACE_SLOT(state, &code->bytes);
      OT_GC_TRACE_SLOT(state, &code->constants);
      OT_GC_TRACE_SLOT(state, &code->params);
      OT_GC_TRACE_SLOT(state, &code->name);
      break;
    }
    case OBJ_FUNCTION: {
      ot_function_obj* function = (ot_function_obj*)object;
      OT_GC_TRACE_SLOT(state, &function->code);
      OT_GC_TRACE_SLOT(state, &function->env);
      OT_GC_TRACE_SLOT(state, &function->namespace_value);
      OT_GC_TRACE_SLOT(state, &function->name);
      break;
    }
    case OBJ_NAT: {
      ot_nat_obj* nat = (ot_nat_obj*)object;
      OT_GC_TRACE_SLOT(state, &nat->name);
      break;
    }
    case OBJ_MACRO: {
      ot_macro_obj* macro = (ot_macro_obj*)object;
      OT_GC_TRACE_SLOT(state, &macro->function);
      break;
    }
    case OBJ_PARAM: {
      ot_param_obj* param = (ot_param_obj*)object;
      OT_GC_TRACE_SLOT(state, &param->name);
      OT_GC_TRACE_SLOT(state, &param->value);
      break;
    }
    case OBJ_RESTART: {
      ot_restart_obj* restart = (ot_restart_obj*)object;
      OT_GC_TRACE_SLOT(state, &restart->name);
      OT_GC_TRACE_SLOT(state, &restart->description);
      break;
    }
  }
}

static void OT_GC_TRACE_VM(ots* state, ot_vm* vm) {
  OT_GC_TRACE_SLOT(state, &vm->current_namespace);
  OT_GC_TRACE_SLOT(state, &vm->condition);
  OT_GC_TRACE_SLOT(state, &vm->unwind_args);

  for (size_t i = 0; i < vm->vm_stack_count; i++) OT_GC_TRACE_SLOT(state, &vm->vm_stack[i]);
  for (size_t i = 0; i < vm->vm_frame_count; i++) {
    OT_GC_TRACE_SLOT(state, &vm->vm_frames[i].function);
    OT_GC_TRACE_SLOT(state, &vm->vm_frames[i].env);
  }

  for (ot_handler_frame* handler = vm->handlers; handler != NULL; handler = handler->prev) {
    OT_GC_TRACE_SLOT(state, &handler->pred);
    OT_GC_TRACE_SLOT(state, &handler->handler);
  }
  for (ot_restart_frame* frame = vm->restarts; frame != NULL; frame = frame->prev)
    for (size_t i = 0; i < frame->count; i++) {
      OT_GC_TRACE_SLOT(state, &frame->clauses[i].restart);
      OT_GC_TRACE_SLOT(state, &frame->clauses[i].params);
      OT_GC_TRACE_SLOT(state, &frame->clauses[i].body);
    }
  for (ot_param_frame* param = vm->params; param != NULL; param = param->prev) {
    OT_GC_TRACE_SLOT(state, &param->param);
    OT_GC_TRACE_SLOT(state, &param->value);
  }
}

static void OT_GC_TRACE_ROOTS(ots* state) {
  for (ot_frame* frame = state->frames; frame != NULL; frame = frame->prev)
    for (size_t i = 0; i < frame->count; i++) OT_GC_TRACE_SLOT(state, frame->slots[i]);
  for (ot_global_root* root = state->globals; root != NULL; root = root->next)
    OT_GC_TRACE_SLOT(state, root->slot);

  OT_GC_TRACE_SLOT(state, &state->symbols);
  OT_GC_TRACE_SLOT(state, &state->namespaces);
  OT_GC_TRACE_SLOT(state, &state->core_namespace);
  OT_GC_TRACE_SLOT(state, &state->expander);
  OT_GC_TRACE_SLOT(state, &state->type_parents);
  OT_GC_TRACE_VM(state, &state->vm);
}

#undef OT_GC_TRACE_OBJECT
#undef OT_GC_TRACE_ROOTS
#undef OT_GC_TRACE_VM
#undef OT_GC_TRACE_SLOT
