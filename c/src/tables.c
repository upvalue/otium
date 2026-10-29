#include "internal.h"
#include <string.h>
#include <stdlib.h>

static ot_value* entries(ot_value t) { return oti_as_table_data(oti_as_table(t)->data)->entries; }

static uint32_t* buckets(ot_value t) { return oti_table_buckets(oti_as_table(t)->data); }

static size_t mix(size_t x) {
  x ^= x >> 16;
  x *= (size_t)0x85ebca6bu;
  x ^= x >> 13;
  x *= (size_t)0xc2b2ae35u;
  x ^= x >> 16;
  return x;
}

/* A bounded structural prefix keeps hashing cyclic or deeply nested pair keys
 * finite. Equal keys have equal prefixes; collisions use full key equality. */
static size_t hash(ot_value key, unsigned depth) {
  if (!value_is_ptr(key)) return mix(key);
  unsigned kind = object_kind(key);
  if (kind == K_PAIR) {
    if (!depth) return mix(K_PAIR);
    return mix(hash(oti_car(key), depth - 1) ^ (hash(oti_cdr(key), depth - 1) * 33) ^ K_PAIR);
  }
  if (kind == K_STRING || kind == K_SYMBOL || kind == K_KEYWORD) {
    size_t length = oti_bytes_length(key), result = kind;
    const unsigned char* text = oti_bytes_data(key);
    for (size_t i = 0; i < length; i++) result = result * 33 + text[i];
    return mix(result);
  }
  if (kind == K_FLOAT) {
    double d = oti_as_float(key)->value;
    uint64_t bits;
    if (d == 0) d = 0;
    memcpy(&bits, &d, sizeof(bits));
    return mix((size_t)bits ^ (size_t)(bits >> 32) ^ K_FLOAT);
  }
  return mix(object_identity(key) ^ kind);
}

static bool freeze_key(ot_state* rt, ot_value key) {
  if (!oti_kind(key, K_PAIR) || object_frozen(key)) return true;
  ot_value* work = NULL;
  size_t count = 0, capacity = 0;
  if (!oti_grow(rt, (void**)&work, &capacity, 1, sizeof(ot_value))) return false;
  work[count++] = key;
  while (count) {
    ot_value pair = work[--count];
    if (object_frozen(pair)) continue;
    if (!oti_grow(rt, (void**)&work, &capacity, count + 2, sizeof(ot_value))) {
      free(work);
      return false;
    }
    object_freeze(pair);
    ot_value children[] = {oti_car(pair), oti_cdr(pair)};
    for (unsigned i = 0; i < 2; i++) {
      ot_value child = children[i];
      if (oti_kind(child, K_PAIR) && !object_frozen(child)) work[count++] = child;
    }
  }
  free(work);
  return true;
}

static bool lookup(ot_state* rt, ot_value table, ot_value key, size_t* slot, size_t* entry) {
  size_t mask = oti_as_table(table)->capacity * 2 - 1, tomb = SIZE_MAX;
  uint32_t* index = buckets(table);
  ot_value* data = entries(table);
  *slot = hash(key, 8) & mask;
  while (index[*slot]) {
    uint32_t candidate = index[*slot];
    if (candidate == UINT32_MAX) {
      if (tomb == SIZE_MAX) tomb = *slot;
    } else {
      ot_value found = data[2 * (size_t)(candidate - 1)];
      ot_value same = found == key ? OT_TRUE : oti_equal(rt, found, key, true);
      if (!same) return false;
      if (same == OT_TRUE) {
        *entry = candidate - 1;
        return true;
      }
    }
    *slot = (*slot + 1) & mask;
  }
  if (tomb != SIZE_MAX) *slot = tomb;
  *entry = SIZE_MAX;
  return true;
}

ot_value oti_table_get(ot_state* rt, ot_value table, ot_value key, ot_value fallback) {
  if (!freeze_key(rt, key)) return 0;
  if (!oti_as_table(table)->count) return fallback;
  size_t slot, entry;
  if (!lookup(rt, table, key, &slot, &entry)) return 0;
  return entry == SIZE_MAX ? fallback : entries(table)[2 * entry + 1];
}

ot_value oti_table_new(ot_state* rt) {
  ot_value table = oti_alloc(rt, K_TABLE, 1, OTI_BYTES(oti_table_obj, 1));
  if (table) {
    oti_table_obj* t = oti_as_table(table);
    t->count = t->used = t->capacity = 0;
  }
  return table;
}

