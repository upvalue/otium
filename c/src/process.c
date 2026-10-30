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
    oti_as_pair(pair)->car = v[2];
    oti_as_pair(pair)->cdr = v[0];
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

/* The variable name for getenv and setenv!, or NULL after recording an error. */
static const char* process_variable(ot_state* rt, size_t first, size_t count, size_t expected) {
  if (count != expected || !environment_string(rt->stack[first], true)) {
    oti_error(rt, "expected an environment variable name");
    return NULL;
  }
  return (const char*)oti_bytes_data(rt->stack[first]);
}

static ot_value process_getenv(ot_state* rt, size_t first, size_t count) {
  const char* name = process_variable(rt, first, count, 1);
  if (!name) return 0;
  const char* value = getenv(name);
  return value ? oti_text(rt, K_STRING, value, strlen(value)) : OT_NIL;
}

static ot_value process_setenv(ot_state* rt, size_t first, size_t count) {
  const char* name = process_variable(rt, first, count, 2);
  if (!name) return 0;
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

static bool process_metadata(ot_state* rt, size_t count) {
  if (count) oti_error(rt, "process metadata takes no arguments");
  return !count;
}

static ot_value process_script_name(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!process_metadata(rt, count)) return 0;
  return rt->process_values[1];
}

static ot_value process_arguments(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!process_metadata(rt, count)) return 0;
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
    oti_as_pair(pair)->car = oti_car(live[0]);
    oti_as_pair(pair)->cdr = OT_EMPTY;
    if (live[1] == OT_EMPTY) live[1] = pair;
    else oti_as_pair(live[2])->cdr = pair;
    live[2] = pair;
    live[0] = oti_cdr(live[0]);
  }
  ot_value result = ok ? live[1] : 0;
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

/* otium.memory reads these. Allocation is cumulative: bytes collections have
 * reclaimed plus bytes still held. */
static ot_value memory_counter(uint64_t value) {
  uint64_t limit = (uint64_t)(INTPTR_MAX / 2);
  return ot_int((intptr_t)(value > limit ? limit : value));
}

static ot_value memory_allocated(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!process_metadata(rt, count)) return 0;
  const oti_heap* h = &rt->heap;
  uint64_t allocated = h->allocated_bytes, current = h->start ? h->used : 0;
  return memory_counter(current > UINT64_MAX - allocated ? UINT64_MAX : allocated + current);
}

static ot_value memory_collections(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!process_metadata(rt, count)) return 0;
  return memory_counter(rt->heap.collections);
}

/* (%heap-stat n): one heap statistic as an integer, so reading it allocates
 * nothing. 0 live bytes after the last collection, 1 bytes in use, 2 the most
 * in use at a collection, 3 bytes held in blocks and large objects, 4 the
 * limit, 5 collections, 6 nanoseconds spent collecting, 7 the longest
 * collection, 8 bytes reclaimed, 9 bytes copied. */
static ot_value memory_heap_stat(ot_state* rt, size_t first, size_t count) {
  if (count != 1) {
    oti_error(rt, "%heap-stat takes one argument");
    return 0;
  }
  ot_value n = rt->stack[first];
  if (!ot_is_int(n)) {
    oti_error(rt, "%heap-stat expects an integer");
    return 0;
  }
  const oti_heap* h = &rt->heap;
  uint64_t value;
  switch (ot_to_int(n)) {
    case 0: value = h->live_bytes; break;
    case 1: value = oti_heap_used(h); break;
    case 2: value = h->peak_used; break;
    case 3: value = h->blocks_used * OTI_BLOCK + h->large_bytes; break;
    case 4: value = h->limit; break;
    case 5: value = h->collections; break;
    case 6: value = h->pause_nanoseconds; break;
    case 7: value = h->longest_pause; break;
    case 8: value = h->reclaimed_bytes; break;
    case 9: value = h->copied_bytes; break;
    default:
      oti_error(rt, "unknown heap statistic");
      return 0;
  }
  return memory_counter(value);
}

const oti_builtin_def oti_process_builtins[] = {
    {B_PROCESS_ARGUMENTS, "otium.process/arguments", process_arguments},
    {B_SCRIPT_NAME, "otium.process/script-name", process_script_name},
    {B_PROCESS_GETENV, "otium.process/getenv", process_getenv},
    {B_PROCESS_SETENV, "otium.process/setenv!", process_setenv},
    {B_MEMORY_ALLOCATED, "otium.memory/%allocated", memory_allocated},
    {B_MEMORY_COLLECTIONS, "otium.memory/%collections", memory_collections},
    {B_MEMORY_HEAP_STAT, "otium.memory/%heap-stat", memory_heap_stat},
    {0, NULL, NULL},
};
