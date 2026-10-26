#include "internal.h"

void rt_restore_params(Runtime* rt, size_t count) {
  rt->param_count = count;
  rt->param_roots.count = count * 2;
}

bool rt_bind_param(Runtime* rt, Value param, Value value) {
  if (!rt_kind(param, K_PARAM)) return rt_error(rt, "with-params requires a param");
  if (rt->param_count >= rt->stack_limit || rt->param_count >= SIZE_MAX / 2 - 1)
    return rt_error(rt, "parameter binding limit exceeded");
  if (!rt_grow(rt, (void**)&rt->params, &rt->param_capacity, 2 * (rt->param_count + 1),
               sizeof(Value)))
    return false;
  rt->param_roots.slots = rt->params;
  rt->params[2 * rt->param_count] = param;
  rt->params[2 * rt->param_count + 1] = value;
  rt_restore_params(rt, rt->param_count + 1);
  return true;
}

Value rt_param_call(Runtime* rt, Value param, size_t count) {
  if (count) {
    rt_coded_error(rt, "E0301", "params take no arguments");
    return 0;
  }
  for (size_t i = rt->param_count; i; i--)
    if (rt->params[2 * (i - 1)] == param) return rt->params[2 * (i - 1) + 1];
  return object_fields(param)[1];
}