static bool resize(ot_state* rt, ot_value* table) {
  size_t capacity = oti_as_table(*table)->capacity;
  if (!capacity) capacity = 8;
  else if (oti_as_table(*table)->count > capacity / 2) {
    if (capacity >= (UINT32_MAX / 4 + 1)) return oti_error(rt, "table too large");
    capacity *= 2;
  }
  /* The caller supplies a rooted slot, updated by allocation. */
  ot_value storage =
      oti_alloc(rt, K_TABLE_DATA, (uint32_t)(2 * capacity), 2 * capacity * sizeof(uint32_t));
  if (!storage) return false;
  ot_value* data = oti_as_table_data(storage)->entries;
  uint32_t* index = oti_table_buckets(storage);
  size_t count = 0;
  for (size_t i = 0; i < oti_as_table(*table)->used; i++) {
    ot_value key = entries(*table)[2 * i], value = entries(*table)[2 * i + 1];
    if (value == OT_NIL) continue;
    size_t slot = hash(key, 8) & (2 * capacity - 1);
    while (index[slot]) slot = (slot + 1) & (2 * capacity - 1);
    index[slot] = (uint32_t)count + 1;
    data[2 * count] = key;
    data[2 * count + 1] = value;
    count++;
  }
  oti_table_obj* t = oti_as_table(*table);
  t->data = storage;
  oti_write(rt, *table);
  t->count = t->used = count;
  t->capacity = capacity;
  return true;
}

ot_value oti_table_set(ot_state* rt, ot_value table, ot_value key, ot_value value) {
  if (!freeze_key(rt, key)) return 0;
  size_t slot = 0, entry = SIZE_MAX;
  if (oti_as_table(table)->capacity && !lookup(rt, table, key, &slot, &entry)) return 0;
  if (entry != SIZE_MAX) {
    entries(table)[2 * entry + 1] = value;
    oti_write(rt, oti_as_table(table)->data);
    if (value == OT_NIL) {
      entries(table)[2 * entry] = OT_NIL;
      buckets(table)[slot] = UINT32_MAX;
      oti_as_table(table)->count--;
    }
    return table;
  }
  if (value == OT_NIL) return table;
  if (oti_as_table(table)->used == oti_as_table(table)->capacity) {
    ot_value live[] = {table, key, value};
    oti_roots root;
    oti_roots_push(&rt->heap, &root, live, 3);
    bool ok = resize(rt, &live[0]);
    oti_roots_pop(&rt->heap, &root);
    if (!ok) return 0;
    table = live[0];
    key = live[1];
    value = live[2];
    slot = hash(key, 8) & (2 * oti_as_table(table)->capacity - 1);
    while (buckets(table)[slot]) slot = (slot + 1) & (2 * oti_as_table(table)->capacity - 1);
  }
  entry = oti_as_table(table)->used++;
  oti_as_table(table)->count++;
  entries(table)[2 * entry] = key;
  entries(table)[2 * entry + 1] = value;
  oti_write(rt, oti_as_table(table)->data);
  buckets(table)[slot] = (uint32_t)entry + 1;
  return table;
}

static ot_value table_table(ot_state* rt, size_t first, size_t count) {
  if (count % 2) return oti_fail(rt, "table needs key/value pairs");
  ot_value table = oti_table_new(rt);
  if (!table) return 0;
  oti_roots root;
  oti_roots_push(&rt->heap, &root, &table, 1);
  for (size_t i = 0; i < count; i += 2) {
    if (!oti_table_set(rt, table, rt->stack[first + i], rt->stack[first + i + 1])) {
      table = 0;
      break;
    }
  }
  oti_roots_pop(&rt->heap, &root);
  return table;
}

static bool table_arity(ot_state* rt, size_t count, size_t min, size_t max) {
  if (count >= min && count <= max) return true;
  return oti_error(rt, "wrong number of collection arguments");
}

/* Checks the argument count, then that the first argument is a table. */
static bool table_arguments(ot_state* rt, const char* who, size_t first, size_t count, size_t min,
                            size_t max) {
  if (!table_arity(rt, count, min, max)) return false;
  if (oti_kind(rt->stack[first], K_TABLE)) return true;
  return oti_type_error(rt, who, "table", rt->stack[first]);
}

static ot_value table_table_p(ot_state* rt, size_t first, size_t count) {
  if (!table_arity(rt, count, 1, 1)) return 0;
  return oti_kind(rt->stack[first], K_TABLE) ? OT_TRUE : OT_FALSE;
}

ot_value oti_table_length_builtin(ot_state* rt, size_t first, size_t count) {
  if (!table_arguments(rt, "length", first, count, 1, 1)) return 0;
  return ot_int((intptr_t)oti_as_table(rt->stack[first])->count);
}

ot_value oti_table_get_builtin(ot_state* rt, size_t first, size_t count) {
  if (!table_arguments(rt, "get", first, count, 2, 3)) return 0;
  return oti_table_get(rt, rt->stack[first], rt->stack[first + 1],
                       count == 3 ? rt->stack[first + 2] : OT_NIL);
}

