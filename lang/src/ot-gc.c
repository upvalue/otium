#define OT_INTERNAL
#include "otium.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static size_t align_object(size_t size) { return (size + 7u) & ~(size_t)7u; }

static uintptr_t make_header(ot_obj_type type, size_t size) {
  return ((uintptr_t)size << 8u) | ((uintptr_t)type << 1u);
}

void ot_frame_push(ots* state, ot_frame* frame, otv** slots, size_t count) {
  frame->prev = state->frames;
  frame->state = state;
  frame->count = count;
  frame->slots = slots;
  state->frames = frame;
}

void ot_frame_pop(ots* state, ot_frame* frame) {
  if (state->frames != frame) {
    fputs("otium: root frame pop out of order\n", stderr);
    abort();
  }
  state->frames = frame->prev;
  frame->state = NULL;
}

void ot_frame_cleanup(ot_frame* frame) {
  if (frame->state != NULL) ot_frame_pop(frame->state, frame);
}

void ot_global_add(ots* state, otv* slot) {
  ot_global_root* root = ot_host_alloc(sizeof(*root));
  if (root == NULL) abort();
  root->next = state->globals;
  root->slot = slot;
  state->globals = root;
}

static bool in_from_space(const ots* state, const void* pointer) {
  const unsigned char* byte = pointer;
  return byte >= state->from_space && byte < state->from_space + state->capacity;
}

void ot_gc_trace_value(ots* state, otv* slot) {
  otv value = *slot;
  if (!ot_is_ptr(value)) return;
  ot_obj* old = ot_as_obj(value);
  if (!in_from_space(state, old)) return;
  if ((old->header & 1u) != 0) {
    *slot = old->header & ~(uintptr_t)1u;
    return;
  }
  size_t size = (size_t)(old->header >> 8u);
  if (state->alloc + size > state->to_space + state->capacity) {
    fprintf(stderr,
            "otium: live heap exceeds semispace during collection %" PRIu64
            " (copy %zu bytes, header %#" PRIxPTR ")\n",
            state->stats.collections + 1, size, old->header);
    abort();
  }
  ot_obj* copy = (ot_obj*)state->alloc;
  memcpy(copy, old, size);
  state->alloc += size;
  old->header = (uintptr_t)copy | 1u;
  *slot = ot_from_obj(copy);
  state->stats.copied_bytes += size;
}

