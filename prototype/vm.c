#include "internal.h"
#include <string.h>

static void height(ot_runtime* rt, size_t sp) { rt->sp = rt->stack_roots.count = sp; }

static bool push(ot_runtime* rt, ot_value v) {
  if (!oti_stack(rt, rt->sp + 1)) return false;
  rt->stack[rt->sp] = v;
  height(rt, rt->sp + 1);
  return true;
}

static bool primitive(ot_value value) {
  return (value & 255) == TAG_BUILTIN || oti_kind(value, K_FOREIGN_FUNCTION) ||
         oti_kind(value, K_PARAM) || oti_kind(value, K_INSTANCE) || oti_kind(value, K_RECORD) ||
         oti_collection(value);
}

static ot_value call_primitive(ot_runtime* rt, size_t call, size_t argc) {
  ot_value value = rt->stack[call];
  if (oti_kind(value, K_RECORD)) return oti_record_call(rt, value, call + 1, argc);
  if (oti_kind(value, K_INSTANCE)) return oti_instance_call(rt, value, call + 1, argc);
  if (oti_kind(value, K_PARAM)) return oti_param_call(rt, value, argc);
  if (oti_collection(value)) return oti_collection_call(rt, call, call + 1, argc);
  if (oti_kind(value, K_FOREIGN_FUNCTION)) return oti_foreign_call(rt, call, call + 1, argc);
  return oti_builtin(rt, (unsigned)(value >> 8), call + 1, argc);
}

bool oti_enter(ot_runtime* rt, size_t call, size_t argc, bool tail) {
  ot_value closure = rt->stack[call];
  if (oti_kind(closure, K_MACRO)) {
    closure = object_fields(closure)[0];
    rt->stack[call] = closure;
  }
  if (oti_kind(closure, K_GENERIC) || oti_kind(closure, K_NEXT)) {
    if (!oti_generic_prepare(rt, call, &argc)) return false;
    closure = rt->stack[call];
  }
  if (!oti_kind(closure, K_CLOSURE)) return oti_coded_error(rt, "E0302", "value is not callable");
  uint32_t id;
  memcpy(&id, object_bytes(closure), sizeof(id));
  oti_function* fn = rt->functions[id];
  if (argc < fn->arity || (!fn->rest && argc > fn->arity + fn->optional))
    return oti_coded_error(rt, "E0301", "wrong number of arguments");
  size_t base = tail ? rt->frames[rt->frame_count - 1].base : call;
  if (fn->locals >= rt->stack_limit || base >= rt->stack_limit - fn->locals)
    return oti_error(rt, "VM stack limit exceeded");
  if (base + 1 + fn->locals > rt->stack_capacity && !oti_stack(rt, base + 1 + fn->locals))
    return false;
  if (!tail && rt->frame_count == rt->frame_capacity &&
      !oti_grow(rt, (void**)&rt->frames, &rt->frame_capacity, rt->frame_count + 1,
                sizeof(oti_frame)))
    return false;
  size_t parameters = fn->arity + fn->optional + (fn->rest ? 1 : 0);
  ot_value rest = OT_EMPTY;
  if (fn->rest && argc > fn->arity) {
    oti_roots root;
    oti_roots_push(&rt->heap, &root, &rest, 1);
    for (size_t i = argc; i > fn->arity; i--) {
      ot_value pair = oti_alloc(rt, K_PAIR, 2, 0);
      if (!pair) {
        oti_roots_pop(&rt->heap, &root);
        return false;
      }
      object_fields(pair)[0] = rt->stack[call + i];
      object_fields(pair)[1] = rest;
      rest = pair;
    }
    oti_roots_pop(&rt->heap, &root);
    closure = rt->stack[call];
  }
  size_t supplied = fn->rest ? fn->arity : argc;
  if (base != call)
    memmove(rt->stack + base + 1, rt->stack + call + 1, supplied * sizeof(ot_value));
  rt->stack[base] = closure;
  for (size_t i = supplied; i < fn->locals; i++) rt->stack[base + 1 + i] = V_UNBOUND;
  if (fn->rest) rt->stack[base + 1 + fn->arity] = rest;
  height(rt, base + 1 + fn->locals);
  if (!tail) rt->frame_count++;
  rt->frames[rt->frame_count - 1] = (oti_frame){id, 0, base};
  if (rt->frame_count > rt->max_frames) rt->max_frames = rt->frame_count;
  for (size_t i = 0; i < parameters; i++) {
    if (!fn->boxed[i]) continue;
    ot_value box = oti_alloc(rt, K_BOX, 1, 0);
    if (!box) return false;
    object_fields(box)[0] = rt->stack[base + 1 + i];
    rt->stack[base + 1 + i] = box;
  }
  return true;
}

