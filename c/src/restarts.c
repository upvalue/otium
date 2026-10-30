#include "internal.h"
#include <string.h>

void oti_restore_restarts(ot_state* rt, size_t scopes) {
  rt->restart_scope_count = scopes;
  rt->restart_count =
      scopes ? rt->restart_scopes[scopes - 1].first + rt->restart_scopes[scopes - 1].count : 0;
}

bool oti_install_restarts(ot_state* rt, size_t first, size_t count, size_t target) {
  size_t old = rt->restart_count;
  if (count > rt->stack_limit - old) return oti_error(rt, "restart limit exceeded");
  if (!oti_grow(rt, (void**)&rt->restart_scopes, &rt->restart_scope_capacity,
                rt->restart_scope_count + 1, sizeof(oti_restart_scope)))
    return false;
  if (!oti_grow(rt, (void**)&rt->restarts, &rt->restart_capacity, old + count, sizeof(ot_value)))
    return false;
  for (size_t i = 0; i < count; i++) {
    ot_value v = oti_alloc(rt, K_RESTART, 3, 0);
    if (!v) {
      oti_restore_restarts(rt, rt->restart_scope_count);
      return false;
    }
    oti_restart_obj* r = oti_as_restart(v);
    r->name = rt->stack[first + 3 * i];
    r->description = rt->stack[first + 3 * i + 1];
    r->handler = rt->stack[first + 3 * i + 2];
    rt->restarts[old + i] = v;
    /* Restarts made so far are roots while the rest are allocated. */
    rt->restart_count = old + i + 1;
  }
  oti_restart_scope scope = {{rt->frame_count, first, target, rt->param_count, rt->handler_count,
                              rt->handler_top, rt->restart_scope_count, rt->cleanup_count, 0},
                             old,
                             count,
                             rt->try_count};
  rt->restart_scopes[rt->restart_scope_count++] = scope;
  rt->restart_count = old + count;
  return true;
}

static ot_value find(ot_state* rt, ot_value name) {
  bool exact = oti_kind(name, K_RESTART);
  if (!exact && !oti_kind(name, K_SYMBOL) && !oti_kind(name, K_STRING) &&
      !oti_kind(name, K_KEYWORD)) {
    oti_error(rt, "restart name must be a symbol, keyword, string or restart");
    return 0;
  }
  size_t length = 0;
  const char* text = NULL;
  if (!exact) {
    text = oti_text_bytes(name, &length);
    if (oti_kind(name, K_KEYWORD)) {
      text++;
      length--;
    }
  }
  for (size_t i = rt->restart_count; i; i--) {
    ot_value r = rt->restarts[i - 1];
    if (exact) {
      if (r == name) return r;
    } else {
      ot_value symbol = oti_as_restart(r)->name;
      size_t n;
      const char* s = oti_text_bytes(symbol, &n);
      if (n == length && !memcmp(s, text, n)) return r;
    }
  }
  return OT_NIL;
}

static ot_value restart_compute(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!oti_check_arity(rt, "compute-restarts", count, 0, 0)) return 0;
  size_t base = rt->sp, n = rt->restart_count;
  if (n > rt->stack_limit - base || !oti_stack(rt, base + n))
    return oti_fail(rt, "VM stack limit exceeded");
  for (size_t i = 0; i < n; i++) rt->stack[base + i] = rt->restarts[n - i - 1];
  rt->sp = base + n;
  ot_value result = oti_array_from(rt, base, n);
  rt->sp = base;
  return result;
}

static ot_value restart_find(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "find-restart", count, 1, 1)) return 0;
  return find(rt, rt->stack[first]);
}

static ot_value restart_name(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "restart-name", count, 1, 1)) return 0;
  ot_value value = rt->stack[first];
  if (!oti_kind(value, K_RESTART)) return oti_fail(rt, "expected restart");
  return oti_as_restart(value)->name;
}

static ot_value restart_description(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "restart-description", count, 1, 1)) return 0;
  ot_value value = rt->stack[first];
  if (!oti_kind(value, K_RESTART)) return oti_fail(rt, "expected restart");
  return oti_as_restart(value)->description;
}

static ot_value restart_invoke(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "invoke-restart", count, 1, SIZE_MAX)) return 0;
  ot_value restart = find(rt, rt->stack[first]);
  if (!restart) return 0;
  if (restart == OT_NIL) return oti_fail(rt, "no active restart");
  rt->restart_transfer[0] = restart;
  ot_value args = oti_list_from(rt, first + 1, count - 1);
  if (!args) {
    rt->restart_transfer[0] = OT_NIL;
    return 0;
  }
  rt->restart_transfer[1] = args;
  oti_error(rt, "restart transfer");
  rt->restart_pending = true;
  return 0;
}

const oti_builtin_def oti_restart_builtins[] = {
    {B_FIND_RESTART, "find-restart", restart_find},
    {B_COMPUTE_RESTARTS, "compute-restarts", restart_compute},
    {B_RESTART_NAME, "restart-name", restart_name},
    {B_RESTART_DESCRIPTION, "restart-description", restart_description},
    {B_INVOKE_RESTART, "invoke-restart", restart_invoke},
    {0, NULL, NULL},
};

bool oti_resume_restart(ot_state* rt, size_t stop_frames) {
  size_t index = rt->restart_count;
  while (index && rt->restarts[index - 1] != rt->restart_transfer[0]) index--;
  if (!index) {
    rt->restart_pending = false;
    return oti_error(rt, "restart is no longer active");
  }
  size_t scope_index = rt->restart_scope_count;
  while (scope_index && rt->restart_scopes[scope_index - 1].first >= index) scope_index--;
  oti_restart_scope scope = rt->restart_scopes[scope_index - 1];
  if (rt->cleanup_count > scope.state.cleanups) {
    if (rt->cleanup_states[rt->cleanup_count - 1].state.frames <= stop_frames) return false;
    oti_run_cleanup(rt, true);
    return true;
  }
  if (scope.state.frames <= stop_frames) return false;
  oti_restore_restarts(rt, scope_index);
  rt->frame_count = scope.state.frames;
  rt->try_count = scope.tries;
  oti_restore_params(rt, scope.state.params);
  oti_restore_handlers(rt, scope.state.handlers, scope.state.handler_top);
  rt->sp = scope.state.sp;
  oti_frame* frame = &rt->frames[rt->frame_count - 1];
  frame->pc = frame->fn->code + scope.state.target;
  rt->restart_pending = false;
  oti_clear_error(rt);
  rt->raised = rt->signaled = false;
  rt->condition_values[0] = OT_NIL;
  size_t base = rt->sp;
  if (!oti_stack(rt, base + 3)) return true;
  rt->stack[base] = ((ot_value)B_APPLY << 8) | TAG_BUILTIN;
  rt->stack[base + 1] = oti_as_restart(rt->restart_transfer[0])->handler;
  rt->stack[base + 2] = rt->restart_transfer[1];
  rt->sp = base + 3;
  rt->restart_transfer[0] = rt->restart_transfer[1] = OT_NIL;
  oti_apply(rt, base, 2, false);
  return true;
}
