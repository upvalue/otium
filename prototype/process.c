#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include <stdlib.h>
#include <string.h>

bool runtime_set_arguments(Runtime* rt, const char* script, size_t count,
                           const char* const* arguments) {
  if (!rt_host_entry(rt)) return false;
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

static bool environment_string(Value value, bool name) {
  if (!rt_kind(value, K_STRING)) return false;
  size_t length = rt_bytes_length(value);
  const uint8_t* bytes = rt_bytes_data(value);
  return !memchr(bytes, 0, length) && (!name || (length && !memchr(bytes, '=', length)));
}

Value rt_process_builtin(Runtime* rt, unsigned id, size_t first, size_t count) {
  if (id == B_PROCESS_GETENV || id == B_PROCESS_SETENV) {
    if (count != (id == B_PROCESS_GETENV ? 1u : 2u) ||
        !environment_string(rt->stack[first], true)) {
      rt_error(rt, "expected an environment variable name");
      return 0;
    }
    const char* name = (const char*)rt_bytes_data(rt->stack[first]);
    if (id == B_PROCESS_GETENV) {
      const char* value = getenv(name);
      return value ? rt_text(rt, K_STRING, value, strlen(value)) : V_NIL;
    }
    Value value = rt->stack[first + 1];
    if (value != V_NIL && !environment_string(value, false)) {
      rt_error(rt, "expected a string without NUL or nil");
      return 0;
    }
    int result = value == V_NIL ? unsetenv(name) : setenv(name, (char*)rt_bytes_data(value), 1);
    if (result) {
      rt_error(rt, "could not change the environment");
      return 0;
    }
    return V_NIL;
  }
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