bool oti_leave(ot_runtime* rt, ot_value result) {
  size_t base = rt->frames[--rt->frame_count].base;
  /* The returning frame already owns this slot; no allocation is needed. */
  rt->stack[base] = result;
  height(rt, base + 1);
  return true;
}

bool oti_apply(ot_runtime* rt, size_t call, size_t argc, bool tail) {
  const ot_value apply = ((ot_value)B_APPLY << 8) | TAG_BUILTIN;
  while (rt->stack[call] == apply) {
    if (argc < 2) return oti_error(rt, "apply needs a callable and a final argument sequence");
    ot_value list = rt->stack[call + argc];
    bool array = oti_kind(list, K_ARRAY);
    ot_value cursor = array || list == OT_NIL ? OT_EMPTY : list, slow = cursor;
    size_t length = array ? ((oti_array_info*)object_bytes(list))->length : 0, prefix = argc - 2;
    while (oti_kind(cursor, K_PAIR)) {
      if (length >= rt->stack_limit) return oti_error(rt, "VM stack limit exceeded");
      length++;
      cursor = object_fields(cursor)[1];
      if (!(length & 1)) slow = object_fields(slow)[1];
      if (cursor != OT_EMPTY && cursor == slow)
        return oti_error(rt, "apply needs an acyclic proper list");
    }
    if (cursor != OT_EMPTY) return oti_error(rt, "apply needs a proper list");
    if (length > rt->stack_limit || prefix > rt->stack_limit - length ||
        call >= rt->stack_limit - prefix - length)
      return oti_error(rt, "VM stack limit exceeded");
    size_t count = prefix + length;
    if (call + 1 + count > rt->stack_capacity && !oti_stack(rt, call + 1 + count)) return false;
    memmove(rt->stack + call, rt->stack + call + 1, (prefix + 1) * sizeof(ot_value));
    if (array && length) {
      memcpy(rt->stack + call + 1 + prefix, object_fields(object_fields(list)[0]),
             length * sizeof(ot_value));
    } else
      for (size_t i = 0; i < length; i++) {
        rt->stack[call + 1 + prefix + i] = object_fields(list)[0];
        list = object_fields(list)[1];
      }
    argc = count;
    height(rt, call + 1 + argc);
  }
  ot_value target = rt->stack[call];
  if (primitive(target)) {
    ot_value answer = call_primitive(rt, call, argc);
    if (!answer || rt->error[0]) return false;
    if (tail) return oti_leave(rt, answer);
    rt->stack[call] = answer;
    height(rt, call + 1);
    return true;
  }
  return oti_enter(rt, call, argc, tail);
}

bool oti_execute(ot_runtime* rt, uint32_t function, ot_value* result) {
  ot_value closure = oti_alloc(rt, K_CLOSURE, 0, sizeof(function));
  if (!closure) return false;
  memcpy(object_bytes(closure), &function, sizeof(function));
  *result = oti_call_list(rt, closure, OT_EMPTY);
  return *result != 0;
}

bool oti_invoke(ot_runtime* rt, size_t argc, ot_value* result) {
  ot_value callable = rt->stack[0];
  if (callable == (((ot_value)B_APPLY << 8) | TAG_BUILTIN)) {
    if (!oti_apply(rt, 0, argc, false)) return false;
    goto dispatch;
  }
  if (primitive(callable)) {
    ot_value answer = call_primitive(rt, 0, argc);
    if (!answer) return false;
    height(rt, 0);
    if (!push(rt, answer)) return false;
    *result = answer;
    return true;
  }
  if (!oti_enter(rt, 0, argc, false)) return false;
dispatch:;
  if (!oti_drive(rt, 0)) return false;
  *result = rt->stack[0];
  return true;
}

