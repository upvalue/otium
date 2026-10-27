#include "internal.h"
#include <stdlib.h>
#include <string.h>

static size_t field_count(ot_value type) { return ((oti_object_header*)type)->fields - 1; }

/* A record type's fields are its name, then one keyword per record value. */
static size_t field_index(ot_value type, ot_value key) {
  if (!oti_kind(key, K_KEYWORD)) return SIZE_MAX;
  for (size_t i = 0; i < field_count(type); i++)
    if (object_fields(type)[i + 1] == key) return i;
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
    object_fields(live[0])[0] = rt->stack[first];
    *(bool*)object_bytes(live[0]) = rt->stack[first + 2] == OT_TRUE;
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
        if (object_fields(live[0])[j + 1] == key) {
          oti_fail(rt, "duplicate record field");
          ok = false;
          break;
        }
      object_fields(live[0])[i + 1] = key;
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
  if (count - 1 != field_count(type)) return oti_fail(rt, "wrong number of record fields");
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
  if (*(bool*)object_bytes(type)) return oti_fail(rt, "cannot mutate immutable record");
  return oti_as_record(record)->values[index] = rt->stack[first + 2];
}

const oti_builtin_def oti_record_builtins[] = {
    {B_MAKE_RECORD_TYPE, "%make-record-type", record_make_type},
    {B_RECORD_NEW, "record-new", record_new},
    {B_RECORD_P, "record?", record_record_p},
    {B_RECORD_TYPE_P, "record-type?", record_type_p},
    {B_RECORD_TYPE, "record-type", record_type},
    {B_RECORD_REF, "record-ref", record_ref},
    {B_RECORD_SET, "record-set!", record_set},
    {0, NULL, NULL},
};