static void trace_object(ots* state, ot_obj* object) {
  otv value = ot_from_obj(object);
  switch (ot_object_type(value)) {
    case OBJ_FLOAT:
    case OBJ_BYTES: break;
    case OBJ_SYMBOL:
    case OBJ_KEYWORD: {
      ot_name_obj* name = (ot_name_obj*)object;
      ot_gc_trace_value(state, &name->next);
      ot_gc_trace_value(state, &name->cache_namespace);
      ot_gc_trace_value(state, &name->cache_var);
      break;
    }
    case OBJ_STRING: {
      ot_string_obj* string = (ot_string_obj*)object;
      ot_gc_trace_value(state, &string->bytes);
      break;
    }
    case OBJ_PAIR: {
      ot_pair_obj* pair = (ot_pair_obj*)object;
      ot_gc_trace_value(state, &pair->car);
      ot_gc_trace_value(state, &pair->cdr);
      break;
    }
    case OBJ_SLOTS: {
      ot_slots_obj* slots = (ot_slots_obj*)object;
      for (size_t i = 0; i < slots->capacity; i++) ot_gc_trace_value(state, &slots->values[i]);
      break;
    }
    case OBJ_ARRAY: {
      ot_array_obj* array = (ot_array_obj*)object;
      ot_gc_trace_value(state, &array->slots);
      break;
    }
    case OBJ_ENTRIES: {
      ot_entries_obj* entries = (ot_entries_obj*)object;
      for (size_t i = 0; i < entries->capacity; i++) {
        if (!entries->values[i].live) continue;
        ot_gc_trace_value(state, &entries->values[i].key);
        ot_gc_trace_value(state, &entries->values[i].value);
      }
      break;
    }
    case OBJ_TABLE: {
      ot_table_obj* table = (ot_table_obj*)object;
      ot_gc_trace_value(state, &table->entries);
      ot_gc_trace_value(state, &table->index);
      break;
    }
    case OBJ_BUFFER: {
      ot_buffer_obj* buffer = (ot_buffer_obj*)object;
      ot_gc_trace_value(state, &buffer->bytes);
      break;
    }
    case OBJ_BINDING: {
      ot_binding_obj* binding = (ot_binding_obj*)object;
      ot_gc_trace_value(state, &binding->name);
      ot_gc_trace_value(state, &binding->value);
      ot_gc_trace_value(state, &binding->next);
      break;
    }
    case OBJ_ENV: {
      ot_env_obj* env = (ot_env_obj*)object;
      ot_gc_trace_value(state, &env->parent);
      ot_gc_trace_value(state, &env->bindings);
      ot_gc_trace_value(state, &env->namespace_value);
      break;
    }
    case OBJ_VAR: {
      ot_var_obj* var = (ot_var_obj*)object;
      ot_gc_trace_value(state, &var->name);
      ot_gc_trace_value(state, &var->value);
      ot_gc_trace_value(state, &var->doc);
      ot_gc_trace_value(state, &var->next);
      break;
    }
    case OBJ_ALIAS: {
      ot_alias_obj* alias = (ot_alias_obj*)object;
      ot_gc_trace_value(state, &alias->name);
      ot_gc_trace_value(state, &alias->value);
      ot_gc_trace_value(state, &alias->next);
      break;
    }
    case OBJ_NAMESPACE: {
      ot_namespace_obj* space = (ot_namespace_obj*)object;
      ot_gc_trace_value(state, &space->name);
      ot_gc_trace_value(state, &space->vars);
      ot_gc_trace_value(state, &space->var_index);
      ot_gc_trace_value(state, &space->refers);
      ot_gc_trace_value(state, &space->refer_index);
      ot_gc_trace_value(state, &space->aliases);
      ot_gc_trace_value(state, &space->next);
      break;
    }
    case OBJ_CODE: {
      ot_code_obj* code = (ot_code_obj*)object;
      ot_gc_trace_value(state, &code->bytes);
      ot_gc_trace_value(state, &code->constants);
      ot_gc_trace_value(state, &code->params);
      ot_gc_trace_value(state, &code->name);
      break;
    }
    case OBJ_FUNCTION: {
      ot_function_obj* function = (ot_function_obj*)object;
      ot_gc_trace_value(state, &function->code);
      ot_gc_trace_value(state, &function->env);
      ot_gc_trace_value(state, &function->namespace_value);
      ot_gc_trace_value(state, &function->name);
      break;
    }
    case OBJ_NAT: {
      ot_nat_obj* nat = (ot_nat_obj*)object;
      ot_gc_trace_value(state, &nat->name);
      break;
    }
    case OBJ_MACRO: {
      ot_macro_obj* macro = (ot_macro_obj*)object;
      ot_gc_trace_value(state, &macro->function);
      break;
    }
    case OBJ_PARAM: {
      ot_param_obj* param = (ot_param_obj*)object;
      ot_gc_trace_value(state, &param->name);
      ot_gc_trace_value(state, &param->value);
      break;
    }
    case OBJ_RESTART: {
      ot_restart_obj* restart = (ot_restart_obj*)object;
      ot_gc_trace_value(state, &restart->name);
      ot_gc_trace_value(state, &restart->description);
      break;
    }
    case OBJ_EXT: break;
  }
}

static void trace_vm(ots* state, ot_vm* vm) {
  ot_gc_trace_value(state, &vm->current_namespace);
  ot_gc_trace_value(state, &vm->condition);
  ot_gc_trace_value(state, &vm->unwind_args);

  for (size_t i = 0; i < vm->vm_stack_count; i++) ot_gc_trace_value(state, &vm->vm_stack[i]);
  for (size_t i = 0; i < vm->vm_frame_count; i++) {
    ot_gc_trace_value(state, &vm->vm_frames[i].function);
    ot_gc_trace_value(state, &vm->vm_frames[i].env);
  }

  for (ot_handler_frame* handler = vm->handlers; handler != NULL; handler = handler->prev) {
    ot_gc_trace_value(state, &handler->pred);
    ot_gc_trace_value(state, &handler->handler);
  }
  for (ot_restart_frame* frame = vm->restarts; frame != NULL; frame = frame->prev)
    for (size_t i = 0; i < frame->count; i++) {
      ot_gc_trace_value(state, &frame->clauses[i].restart);
      ot_gc_trace_value(state, &frame->clauses[i].params);
      ot_gc_trace_value(state, &frame->clauses[i].body);
    }
  for (ot_param_frame* param = vm->params; param != NULL; param = param->prev) {
    ot_gc_trace_value(state, &param->param);
    ot_gc_trace_value(state, &param->value);
  }
}

