#include "internal.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* Tables are insertion-ordered hash maps.
 *
 * A table's key/value pairs sit in one K_TABLE_DATA object in the order their
 * keys were first stored. Storing nil deletes: the pair becomes a hole, nil
 * twice, so that the pairs after it keep their places. Iteration walks the
 * pairs and skips holes.
 *
 * Up to TABLE_SMALL pairs are found by scanning them, so a small table is its
 * pairs and nothing else, sized to the literal that made it. A bigger table
 * follows its pairs with an open-addressed index of pair numbers, twice as
 * many slots as pairs so it's never more than half full, and one, two or four
 * bytes a slot as the capacity needs.
 *
 * Holes go when a table is rebuilt. Storing a new key in a full table rebuilds
 * it: into storage twice the size when over half the pairs are live, in place
 * when they aren't, and into less when under an eighth are. Deleting rebuilds
 * in place once holes outnumber live pairs, and drops the storage of an
 * indexed table that empties. So deleting never allocates and can't fail,
 * each operation is O(1) amortized, and a table that shrinks gives its memory
 * back when it next takes a new key. */

enum { TABLE_SMALL = 8 };

/* The storage object's size and field count are 32 bits. */
#define TABLE_MAX_CAPACITY ((uint32_t)1 << 27)
#define PAIR_ABSENT UINT32_MAX
#define PAIR_FAILED (UINT32_MAX - 1)

/* What an index slot holds: nothing, the place of a deleted pair, which a
 * search has to pass, or a pair's number plus SLOT_PAIR. */
enum { SLOT_EMPTY, SLOT_DEAD, SLOT_PAIR };

static ot_value* entries(ot_value t) { return oti_as_table_data(oti_as_table(t)->data)->entries; }

static unsigned index_width(uint32_t capacity) {
  return capacity <= 128 ? 1 : capacity <= 32768 ? 2 : 4;
}

static size_t index_bytes(uint32_t capacity) {
  return capacity <= TABLE_SMALL ? 0 : 2 * (size_t)capacity * index_width(capacity);
}

static inline uint32_t slot_get(const void* index, unsigned width, size_t slot) {
  if (width == 1) return ((const uint8_t*)index)[slot];
  if (width == 2) return ((const uint16_t*)index)[slot];
  return ((const uint32_t*)index)[slot];
}

static inline void slot_set(void* index, unsigned width, size_t slot, uint32_t value) {
  if (width == 1) ((uint8_t*)index)[slot] = (uint8_t)value;
  else if (width == 2) ((uint16_t*)index)[slot] = (uint16_t)value;
  else ((uint32_t*)index)[slot] = value;
}

/* The smallest capacity tables grow through that holds n pairs. */
static uint32_t capacity_holding(uint32_t n) {
  uint32_t capacity = n <= 4 ? 4 : n <= TABLE_SMALL ? TABLE_SMALL : 2 * TABLE_SMALL;
  while (capacity < n) capacity *= 2;
  return capacity;
}

/* Hashes are 32 bits, and the high ones pick the slot a search starts at.
 * One multiplication would spread consecutive integers perfectly, but it
 * leaves other evenly spaced keys in clumps: steps of 144 land a few slots
 * apart. Two, with a fold between, treat every spacing alike. */
static inline uint32_t mix(uint64_t x) {
  x *= 0x9e3779b97f4a7c15u;
  x ^= x >> 32;
  return (uint32_t)((x * 0xd6e8feb86659fd93u) >> 32);
}

static inline size_t home(uint32_t hash, uint32_t capacity) {
  return hash >> __builtin_clz(2 * capacity - 1);
}

/* Text is immutable, so a digest of it is worked out once and kept in its
 * header, where there are 23 bits for it. */
static uint32_t text_digest(ot_value key) {
  uint32_t h = object_text_hash(key);
  if (!h) {
    size_t length = oti_bytes_length(key);
    const unsigned char* text = oti_bytes_data(key);
    uint64_t x = 0xcbf29ce484222325u ^ object_kind(key);
    for (size_t i = 0; i < length; i++) x = (x ^ text[i]) * 0x100000001b3u;
    h = mix(x) >> 9;
    if (!h) h = 1;
    object_set_text_hash(key, h);
  }
  /* Spread the 23 bits over an index of any size. */
  return h * 0x9e3779b1u;
}

