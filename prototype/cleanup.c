#include "internal.h"
#include <string.h>

void rt_restore_cleanups(Runtime* rt, size_t count) {
  rt->cleanup_count = rt->cleanup_roots.count = count;
}

bool rt_install_cleanup(Runtime* rt, bool outcome) {
  size_t count = rt->cleanup_count;
  if (count >= rt->stack_limit) return rt_error(rt, "cleanup limit exceeded");
  if (!rt_grow(rt, (void**)&rt->cleanup_states, &rt->cleanup_state_capacity, count + 1,
               sizeof(Cleanup)) ||
      !rt_grow(rt, (void**)&rt->cleanups, &rt->cleanup_capacity, count + 1, sizeof(Value)))
    return false;
  rt->cleanup_roots.slots = rt->cleanups;
  rt->cleanups[count] = rt->stack[rt->sp - 1];
  rt->cleanup_states[count] =
      (Cleanup){{rt->frame_count, rt->sp - 1, 0, rt->param_count, rt->handler_count,
                 rt->handler_top, rt->restart_scope_count, count},
                rt->try_count,
                outcome};
  rt_restore_cleanups(rt, count + 1);
  return true;
}

bool rt_run_cleanup(Runtime* rt, bool unwinding) {
  if (!rt->cleanup_count) return rt_error(rt, "invalid cleanup stack");
  size_t index = rt->cleanup_count - 1;
  Cleanup cleanup = rt->cleanup_states[index];
  Value values[] = {rt->cleanups[index],
                    rt->condition_values[0],
                    rt->restart_transfer[0],
                    rt->restart_transfer[1],
                    V_EMPTY,
                    V_NIL};
  Roots roots;
  roots_push(&rt->heap, &roots, values, 6);
  rt_restore_cleanups(rt, index);
  bool restart = rt->restart_pending, exiting = rt->exit_pending, signaled = rt->signaled;
  bool raised = rt->raised, interrupted = rt->interrupted;
  int status = rt->exit_status;
  size_t offset = rt->error_offset;
  const char* code = rt->error_code;
  char message[sizeof(rt->error)];
  memcpy(message, rt->error, sizeof(message));
  bool error = unwinding && !restart && !exiting && !interrupted;
  if (unwinding) {
    rt->frame_count = cleanup.state.frames;
    rt->try_count = cleanup.tries;
    rt->sp = rt->stack_roots.count = cleanup.state.sp;
    rt_restore_params(rt, cleanup.state.params);
    rt_restore_handlers(rt, cleanup.state.handlers, cleanup.state.handler_top);
    rt_restore_restarts(rt, cleanup.state.restarts);
  }
  bool ok = true;
  if (error) {
    values[1] = rt_error_condition(rt);
    ok = values[1] != 0;
    raised = true;
  }
  if (ok) {
    rt->error[0] = 0;
    rt->error_code = NULL;
    rt->raised = rt->signaled = rt->restart_pending = rt->exit_pending = rt->interrupted = false;
    rt->condition_values[0] = V_NIL;
    rt->restart_transfer[0] = rt->restart_transfer[1] = V_NIL;
    if (cleanup.outcome) {
      values[5] = error                      ? values[1]
                  : (exiting || interrupted) ? rt_intern(rt, K_KEYWORD, ":quit", 5)
                                             : V_NIL;
      values[4] = values[5] ? rt_alloc(rt, K_PAIR, 2, 0) : 0;
      if (values[4]) {
        object_fields(values[4])[0] = values[5];
        object_fields(values[4])[1] = V_EMPTY;
      } else ok = false;
    }
    if (ok) ok = rt_call_list(rt, values[0], values[4]) != 0;
  }
  /* Resource cleanup preserves an in-flight failure; unwind-protect replaces it. */
  if (ok || (cleanup.outcome && unwinding && !rt->restart_pending && !rt->exit_pending &&
             !rt->interrupted)) {
    memcpy(rt->error, message, sizeof(message));
    rt->error_code = code;
    rt->error_offset = offset;
    rt->raised = raised;
    rt->signaled = signaled;
    rt->condition_values[0] = values[1];
    rt->restart_pending = restart;
    rt->restart_transfer[0] = values[2];
    rt->restart_transfer[1] = values[3];
    rt->interrupted = interrupted;
    rt->exit_pending = exiting;
    rt->exit_status = status;
    ok = true;
  }
  roots_pop(&rt->heap, &roots);
  return ok;
}