static void trace_roots(ots* state) {
  for (ot_frame* frame = state->frames; frame != NULL; frame = frame->prev)
    for (size_t i = 0; i < frame->count; i++) ot_gc_trace_value(state, frame->slots[i]);
  for (ot_global_root* root = state->globals; root != NULL; root = root->next)
    ot_gc_trace_value(state, root->slot);

  ot_gc_trace_value(state, &state->symbols);
  ot_gc_trace_value(state, &state->namespaces);
  ot_gc_trace_value(state, &state->core_namespace);
  ot_gc_trace_value(state, &state->expander);
  ot_gc_trace_value(state, &state->type_parents);
  trace_vm(state, &state->vm);
}

static void finish_exts(ots* state, otv old_exts) {
  otv live = ot_nil;
  while (ot_is_ptr(old_exts)) {
    ot_ext_obj* ext = (ot_ext_obj*)ot_as_obj(old_exts);
    otv next = ext->next;
    if ((ext->header & 1u) != 0) {
      ot_ext_obj* copy = (ot_ext_obj*)(ext->header & ~(uintptr_t)1u);
      copy->next = live;
      live = ot_from_obj(copy);
    } else if (!ext->released && ext->pointer_payload && ext->payload.pointer != NULL) {
      if (ext->type > 0 && ext->type <= state->ext_type_count) {
        ot_ext_finalizer finalizer = state->ext_types[ext->type - 1].finalizer;
        if (finalizer != NULL) finalizer(state, ext->payload.pointer);
      }
    }
    old_exts = next;
  }
  state->exts = live;
}

#ifdef OT_GC_VALIDATE
static void validate_value(ots* state, otv value, const char* owner) {
  if (!ot_is_ptr(value)) return;
  unsigned char* pointer = (unsigned char*)ot_as_obj(value);
  if (pointer < state->from_space || pointer >= state->from_space + state->capacity) {
    fprintf(stderr, "otium: stale GC value in %s after collection %" PRIu64 "\n", owner,
            state->stats.collections + 1);
    abort();
  }
}

static void validate_vm(ots* state, const ot_vm* vm) {
  validate_value(state, vm->current_namespace, "VM namespace");
  validate_value(state, vm->condition, "VM condition");
  validate_value(state, vm->unwind_args, "VM unwind args");
  for (size_t i = 0; i < vm->vm_stack_count; i++)
    validate_value(state, vm->vm_stack[i], "VM operand stack");
  for (size_t i = 0; i < vm->vm_frame_count; i++) {
    validate_value(state, vm->vm_frames[i].function, "VM function");
    validate_value(state, vm->vm_frames[i].env, "VM environment");
  }
  for (ot_handler_frame* handler = vm->handlers; handler != NULL; handler = handler->prev) {
    validate_value(state, handler->pred, "VM handler predicate");
    validate_value(state, handler->handler, "VM handler");
  }
  for (ot_restart_frame* frame = vm->restarts; frame != NULL; frame = frame->prev)
    for (size_t i = 0; i < frame->count; i++) {
      validate_value(state, frame->clauses[i].restart, "VM restart");
      validate_value(state, frame->clauses[i].params, "VM restart parameters");
      validate_value(state, frame->clauses[i].body, "VM restart body");
    }
  for (ot_param_frame* param = vm->params; param != NULL; param = param->prev) {
    validate_value(state, param->param, "VM parameter");
    validate_value(state, param->value, "VM parameter value");
  }
}

