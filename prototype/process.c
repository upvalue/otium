#include "internal.h"
#include <stdlib.h>
#include <string.h>

bool ot_set_arguments(ot_state* rt, const char* script, size_t count,
                      const char* const* arguments) {
  if (!oti_host_entry(rt)) return false;
  if (count && !arguments) return oti_error(rt, "missing argument array");
  ot_value v[] = {OT_EMPTY, OT_NIL, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, v, 3);
  bool ok = true;
  if (script) {
    v[1] = oti_text(rt, K_STRING, script, strlen(script));
    ok = v[1] != 0;
  }
  for (size_t i = count; ok && i; i--) {
    if (!arguments[i - 1]) {
      ok = oti_error(rt, "null argument string");
      break;
    }
    v[2] = oti_text(rt, K_STRING, arguments[i - 1], strlen(arguments[i - 1]));
    if (!v[2]) {
      ok = false;
      break;
    }
    ot_value pair = oti_alloc(rt, K_PAIR, 2, 0);
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
  oti_roots_pop(&rt->heap, &roots);
  return ok;
}

static bool environment_string(ot_value value, bool name) {
  if (!oti_kind(value, K_STRING)) return false;
  size_t length = oti_bytes_length(value);
  const uint8_t* bytes = oti_bytes_data(value);
  return !memchr(bytes, 0, length) && (!name || (length && !memchr(bytes, '=', length)));
}

ot_value oti_process_builtin(ot_state* rt, unsigned id, size_t first, size_t count) {
  if (id == B_PROCESS_GETENV || id == B_PROCESS_SETENV) {
    if (count != (id == B_PROCESS_GETENV ? 1u : 2u) ||
        !environment_string(rt->stack[first], true)) {
      oti_error(rt, "expected an environment variable name");
      return 0;
    }
    const char* name = (const char*)oti_bytes_data(rt->stack[first]);
    if (id == B_PROCESS_GETENV) {
      const char* value = getenv(name);
      return value ? oti_text(rt, K_STRING, value, strlen(value)) : OT_NIL;
    }
    ot_value value = rt->stack[first + 1];
    if (value != OT_NIL && !environment_string(value, false)) {
      oti_error(rt, "expected a string without NUL or nil");
      return 0;
    }
    int result = value == OT_NIL ? unsetenv(name) : setenv(name, (char*)oti_bytes_data(value), 1);
    if (result) {
      oti_error(rt, "could not change the environment");
      return 0;
    }
    return OT_NIL;
  }
  if (count) {
    oti_error(rt, "process metadata takes no arguments");
    return 0;
  }
  if (id == B_SCRIPT_NAME) return rt->process_values[1];
  ot_value live[] = {rt->process_values[0], OT_EMPTY, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 3);
  bool ok = true;
  while (live[0] != OT_EMPTY) {
    ot_value pair = oti_alloc(rt, K_PAIR, 2, 0);
    if (!pair) {
      ok = false;
      break;
    }
    object_fields(pair)[0] = object_fields(live[0])[0];
    object_fields(pair)[1] = OT_EMPTY;
    if (live[1] == OT_EMPTY) live[1] = pair;
    else object_fields(live[2])[1] = pair;
    live[2] = pair;
    live[0] = object_fields(live[0])[1];
  }
  ot_value result = ok ? live[1] : 0;
  oti_roots_pop(&rt->heap, &roots);
  return result;
}
