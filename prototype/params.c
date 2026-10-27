#include "internal.h"

void oti_restore_params(ot_state* rt, size_t count) {
  rt->param_count = count;
  rt->param_roots.count = count * 2;
}

bool oti_bind_param(ot_state* rt, ot_value param, ot_value value) {
  if (!oti_kind(param, K_PARAM)) return oti_error(rt, "with-params requires a param");
  if (rt->param_count >= rt->stack_limit || rt->param_count >= SIZE_MAX / 2 - 1)
    return oti_error(rt, "parameter binding limit exceeded");
  if (!oti_grow(rt, (void**)&rt->params, &rt->param_capacity, 2 * (rt->param_count + 1),
                sizeof(ot_value)))
    return false;
  rt->param_roots.slots = rt->params;
  rt->params[2 * rt->param_count] = param;
  rt->params[2 * rt->param_count + 1] = value;
  oti_restore_params(rt, rt->param_count + 1);
  return true;
}

ot_value oti_param_call(ot_state* rt, ot_value param, size_t count) {
  if (count) {
    oti_coded_error(rt, "E0301", "params take no arguments");
    return 0;
  }
  for (size_t i = rt->param_count; i; i--)
    if (rt->params[2 * (i - 1)] == param) return rt->params[2 * (i - 1) + 1];
  return oti_as_param(param)->value;
}