static void validate_heap(ots* state) {
  if (!state->config.gc_stress) return;
  for (ot_frame* frame = state->frames; frame != NULL; frame = frame->prev)
    for (size_t i = 0; i < frame->count; i++) validate_value(state, *frame->slots[i], "frame");
  validate_value(state, state->symbols, "symbols");
  validate_value(state, state->namespaces, "namespaces");
  validate_value(state, state->core_namespace, "core namespace");
  validate_value(state, state->expander, "expander");
  validate_value(state, state->type_parents, "condition types");
  validate_vm(state, &state->vm);
  unsigned char* scan = state->from_space;
  while (scan < state->alloc) {
    otv value = ot_from_obj(scan);
    size_t size = ot_object_size(value);
    switch (ot_object_type(value)) {
      case OBJ_SYMBOL:
      case OBJ_KEYWORD:
        validate_value(state, ((ot_name_obj*)scan)->next, "name.next");
        validate_value(state, ((ot_name_obj*)scan)->cache_namespace, "name.cache-namespace");
        validate_value(state, ((ot_name_obj*)scan)->cache_var, "name.cache-var");
        break;
      case OBJ_STRING: validate_value(state, ((ot_string_obj*)scan)->bytes, "string.bytes"); break;
      case OBJ_PAIR:
        validate_value(state, ((ot_pair_obj*)scan)->car, "pair.car");
        validate_value(state, ((ot_pair_obj*)scan)->cdr, "pair.cdr");
        break;
      case OBJ_SLOTS: {
        ot_slots_obj* slots = (ot_slots_obj*)scan;
        for (size_t i = 0; i < slots->capacity; i++)
          validate_value(state, slots->values[i], "slots.value");
        break;
      }
      case OBJ_ARRAY: validate_value(state, ((ot_array_obj*)scan)->slots, "array.slots"); break;
      case OBJ_ENTRIES: {
        ot_entries_obj* entries = (ot_entries_obj*)scan;
        for (size_t i = 0; i < entries->capacity; i++)
          if (entries->values[i].live) {
            validate_value(state, entries->values[i].key, "entry.key");
            validate_value(state, entries->values[i].value, "entry.value");
          }
        break;
      }
      case OBJ_TABLE:
        validate_value(state, ((ot_table_obj*)scan)->entries, "table.entries");
        validate_value(state, ((ot_table_obj*)scan)->index, "table.index");
        break;
      case OBJ_BUFFER: validate_value(state, ((ot_buffer_obj*)scan)->bytes, "buffer.bytes"); break;
      case OBJ_BINDING:
        validate_value(state, ((ot_binding_obj*)scan)->name, "binding.name");
        validate_value(state, ((ot_binding_obj*)scan)->value, "binding.value");
        validate_value(state, ((ot_binding_obj*)scan)->next, "binding.next");
        break;
      case OBJ_ENV:
        validate_value(state, ((ot_env_obj*)scan)->parent, "env.parent");
        validate_value(state, ((ot_env_obj*)scan)->bindings, "env.bindings");
        validate_value(state, ((ot_env_obj*)scan)->namespace_value, "env.namespace");
        break;
      case OBJ_VAR:
        validate_value(state, ((ot_var_obj*)scan)->name, "var.name");
        validate_value(state, ((ot_var_obj*)scan)->value, "var.value");
        validate_value(state, ((ot_var_obj*)scan)->doc, "var.doc");
        validate_value(state, ((ot_var_obj*)scan)->next, "var.next");
        break;
      case OBJ_ALIAS:
        validate_value(state, ((ot_alias_obj*)scan)->name, "alias.name");
        validate_value(state, ((ot_alias_obj*)scan)->value, "alias.value");
        validate_value(state, ((ot_alias_obj*)scan)->next, "alias.next");
        break;
      case OBJ_NAMESPACE:
        validate_value(state, ((ot_namespace_obj*)scan)->name, "namespace.name");
        validate_value(state, ((ot_namespace_obj*)scan)->vars, "namespace.vars");
        validate_value(state, ((ot_namespace_obj*)scan)->var_index, "namespace.var-index");
        validate_value(state, ((ot_namespace_obj*)scan)->refers, "namespace.refers");
        validate_value(state, ((ot_namespace_obj*)scan)->refer_index, "namespace.refer-index");
        validate_value(state, ((ot_namespace_obj*)scan)->aliases, "namespace.aliases");
        validate_value(state, ((ot_namespace_obj*)scan)->next, "namespace.next");
        break;
      case OBJ_CODE:
        validate_value(state, ((ot_code_obj*)scan)->bytes, "code.bytes");
        validate_value(state, ((ot_code_obj*)scan)->constants, "code.constants");
        validate_value(state, ((ot_code_obj*)scan)->params, "code.params");
        validate_value(state, ((ot_code_obj*)scan)->name, "code.name");
        break;
      case OBJ_FUNCTION:
        validate_value(state, ((ot_function_obj*)scan)->code, "function.code");
        validate_value(state, ((ot_function_obj*)scan)->env, "function.env");
        validate_value(state, ((ot_function_obj*)scan)->namespace_value, "function.namespace");
        validate_value(state, ((ot_function_obj*)scan)->name, "function.name");
        break;
      case OBJ_NAT: validate_value(state, ((ot_nat_obj*)scan)->name, "nat.name"); break;
      case OBJ_MACRO:
        validate_value(state, ((ot_macro_obj*)scan)->function, "macro.function");
        break;
      case OBJ_PARAM:
        validate_value(state, ((ot_param_obj*)scan)->name, "param.name");
        validate_value(state, ((ot_param_obj*)scan)->value, "param.value");
        break;
      case OBJ_RESTART:
        validate_value(state, ((ot_restart_obj*)scan)->name, "restart.name");
        validate_value(state, ((ot_restart_obj*)scan)->description, "restart.description");
        break;
      default: break;
    }
    scan += size;
  }
}
#endif

