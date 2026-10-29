#include "internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t field_index(ot_value type, ot_value key) {
  if (!oti_kind(key, K_KEYWORD)) return SIZE_MAX;
  for (size_t i = 0; i < oti_record_type_fields(type); i++)
    if (oti_as_record_type(type)->keys[i] == key) return i;
  return SIZE_MAX;
}

ot_value oti_record_call(ot_state* rt, ot_value record, size_t first, size_t count) {
  if (count != 1) return oti_fail(rt, "record call needs one field keyword");
  size_t index = field_index(oti_as_record(record)->type, rt->stack[first]);
  return index == SIZE_MAX ? oti_fail(rt, "unknown record field")
                           : oti_as_record(record)->values[index];
}

static ot_value record_make_type(ot_state* rt, size_t first, size_t count) {
  if (count != 3 || !oti_kind(rt->stack[first], K_SYMBOL) ||
      (rt->stack[first + 2] != OT_TRUE && rt->stack[first + 2] != OT_FALSE))
    return oti_fail(rt, "invalid record type declaration");
  ot_value cursor = rt->stack[first + 1], slow = cursor, fast = cursor;
  size_t n = 0;
  while (oti_kind(cursor, K_PAIR)) {
    if (!oti_kind(oti_car(cursor), K_SYMBOL)) return oti_fail(rt, "record fields must be symbols");
    if (n == UINT32_MAX - 1) return oti_fail(rt, "too many record fields");
    n++;
    cursor = oti_cdr(cursor);
    if (oti_kind(slow, K_PAIR)) slow = oti_cdr(slow);
    for (unsigned i = 0; i < 2 && oti_kind(fast, K_PAIR); i++) fast = oti_cdr(fast);
    if (oti_kind(fast, K_PAIR) && slow == fast) return oti_fail(rt, "cyclic record field list");
  }
  if (cursor != OT_EMPTY) return oti_fail(rt, "record fields must form a proper list");
  ot_value live[] = {OT_NIL, rt->stack[first + 1]};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 2);
  live[0] = oti_alloc(rt, K_RECORD_TYPE, (uint32_t)n + 1, sizeof(bool));
  bool ok = live[0] != 0;
  if (ok) {
    oti_as_record_type(live[0])->name = rt->stack[first];
    *oti_record_type_immutable(live[0]) = rt->stack[first + 2] == OT_TRUE;
  }
  for (size_t i = 0; ok && i < n; i++) {
    ot_value name = oti_car(live[1]);
    size_t length = oti_bytes_length(name);
    char* text = malloc(length + 2);
    if (!text) {
      oti_fail(rt, "out of host memory");
      ok = false;
      break;
    }
    text[0] = ':';
    memcpy(text + 1, oti_bytes_data(name), length);
    text[length + 1] = 0;
    ot_value key = oti_intern(rt, K_KEYWORD, text, length + 1);
    free(text);
    ok = key != 0;
    if (ok) {
      for (size_t j = 0; j < i; j++)
        if (oti_as_record_type(live[0])->keys[j] == key) {
          oti_fail(rt, "duplicate record field");
          ok = false;
          break;
        }
      oti_as_record_type(live[0])->keys[i] = key;
    }
    live[1] = oti_cdr(live[1]);
  }
  ot_value result = ok ? live[0] : 0;
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

static ot_value record_new(ot_state* rt, size_t first, size_t count) {
  if (!count) return oti_fail(rt, "record-new needs a record type");
  ot_value type = rt->stack[first];
  if (!oti_kind(type, K_RECORD_TYPE)) return oti_fail(rt, "expected record type");
  if (count - 1 != oti_record_type_fields(type))
    return oti_fail(rt, "wrong number of record fields");
  ot_value record = oti_alloc(rt, K_RECORD, (uint32_t)count, 0);
  if (record) {
    oti_as_record(record)->type = rt->stack[first];
    memcpy(oti_as_record(record)->values, rt->stack + first + 1, (count - 1) * sizeof(ot_value));
  }
  return record;
}

static ot_value record_record_p(ot_state* rt, size_t first, size_t count) {
  if (count != 1) return oti_fail(rt, "wrong number of record arguments");
  return oti_kind(rt->stack[first], K_RECORD) ? OT_TRUE : OT_FALSE;
}

static ot_value record_type_p(ot_state* rt, size_t first, size_t count) {
  if (count != 1) return oti_fail(rt, "wrong number of record arguments");
  return oti_kind(rt->stack[first], K_RECORD_TYPE) ? OT_TRUE : OT_FALSE;
}

/* Check the argument count and that the first argument is a record. */
static bool record_args(ot_state* rt, size_t first, size_t count, size_t wanted) {
  if (count != wanted) return oti_error(rt, "wrong number of record arguments");
  if (!oti_kind(rt->stack[first], K_RECORD)) return oti_error(rt, "expected record instance");
  return true;
}

static ot_value record_type(ot_state* rt, size_t first, size_t count) {
  if (!record_args(rt, first, count, 1)) return 0;
  return oti_as_record(rt->stack[first])->type;
}

