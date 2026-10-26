#include "internal.h"
#include <string.h>
#include <stdlib.h>

static oti_table_info* table_info(ot_value t) { return object_bytes(t); }

static ot_value* entries(ot_value t) { return object_fields(object_fields(t)[0]); }

static uint32_t* buckets(ot_value t) { return object_bytes(object_fields(t)[0]); }

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
    return mix(hash(object_fields(key)[0], depth - 1) ^
               (hash(object_fields(key)[1], depth - 1) * 33) ^ K_PAIR);
  }
  if (kind == K_STRING || kind == K_SYMBOL || kind == K_KEYWORD) {
    size_t length, result = kind;
    unsigned char* text = object_bytes(key);
    memcpy(&length, text, sizeof(length));
    for (size_t i = 0; i < length; i++) result = result * 33 + text[sizeof(length) + i];
    return mix(result);
  }
  if (kind == K_FLOAT) {
    double d;
    uint64_t bits;
    memcpy(&d, object_bytes(key), sizeof(d));
    if (d == 0) d = 0;
    memcpy(&bits, &d, sizeof(bits));
    return mix((size_t)bits ^ (size_t)(bits >> 32) ^ K_FLOAT);
  }
  return mix(object_identity(key) ^ kind);
}

static bool freeze_key(ot_runtime* rt, ot_value key) {
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
    for (unsigned i = 0; i < 2; i++) {
      ot_value child = object_fields(pair)[i];
      if (oti_kind(child, K_PAIR) && !object_frozen(child)) work[count++] = child;
    }
  }
  free(work);
  return true;
}