/* Pairs hash by structure, down to a fixed number of pairs so that cyclic and
 * very long keys cost no more. Equal keys are the same that far; keys that
 * differ only further in collide and are told apart by comparing them. */
static uint32_t hash_within(ot_value key, unsigned* pairs) {
  if (!value_is_ptr(key)) return mix(key);
  unsigned kind = object_kind(key);
  if (kind == K_STRING || kind == K_SYMBOL || kind == K_KEYWORD) return text_digest(key);
  if (kind == K_PAIR) {
    uint64_t h = K_PAIR;
    while (*pairs && oti_kind(key, K_PAIR)) {
      --*pairs;
      h = (h ^ hash_within(oti_car(key), pairs)) * 0x100000001b3u;
      key = oti_cdr(key);
    }
    if (!oti_kind(key, K_PAIR)) h ^= hash_within(key, pairs);
    return mix(h);
  }
  if (kind == K_FLOAT) {
    double d = oti_as_float(key)->value;
    uint64_t bits;
    if (d == 0) d = 0;   /* -0.0 is the same key as 0.0 */
    if (d != d) d = NAN; /* every NaN hashes alike, since equal? matches them */
    memcpy(&bits, &d, sizeof(bits));
    return mix(bits);
  }
  return mix(((uint64_t)kind << 32) | object_identity(key));
}

static uint32_t hash_object(ot_value key) {
  unsigned pairs = 32;
  return hash_within(key, &pairs);
}

static inline uint32_t hash(ot_value key) {
  return value_is_ptr(key) ? hash_object(key) : mix(key);
}

/* Whether a key can match a stored key that is a different object. Keywords
 * are always interned and everything else mutable keeps its identity. */
static inline bool structural(ot_value key) {
  if (!value_is_ptr(key)) return false;
  unsigned kind = object_kind(key);
  return kind == K_STRING || kind == K_SYMBOL || kind == K_FLOAT || kind == K_PAIR;
}

/* OT_TRUE or OT_FALSE as a stored key matches a structural one, as equal?
 * would have it, or 0 when comparing pairs failed. */
static ot_value key_equal(ot_state* rt, ot_value stored, ot_value key) {
  if (stored == key) return OT_TRUE;
  if (!value_is_ptr(stored)) return OT_FALSE;
  unsigned kind = object_kind(key);
  if (object_kind(stored) != kind) return OT_FALSE;
  if (kind == K_PAIR) return oti_equal(rt, stored, key, true);
  if (kind == K_FLOAT) {
    double x = oti_as_float(stored)->value, y = oti_as_float(key)->value;
    return ot_bool(x == y || (x != x && y != y));
  }
  uint32_t a = object_text_hash(stored), b = object_text_hash(key);
  if (a && b && a != b) return OT_FALSE;
  size_t length = oti_bytes_length(key);
  return ot_bool(oti_bytes_length(stored) == length &&
                 !memcmp(oti_bytes_data(stored), oti_bytes_data(key), length));
}

/* Freeze a pair key and the pairs it reaches, so its hash can't change. */
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

/* The number of key's pair, PAIR_ABSENT, or PAIR_FAILED when comparing keys
 * failed. For an indexed table a slot that isn't NULL gets the pair's slot,
 * or the one a new pair for the key should take. */
static uint32_t find_pair(ot_state* rt, ot_value table, ot_value key, size_t* slot) {
  oti_table_obj* t = oti_as_table(table);
  if (!t->capacity) return PAIR_ABSENT;
  const ot_value* data = entries(table);
  bool deep = structural(key);
  if (t->capacity <= TABLE_SMALL) {
    for (uint32_t i = 0; i < t->used; i++)
      if (data[2 * i] == key && data[2 * i + 1] != OT_NIL) return i;
    if (!deep) return PAIR_ABSENT;
    for (uint32_t i = 0; i < t->used; i++) {
      if (data[2 * i + 1] == OT_NIL) continue;
      ot_value same = key_equal(rt, data[2 * i], key);
      if (!same) return PAIR_FAILED;
      if (same == OT_TRUE) return i;
    }
    return PAIR_ABSENT;
  }
  const void* index = oti_table_index(t->data);
  unsigned width = index_width(t->capacity);
  size_t mask = 2 * (size_t)t->capacity - 1, at = home(hash(key), t->capacity), dead = SIZE_MAX;
  /* Pairs fill at most half the slots, so there is an empty one to stop at. */
  for (;; at = (at + 1) & mask) {
    uint32_t held = slot_get(index, width, at);
    if (held == SLOT_EMPTY) break;
    if (held == SLOT_DEAD) {
      if (dead == SIZE_MAX) dead = at;
      continue;
    }
    ot_value found = data[2 * (size_t)(held - SLOT_PAIR)];
    if (found != key) {
      if (!deep) continue;
      ot_value same = key_equal(rt, found, key);
      if (!same) return PAIR_FAILED;
      if (same != OT_TRUE) continue;
    }
    if (slot) *slot = at;
    return held - SLOT_PAIR;
  }
  if (slot) *slot = dead != SIZE_MAX ? dead : at;
  return PAIR_ABSENT;
}