static bool dispatch(ot_runtime* rt, size_t stop_frames) {
  while (rt->frame_count > stop_frames && !rt->error[0]) {
    if (rt->interrupt_flag && *rt->interrupt_flag && !ot_poll_interrupt(rt)) return false;
    oti_frame* frame = &rt->frames[rt->frame_count - 1];
    oti_function* fn = rt->functions[frame->function];
    oti_instruction ins = fn->code[frame->ip++];
    size_t base = frame->base;
    ot_value v;
    switch (ins.op) {
      case OP_CLEANUP:
        if (!oti_install_cleanup(rt, ins.arg != 0)) return false;
        height(rt, rt->sp - 1);
        break;
      case OP_RUN_CLEANUP:
        if (!oti_run_cleanup(rt, false)) return false;
        break;
      case OP_RESTARTS: {
        size_t target = fn->code[frame->ip++].arg;
        size_t first = rt->sp - 3 * ins.arg;
        if (!oti_install_restarts(rt, first, ins.arg, target)) return false;
        height(rt, first);
        break;
      }
      case OP_END_RESTARTS:
        if (!rt->restart_scope_count) return oti_error(rt, "invalid restart scope");
        oti_restore_restarts(rt, rt->restart_scope_count - 1);
        break;
      case OP_TRY:
        if (!oti_grow(rt, (void**)&rt->tries, &rt->try_capacity, rt->try_count + 1,
                      sizeof(oti_try_frame)))
          return false;
        rt->tries[rt->try_count++] = (oti_try_frame){rt->frame_count,
                                                     rt->sp,
                                                     ins.arg,
                                                     rt->param_count,
                                                     rt->handler_count,
                                                     rt->handler_top,
                                                     rt->restart_scope_count,
                                                     rt->cleanup_count};
        break;
      case OP_END_TRY:
        if (!rt->try_count) return oti_error(rt, "invalid try stack");
        rt->try_count--;
        break;
      case OP_RAISE:
        rt->condition_values[0] = rt->stack[rt->sp - 1];
        rt->raised = true;
        oti_error(rt, "uncaught condition");
        rt->signaled = true;
        return false;
      case OP_BIND_HANDLERS:
        if (!oti_install_handlers(rt, rt->sp - 2 * ins.arg, ins.arg)) return false;
        height(rt, rt->sp - 2 * ins.arg);
        break;
      case OP_UNBIND_HANDLERS:
        if (ins.arg > rt->handler_count) return oti_error(rt, "invalid handler stack");
        oti_remove_handlers(rt, ins.arg);
        break;
      case OP_MAKE_PARAM:
        v = oti_alloc(rt, K_PARAM, 2, 0);
        if (!v) return false;
        object_fields(v)[0] = rt->stack[rt->sp - 2];
        object_fields(v)[1] = rt->stack[rt->sp - 1];
        height(rt, rt->sp - 2);
        if (!push(rt, v)) return false;
        break;
      case OP_BIND_PARAM:
        if (!oti_bind_param(rt, rt->stack[rt->sp - 2], rt->stack[rt->sp - 1])) return false;
        height(rt, rt->sp - 2);
        break;
      case OP_UNBIND_PARAMS:
        if (ins.arg > rt->param_count) return oti_error(rt, "invalid parameter stack");
        oti_restore_params(rt, rt->param_count - ins.arg);
        break;
      case OP_CONST:
        if (!push(rt, rt->constants[ins.arg])) return false;
        break;
      case OP_GLOBAL:
        v = rt->globals[ins.arg];
        if (v == (((ot_value)B_EVAL << 8) | TAG_BUILTIN)) {
          v = oti_evaluator(rt, fn->namespace);
          if (!v) return false;
        }
        if (v == V_UNBOUND) {
          char message[192];
          snprintf(message, sizeof(message), "unbound global: %s", rt->global_info[ins.arg].name);
          return oti_coded_error(rt, "E0201", message);
        }
        if (!push(rt, v)) return false;
        break;
      case OP_SET_GLOBAL:
        if (rt->globals[ins.arg] == V_UNBOUND)
          return oti_coded_error(rt, "E0201", "cannot set an unbound global");
        /* fall through */
      case OP_DEFINE_GLOBAL:
        if (ins.op == OP_DEFINE_GLOBAL) {
          rt->global_info[ins.arg].defined = 1;
        }
        rt->global_info[ins.arg].macro = oti_kind(rt->stack[rt->sp - 1], K_MACRO);
        rt->globals[ins.arg] = rt->stack[rt->sp - 1];
        height(rt, rt->sp - 1);
        break;
      case OP_LOCAL:
      case OP_PRESENT:
        v = rt->stack[base + 1 + ins.arg];
        if (fn->boxed[ins.arg] && oti_kind(v, K_BOX)) v = object_fields(v)[0];
        if (ins.op == OP_PRESENT) v = v == V_UNBOUND ? OT_FALSE : OT_TRUE;
        else if (v == V_UNBOUND) return oti_error(rt, "uninitialized local");
        if (!push(rt, v)) return false;
        break;
      case OP_CLEAR_LOCAL: rt->stack[base + 1 + ins.arg] = V_UNBOUND; break;
      case OP_BIND:
        if (fn->boxed[ins.arg]) {
          v = oti_alloc(rt, K_BOX, 1, 0);
          if (!v) return false;
          object_fields(v)[0] = rt->stack[rt->sp - 1];
        } else v = rt->stack[rt->sp - 1];
        rt->stack[base + 1 + ins.arg] = v;
        height(rt, rt->sp - 1);
        break;
      case OP_SET_LOCAL:
        if (fn->boxed[ins.arg]) {
          ot_value box = rt->stack[base + 1 + ins.arg];
          if (!oti_kind(box, K_BOX)) return oti_error(rt, "uninitialized captured binding");
          object_fields(box)[0] = rt->stack[rt->sp - 1];
        } else rt->stack[base + 1 + ins.arg] = rt->stack[rt->sp - 1];
        height(rt, rt->sp - 1);
        break;
      case OP_UPVALUE:
        v = object_fields(object_fields(rt->stack[base])[ins.arg])[0];
        if (v == V_UNBOUND) return oti_error(rt, "uninitialized captured binding");
        if (!push(rt, v)) return false;
        break;
      case OP_SET_UPVALUE:
        object_fields(object_fields(rt->stack[base])[ins.arg])[0] = rt->stack[rt->sp - 1];
        height(rt, rt->sp - 1);
        break;
      case OP_CLOSURE: {
        oti_function* child = rt->functions[ins.arg];
        v = oti_alloc(rt, K_CLOSURE, (uint32_t)child->capture_count, sizeof(uint32_t));
        if (!v) return false;
        memcpy(object_bytes(v), &ins.arg, sizeof(ins.arg));
        for (size_t i = 0; i < child->capture_count; i++) {
          oti_capture capture = child->captures[i];
          object_fields(v)[i] = capture.local ? rt->stack[base + 1 + capture.index]
                                              : object_fields(rt->stack[base])[capture.index];
          if (!oti_kind(object_fields(v)[i], K_BOX))
            return oti_error(rt, "uninitialized captured binding");
        }
        if (!push(rt, v)) return false;
        break;
      }
      case OP_CALL:
      case OP_TAIL_CALL: {
        size_t call = rt->sp - ins.arg - 1;
        v = rt->stack[call];
        if (v == (((ot_value)B_APPLY << 8) | TAG_BUILTIN)) {
          if (!oti_apply(rt, call, ins.arg, ins.op == OP_TAIL_CALL)) return false;
          break;
        }
        if (primitive(v)) {
          ot_value answer = call_primitive(rt, call, ins.arg);
          if (!answer || rt->error[0]) return false;
          if (ins.op == OP_TAIL_CALL) {
            if (!oti_leave(rt, answer)) return false;
          } else {
            height(rt, call);
            if (!push(rt, answer)) return false;
          }
        } else if (!oti_enter(rt, call, ins.arg, ins.op == OP_TAIL_CALL)) return false;
        break;
      }
      case OP_RETURN:
        v = rt->stack[rt->sp - 1];
        if (!oti_leave(rt, v)) return false;
        break;
      case OP_POP: height(rt, rt->sp - 1); break;
      case OP_DUP:
        if (!push(rt, rt->stack[rt->sp - 1])) return false;
        break;
      case OP_JUMP: frame->ip = ins.arg; break;
      case OP_JFALSE:
        v = rt->stack[rt->sp - 1];
        height(rt, rt->sp - 1);
        if (!oti_truth(v)) frame->ip = ins.arg;
        break;
      default: return oti_error(rt, "invalid bytecode");
    }
  }
  return !rt->error[0];
}

