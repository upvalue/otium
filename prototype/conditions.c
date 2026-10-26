#include "internal.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static ot_value fail(ot_runtime* rt, const char* message) {
  oti_error(rt, message);
  return 0;
}

static ot_value key(ot_runtime* rt, const char* name) {
  return oti_intern(rt, K_KEYWORD, name, strlen(name));
}

static ot_value symbol(ot_runtime* rt, ot_value v) {
  if (oti_kind(v, K_SYMBOL)) return v;
  if (!oti_kind(v, K_KEYWORD) && !oti_kind(v, K_STRING))
    return fail(rt, "condition type must be a name");
  size_t n;
  memcpy(&n, object_bytes(v), sizeof(n));
  const char* s = (const char*)object_bytes(v) + sizeof(n);
  if (oti_kind(v, K_KEYWORD)) {
    s++;
    n--;
  }
  return oti_intern(rt, K_SYMBOL, s, n);
}

static ot_value field(ot_runtime* rt, ot_value table, const char* name) {
  if (!oti_kind(table, K_TABLE)) return OT_NIL;
  oti_roots root;
  oti_roots_push(&rt->heap, &root, &table, 1);
  ot_value k = key(rt, name);
  ot_value result = k ? oti_table_get(rt, table, k, OT_NIL) : 0;
  oti_roots_pop(&rt->heap, &root);
  return result;
}

static const char* diagnostic_type(const char* code) {
  if (!code) return "error";
  if (!strncmp(code, "E00", 3)) return "read-error";
  if (!strncmp(code, "E01", 3)) return "syntax-error";
  if (!strcmp(code, "E0201")) return "name-error";
  if (!strncmp(code, "E02", 3)) return "namespace-error";
  if (!strcmp(code, "E0301")) return "arity-error";
  if (!strcmp(code, "E0302") || !strcmp(code, "E0401")) return "type-error";
  return "error";
}

