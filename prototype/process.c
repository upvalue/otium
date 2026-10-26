#include "internal.h"
#include <string.h>

bool runtime_set_arguments(Runtime* rt, const char* script, size_t count,
                           const char* const* arguments) {
  if (count && !arguments) return rt_error(rt, "missing argument array");
  Value v[] = {V_EMPTY, V_NIL, V_NIL};
  Roots roots;
  roots_push(&rt->heap, &roots, v, 3);
  bool ok = true;
  if (script) {
    v[1] = rt_text(rt, K_STRING, script, strlen(script));
    ok = v[1] != 0;
  }
  for (size_t i = count; ok && i; i--) {
    if (!arguments[i - 1]) {
      ok = rt_error(rt, "null argument string");
      break;
    }
    v[2] = rt_text(rt, K_STRING, arguments[i - 1], strlen(arguments[i - 1]));
    if (!v[2]) {
      ok = false;
      break;
    }
    Value pair = rt_alloc(rt, K_PAIR, 2, 0);
    if (!pair) {
      ok = false;
      break;
    }
    object_fields(pair)[0] = v[2];
    object_fields(pair)[1] = v[0];
    v[0] = pair;
  }
  if (ok) {
    rt->process_values[0] = v[0];
    rt->process_values[1] = v[1];
  }
  roots_pop(&rt->heap, &roots);
  return ok;
}

Value rt_process_builtin(Runtime* rt, unsigned id, size_t count) {
  if (count) {
    rt_error(rt, "process metadata takes no arguments");
    return 0;
  }
  if (id == B_SCRIPT_NAME) return rt->process_values[1];
  Value live[] = {rt->process_values[0], V_EMPTY, V_NIL};
  Roots roots;
  roots_push(&rt->heap, &roots, live, 3);
  bool ok = true;
  while (live[0] != V_EMPTY) {
    Value pair = rt_alloc(rt, K_PAIR, 2, 0);
    if (!pair) {
      ok = false;
      break;
    }
    object_fields(pair)[0] = object_fields(live[0])[0];
    object_fields(pair)[1] = V_EMPTY;
    if (live[1] == V_EMPTY) live[1] = pair;
    else object_fields(live[2])[1] = pair;
    live[2] = pair;
    live[0] = object_fields(live[0])[1];
  }
  Value result = ok ? live[1] : 0;
  roots_pop(&rt->heap, &roots);
  return result;
}