ot_value oti_table_contains_builtin(ot_state* rt, size_t first, size_t count) {
  if (!table_arguments(rt, "contains?", first, count, 2, 2)) return 0;
  ot_value value = oti_table_get(rt, rt->stack[first], rt->stack[first + 1], OT_NIL);
  return !value ? 0 : value == OT_NIL ? OT_FALSE : OT_TRUE;
}

ot_value oti_table_put_builtin(ot_state* rt, size_t first, size_t count) {
  if (!table_arity(rt, count, 3, SIZE_MAX)) return 0;
  if (!(count % 2)) return oti_fail(rt, "wrong number of collection arguments");
  if (!oti_kind(rt->stack[first], K_TABLE)) {
    oti_type_error(rt, "put!", "table", rt->stack[first]);
    return 0;
  }
  for (size_t i = 1; i < count; i += 2)
    if (!oti_table_set(rt, rt->stack[first], rt->stack[first + i], rt->stack[first + i + 1]))
      return 0;
  return rt->stack[first];
}

ot_value oti_table_copy_builtin(ot_state* rt, size_t first, size_t count) {
  if (!table_arguments(rt, "copy", first, count, 1, 1)) return 0;
  ot_value copy = oti_table_new(rt);
  if (!copy) return 0;
  ot_value table = rt->stack[first];
  size_t capacity = oti_as_table(table)->capacity;
  if (!capacity) return copy;
  oti_roots root;
  oti_roots_push(&rt->heap, &root, &copy, 1);
  ot_value storage =
      oti_alloc(rt, K_TABLE_DATA, (uint32_t)(2 * capacity), 2 * capacity * sizeof(uint32_t));
  oti_roots_pop(&rt->heap, &root);
  if (!storage) return 0;
  table = rt->stack[first];
  memcpy(oti_as_table_data(storage)->entries, entries(table), 2 * capacity * sizeof(ot_value));
  memcpy(oti_table_buckets(storage), buckets(table), 2 * capacity * sizeof(uint32_t));
  oti_table_obj *to = oti_as_table(copy), *from = oti_as_table(table);
  to->data = storage;
  oti_write(rt, copy);
  to->count = from->count;
  to->used = from->used;
  to->capacity = from->capacity;
  return copy;
}

/* Shared by keys and values, which differ only in which half of each entry they take. */
static ot_value table_keys_or_values(ot_state* rt, size_t first, size_t count, bool values) {
  if (count == 1 && rt->stack[first] == OT_NIL) return oti_array_from(rt, first, 0);
  if (!table_arguments(rt, values ? "values" : "keys", first, count, 1, 1)) return 0;
  ot_value array = oti_alloc(rt, K_ARRAY, 1, OTI_BYTES(oti_array_obj, 1));
  if (!array) return 0;
  ot_value table = rt->stack[first];
  size_t length = oti_as_table(table)->count;
  oti_as_array(array)->length = oti_as_array(array)->capacity = length;
  if (!length) return array;
  oti_roots root;
  oti_roots_push(&rt->heap, &root, &array, 1);
  ot_value storage = oti_alloc(rt, K_ARRAY_DATA, (uint32_t)length, 0);
  oti_roots_pop(&rt->heap, &root);
  if (!storage) return 0;
  table = rt->stack[first];
  size_t out = 0;
  for (size_t i = 0; i < oti_as_table(table)->used; i++)
    if (entries(table)[2 * i + 1] != OT_NIL)
      oti_as_array_data(storage)->items[out++] = entries(table)[2 * i + values];
  oti_as_array(array)->data = storage;
  oti_write(rt, array);
  return array;
}

static ot_value table_keys(ot_state* rt, size_t first, size_t count) {
  return table_keys_or_values(rt, first, count, false);
}

static ot_value table_values(ot_state* rt, size_t first, size_t count) {
  return table_keys_or_values(rt, first, count, true);
}

/* Collection builtins with no table version, such as push! on a table. */
static ot_value table_unsupported(ot_state* rt, size_t first, size_t count) {
  if (!table_arguments(rt, "collection operation", first, count, 1, 1)) return 0;
  return oti_fail(rt, "unsupported table operation");
}

ot_value oti_table_push_builtin(ot_state* rt, size_t first, size_t count) {
  return table_unsupported(rt, first, count);
}

ot_value oti_table_pop_builtin(ot_state* rt, size_t first, size_t count) {
  return table_unsupported(rt, first, count);
}

const oti_builtin_def oti_table_builtins[] = {
    {B_TABLE, "table", table_table},
    {B_TABLE_P, "table?", table_table_p},
    {B_KEYS, "keys", table_keys},
    {B_VALUES, "values", table_values},
    /* The VM handles apply before dispatch; this keeps the old fall-through. */
    {B_APPLY, "apply", table_unsupported},
    {0, NULL, NULL},
};