static ot_value message_condition(ot_runtime* rt, ot_value message, size_t first, size_t count,
                               const char* code) {
  ot_value values[] = {message, OT_NIL, OT_NIL, OT_NIL, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, values, 5);
  values[1] = oti_table_builtin(rt, B_TABLE, 0, 0);
  const char* type = diagnostic_type(code);
  values[2] = oti_intern(rt, K_SYMBOL, type, strlen(type));
  bool registered = values[2] != 0;
  if (registered && strcmp(type, "error")) {
    if (rt->condition_values[1] == OT_NIL) {
      ot_value registry = oti_table_builtin(rt, B_TABLE, 0, 0);
      registered = registry != 0;
      if (registered) rt->condition_values[1] = registry;
    }
    if (registered &&
        oti_table_get(rt, rt->condition_values[1], values[2], V_UNBOUND) == V_UNBOUND) {
      values[4] = oti_intern(rt, K_SYMBOL, "error", 5);
      registered = values[4] && oti_table_set(rt, rt->condition_values[1], values[2], values[4]);
    }
  }
  ot_value k = key(rt, ":type");
  bool ok = registered && values[1] && values[2] && k && oti_table_set(rt, values[1], k, values[2]);
  if (ok) {
    k = key(rt, ":message");
    ok = k && oti_table_set(rt, values[1], k, values[0]);
  }
  if (ok && code) {
    values[4] = oti_text(rt, K_STRING, code, strlen(code));
    k = key(rt, ":code");
    ok = values[4] && k && oti_table_set(rt, values[1], k, values[4]);
  }
  if (ok && count) {
    values[3] = oti_collection_builtin(rt, B_ARRAY, first, count);
    k = key(rt, ":data");
    ok = values[3] && k && oti_table_set(rt, values[1], k, values[3]);
  }
  ot_value result = ok ? values[1] : 0;
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

ot_value oti_error_condition(ot_runtime* rt) {
  if (rt->raised) return rt->condition_values[0];
  char message[sizeof(rt->error)];
  memcpy(message, rt->error, sizeof(message));
  const char* code = rt->error_code;
  rt->error[0] = 0;
  ot_value text = oti_text(rt, K_STRING, message, strlen(message));
  return text ? message_condition(rt, text, 0, 0, code) : 0;
}

ot_value oti_raise(ot_runtime* rt, size_t first, size_t count) {
  if (!count) return fail(rt, "error needs an argument");
  ot_value condition = rt->stack[first];
  if (oti_kind(condition, K_STRING))
    condition = message_condition(rt, condition, first + 1, count - 1, NULL);
  else if (count != 1) return fail(rt, "extra arguments after non-string condition");
  if (!condition) return 0;
  rt->condition_values[0] = condition;
  rt->raised = true;
  if (oti_kind(rt->stack[first], K_STRING) || oti_kind(condition, K_SYMBOL) ||
      oti_kind(condition, K_KEYWORD)) {
    const char* message = (const char*)object_bytes(rt->stack[first]) + sizeof(size_t);
    return fail(rt, message);
  }
  return fail(rt, "uncaught condition");
}

ot_value oti_condition_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count) {
  if (count != (id == B_DEFINE_CONDITION || id == B_CONDITION_IS ? 2u : 1u))
    return fail(rt, "wrong number of condition arguments");
  if (id == B_CONDITION_TYPE || id == B_CONDITION_MESSAGE)
    return field(rt, rt->stack[first], id == B_CONDITION_TYPE ? ":type" : ":message");
  if (id == B_ERROR_P) {
    ot_value type = field(rt, rt->stack[first], ":type");
    return !type ? 0 : type == OT_NIL ? OT_FALSE : OT_TRUE;
  }
  ot_value values[] = {OT_NIL, OT_NIL, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, values, 3);
  ot_value result = 0;
  if (id == B_DEFINE_CONDITION) {
    values[0] = symbol(rt, rt->stack[first]);
    values[1] = rt->stack[first + 1] == OT_NIL ? OT_NIL : symbol(rt, rt->stack[first + 1]);
    if (!values[0] || !values[1]) goto done;
    if (rt->condition_values[1] == OT_NIL) {
      rt->condition_values[1] = oti_table_builtin(rt, B_TABLE, 0, 0);
      if (!rt->condition_values[1]) {
        rt->condition_values[1] = OT_NIL;
        goto done;
      }
    }
    ot_value cursor = values[1];
    while (cursor != OT_NIL) {
      if (cursor == values[0]) {
        fail(rt, "condition hierarchy cycle");
        goto done;
      }
      cursor = oti_table_get(rt, rt->condition_values[1], cursor, OT_NIL);
    }
    if (oti_table_set(rt, rt->condition_values[1], values[0], values[1])) result = values[0];
  } else {
    values[0] = field(rt, rt->stack[first], ":type");
    if (!values[0]) goto done;
    values[1] = symbol(rt, rt->stack[first + 1]);
    if (!values[1]) goto done;
    result = OT_FALSE;
    while (values[0] != OT_NIL) {
      if (values[0] == values[1]) {
        result = OT_TRUE;
        break;
      }
      if (rt->condition_values[1] == OT_NIL) break;
      values[0] = oti_table_get(rt, rt->condition_values[1], values[0], OT_NIL);
    }
  }
done:
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

void oti_restore_handlers(ot_runtime* rt, size_t count, size_t top) {
  rt->handler_count = count;
  rt->handler_top = top;
  rt->handler_roots.count = count * 2;
}

bool oti_install_handlers(ot_runtime* rt, size_t first, size_t count) {
  if (count > rt->stack_limit - rt->handler_count || count > SIZE_MAX / 2 - rt->handler_count)
    return oti_error(rt, "handler binding limit exceeded");
  size_t needed = rt->handler_count + count;
  if (!oti_grow(rt, (void**)&rt->handlers, &rt->handler_capacity, 2 * needed, sizeof(ot_value)))
    return false;
  rt->handler_roots.slots = rt->handlers;
  if (!oti_grow(rt, (void**)&rt->handler_previous, &rt->handler_previous_capacity, needed,
               sizeof(size_t)))
    return false;
  for (size_t i = 0; i < count; i++) {
    size_t index = rt->handler_count++;
    rt->handlers[2 * index] = rt->stack[first + 2 * i];
    rt->handlers[2 * index + 1] = rt->stack[first + 2 * i + 1];
    rt->handler_previous[index] = rt->handler_top;
    rt->handler_top = index + 1;
  }
  rt->handler_roots.count = rt->handler_count * 2;
  return true;
}

void oti_remove_handlers(ot_runtime* rt, size_t count) {
  if (!count) return;
  size_t remaining = rt->handler_count - count;
  oti_restore_handlers(rt, remaining, rt->handler_previous[remaining]);
}

static bool signal_handlers(ot_runtime* rt, ot_value condition) {
  if (!rt->handler_top) return true;
  ot_value args = OT_NIL;
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, &condition, 1);
  args = oti_alloc(rt, K_PAIR, 2, 0);
  if (args) {
    object_fields(args)[0] = condition;
    object_fields(args)[1] = OT_EMPTY;
  }
  oti_roots_pop(&rt->heap, &roots);
  if (!args) {
    rt->signaled = true;
    return false;
  }
  oti_roots_push(&rt->heap, &roots, &args, 1);
  size_t visible = rt->handler_top;
  bool ok = true;
  for (size_t entry = visible; entry && ok;) {
    size_t index = entry - 1, next = rt->handler_previous[index];
    rt->handler_top = next;
    ot_value match = oti_call_list(rt, rt->handlers[2 * index], args);
    if (!match) ok = false;
    else if (oti_truth(match) && !oti_call_list(rt, rt->handlers[2 * index + 1], args)) ok = false;
    entry = next;
  }
  rt->handler_top = visible;
  oti_roots_pop(&rt->heap, &roots);
  return ok;
}