bool oti_drive(ot_runtime* rt, size_t stop_frames) {
  while (!dispatch(rt, stop_frames)) {
    if (!rt->interrupted && !rt->exit_pending && !rt->restart_pending && !rt->signaled)
      oti_signal_error(rt);
    if (rt->restart_pending) {
      if (!oti_resume_restart(rt, stop_frames)) return false;
      continue;
    }
    size_t target_cleanups = !rt->interrupted && !rt->exit_pending && rt->try_count
                                 ? rt->tries[rt->try_count - 1].cleanups
                                 : 0;
    if (rt->cleanup_count > target_cleanups) {
      if (rt->cleanup_states[rt->cleanup_count - 1].state.frames <= stop_frames) return false;
      oti_run_cleanup(rt, true);
      continue;
    }
    if (rt->interrupted || rt->exit_pending || !rt->try_count ||
        rt->tries[rt->try_count - 1].frames <= stop_frames)
      return false;
    oti_try_frame handler = rt->tries[--rt->try_count];
    rt->frame_count = handler.frames;
    oti_restore_restarts(rt, handler.restarts);
    oti_restore_params(rt, handler.params);
    oti_restore_handlers(rt, handler.handlers, handler.handler_top);
    height(rt, handler.sp);
    ot_value condition = oti_error_condition(rt);
    if (!condition) continue;
    rt->error[0] = 0;
    rt->raised = false;
    rt->signaled = false;
    if (!push(rt, condition)) continue;
    rt->condition_values[0] = OT_NIL;
    rt->frames[rt->frame_count - 1].ip = handler.target;
  }
  return true;
}