static void collect_for(ots* state, size_t requested) {
  /* Cheney scan: copy roots into the other semispace, then scan copied objects
   * until every reachable edge has been forwarded. */
  size_t before = (size_t)(state->alloc - state->from_space);
  otv old_exts = state->exts;
  unsigned char* old_from = state->from_space;
  state->alloc = state->to_space;
  trace_roots(state);
  unsigned char* scan = state->to_space;
  while (scan < state->alloc) {
    ot_obj* object = (ot_obj*)scan;
    size_t size = (size_t)(object->header >> 8u);
    trace_object(state, object);
    scan += size;
  }
  finish_exts(state, old_exts);

  size_t after = (size_t)(state->alloc - state->to_space);
  state->from_space = state->to_space;
  state->to_space = old_from;
  state->limit = state->from_space + state->capacity;
#ifdef OT_GC_VALIDATE
  validate_heap(state);
#endif
  state->stats.collections++;
  state->stats.reclaimed_bytes += before > after ? before - after : 0;

  if (after + requested > state->capacity) {
    size_t grown = state->capacity;
    while (grown < after + requested && grown < state->config.heap_max) {
      size_t next = grown > state->config.heap_max / 2 ? state->config.heap_max : grown * 2;
      if (next <= grown) break;
      grown = next;
    }
    state->capacity = grown;
    state->limit = state->from_space + state->capacity;
  }
}

void* ot_alloc(ots* state, size_t size, ot_obj_type type) {
  size = align_object(size);
  if (size > state->config.heap_max) return NULL;
  if (state->config.gc_stress && state->alloc != state->from_space) collect_for(state, size);
  if (state->alloc + size > state->limit) collect_for(state, size);
  if (state->alloc + size > state->limit) return NULL;
  ot_obj* object = (ot_obj*)state->alloc;
  state->alloc += size;
  memset(object, 0, size);
  object->header = make_header(type, size);
  state->stats.allocations++;
  state->stats.allocated_bytes += size;
  size_t used = (size_t)(state->alloc - state->from_space);
  if (used > state->stats.peak_used_bytes) state->stats.peak_used_bytes = used;
  return object;
}

void ot_collect(ots* state) { collect_for(state, 0); }

ot_gc_stats ot_get_gc_stats(const ots* state) {
  ot_gc_stats stats = state->stats;
  stats.used_bytes = (size_t)(state->alloc - state->from_space);
  stats.capacity_bytes = state->capacity;
  return stats;
}