ot_value oti_signal(ot_runtime* rt, size_t first, size_t count) {
  if (!count) return fail(rt, "signal needs an argument");
  ot_value condition = rt->stack[first];
  if (oti_kind(condition, K_STRING))
    condition = message_condition(rt, condition, first + 1, count - 1, NULL);
  else if (count != 1) return fail(rt, "extra arguments after non-string condition");
  return condition && signal_handlers(rt, condition) ? OT_NIL : 0;
}

/* Offer an error before unwinding. A handler's error replaces the original;
 * a normally returning handler leaves the original condition pending. */
void oti_signal_error(ot_runtime* rt) {
  size_t offset = rt->error_offset;
  char message[sizeof(rt->error)];
  memcpy(message, rt->error, sizeof(message));
  ot_value condition = oti_error_condition(rt);
  if (!condition) {
    rt->signaled = true;
    return;
  }
  oti_roots root;
  oti_roots_push(&rt->heap, &root, &condition, 1);
  rt->error[0] = 0;
  rt->raised = false;
  rt->condition_values[0] = OT_NIL;
  rt->signaled = false;
  if (signal_handlers(rt, condition)) {
    memcpy(rt->error, message, sizeof(message));
    rt->error_offset = offset;
    rt->error_code = NULL;
    rt->condition_values[0] = condition;
    rt->raised = true;
    rt->signaled = true;
  }
  oti_roots_pop(&rt->heap, &root);
}

ot_value oti_decode_error(ot_runtime* rt, size_t position) {
  ot_value v[] = {OT_NIL, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, v, 2);
  v[0] = oti_text(rt, K_STRING, "invalid UTF-8", 13);
  if (v[0]) v[0] = message_condition(rt, v[0], 0, 0, NULL);
  bool ok = v[0] != 0;
  if (ok) {
    v[1] = oti_intern(rt, K_SYMBOL, "decode-error", 12);
    ot_value k = key(rt, ":type");
    ok = v[1] && k && oti_table_set(rt, v[0], k, v[1]);
  }
  if (ok) {
    ot_value k = key(rt, ":position");
    ok = k && oti_table_set(rt, v[0], k, ot_int((intptr_t)position));
  }
  if (ok) {
    rt->condition_values[0] = v[0];
    rt->raised = true;
    oti_error(rt, "invalid UTF-8");
  }
  oti_roots_pop(&rt->heap, &roots);
  return 0;
}