ot_value oti_table_get(ot_state* rt, ot_value table, ot_value key, ot_value fallback) {
  uint32_t entry = find_pair(rt, table, key, NULL);
  if (entry == PAIR_ABSENT) return fallback;
  return entry == PAIR_FAILED ? 0 : entries(table)[2 * (size_t)entry + 1];
}

ot_value oti_table_new(ot_state* rt) {
  ot_value table = oti_alloc(rt, K_TABLE, 1, OTI_BYTES(oti_table_obj, 1));
  if (table) {
    oti_table_obj* t = oti_as_table(table);
    t->count = t->used = t->capacity = t->revision = 0;
  }
  return table;
}

static ot_value storage_new(ot_state* rt, uint32_t capacity) {
  return oti_alloc(rt, K_TABLE_DATA, 2 * capacity, index_bytes(capacity));
}

/* Index the first count pairs of storage, which are live. The index is empty. */
static void index_pairs(ot_value storage, uint32_t capacity, uint32_t count) {
  const ot_value* data = oti_as_table_data(storage)->entries;
  void* index = oti_table_index(storage);
  unsigned width = index_width(capacity);
  size_t mask = 2 * (size_t)capacity - 1;
  for (uint32_t i = 0; i < count; i++) {
    size_t at = home(hash(data[2 * (size_t)i]), capacity);
    while (slot_get(index, width, at) != SLOT_EMPTY) at = (at + 1) & mask;
    slot_set(index, width, at, i + SLOT_PAIR);
  }
}

/* Copy the live pairs of a table to the start of data, in order. */
static void copy_pairs(ot_value* data, ot_value from) {
  const ot_value* old = entries(from);
  size_t used = oti_as_table(from)->used, live = 0;
  for (size_t i = 0; i < used; i++) {
    if (old[2 * i + 1] == OT_NIL) continue;
    data[2 * live] = old[2 * i];
    data[2 * live + 1] = old[2 * i + 1];
    live++;
  }
}

/* Close the holes in a table's storage, where it is. */
static void compact(ot_value table) {
  oti_table_obj* t = oti_as_table(table);
  ot_value* data = entries(table);
  copy_pairs(data, table);
  for (size_t i = t->count; i < t->used; i++) data[2 * i] = data[2 * i + 1] = OT_NIL;
  t->used = t->count;
  if (t->capacity > TABLE_SMALL) {
    memset(oti_table_index(t->data), 0, index_bytes(t->capacity));
    index_pairs(t->data, t->capacity, t->count);
  }
}

/* Make room for a new pair in the table in a rooted slot, which allocation
 * updates: without holes, in storage the size its live pairs call for. */
static bool rebuild(ot_state* rt, ot_value* table) {
  uint32_t count = oti_as_table(*table)->count;
  uint32_t capacity = capacity_holding(count < TABLE_SMALL ? count + 1 : 2 * count);
  if (capacity > TABLE_MAX_CAPACITY) return oti_error(rt, "table too large");
  if (capacity == oti_as_table(*table)->capacity) {
    compact(*table);
    return true;
  }
  ot_value storage = storage_new(rt, capacity);
  if (!storage) return false;
  oti_table_obj* t = oti_as_table(*table);
  if (count) copy_pairs(oti_as_table_data(storage)->entries, *table);
  if (capacity > TABLE_SMALL) index_pairs(storage, capacity, count);
  t->data = storage;
  t->used = count;
  t->capacity = capacity;
  return true;
}