static ot_value record_ref(ot_state* rt, size_t first, size_t count) {
  if (!record_args(rt, first, count, 2)) return 0;
  ot_value record = rt->stack[first];
  size_t index = field_index(oti_as_record(record)->type, rt->stack[first + 1]);
  if (index == SIZE_MAX) return oti_fail(rt, "unknown record field");
  return oti_as_record(record)->values[index];
}

static ot_value record_set(ot_state* rt, size_t first, size_t count) {
  if (!record_args(rt, first, count, 3)) return 0;
  ot_value record = rt->stack[first], type = oti_as_record(record)->type;
  size_t index = field_index(type, rt->stack[first + 1]);
  if (index == SIZE_MAX) return oti_fail(rt, "unknown record field");
  if (*oti_record_type_immutable(type)) return oti_fail(rt, "cannot mutate immutable record");
  return oti_as_record(record)->values[index] = rt->stack[first + 2];
}

/* The record at first, when it has the type at first + 1, checked for the
 * accessor whose cell is at first + 3, and the field index at first + 2.
 * The accessors defrecord defines call these. */
static bool accessor_args(ot_state* rt, size_t first, size_t count, size_t wanted, size_t* index) {
  if (count != wanted) return oti_error(rt, "wrong number of record arguments");
  ot_value record = rt->stack[first], type = rt->stack[first + 1], at = rt->stack[first + 2],
           cell = rt->stack[first + 3];
  if (!oti_kind(type, K_RECORD_TYPE) || !ot_is_int(at) || ot_to_int(at) < 0 ||
      (size_t)ot_to_int(at) >= oti_record_type_fields(type) || !ot_is_int(cell) ||
      ot_to_int(cell) < 0 || (size_t)ot_to_int(cell) >= rt->global_count)
    return oti_error(rt, "invalid record accessor");
  if (!oti_kind(record, K_RECORD) || oti_as_record(record)->type != type) {
    ot_value name = oti_as_record_type(type)->name;
    size_t length = oti_bytes_length(name);
    char what[160];
    snprintf(what, sizeof(what), "a %.*s record", (int)(length > 140 ? 140 : length),
             (const char*)oti_bytes_data(name));
    return oti_type_error(rt, rt->global_info[ot_to_int(cell)].name, what, record);
  }
  *index = (size_t)ot_to_int(at);
  return true;
}

static ot_value record_get_at(ot_state* rt, size_t first, size_t count) {
  size_t index;
  if (!accessor_args(rt, first, count, 4, &index)) return 0;
  return oti_as_record(rt->stack[first])->values[index];
}

static ot_value record_set_at(ot_state* rt, size_t first, size_t count) {
  size_t index;
  if (!accessor_args(rt, first, count, 5, &index)) return 0;
  return oti_as_record(rt->stack[first])->values[index] = rt->stack[first + 4];
}

/* Define the global cell at first as a record accessor, which only another
 * call of this can change. */
static ot_value record_define_accessor(ot_state* rt, size_t first, size_t count) {
  if (count != 2) return oti_fail(rt, "wrong number of arguments");
  ot_value cell = rt->stack[first];
  if (!ot_is_int(cell) || ot_to_int(cell) < 0 || (size_t)ot_to_int(cell) >= rt->global_count)
    return oti_fail(rt, "expected a global cell number");
  uint32_t global = (uint32_t)ot_to_int(cell);
  if (rt->core_locked && rt->global_info[global].space == 0) {
    oti_core_locked_error(rt, global);
    return 0;
  }
  rt->global_info[global].defined = rt->global_info[global].accessor = true;
  rt->global_info[global].macro = false;
  oti_set_global(rt, global, rt->stack[first + 1]);
  return OT_NIL;
}

ot_value oti_record_copy(ot_state* rt, ot_value record) {
  size_t n = oti_record_type_fields(oti_as_record(record)->type);
  ot_value live[] = {record};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 1);
  ot_value copy = oti_alloc(rt, K_RECORD, (uint32_t)n + 1, 0);
  if (copy) {
    oti_as_record(copy)->type = oti_as_record(live[0])->type;
    memcpy(oti_as_record(copy)->values, oti_as_record(live[0])->values, n * sizeof(ot_value));
  }
  oti_roots_pop(&rt->heap, &roots);
  return copy;
}

const oti_builtin_def oti_record_builtins[] = {
    {B_MAKE_RECORD_TYPE, "%make-record-type", record_make_type},
    {B_RECORD_NEW, "record-new", record_new},
    {B_RECORD_P, "record?", record_record_p},
    {B_RECORD_TYPE_P, "record-type?", record_type_p},
    {B_RECORD_TYPE, "record-type", record_type},
    {B_RECORD_REF, "record-ref", record_ref},
    {B_RECORD_SET, "record-set!", record_set},
    {B_RECORD_GET_AT, "%record-get", record_get_at},
    {B_RECORD_SET_AT, "%record-set!", record_set_at},
    {B_DEFINE_ACCESSOR, "%define-accessor!", record_define_accessor},
    {0, NULL, NULL},
};