ot_value oti_io_error(ot_runtime* rt, const char* reason) {
  ot_value v[] = {OT_NIL, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, v, 2);
  v[0] = oti_text(rt, K_STRING, "port operation failed", 21);
  if (v[0]) v[0] = message_condition(rt, v[0], 0, 0, NULL);
  bool ok = v[0] != 0;
  if (ok) {
    v[1] = oti_intern(rt, K_SYMBOL, "io-error", 8);
    ot_value k = key(rt, ":type");
    ok = v[1] && k && oti_table_set(rt, v[0], k, v[1]);
  }
  if (ok) {
    v[1] = key(rt, reason);
    ot_value k = key(rt, ":reason");
    ok = v[1] && k && oti_table_set(rt, v[0], k, v[1]);
  }
  if (ok) {
    rt->condition_values[0] = v[0];
    rt->raised = true;
    oti_error(rt, "port operation failed");
  }
  oti_roots_pop(&rt->heap, &roots);
  return 0;
}

ot_value oti_file_error(ot_runtime* rt, const char* op, ot_value path, int error) {
  const char* reason = error == ENOENT                     ? ":not-found"
                       : error == EEXIST                   ? ":exists"
                       : error == EACCES || error == EPERM ? ":permission"
                       : error == EISDIR                   ? ":is-directory"
                       : error == ENOTDIR                  ? ":not-directory"
                       : error == ENOSPC                   ? ":no-space"
                                                           : ":other";
  ot_value v[] = {path, OT_NIL, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, v, 3);
  const char* detail = strerror(error);
  v[1] = oti_text(rt, K_STRING, detail, strlen(detail));
  if (v[1]) v[1] = message_condition(rt, v[1], 0, 0, NULL);
  bool ok = v[1] != 0;
  const char* keys[] = {":type", ":op", ":path", ":reason", ":detail"};
  for (size_t i = 0; ok && i < 5; i++) {
    if (i == 0) v[2] = oti_intern(rt, K_SYMBOL, "file-error", 10);
    else if (i == 1) v[2] = oti_intern(rt, K_SYMBOL, op, strlen(op));
    else if (i == 2) v[2] = v[0];
    else if (i == 3) v[2] = key(rt, reason);
    else v[2] = oti_text(rt, K_STRING, detail, strlen(detail));
    ot_value k = key(rt, keys[i]);
    ok = v[2] && k && oti_table_set(rt, v[1], k, v[2]);
  }
  if (ok) {
    rt->condition_values[0] = v[1];
    rt->raised = true;
    oti_error(rt, detail);
  }
  oti_roots_pop(&rt->heap, &roots);
  return 0;
}

ot_value oti_ffi_error(ot_runtime* rt, const char* operation, const char* reason, const char* message) {
  char op[64], why[64];
  size_t length = strlen(message);
  char* detail = malloc(length + 1);
  if (!detail) return fail(rt, "out of host memory");
  memcpy(detail, message, length + 1);
  snprintf(op, sizeof(op), "%s", operation);
  snprintf(why, sizeof(why), "%s", reason);
  ot_value live[] = {OT_NIL, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 2);
  live[0] = oti_text(rt, K_STRING, detail, strlen(detail));
  if (live[0]) live[0] = message_condition(rt, live[0], 0, 0, NULL);
  bool ok = live[0] != 0;
  const char* names[] = {":type",   ":operation", ":reason",       ":library",
                         ":symbol", ":argument",  ":expected-type"};
  const char* values[] = {"ffi-error", op, why};
  for (size_t i = 0; ok && i < 7; i++) {
    live[1] = i < 3 ? oti_intern(rt, K_SYMBOL, values[i], strlen(values[i])) : OT_FALSE;
    ot_value k = key(rt, names[i]);
    ok = live[1] && k && oti_table_set(rt, live[0], k, live[1]);
  }
  if (ok) {
    rt->condition_values[0] = live[0];
    rt->raised = true;
    oti_error(rt, detail);
  }
  oti_roots_pop(&rt->heap, &roots);
  free(detail);
  return 0;
}