static void remove_pair(ot_value table, uint32_t entry, size_t slot) {
  oti_table_obj* t = oti_as_table(table);
  ot_value* data = entries(table);
  data[2 * (size_t)entry] = data[2 * (size_t)entry + 1] = OT_NIL;
  t->count--;
  t->revision++;
  if (t->capacity <= TABLE_SMALL) {
    /* Holes at the end cost nothing to take back. */
    while (t->used && data[2 * (size_t)t->used - 1] == OT_NIL) t->used--;
  } else if (!t->count) {
    t->data = OT_NIL;
    t->used = t->capacity = 0;
  } else {
    slot_set(oti_table_index(t->data), index_width(t->capacity), slot, SLOT_DEAD);
    /* Clearing the index costs the capacity, which is only worth paying
     * while the table isn't about to shrink. */
    if (t->used - t->count > t->count && t->count >= t->capacity / 8) compact(table);
  }
}

ot_value oti_table_set(ot_state* rt, ot_value table, ot_value key, ot_value value) {
  /* Deleting freezes the key as well, whether or not it was there. */
  if (!freeze_key(rt, key)) return 0;
  size_t slot = 0;
  uint32_t entry = find_pair(rt, table, key, &slot);
  if (entry == PAIR_FAILED) return 0;
  if (entry != PAIR_ABSENT) {
    if (value == OT_NIL) remove_pair(table, entry, slot);
    else entries(table)[2 * (size_t)entry + 1] = value;
    return table;
  }
  if (value == OT_NIL) return table;
  oti_table_obj* t = oti_as_table(table);
  if (t->used == t->capacity || (t->capacity > TABLE_SMALL && t->count < t->capacity / 8)) {
    ot_value live[] = {table, key, value};
    oti_roots root;
    oti_roots_push(&rt->heap, &root, live, 3);
    bool ok = rebuild(rt, &live[0]);
    oti_roots_pop(&rt->heap, &root);
    if (!ok) return 0;
    table = live[0];
    key = live[1];
    value = live[2];
    t = oti_as_table(table);
    if (t->capacity > TABLE_SMALL) {
      size_t mask = 2 * (size_t)t->capacity - 1;
      slot = home(hash(key), t->capacity);
      while (slot_get(oti_table_index(t->data), index_width(t->capacity), slot) != SLOT_EMPTY)
        slot = (slot + 1) & mask;
    }
  }
  entry = t->used++;
  t->count++;
  t->revision++;
  ot_value* data = entries(table);
  data[2 * (size_t)entry] = key;
  data[2 * (size_t)entry + 1] = value;
  if (t->capacity > TABLE_SMALL)
    slot_set(oti_table_index(t->data), index_width(t->capacity), slot, entry + SLOT_PAIR);
  return table;
}

static ot_value table_table(ot_state* rt, size_t first, size_t count) {
  if (count % 2) return oti_fail(rt, "table needs key/value pairs");
  ot_value table = oti_table_new(rt);
  if (!table) return 0;
  oti_roots root;
  oti_roots_push(&rt->heap, &root, &table, 1);
  /* A small literal gets exactly the room it needs. */
  if (count && count / 2 <= TABLE_SMALL) {
    ot_value storage = storage_new(rt, (uint32_t)(count / 2));
    if (storage) {
      oti_as_table(table)->data = storage;
      oti_as_table(table)->capacity = (uint32_t)(count / 2);
    } else table = 0;
  }
  for (size_t i = 0; table && i < count; i += 2)
    if (!oti_table_set(rt, table, rt->stack[first + i], rt->stack[first + i + 1])) table = 0;
  oti_roots_pop(&rt->heap, &root);
  return table;
}

/* Checks the argument count, then that the first argument is a table. */
static inline bool table_arguments(ot_state* rt, const char* who, size_t first, size_t count,
                                   size_t min, size_t max) {
  if (!oti_check_arity(rt, who, count, min, max)) return false;
  if (oti_kind(rt->stack[first], K_TABLE)) return true;
  return oti_type_error(rt, who, "table", rt->stack[first]);
}

static ot_value table_table_p(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "table?", count, 1, 1)) return 0;
  return oti_kind(rt->stack[first], K_TABLE) ? OT_TRUE : OT_FALSE;
}

ot_value oti_table_length_builtin(ot_state* rt, size_t first, size_t count) {
  if (!table_arguments(rt, "length", first, count, 1, 1)) return 0;
  return ot_int((intptr_t)oti_as_table(rt->stack[first])->count);
}