/* Call back into the VM without discarding the suspended caller's roots. */
ot_value oti_call_list(ot_runtime* rt, ot_value function, ot_value args) {
  if (!oti_host_entry(rt)) return 0;
  size_t base = rt->sp, frames = rt->frame_count, tries = rt->try_count, params = rt->param_count;
  size_t handlers = rt->handler_count, handler_top = rt->handler_top,
         restarts = rt->restart_scope_count, cleanups = rt->cleanup_count;
  if (rt->callback_depth >= 128) {
    oti_error(rt, "VM callback nesting limit exceeded");
    rt->signaled = true;
    return 0;
  }
  if (rt->stack_limit < 3 || base > rt->stack_limit - 3) {
    oti_error(rt, "VM stack limit exceeded");
    rt->signaled = true;
    return 0;
  }
  if (!oti_stack(rt, base + 3)) {
    rt->signaled = true;
    return 0;
  }
  rt->callback_depth++;
  rt->stack[base] = ((ot_value)B_APPLY << 8) | TAG_BUILTIN;
  rt->stack[base + 1] = function;
  rt->stack[base + 2] = args;
  height(rt, base + 3);
  bool applied = oti_apply(rt, base, 2, false);
  if (!applied && !rt->error[0]) oti_error(rt, "VM call failed");
  bool ok = oti_drive(rt, frames);
  ot_value result = ok ? rt->stack[base] : 0;
  rt->frame_count = frames;
  rt->try_count = tries;
  oti_restore_restarts(rt, restarts);
  oti_restore_cleanups(rt, cleanups);
  oti_restore_params(rt, params);
  oti_restore_handlers(rt, handlers, handler_top);
  rt->callback_depth--;
  height(rt, base);
  return result;
}
