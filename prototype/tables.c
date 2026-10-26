#include "internal.h"
#include <string.h>
#include <stdlib.h>

static TableInfo* info(Value t) { return object_bytes(t); }

static Value* entries(Value t) { return object_fields(object_fields(t)[0]); }

static uint32_t* buckets(Value t) { return object_bytes(object_fields(t)[0]); }

static Value fail(Runtime* rt, const char* message) {
  rt_error(rt, message);
  return 0;
}

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
static size_t hash(Value key, unsigned depth) {
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

static bool freeze_key(Runtime* rt, Value key) {
  if (!rt_kind(key, K_PAIR) || object_frozen(key)) return true;
  Value* work = NULL;
  size_t count = 0, capacity = 0;
  if (!rt_grow(rt, (void**)&work, &capacity, 1, sizeof(Value))) return false;
  work[count++] = key;
  while (count) {
    Value pair = work[--count];
    if (object_frozen(pair)) continue;
    if (!rt_grow(rt, (void**)&work, &capacity, count + 2, sizeof(Value))) {
      free(work);
      return false;
    }
    object_freeze(pair);
    for (unsigned i = 0; i < 2; i++) {
      Value child = object_fields(pair)[i];
      if (rt_kind(child, K_PAIR) && !object_frozen(child)) work[count++] = child;
    }
  }
  free(work);
  return true;
}

static bool lookup(Runtime* rt, Value table, Value key, size_t* slot, size_t* entry) {
  size_t mask = info(table)->capacity * 2 - 1, tomb = SIZE_MAX;
  uint32_t* index = buckets(table);
  Value* data = entries(table);
  *slot = hash(key, 8) & mask;
  while (index[*slot]) {
    uint32_t candidate = index[*slot];
    if (candidate == UINT32_MAX) {
      if (tomb == SIZE_MAX) tomb = *slot;
    } else {
      Value found = data[2 * (size_t)(candidate - 1)];
      Value same = found == key ? V_TRUE : rt_equal(rt, found, key, true);
      if (!same) return false;
      if (same == V_TRUE) {
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

Value rt_table_get(Runtime* rt, Value table, Value key, Value fallback) {
  if (!freeze_key(rt, key)) return 0;
  if (!info(table)->count) return fallback;
  size_t slot, entry;
  if (!lookup(rt, table, key, &slot, &entry)) return 0;
  return entry == SIZE_MAX ? fallback : entries(table)[2 * entry + 1];
}

static Value new_table(Runtime* rt) {
  Value table = rt_alloc(rt, K_TABLE, 1, sizeof(TableInfo));
  if (table) *info(table) = (TableInfo){0};
  return table;
}

static bool resize(Runtime* rt, Value* table) {
  size_t capacity = info(*table)->capacity;
  if (!capacity) capacity = 8;
  else if (info(*table)->count > capacity / 2) {
    if (capacity >= (UINT32_MAX / 4 + 1)) return rt_error(rt, "table too large");
    capacity *= 2;
  }
  /* The caller supplies a rooted slot, updated by allocation. */
  Value storage =
      rt_alloc(rt, K_TABLE_DATA, (uint32_t)(2 * capacity), 2 * capacity * sizeof(uint32_t));
  if (!storage) return false;
  Value* data = object_fields(storage);
  uint32_t* index = object_bytes(storage);
  size_t count = 0;
  for (size_t i = 0; i < info(*table)->used; i++) {
    Value key = entries(*table)[2 * i], value = entries(*table)[2 * i + 1];
    if (value == V_NIL) continue;
    size_t slot = hash(key, 8) & (2 * capacity - 1);
    while (index[slot]) slot = (slot + 1) & (2 * capacity - 1);
    index[slot] = (uint32_t)count + 1;
    data[2 * count] = key;
    data[2 * count + 1] = value;
    count++;
  }
  object_fields(*table)[0] = storage;
  *info(*table) = (TableInfo){count, count, capacity};
  return true;
}

Value rt_table_set(Runtime* rt, Value table, Value key, Value value) {
  if (!freeze_key(rt, key)) return 0;
  size_t slot = 0, entry = SIZE_MAX;
  if (info(table)->capacity && !lookup(rt, table, key, &slot, &entry)) return 0;
  if (entry != SIZE_MAX) {
    entries(table)[2 * entry + 1] = value;
    if (value == V_NIL) {
      entries(table)[2 * entry] = V_NIL;
      buckets(table)[slot] = UINT32_MAX;
      info(table)->count--;
    }
    return table;
  }
  if (value == V_NIL) return table;
  if (info(table)->used == info(table)->capacity) {
    Value live[] = {table, key, value};
    Roots root;
    roots_push(&rt->heap, &root, live, 3);
    bool ok = resize(rt, &live[0]);
    roots_pop(&rt->heap, &root);
    if (!ok) return 0;
    table = live[0];
    key = live[1];
    value = live[2];
    slot = hash(key, 8) & (2 * info(table)->capacity - 1);
    while (buckets(table)[slot]) slot = (slot + 1) & (2 * info(table)->capacity - 1);
  }
  entry = info(table)->used++;
  info(table)->count++;
  entries(table)[2 * entry] = key;
  entries(table)[2 * entry + 1] = value;
  buckets(table)[slot] = (uint32_t)entry + 1;
  return table;
}

Value rt_table_builtin(Runtime* rt, unsigned id, size_t first, size_t count) {
  if (id == B_TABLE) {
    if (count % 2) return fail(rt, "table needs key/value pairs");
    Value table = new_table(rt);
    if (!table) return 0;
    Roots root;
    roots_push(&rt->heap, &root, &table, 1);
    for (size_t i = 0; i < count; i += 2) {
      if (!rt_table_set(rt, table, rt->stack[first + i], rt->stack[first + i + 1])) {
        table = 0;
        break;
      }
    }
    roots_pop(&rt->heap, &root);
    return table;
  }
  size_t min = id == B_PUT_MUT ? 3 : id == B_GET || id == B_CONTAINS ? 2 : 1;
  if (count < min ||
      count > (id == B_GET       ? 3
               : id == B_PUT_MUT ? SIZE_MAX
                                 : min) ||
      (id == B_PUT_MUT && !(count % 2)))
    return fail(rt, "wrong number of collection arguments");
  Value table = rt->stack[first];
  if (id == B_TABLE_P) return rt_kind(table, K_TABLE) ? V_TRUE : V_FALSE;
  if (!rt_kind(table, K_TABLE)) return fail(rt, "expected a table");
  switch (id) {
    case B_LENGTH: return value_int((intptr_t)info(table)->count);
    case B_GET:
      return rt_table_get(rt, table, rt->stack[first + 1],
                          count == 3 ? rt->stack[first + 2] : V_NIL);
    case B_CONTAINS: {
      Value value = rt_table_get(rt, table, rt->stack[first + 1], V_NIL);
      return !value ? 0 : value == V_NIL ? V_FALSE : V_TRUE;
    }
    case B_PUT_MUT:
      for (size_t i = 1; i < count; i += 2)
        if (!rt_table_set(rt, rt->stack[first], rt->stack[first + i], rt->stack[first + i + 1]))
          return 0;
      return rt->stack[first];
    case B_COPY: {
      Value copy = new_table(rt);
      if (!copy) return 0;
      table = rt->stack[first];
      size_t capacity = info(table)->capacity;
      if (!capacity) return copy;
      Roots root;
      roots_push(&rt->heap, &root, &copy, 1);
      Value storage =
          rt_alloc(rt, K_TABLE_DATA, (uint32_t)(2 * capacity), 2 * capacity * sizeof(uint32_t));
      roots_pop(&rt->heap, &root);
      if (!storage) return 0;
      table = rt->stack[first];
      memcpy(object_fields(storage), entries(table), 2 * capacity * sizeof(Value));
      memcpy(object_bytes(storage), buckets(table), 2 * capacity * sizeof(uint32_t));
      object_fields(copy)[0] = storage;
      *info(copy) = *info(table);
      return copy;
    }
    case B_KEYS:
    case B_VALUES: {
      Value array = rt_alloc(rt, K_ARRAY, 1, sizeof(ArrayInfo));
      if (!array) return 0;
      table = rt->stack[first];
      size_t length = info(table)->count;
      *(ArrayInfo*)object_bytes(array) = (ArrayInfo){length, length};
      if (!length) return array;
      Roots root;
      roots_push(&rt->heap, &root, &array, 1);
      Value storage = rt_alloc(rt, K_ARRAY_DATA, (uint32_t)length, 0);
      roots_pop(&rt->heap, &root);
      if (!storage) return 0;
      table = rt->stack[first];
      size_t out = 0;
      for (size_t i = 0; i < info(table)->used; i++)
        if (entries(table)[2 * i + 1] != V_NIL)
          object_fields(storage)[out++] = entries(table)[2 * i + (id == B_VALUES)];
      object_fields(array)[0] = storage;
      return array;
    }
  }
  return fail(rt, "unsupported table operation");
}
