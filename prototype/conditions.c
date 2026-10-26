#include <errno.h>
#include <stdlib.h>
#include "internal.h"
#include <string.h>

static Value fail(Runtime* rt, const char* message) {
  rt_error(rt, message);
  return 0;
}

static Value key(Runtime* rt, const char* name) {
  return rt_intern(rt, K_KEYWORD, name, strlen(name));
}

static Value symbol(Runtime* rt, Value v) {
  if (rt_kind(v, K_SYMBOL)) return v;
  if (!rt_kind(v, K_KEYWORD) && !rt_kind(v, K_STRING))
    return fail(rt, "condition type must be a name");
  size_t n;
  memcpy(&n, object_bytes(v), sizeof(n));
  const char* s = (const char*)object_bytes(v) + sizeof(n);
  if (rt_kind(v, K_KEYWORD)) {
    s++;
    n--;
  }
  return rt_intern(rt, K_SYMBOL, s, n);
}

static Value field(Runtime* rt, Value table, const char* name) {
  if (!rt_kind(table, K_TABLE)) return V_NIL;
  Roots root;
  roots_push(&rt->heap, &root, &table, 1);
  Value k = key(rt, name);
  Value result = k ? rt_table_get(rt, table, k, V_NIL) : 0;
  roots_pop(&rt->heap, &root);
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

static Value message_condition(Runtime* rt, Value message, size_t first, size_t count,
                               const char* code) {
  Value values[] = {message, V_NIL, V_NIL, V_NIL, V_NIL};
  Roots roots;
  roots_push(&rt->heap, &roots, values, 5);
  values[1] = rt_table_builtin(rt, B_TABLE, 0, 0);
  const char* type = diagnostic_type(code);
  values[2] = rt_intern(rt, K_SYMBOL, type, strlen(type));
  bool registered = values[2] != 0;
  if (registered && strcmp(type, "error")) {
    if (rt->condition_values[1] == V_NIL) {
      Value registry = rt_table_builtin(rt, B_TABLE, 0, 0);
      registered = registry != 0;
      if (registered) rt->condition_values[1] = registry;
    }
    if (registered &&
        rt_table_get(rt, rt->condition_values[1], values[2], V_UNBOUND) == V_UNBOUND) {
      values[4] = rt_intern(rt, K_SYMBOL, "error", 5);
      registered = values[4] && rt_table_set(rt, rt->condition_values[1], values[2], values[4]);
    }
  }
  Value k = key(rt, ":type");
  bool ok = registered && values[1] && values[2] && k && rt_table_set(rt, values[1], k, values[2]);
  if (ok) {
    k = key(rt, ":message");
    ok = k && rt_table_set(rt, values[1], k, values[0]);
  }
  if (ok && code) {
    values[4] = rt_text(rt, K_STRING, code, strlen(code));
    k = key(rt, ":code");
    ok = values[4] && k && rt_table_set(rt, values[1], k, values[4]);
  }
  if (ok && count) {
    values[3] = rt_collection_builtin(rt, B_ARRAY, first, count);
    k = key(rt, ":data");
    ok = values[3] && k && rt_table_set(rt, values[1], k, values[3]);
  }
  Value result = ok ? values[1] : 0;
  roots_pop(&rt->heap, &roots);
  return result;
}

Value rt_error_condition(Runtime* rt) {
  if (rt->raised) return rt->condition_values[0];
  char message[sizeof(rt->error)];
  memcpy(message, rt->error, sizeof(message));
  const char* code = rt->error_code;
  rt->error[0] = 0;
  Value text = rt_text(rt, K_STRING, message, strlen(message));
  return text ? message_condition(rt, text, 0, 0, code) : 0;
}

Value rt_raise(Runtime* rt, size_t first, size_t count) {
  if (!count) return fail(rt, "error needs an argument");
  Value condition = rt->stack[first];
  if (rt_kind(condition, K_STRING))
    condition = message_condition(rt, condition, first + 1, count - 1, NULL);
  else if (count != 1) return fail(rt, "extra arguments after non-string condition");
  if (!condition) return 0;
  rt->condition_values[0] = condition;
  rt->raised = true;
  if (rt_kind(rt->stack[first], K_STRING) || rt_kind(condition, K_SYMBOL) ||
      rt_kind(condition, K_KEYWORD)) {
    const char* message = (const char*)object_bytes(rt->stack[first]) + sizeof(size_t);
    return fail(rt, message);
  }
  return fail(rt, "uncaught condition");
}

Value rt_condition_builtin(Runtime* rt, unsigned id, size_t first, size_t count) {
  if (count != (id == B_DEFINE_CONDITION || id == B_CONDITION_IS ? 2u : 1u))
    return fail(rt, "wrong number of condition arguments");
  if (id == B_CONDITION_TYPE || id == B_CONDITION_MESSAGE)
    return field(rt, rt->stack[first], id == B_CONDITION_TYPE ? ":type" : ":message");
  if (id == B_ERROR_P) {
    Value type = field(rt, rt->stack[first], ":type");
    return !type ? 0 : type == V_NIL ? V_FALSE : V_TRUE;
  }
  Value values[] = {V_NIL, V_NIL, V_NIL};
  Roots roots;
  roots_push(&rt->heap, &roots, values, 3);
  Value result = 0;
  if (id == B_DEFINE_CONDITION) {
    values[0] = symbol(rt, rt->stack[first]);
    values[1] = rt->stack[first + 1] == V_NIL ? V_NIL : symbol(rt, rt->stack[first + 1]);
    if (!values[0] || !values[1]) goto done;
    if (rt->condition_values[1] == V_NIL) {
      rt->condition_values[1] = rt_table_builtin(rt, B_TABLE, 0, 0);
      if (!rt->condition_values[1]) {
        rt->condition_values[1] = V_NIL;
        goto done;
      }
    }
    Value cursor = values[1];
    while (cursor != V_NIL) {
      if (cursor == values[0]) {
        fail(rt, "condition hierarchy cycle");
        goto done;
      }
      cursor = rt_table_get(rt, rt->condition_values[1], cursor, V_NIL);
    }
    if (rt_table_set(rt, rt->condition_values[1], values[0], values[1])) result = values[0];
  } else {
    values[0] = field(rt, rt->stack[first], ":type");
    if (!values[0]) goto done;
    values[1] = symbol(rt, rt->stack[first + 1]);
    if (!values[1]) goto done;
    result = V_FALSE;
    while (values[0] != V_NIL) {
      if (values[0] == values[1]) {
        result = V_TRUE;
        break;
      }
      if (rt->condition_values[1] == V_NIL) break;
      values[0] = rt_table_get(rt, rt->condition_values[1], values[0], V_NIL);
    }
  }
done:
  roots_pop(&rt->heap, &roots);
  return result;
}

void rt_restore_handlers(Runtime* rt, size_t count, size_t top) {
  rt->handler_count = count;
  rt->handler_top = top;
  rt->handler_roots.count = count * 2;
}

bool rt_install_handlers(Runtime* rt, size_t first, size_t count) {
  if (count > rt->stack_limit - rt->handler_count || count > SIZE_MAX / 2 - rt->handler_count)
    return rt_error(rt, "handler binding limit exceeded");
  size_t needed = rt->handler_count + count;
  if (!rt_grow(rt, (void**)&rt->handlers, &rt->handler_capacity, 2 * needed, sizeof(Value)))
    return false;
  rt->handler_roots.slots = rt->handlers;
  if (!rt_grow(rt, (void**)&rt->handler_previous, &rt->handler_previous_capacity, needed,
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

void rt_remove_handlers(Runtime* rt, size_t count) {
  if (!count) return;
  size_t remaining = rt->handler_count - count;
  rt_restore_handlers(rt, remaining, rt->handler_previous[remaining]);
}

static bool signal_handlers(Runtime* rt, Value condition) {
  if (!rt->handler_top) return true;
  Value args = V_NIL;
  Roots roots;
  roots_push(&rt->heap, &roots, &condition, 1);
  args = rt_alloc(rt, K_PAIR, 2, 0);
  if (args) {
    object_fields(args)[0] = condition;
    object_fields(args)[1] = V_EMPTY;
  }
  roots_pop(&rt->heap, &roots);
  if (!args) {
    rt->signaled = true;
    return false;
  }
  roots_push(&rt->heap, &roots, &args, 1);
  size_t visible = rt->handler_top;
  bool ok = true;
  for (size_t entry = visible; entry && ok;) {
    size_t index = entry - 1, next = rt->handler_previous[index];
    rt->handler_top = next;
    Value match = rt_call_list(rt, rt->handlers[2 * index], args);
    if (!match) ok = false;
    else if (rt_truth(match) && !rt_call_list(rt, rt->handlers[2 * index + 1], args)) ok = false;
    entry = next;
  }
  rt->handler_top = visible;
  roots_pop(&rt->heap, &roots);
  return ok;
}

Value rt_signal(Runtime* rt, size_t first, size_t count) {
  if (!count) return fail(rt, "signal needs an argument");
  Value condition = rt->stack[first];
  if (rt_kind(condition, K_STRING))
    condition = message_condition(rt, condition, first + 1, count - 1, NULL);
  else if (count != 1) return fail(rt, "extra arguments after non-string condition");
  return condition && signal_handlers(rt, condition) ? V_NIL : 0;
}

/* Offer an error before unwinding. A handler's error replaces the original;
 * a normally returning handler leaves the original condition pending. */
void rt_signal_error(Runtime* rt) {
  size_t offset = rt->error_offset;
  char message[sizeof(rt->error)];
  memcpy(message, rt->error, sizeof(message));
  Value condition = rt_error_condition(rt);
  if (!condition) {
    rt->signaled = true;
    return;
  }
  Roots root;
  roots_push(&rt->heap, &root, &condition, 1);
  rt->error[0] = 0;
  rt->raised = false;
  rt->condition_values[0] = V_NIL;
  rt->signaled = false;
  if (signal_handlers(rt, condition)) {
    memcpy(rt->error, message, sizeof(message));
    rt->error_offset = offset;
    rt->error_code = NULL;
    rt->condition_values[0] = condition;
    rt->raised = true;
    rt->signaled = true;
  }
  roots_pop(&rt->heap, &root);
}

Value rt_decode_error(Runtime* rt, size_t position) {
  Value v[] = {V_NIL, V_NIL};
  Roots roots;
  roots_push(&rt->heap, &roots, v, 2);
  v[0] = rt_text(rt, K_STRING, "invalid UTF-8", 13);
  if (v[0]) v[0] = message_condition(rt, v[0], 0, 0, NULL);
  bool ok = v[0] != 0;
  if (ok) {
    v[1] = rt_intern(rt, K_SYMBOL, "decode-error", 12);
    Value k = key(rt, ":type");
    ok = v[1] && k && rt_table_set(rt, v[0], k, v[1]);
  }
  if (ok) {
    Value k = key(rt, ":position");
    ok = k && rt_table_set(rt, v[0], k, value_int((intptr_t)position));
  }
  if (ok) {
    rt->condition_values[0] = v[0];
    rt->raised = true;
    rt_error(rt, "invalid UTF-8");
  }
  roots_pop(&rt->heap, &roots);
  return 0;
}

Value rt_io_error(Runtime* rt, const char* reason) {
  Value v[] = {V_NIL, V_NIL};
  Roots roots;
  roots_push(&rt->heap, &roots, v, 2);
  v[0] = rt_text(rt, K_STRING, "port operation failed", 21);
  if (v[0]) v[0] = message_condition(rt, v[0], 0, 0, NULL);
  bool ok = v[0] != 0;
  if (ok) {
    v[1] = rt_intern(rt, K_SYMBOL, "io-error", 8);
    Value k = key(rt, ":type");
    ok = v[1] && k && rt_table_set(rt, v[0], k, v[1]);
  }
  if (ok) {
    v[1] = key(rt, reason);
    Value k = key(rt, ":reason");
    ok = v[1] && k && rt_table_set(rt, v[0], k, v[1]);
  }
  if (ok) {
    rt->condition_values[0] = v[0];
    rt->raised = true;
    rt_error(rt, "port operation failed");
  }
  roots_pop(&rt->heap, &roots);
  return 0;
}

Value rt_file_error(Runtime* rt, const char* op, Value path, int error) {
  const char* reason = error == ENOENT                     ? ":not-found"
                       : error == EEXIST                   ? ":exists"
                       : error == EACCES || error == EPERM ? ":permission"
                       : error == EISDIR                   ? ":is-directory"
                       : error == ENOTDIR                  ? ":not-directory"
                       : error == ENOSPC                   ? ":no-space"
                                                           : ":other";
  Value v[] = {path, V_NIL, V_NIL};
  Roots roots;
  roots_push(&rt->heap, &roots, v, 3);
  const char* detail = strerror(error);
  v[1] = rt_text(rt, K_STRING, detail, strlen(detail));
  if (v[1]) v[1] = message_condition(rt, v[1], 0, 0, NULL);
  bool ok = v[1] != 0;
  const char* keys[] = {":type", ":op", ":path", ":reason", ":detail"};
  for (size_t i = 0; ok && i < 5; i++) {
    if (i == 0) v[2] = rt_intern(rt, K_SYMBOL, "file-error", 10);
    else if (i == 1) v[2] = rt_intern(rt, K_SYMBOL, op, strlen(op));
    else if (i == 2) v[2] = v[0];
    else if (i == 3) v[2] = key(rt, reason);
    else v[2] = rt_text(rt, K_STRING, detail, strlen(detail));
    Value k = key(rt, keys[i]);
    ok = v[2] && k && rt_table_set(rt, v[1], k, v[2]);
  }
  if (ok) {
    rt->condition_values[0] = v[1];
    rt->raised = true;
    rt_error(rt, detail);
  }
  roots_pop(&rt->heap, &roots);
  return 0;
}

Value rt_ffi_error(Runtime* rt, const char* operation, const char* reason, const char* message) {
  char op[64], why[64];
  size_t length = strlen(message);
  char* detail = malloc(length + 1);
  if (!detail) return fail(rt, "out of host memory");
  memcpy(detail, message, length + 1);
  snprintf(op, sizeof(op), "%s", operation);
  snprintf(why, sizeof(why), "%s", reason);
  Value live[] = {V_NIL, V_NIL};
  Roots roots;
  roots_push(&rt->heap, &roots, live, 2);
  live[0] = rt_text(rt, K_STRING, detail, strlen(detail));
  if (live[0]) live[0] = message_condition(rt, live[0], 0, 0, NULL);
  bool ok = live[0] != 0;
  const char* names[] = {":type",   ":operation", ":reason",       ":library",
                         ":symbol", ":argument",  ":expected-type"};
  const char* values[] = {"ffi-error", op, why};
  for (size_t i = 0; ok && i < 7; i++) {
    live[1] = i < 3 ? rt_intern(rt, K_SYMBOL, values[i], strlen(values[i])) : V_FALSE;
    Value k = key(rt, names[i]);
    ok = live[1] && k && rt_table_set(rt, live[0], k, live[1]);
  }
  if (ok) {
    rt->condition_values[0] = live[0];
    rt->raised = true;
    rt_error(rt, detail);
  }
  roots_pop(&rt->heap, &roots);
  free(detail);
  return 0;
}
