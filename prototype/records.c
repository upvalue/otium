#include "internal.h"
#include <stdlib.h>
#include <string.h>

static size_t field_count(ot_value type) { return ((oti_object_header*)type)->fields - 1; }

static size_t field_index(ot_value type, ot_value key) {
  if (!oti_kind(key, K_KEYWORD)) return SIZE_MAX;
  for (size_t i = 0; i < field_count(type); i++)
    if (object_fields(type)[i + 1] == key) return i + 1;
  return SIZE_MAX;
}

ot_value oti_record_call(ot_state* rt, ot_value record, size_t first, size_t count) {
  if (count != 1) return oti_fail(rt, "record call needs one field keyword");
  size_t index = field_index(object_fields(record)[0], rt->stack[first]);
  return index == SIZE_MAX ? oti_fail(rt, "unknown record field") : object_fields(record)[index];
}

static ot_value make_type(ot_state* rt, size_t first, size_t count) {
  if (count != 3 || !oti_kind(rt->stack[first], K_SYMBOL) ||
      (rt->stack[first + 2] != OT_TRUE && rt->stack[first + 2] != OT_FALSE))
    return oti_fail(rt, "invalid record type declaration");
  ot_value cursor = rt->stack[first + 1], slow = cursor, fast = cursor;
  size_t n = 0;
  while (oti_kind(cursor, K_PAIR)) {
    if (!oti_kind(object_fields(cursor)[0], K_SYMBOL))
      return oti_fail(rt, "record fields must be symbols");
    if (n == UINT32_MAX - 1) return oti_fail(rt, "too many record fields");
    n++;
    cursor = object_fields(cursor)[1];
    if (oti_kind(slow, K_PAIR)) slow = object_fields(slow)[1];
    for (unsigned i = 0; i < 2 && oti_kind(fast, K_PAIR); i++) fast = object_fields(fast)[1];
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
    ot_value name = object_fields(live[1])[0];
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
    live[1] = object_fields(live[1])[1];
  }
  ot_value result = ok ? live[0] : 0;
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

ot_value oti_record_builtin(ot_state* rt, unsigned id, size_t first, size_t count) {
  if (id == B_MAKE_RECORD_TYPE) return make_type(rt, first, count);
  size_t wanted = id == B_RECORD_REF ? 2 : id == B_RECORD_SET ? 3 : 1;
  if (id != B_RECORD_NEW && count != wanted)
    return oti_fail(rt, "wrong number of record arguments");
  if (!count) return oti_fail(rt, "record-new needs a record type");
  ot_value v = rt->stack[first];
  if (id == B_RECORD_P) return oti_kind(v, K_RECORD) ? OT_TRUE : OT_FALSE;
  if (id == B_RECORD_TYPE_P) return oti_kind(v, K_RECORD_TYPE) ? OT_TRUE : OT_FALSE;
  if (id == B_RECORD_NEW) {
    if (!oti_kind(v, K_RECORD_TYPE)) return oti_fail(rt, "expected record type");
    if (count - 1 != field_count(v)) return oti_fail(rt, "wrong number of record fields");
    ot_value record = oti_alloc(rt, K_RECORD, (uint32_t)count, 0);
    if (record) memcpy(object_fields(record), rt->stack + first, count * sizeof(ot_value));
    return record;
  }
  if (!oti_kind(v, K_RECORD)) return oti_fail(rt, "expected record instance");
  ot_value type = object_fields(v)[0];
  if (id == B_RECORD_TYPE) return type;
  size_t index = field_index(type, rt->stack[first + 1]);
  if (index == SIZE_MAX) return oti_fail(rt, "unknown record field");
  if (id == B_RECORD_REF) return object_fields(v)[index];
  if (*(bool*)object_bytes(type)) return oti_fail(rt, "cannot mutate immutable record");
  return object_fields(v)[index] = rt->stack[first + 2];
}