static bool lookup(ot_runtime* rt, ot_value table, ot_value key, size_t* slot, size_t* entry) {
  size_t mask = table_info(table)->capacity * 2 - 1, tomb = SIZE_MAX;
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

ot_value oti_table_get(ot_runtime* rt, ot_value table, ot_value key, ot_value fallback) {
  if (!freeze_key(rt, key)) return 0;
  if (!table_info(table)->count) return fallback;
  size_t slot, entry;
  if (!lookup(rt, table, key, &slot, &entry)) return 0;
  return entry == SIZE_MAX ? fallback : entries(table)[2 * entry + 1];
}

static ot_value new_table(ot_runtime* rt) {
  ot_value table = oti_alloc(rt, K_TABLE, 1, sizeof(oti_table_info));
  if (table) *table_info(table) = (oti_table_info){0};
  return table;
}

static bool resize(ot_runtime* rt, ot_value* table) {
  size_t capacity = table_info(*table)->capacity;
  if (!capacity) capacity = 8;
  else if (table_info(*table)->count > capacity / 2) {
    if (capacity >= (UINT32_MAX / 4 + 1)) return oti_error(rt, "table too large");
    capacity *= 2;
  }
  /* The caller supplies a rooted slot, updated by allocation. */
  ot_value storage =
      oti_alloc(rt, K_TABLE_DATA, (uint32_t)(2 * capacity), 2 * capacity * sizeof(uint32_t));
  if (!storage) return false;
  ot_value* data = object_fields(storage);
  uint32_t* index = object_bytes(storage);
  size_t count = 0;
  for (size_t i = 0; i < table_info(*table)->used; i++) {
    ot_value key = entries(*table)[2 * i], value = entries(*table)[2 * i + 1];
    if (value == OT_NIL) continue;
    size_t slot = hash(key, 8) & (2 * capacity - 1);
    while (index[slot]) slot = (slot + 1) & (2 * capacity - 1);
    index[slot] = (uint32_t)count + 1;
    data[2 * count] = key;
    data[2 * count + 1] = value;
    count++;
  }
  object_fields(*table)[0] = storage;
  *table_info(*table) = (oti_table_info){count, count, capacity};
  return true;
}

ot_value oti_table_set(ot_runtime* rt, ot_value table, ot_value key, ot_value value) {
  if (!freeze_key(rt, key)) return 0;
  size_t slot = 0, entry = SIZE_MAX;
  if (table_info(table)->capacity && !lookup(rt, table, key, &slot, &entry)) return 0;
  if (entry != SIZE_MAX) {
    entries(table)[2 * entry + 1] = value;
    if (value == OT_NIL) {
      entries(table)[2 * entry] = OT_NIL;
      buckets(table)[slot] = UINT32_MAX;
      table_info(table)->count--;
    }
    return table;
  }
  if (value == OT_NIL) return table;
  if (table_info(table)->used == table_info(table)->capacity) {
    ot_value live[] = {table, key, value};
    oti_roots root;
    oti_roots_push(&rt->heap, &root, live, 3);
    bool ok = resize(rt, &live[0]);
    oti_roots_pop(&rt->heap, &root);
    if (!ok) return 0;
    table = live[0];
    key = live[1];
    value = live[2];
    slot = hash(key, 8) & (2 * table_info(table)->capacity - 1);
    while (buckets(table)[slot]) slot = (slot + 1) & (2 * table_info(table)->capacity - 1);
  }
  entry = table_info(table)->used++;
  table_info(table)->count++;
  entries(table)[2 * entry] = key;
  entries(table)[2 * entry + 1] = value;
  buckets(table)[slot] = (uint32_t)entry + 1;
  return table;
}

ot_value oti_table_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count) {
  if (id == B_TABLE) {
    if (count % 2) return oti_fail(rt, "table needs key/value pairs");
    ot_value table = new_table(rt);
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
  size_t min = id == B_PUT_MUT ? 3 : id == B_GET || id == B_CONTAINS ? 2 : 1;
  if (count < min ||
      count > (id == B_GET       ? 3
               : id == B_PUT_MUT ? SIZE_MAX
                                 : min) ||
      (id == B_PUT_MUT && !(count % 2)))
    return oti_fail(rt, "wrong number of collection arguments");
  ot_value table = rt->stack[first];
  if (id == B_TABLE_P) return oti_kind(table, K_TABLE) ? OT_TRUE : OT_FALSE;
  if (!oti_kind(table, K_TABLE)) return oti_fail(rt, "expected a table");
  switch (id) {
    case B_LENGTH: return ot_int((intptr_t)table_info(table)->count);
    case B_GET:
      return oti_table_get(rt, table, rt->stack[first + 1],
                           count == 3 ? rt->stack[first + 2] : OT_NIL);
    case B_CONTAINS: {
      ot_value value = oti_table_get(rt, table, rt->stack[first + 1], OT_NIL);
      return !value ? 0 : value == OT_NIL ? OT_FALSE : OT_TRUE;
    }
    case B_PUT_MUT:
      for (size_t i = 1; i < count; i += 2)
        if (!oti_table_set(rt, rt->stack[first], rt->stack[first + i], rt->stack[first + i + 1]))
          return 0;
      return rt->stack[first];
    case B_COPY: {
      ot_value copy = new_table(rt);
      if (!copy) return 0;
      table = rt->stack[first];
      size_t capacity = table_info(table)->capacity;
      if (!capacity) return copy;
      oti_roots root;
      oti_roots_push(&rt->heap, &root, &copy, 1);
      ot_value storage =
          oti_alloc(rt, K_TABLE_DATA, (uint32_t)(2 * capacity), 2 * capacity * sizeof(uint32_t));
      oti_roots_pop(&rt->heap, &root);
      if (!storage) return 0;
      table = rt->stack[first];
      memcpy(object_fields(storage), entries(table), 2 * capacity * sizeof(ot_value));
      memcpy(object_bytes(storage), buckets(table), 2 * capacity * sizeof(uint32_t));
      object_fields(copy)[0] = storage;
      *table_info(copy) = *table_info(table);
      return copy;
    }
    case B_KEYS:
    case B_VALUES: {
      ot_value array = oti_alloc(rt, K_ARRAY, 1, sizeof(oti_array_info));
      if (!array) return 0;
      table = rt->stack[first];
      size_t length = table_info(table)->count;
      *(oti_array_info*)object_bytes(array) = (oti_array_info){length, length};
      if (!length) return array;
      oti_roots root;
      oti_roots_push(&rt->heap, &root, &array, 1);
      ot_value storage = oti_alloc(rt, K_ARRAY_DATA, (uint32_t)length, 0);
      oti_roots_pop(&rt->heap, &root);
      if (!storage) return 0;
      table = rt->stack[first];
      size_t out = 0;
      for (size_t i = 0; i < table_info(table)->used; i++)
        if (entries(table)[2 * i + 1] != OT_NIL)
          object_fields(storage)[out++] = entries(table)[2 * i + (id == B_VALUES)];
      object_fields(array)[0] = storage;
      return array;
    }
  }
  return oti_fail(rt, "unsupported table operation");
}
