#include "internal.h"
#include <string.h>

void oti_restore_cleanups(ot_runtime* rt, size_t count) {
  rt->cleanup_count = rt->cleanup_roots.count = count;
}

bool oti_install_cleanup(ot_runtime* rt, bool outcome) {
  size_t count = rt->cleanup_count;
  if (count >= rt->stack_limit) return oti_error(rt, "cleanup limit exceeded");
  if (!oti_grow(rt, (void**)&rt->cleanup_states, &rt->cleanup_state_capacity, count + 1,
                sizeof(oti_cleanup)) ||
      !oti_grow(rt, (void**)&rt->cleanups, &rt->cleanup_capacity, count + 1, sizeof(ot_value)))
    return false;
  rt->cleanup_roots.slots = rt->cleanups;
  rt->cleanups[count] = rt->stack[rt->sp - 1];
  rt->cleanup_states[count] =
      (oti_cleanup){{rt->frame_count, rt->sp - 1, 0, rt->param_count, rt->handler_count,
                     rt->handler_top, rt->restart_scope_count, count},
                    rt->try_count,
                    outcome};
  oti_restore_cleanups(rt, count + 1);
  return true;
}

bool oti_run_cleanup(ot_runtime* rt, bool unwinding) {
  if (!rt->cleanup_count) return oti_error(rt, "invalid cleanup stack");
  size_t index = rt->cleanup_count - 1;
  oti_cleanup cleanup = rt->cleanup_states[index];
  ot_value values[] = {rt->cleanups[index],
                       rt->condition_values[0],
                       rt->restart_transfer[0],
                       rt->restart_transfer[1],
                       OT_EMPTY,
                       OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, values, 6);
  oti_restore_cleanups(rt, index);
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
    oti_restore_params(rt, cleanup.state.params);
    oti_restore_handlers(rt, cleanup.state.handlers, cleanup.state.handler_top);
    oti_restore_restarts(rt, cleanup.state.restarts);
  }
  bool ok = true;
  if (error) {
    values[1] = oti_error_condition(rt);
    ok = values[1] != 0;
    raised = true;
  }
  if (ok) {
    rt->error[0] = 0;
    rt->error_code = NULL;
    rt->raised = rt->signaled = rt->restart_pending = rt->exit_pending = rt->interrupted = false;
    rt->condition_values[0] = OT_NIL;
    rt->restart_transfer[0] = rt->restart_transfer[1] = OT_NIL;
    if (cleanup.outcome) {
      values[5] = error                      ? values[1]
                  : (exiting || interrupted) ? oti_intern(rt, K_KEYWORD, ":quit", 5)
                                             : OT_NIL;
      values[4] = values[5] ? oti_alloc(rt, K_PAIR, 2, 0) : 0;
      if (values[4]) {
        object_fields(values[4])[0] = values[5];
        object_fields(values[4])[1] = OT_EMPTY;
      } else ok = false;
    }
    if (ok) ok = oti_call_list(rt, values[0], values[4]) != 0;
  }
  if (ok) {
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
  oti_roots_pop(&rt->heap, &roots);
  return ok;
}
