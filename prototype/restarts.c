#include "internal.h"
#include <string.h>

void rt_restore_restarts(Runtime *rt, size_t scopes) {
    rt->restart_scope_count = scopes;
    rt->restart_count = scopes ? rt->restart_scopes[scopes-1].first + rt->restart_scopes[scopes-1].count : 0;
    rt->restart_roots.count = rt->restart_count;
}
bool rt_install_restarts(Runtime *rt, size_t first, size_t count, size_t target) {
    size_t old = rt->restart_count;
    if (count > rt->stack_limit - old) return rt_error(rt, "restart limit exceeded");
    if (!rt_grow(rt, (void **)&rt->restart_scopes, &rt->restart_scope_capacity,
                 rt->restart_scope_count + 1, sizeof(RestartScope))) return false;
    if (!rt_grow(rt, (void **)&rt->restarts, &rt->restart_capacity, old + count, sizeof(Value))) return false;
    rt->restart_roots.slots = rt->restarts;
    for (size_t i = 0; i < count; i++) {
        Value v = rt_alloc(rt, K_RESTART, 3, 0);
        if (!v) { rt_restore_restarts(rt, rt->restart_scope_count); return false; }
        memcpy(object_fields(v), rt->stack + first + 3*i, 3*sizeof(Value));
        rt->restarts[old+i] = v; rt->restart_roots.count = old+i+1;
    }
    RestartScope scope = {{rt->frame_count, first, target, rt->param_count,
        rt->handler_count, rt->handler_top, rt->restart_scope_count, rt->cleanup_count}, old, count, rt->try_count};
    rt->restart_scopes[rt->restart_scope_count++] = scope;
    rt->restart_count = old + count;
    return true;
}
static Value find(Runtime *rt, Value name) {
    bool exact = rt_kind(name, K_RESTART);
    if (!exact && !rt_kind(name, K_SYMBOL) && !rt_kind(name, K_STRING) && !rt_kind(name, K_KEYWORD)) {
        rt_error(rt, "restart name must be a symbol, keyword, string or restart"); return 0;
    }
    size_t length = 0; const char *text = NULL;
    if (!exact) {
        memcpy(&length, object_bytes(name), sizeof(length)); text = (char *)object_bytes(name) + sizeof(length);
        if (rt_kind(name, K_KEYWORD)) { text++; length--; }
    }
    for (size_t i = rt->restart_count; i; i--) {
        Value r = rt->restarts[i-1];
        if (exact) { if (r == name) return r; }
        else {
            Value symbol = object_fields(r)[0]; size_t n;
            memcpy(&n, object_bytes(symbol), sizeof(n));
            if (n == length && !memcmp((char *)object_bytes(symbol) + sizeof(n), text, n)) return r;
        }
    }
    return V_NIL;
}
Value rt_restart_builtin(Runtime *rt, unsigned id, size_t first, size_t count) {
    if ((id == B_COMPUTE_RESTARTS && count) ||
        (id == B_INVOKE_RESTART ? count < 1 : id != B_COMPUTE_RESTARTS && count != 1)) {
        rt_error(rt, "wrong number of restart arguments"); return 0;
    }
    if (id == B_COMPUTE_RESTARTS) {
        size_t base = rt->sp, n = rt->restart_count;
        if (n > rt->stack_limit - base || !rt_stack(rt, base + n)) { rt_error(rt, "VM stack limit exceeded"); return 0; }
        for (size_t i = 0; i < n; i++) rt->stack[base+i] = rt->restarts[n-i-1];
        rt->sp = rt->stack_roots.count = base+n;
        Value result = rt_collection_builtin(rt, B_ARRAY, base, n);
        rt->sp = rt->stack_roots.count = base; return result;
    }
    Value value = rt->stack[first];
    if (id == B_RESTART_NAME || id == B_RESTART_DESCRIPTION) {
        if (!rt_kind(value, K_RESTART)) { rt_error(rt, "expected restart"); return 0; }
        return object_fields(value)[id == B_RESTART_DESCRIPTION];
    }
    Value restart = find(rt, value);
    if (!restart || id == B_FIND_RESTART) return restart;
    if (restart == V_NIL) { rt_error(rt, "no active restart"); return 0; }
    rt->restart_transfer[0] = restart;
    Value args = rt_builtin(rt, B_LIST, first+1, count-1);
    if (!args) { rt->restart_transfer[0] = V_NIL; return 0; }
    rt->restart_transfer[1] = args;
    rt_error(rt, "restart transfer"); rt->restart_pending = true;
    return 0;
}
bool rt_resume_restart(Runtime *rt, size_t stop_frames) {
    size_t index = rt->restart_count;
    while (index && rt->restarts[index-1] != rt->restart_transfer[0]) index--;
    if (!index) { rt->restart_pending = false; return rt_error(rt, "restart is no longer active"); }
    size_t scope_index = rt->restart_scope_count;
    while (scope_index && rt->restart_scopes[scope_index-1].first >= index) scope_index--;
    RestartScope scope = rt->restart_scopes[scope_index-1];
    if (rt->cleanup_count > scope.state.cleanups) {
        if (rt->cleanup_states[rt->cleanup_count-1].state.frames <= stop_frames) return false;
        rt_run_cleanup(rt, true); return true;
    }
    if (scope.state.frames <= stop_frames) return false;
    rt_restore_restarts(rt, scope_index);
    rt->frame_count = scope.state.frames; rt->try_count = scope.tries;
    rt_restore_params(rt, scope.state.params);
    rt_restore_handlers(rt, scope.state.handlers, scope.state.handler_top);
    rt->sp = rt->stack_roots.count = scope.state.sp;
    rt->frames[rt->frame_count-1].ip = scope.state.target;
    rt->restart_pending = false; rt->error[0] = 0; rt->error_code = NULL;
    rt->raised = rt->signaled = false; rt->condition_values[0] = V_NIL;
    size_t base = rt->sp;
    if (!rt_stack(rt, base+3)) return true;
    rt->stack[base] = ((Value)B_APPLY << 8) | TAG_BUILTIN;
    rt->stack[base+1] = object_fields(rt->restart_transfer[0])[2];
    rt->stack[base+2] = rt->restart_transfer[1];
    rt->sp = rt->stack_roots.count = base+3;
    rt->restart_transfer[0] = rt->restart_transfer[1] = V_NIL;
    rt_apply(rt, base, 2, false);
    return true;
}