ot_value oti_table_get_builtin(ot_state* rt, size_t first, size_t count) {
  if (!table_arguments(rt, "get", first, count, 2, 3)) return 0;
  uint32_t entry = find_pair(rt, rt->stack[first], rt->stack[first + 1], NULL);
  if (entry == PAIR_ABSENT) return count == 3 ? rt->stack[first + 2] : OT_NIL;
  return entry == PAIR_FAILED ? 0 : entries(rt->stack[first])[2 * (size_t)entry + 1];
}

ot_value oti_table_put_builtin(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "put!", count, 3, SIZE_MAX)) return 0;
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

/* The copy has no holes and room to take another pair. */
ot_value oti_table_copy_builtin(ot_state* rt, size_t first, size_t count) {
  if (!table_arguments(rt, "copy", first, count, 1, 1)) return 0;
  ot_value copy = oti_table_new(rt);
  if (!copy) return 0;
  uint32_t live = oti_as_table(rt->stack[first])->count;
  if (!live) return copy;
  uint32_t capacity = capacity_holding(live + 1);
  oti_roots root;
  oti_roots_push(&rt->heap, &root, &copy, 1);
  ot_value storage = storage_new(rt, capacity);
  oti_roots_pop(&rt->heap, &root);
  if (!storage) return 0;
  ot_value table = rt->stack[first];
  oti_table_obj *to = oti_as_table(copy), *from = oti_as_table(table);
  ot_value* data = oti_as_table_data(storage)->entries;
  if (from->used == live && from->capacity == capacity) {
    memcpy(data, entries(table), 2 * (size_t)live * sizeof(ot_value));
    memcpy(oti_table_index(storage), oti_table_index(from->data), index_bytes(capacity));
  } else {
    copy_pairs(data, table);
    if (capacity > TABLE_SMALL) index_pairs(storage, capacity, live);
  }
  to->data = storage;
  to->count = to->used = live;
  to->capacity = capacity;
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

/* General bounded table scan. Result: (revision next done? scanned entries).
 * Each entry is (key . value). Replacements are live; membership changes fail.
 * At most 64 entries and 512 physical slots per call, including tombstones. */
static ot_value table_scan(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "table-scan", count, 3, 3)) return 0;
  if (!oti_kind(rt->stack[first], K_TABLE) || !ot_is_int(rt->stack[first + 1]) ||
      !ot_is_int(rt->stack[first + 2]))
    return oti_fail(rt, "table-scan expects table, revision and offset");
  intptr_t expected = ot_to_int(rt->stack[first + 1]), start = ot_to_int(rt->stack[first + 2]);
  uint32_t revision = oti_as_table(rt->stack[first])->revision;
  if (start < 0 || (size_t)start > oti_as_table(rt->stack[first])->used)
    return oti_fail(rt, "invalid table scan offset");
  if (expected != -1 && expected != (intptr_t)revision)
    return oti_fail(rt, "table changed; restart scan");
  ot_value live[] = {rt->stack[first], OT_EMPTY, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 3);
  size_t at = (size_t)start, scanned = 0, found = 0;
  bool ok = true;
  while (ok && at < oti_as_table(live[0])->used && scanned < 512 && found < 64) {
    size_t slot = at++;
    scanned++;
    if (entries(live[0])[2 * slot + 1] == OT_NIL) continue;
    live[2] = oti_cons(rt, entries(live[0])[2 * slot], entries(live[0])[2 * slot + 1]);
    ok = live[2] && (live[1] = oti_cons(rt, live[2], live[1]));
    found++;
  }
  if (ok) {
    live[1] = oti_cons(rt, live[1], OT_EMPTY);
    if (live[1]) live[1] = oti_cons(rt, ot_int((intptr_t)scanned), live[1]);
    if (live[1]) live[1] = oti_cons(rt, ot_bool(at == oti_as_table(live[0])->used), live[1]);
    if (live[1]) live[1] = oti_cons(rt, ot_int((intptr_t)at), live[1]);
    if (live[1]) live[1] = oti_cons(rt, ot_int((intptr_t)revision), live[1]);
  }
  ot_value result = ok ? live[1] : 0;
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

const oti_builtin_def oti_table_builtins[] = {
    {B_TABLE_SCAN, "table-scan", table_scan},
    {B_TABLE, "table", table_table},
    {B_TABLE_P, "table?", table_table_p},
    {B_KEYS, "keys", table_keys},
    {B_VALUES, "values", table_values},
    /* The VM handles apply before dispatch; this keeps the old fall-through. */
    {B_APPLY, "apply", table_unsupported},
    {0, NULL, NULL},
};
