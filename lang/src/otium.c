#define OT_INTERNAL
#include "otium.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "expander.h"
#include "prelude.h"

#ifndef OT_HEAP_INIT
#define OT_HEAP_INIT (1024u * 1024u)
#endif
#ifndef OT_HEAP_MAX
#define OT_HEAP_MAX (64u * 1024u * 1024u)
#endif
#ifndef OT_MAX_DEPTH
#define OT_MAX_DEPTH 200000
#endif

#define BUF_INLINE_SIZE 256

/* =========================================================================
 * 1. TEMPORARY BUFFERS AND COMMON HELPERS
 * ========================================================================= */

typedef struct buf {
  char* data;
  size_t length;
  size_t capacity;
  /* Writes become no-ops after failure, so recursive renderers check once. */
  bool failed;
  char inline_data[BUF_INLINE_SIZE];
} buf;

static void buf_reserve(buf* b, size_t extra) {
  if (b->failed) return;
  if (extra > SIZE_MAX - b->length) {
    b->failed = true;
    return;
  }
  size_t needed = b->length + extra;
  if (b->data == NULL) {
    b->data = b->inline_data;
    b->capacity = sizeof b->inline_data;
  }
  if (needed <= b->capacity) return;
  size_t capacity = b->capacity;
  while (capacity < needed) {
    if (capacity > SIZE_MAX / 2) {
      b->failed = true;
      return;
    }
    capacity *= 2;
  }
  char* data;
  if (b->data == b->inline_data) {
    data = ot_host_alloc(capacity);
    if (data != NULL) memcpy(data, b->data, b->length);
  } else {
    data = ot_host_realloc(b->data, capacity);
  }
  if (data == NULL) {
    b->failed = true;
    return;
  }
  b->data = data;
  b->capacity = capacity;
}

static void buf_write(buf* b, const char* bytes, size_t length) {
  buf_reserve(b, length);
  if (b->failed) return;
  memcpy(b->data + b->length, bytes, length);
  b->length += length;
}

static void buf_byte(buf* b, char byte) { buf_write(b, &byte, 1); }

static void buf_cstr(buf* b, const char* string) { buf_write(b, string, strlen(string)); }

static void buf_printf(buf* b, const char* format, ...) {
  va_list args;
  va_start(args, format);
  va_list copy;
  va_copy(copy, args);
  int length = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  if (length < 0) {
    b->failed = true;
    va_end(args);
    return;
  }
  buf_reserve(b, (size_t)length + 1);
  if (!b->failed) vsnprintf(b->data + b->length, (size_t)length + 1, format, args);
  va_end(args);
  if (!b->failed) b->length += (size_t)length;
}

static void buf_free(buf* b) {
  if (b->data != b->inline_data) ot_host_free(b->data);
  *b = (buf){0};
}

static void* must_alloc(ots* state, size_t size, ot_obj_type type) {
  void* object = ot_alloc(state, size, type);
  if (object == NULL) {
    fputs("otium: heap exhausted\n", stderr);
    abort();
  }
  return object;
}

static uint32_t hash_bytes(const char* bytes, size_t length) {
  uint32_t hash = UINT32_C(2166136261);
  for (size_t i = 0; i < length; i++) {
    hash ^= (unsigned char)bytes[i];
    hash *= UINT32_C(16777619);
  }
  return hash == 0 ? 1 : hash;
}

static bool special_name(const char* bytes, size_t length) {
  static const char* const names[] = {
      "quote",
      "if",
      "begin",
      "do",
      "lambda",
      "fn",
      "define",
      "def",
      "define-",
      "set!",
      "let",
      "while",
      "and",
      "or",
      "cond",
      "quasiquote",
      "unquote",
      "unquote-splicing",
      "defmacro",
      "try",
      "handler-bind",
      "restart-case",
      "unwind-protect",
      "defer",
      "with-params",
      "defparam",
      "in-ns",
      "ns",
      "require",
  };
  for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
    if (strlen(names[i]) == length && memcmp(names[i], bytes, length) == 0) return true;
  return false;
}

#define as_name(V) ((ot_name_obj*)ot_as_obj(V))
#define as_bytes(V) ((ot_bytes_obj*)ot_as_obj(V))
#define as_string(V) ((ot_string_obj*)ot_as_obj(V))
#define as_pair(V) ((ot_pair_obj*)ot_as_obj(V))
#define as_slots(V) ((ot_slots_obj*)ot_as_obj(V))
#define as_array(V) ((ot_array_obj*)ot_as_obj(V))
#define as_entries(V) ((ot_entries_obj*)ot_as_obj(V))
#define as_table(V) ((ot_table_obj*)ot_as_obj(V))
#define as_code(V) ((ot_code_obj*)ot_as_obj(V))

/* =========================================================================
 * 2. CORE VALUES AND COLLECTIONS
 * ========================================================================= */

static otv make_bytes(ots* state, size_t length) {
  ot_bytes_obj* bytes = must_alloc(state, sizeof(*bytes) + length, OBJ_BYTES);
  bytes->length = length;
  return ot_from_obj(bytes);
}

static otv make_string_slice(ots* state, otv owner, size_t offset, size_t length) {
  OT_FRAME_SCOPED(state, &owner);
  otv bytes_value = make_bytes(state, length);
  OT_FRAME_SCOPED(state, &bytes_value);
  const unsigned char* source;
  if (ot_has_type(owner, OBJ_STRING)) source = as_bytes(as_string(owner)->bytes)->data;
  else source = as_bytes(((ot_buffer_obj*)ot_as_obj(owner))->bytes)->data;
  if (length != 0) memcpy(as_bytes(bytes_value)->data, source + offset, length);
  ot_string_obj* string = must_alloc(state, sizeof(*string), OBJ_STRING);
  string->bytes = bytes_value;
  string->length = length;
  return ot_from_obj(string);
}

static otv make_string_from_name(ots* state, otv owner) {
  OT_FRAME_SCOPED(state, &owner);
  size_t length = as_name(owner)->length;
  otv bytes_value = make_bytes(state, length);
  OT_FRAME_SCOPED(state, &bytes_value);
  memcpy(as_bytes(bytes_value)->data, as_name(owner)->bytes, length);
  ot_string_obj* string = must_alloc(state, sizeof(*string), OBJ_STRING);
  string->bytes = bytes_value;
  string->length = length;
  return ot_from_obj(string);
}

otv ot_make_float(ots* state, double value) {
  ot_float_obj* number = must_alloc(state, sizeof(*number), OBJ_FLOAT);
  number->value = value;
  return ot_from_obj(number);
}

bool ot_float_value(otv value, double* out) {
  if (!ot_is_ptr(value) || ot_object_type(value) != OBJ_FLOAT) return false;
  *out = ((ot_float_obj*)ot_as_obj(value))->value;
  return true;
}

otv ot_make_string(ots* state, const char* input, size_t length) {
  otv bytes_value = make_bytes(state, length);
  OT_FRAME_SCOPED(state, &bytes_value);
  if (length != 0) memcpy(as_bytes(bytes_value)->data, input, length);
  ot_string_obj* string = must_alloc(state, sizeof(*string), OBJ_STRING);
  string->bytes = bytes_value;
  string->length = length;
  return ot_from_obj(string);
}

bool ot_string_bytes(otv value, const char** out, size_t* length) {
  if (!ot_is_ptr(value) || ot_object_type(value) != OBJ_STRING) return false;
  ot_string_obj* string = as_string(value);
  *out = (const char*)as_bytes(string->bytes)->data;
  *length = string->length;
  return true;
}

bool ot_function_bytecode(otv value, const char** out, size_t* length) {
  if (!ot_has_type(value, OBJ_FUNCTION)) return false;
  otv code_value = ((ot_function_obj*)ot_as_obj(value))->code;
  if (!ot_has_type(code_value, OBJ_CODE)) return false;
  ot_code_obj* code = as_code(code_value);
  *out = (const char*)as_bytes(code->bytes)->data;
  *length = code->length;
  return true;
}

static bool name_equal(otv value, const char* bytes, size_t length, ot_obj_type type,
                       uint32_t hash) {
  if (!ot_is_ptr(value) || ot_object_type(value) != type) return false;
  ot_name_obj* name = as_name(value);
  return name->hash == hash && name->length == length && memcmp(name->bytes, bytes, length) == 0;
}

otv ot_intern(ots* state, const char* bytes, size_t length, bool keyword) {
  ot_obj_type type = keyword ? OBJ_KEYWORD : OBJ_SYMBOL;
  uint32_t hash = hash_bytes(bytes, length);
  for (otv cursor = state->symbols; ot_is_ptr(cursor); cursor = as_name(cursor)->next)
    if (name_equal(cursor, bytes, length, type, hash)) return cursor;
  ot_name_obj* name = must_alloc(state, sizeof(*name) + length + 1, type);
  name->next = state->symbols;
  name->cache_namespace = ot_nil;
  name->cache_var = ot_nil;
  name->hash = hash;
  name->length = (uint32_t)length;
  name->special_form = !keyword && special_name(bytes, length);
  memcpy(name->bytes, bytes, length);
  name->bytes[length] = '\0';
  otv value = ot_from_obj(name);
  state->symbols = value;
  return value;
}

otv ot_make_symbol(ots* state, const char* bytes, size_t length) {
  return ot_intern(state, bytes, length, false);
}

otv ot_make_keyword(ots* state, const char* bytes, size_t length) {
  return ot_intern(state, bytes, length, true);
}

static bool is_type(otv value, ot_obj_type type) {
  return ot_is_ptr(value) && ot_object_type(value) == type;
}

ot_type ot_value_type(otv value) {
  if (ot_is_int(value)) return OT_TYPE_INT;
  if (value == ot_nil) return OT_TYPE_NIL;
  if (value == ot_null) return OT_TYPE_NULL;
  if (value == ot_true || value == ot_false) return OT_TYPE_BOOLEAN;
  if (!ot_is_ptr(value)) return OT_TYPE_INTERNAL;
  switch (ot_object_type(value)) {
    case OBJ_FLOAT: return OT_TYPE_FLOAT;
    case OBJ_SYMBOL: return OT_TYPE_SYMBOL;
    case OBJ_KEYWORD: return OT_TYPE_KEYWORD;
    case OBJ_STRING: return OT_TYPE_STRING;
    case OBJ_PAIR: return OT_TYPE_PAIR;
    case OBJ_ARRAY: return OT_TYPE_ARRAY;
    case OBJ_TABLE: return OT_TYPE_TABLE;
    case OBJ_BUFFER: return OT_TYPE_BUFFER;
    case OBJ_FUNCTION:
    case OBJ_NAT: return OT_TYPE_FUNCTION;
    case OBJ_MACRO: return OT_TYPE_MACRO;
    case OBJ_PARAM: return OT_TYPE_PARAM;
    case OBJ_RESTART: return OT_TYPE_RESTART;
    case OBJ_EXT: return OT_TYPE_EXT;
    default: return OT_TYPE_INTERNAL;
  }
}

otv ot_cons(ots* state, otv car, otv cdr) {
  OT_FRAME_SCOPED(state, &car, &cdr);
  ot_pair_obj* pair = must_alloc(state, sizeof(*pair), OBJ_PAIR);
  pair->car = car;
  pair->cdr = cdr;
  pair->stable_id = 0;
  pair->frozen = false;
  return ot_from_obj(pair);
}

bool ot_pair_values(otv pair, otv* car, otv* cdr) {
  if (!is_type(pair, OBJ_PAIR)) return false;
  if (car != NULL) *car = as_pair(pair)->car;
  if (cdr != NULL) *cdr = as_pair(pair)->cdr;
  return true;
}

static otv make_slots(ots* state, size_t capacity) {
  ot_slots_obj* slots = must_alloc(state, sizeof(*slots) + capacity * sizeof(otv), OBJ_SLOTS);
  slots->capacity = capacity;
  for (size_t i = 0; i < capacity; i++) slots->values[i] = ot_nil;
  return ot_from_obj(slots);
}

otv ot_array_new(ots* state, size_t capacity) {
  if (capacity < 4) capacity = 4;
  otv slots = make_slots(state, capacity);
  OT_FRAME_SCOPED(state, &slots);
  ot_array_obj* array = must_alloc(state, sizeof(*array), OBJ_ARRAY);
  array->slots = slots;
  array->length = 0;
  array->stable_id = 0;
  return ot_from_obj(array);
}

static void array_reserve(ots* state, otv* array_value, size_t capacity) {
  ot_array_obj* array = as_array(*array_value);
  ot_slots_obj* old = as_slots(array->slots);
  if (old->capacity >= capacity) return;
  size_t grown = old->capacity;
  while (grown < capacity) grown *= 2;
  otv new_slots = make_slots(state, grown);
  OT_FRAME_SCOPED(state, array_value, &new_slots);
  array = as_array(*array_value);
  old = as_slots(array->slots);
  memcpy(as_slots(new_slots)->values, old->values, array->length * sizeof(otv));
  array->slots = new_slots;
}

static void array_push(ots* state, otv* array_value, otv value) {
  OT_FRAME_SCOPED(state, array_value, &value);
  ot_array_obj* array = as_array(*array_value);
  array_reserve(state, array_value, array->length + 1);
  array = as_array(*array_value);
  as_slots(array->slots)->values[array->length++] = value;
}

otv ot_array_append(ots* state, otv array, otv value) {
  array_push(state, &array, value);
  return array;
}

otv ot_array_get(otv array, size_t index, otv fallback) {
  if (!is_type(array, OBJ_ARRAY)) return fallback;
  ot_array_obj* object = as_array(array);
  if (index >= object->length) return fallback;
  return as_slots(object->slots)->values[index];
}

size_t ot_array_length(otv array) {
  return is_type(array, OBJ_ARRAY) ? as_array(array)->length : 0;
}

static otv make_entries(ots* state, size_t capacity) {
  ot_entries_obj* entries =
      must_alloc(state, sizeof(*entries) + capacity * sizeof(ot_entry), OBJ_ENTRIES);
  entries->capacity = capacity;
  for (size_t i = 0; i < capacity; i++) {
    entries->values[i].key = ot_nil;
    entries->values[i].value = ot_nil;
    entries->values[i].hash = 0;
    entries->values[i].live = false;
  }
  return ot_from_obj(entries);
}

otv ot_table_new(ots* state, size_t capacity) {
  if (capacity < 4) capacity = 4;
  size_t rounded = 4;
  while (rounded < capacity) rounded *= 2;
  capacity = rounded;
  otv entries = make_entries(state, capacity);
  otv index = ot_nil;
  OT_FRAME_SCOPED(state, &entries, &index);
  index = make_bytes(state, capacity * 2 * sizeof(uint32_t));
  memset(as_bytes(index)->data, 0, capacity * 2 * sizeof(uint32_t));
  ot_table_obj* table = must_alloc(state, sizeof(*table), OBJ_TABLE);
  table->entries = entries;
  table->index = index;
  table->length = 0;
  table->used = 0;
  table->stable_id = 0;
  return ot_from_obj(table);
}

static bool float_same(double left, double right) {
  return left == right || (isnan(left) && isnan(right));
}

bool ot_equal(ots* state, otv left, otv right, bool structural) {
  (void)state;
  if (left == right) return true;
  if (ot_is_int(left) || ot_is_int(right)) return false;
  if (!ot_is_ptr(left) || !ot_is_ptr(right)) return false;
  ot_obj_type left_type = ot_object_type(left);
  if (left_type != ot_object_type(right)) return false;
  switch (left_type) {
    case OBJ_FLOAT:
      return float_same(((ot_float_obj*)ot_as_obj(left))->value,
                        ((ot_float_obj*)ot_as_obj(right))->value);
    case OBJ_STRING: {
      ot_string_obj* a = as_string(left);
      ot_string_obj* b = as_string(right);
      return a->length == b->length &&
             memcmp(as_bytes(a->bytes)->data, as_bytes(b->bytes)->data, a->length) == 0;
    }
    case OBJ_PAIR:
      return structural && ot_equal(state, as_pair(left)->car, as_pair(right)->car, true) &&
             ot_equal(state, as_pair(left)->cdr, as_pair(right)->cdr, true);
    case OBJ_ARRAY: {
      return false;
    }
    default: return false;
  }
}

static bool key_equal(ots* state, otv left, otv right) {
  if (left == right) return true;
  if (is_type(left, OBJ_ARRAY) || is_type(left, OBJ_TABLE) || is_type(left, OBJ_BUFFER) ||
      is_type(left, OBJ_FUNCTION) || is_type(left, OBJ_NAT) || is_type(left, OBJ_EXT))
    return false;
  return ot_equal(state, left, right, true);
}

static uint32_t value_hash(ots* state, otv value) {
  (void)state;
  if (ot_is_int(value)) return (uint32_t)((uintptr_t)value ^ ((uintptr_t)value >> 32u));
  if (!ot_is_ptr(value)) return (uint32_t)value * UINT32_C(2654435761);
  switch (ot_object_type(value)) {
    case OBJ_SYMBOL:
    case OBJ_KEYWORD: return as_name(value)->hash;
    case OBJ_STRING: {
      ot_string_obj* string = as_string(value);
      return hash_bytes((const char*)as_bytes(string->bytes)->data, string->length);
    }
    case OBJ_FLOAT: {
      double number = ((ot_float_obj*)ot_as_obj(value))->value;
      if (number == 0.0) number = 0.0;
      if (isnan(number)) return UINT32_C(0x7fc00000);
      uint64_t bits;
      memcpy(&bits, &number, sizeof bits);
      return (uint32_t)(bits ^ (bits >> 32u));
    }
    case OBJ_PAIR: {
      ot_pair_obj* pair = as_pair(value);
      pair->frozen = true;
      return value_hash(state, pair->car) * 33u ^ value_hash(state, pair->cdr);
    }
    case OBJ_ARRAY: {
      ot_array_obj* array = as_array(value);
      if (array->stable_id == 0) array->stable_id = ++state->next_stable_id;
      return (uint32_t)array->stable_id;
    }
    case OBJ_TABLE: {
      ot_table_obj* table = as_table(value);
      if (table->stable_id == 0) table->stable_id = ++state->next_stable_id;
      return (uint32_t)table->stable_id;
    }
    default: return (uint32_t)((uintptr_t)value >> 3u);
  }
}

static ptrdiff_t table_lookup(ots* state, otv table_value, otv key, size_t* slot_out) {
  /* The byte index stores entry offsets plus one; zero is empty and UINT32_MAX
   * is a tombstone. Entries stay ordered while probing remains O(1). */
  ot_table_obj* table = as_table(table_value);
  ot_entries_obj* entries = as_entries(table->entries);
  ot_bytes_obj* index_object = as_bytes(table->index);
  uint32_t* index = (uint32_t*)index_object->data;
  size_t index_capacity = index_object->length / sizeof(*index);
  size_t mask = index_capacity - 1;
  uint32_t hash = value_hash(state, key);
  size_t slot = (size_t)hash & mask;
  size_t tombstone = SIZE_MAX;
  for (size_t probes = 0; probes < index_capacity; probes++) {
    uint32_t position = index[slot];
    if (position == 0) {
      if (slot_out != NULL) *slot_out = tombstone == SIZE_MAX ? slot : tombstone;
      return -1;
    }
    if (position == UINT32_MAX) {
      if (tombstone == SIZE_MAX) tombstone = slot;
    } else {
      size_t entry_index = (size_t)position - 1;
      ot_entry* entry = &entries->values[entry_index];
      if (entry->live && entry->hash == hash && key_equal(state, entry->key, key)) {
        if (slot_out != NULL) *slot_out = slot;
        return (ptrdiff_t)entry_index;
      }
    }
    slot = (slot + 1) & mask;
  }
  if (slot_out != NULL) *slot_out = tombstone;
  return -1;
}

static ptrdiff_t table_find(ots* state, otv table_value, otv key) {
  return table_lookup(state, table_value, key, NULL);
}

static void table_rebuild_index(otv table_value) {
  ot_table_obj* table = as_table(table_value);
  ot_entries_obj* entries = as_entries(table->entries);
  ot_bytes_obj* index_object = as_bytes(table->index);
  uint32_t* index = (uint32_t*)index_object->data;
  size_t index_capacity = index_object->length / sizeof(*index);
  size_t mask = index_capacity - 1;
  memset(index, 0, index_object->length);
  for (size_t i = 0; i < table->used; i++) {
    if (!entries->values[i].live) continue;
    size_t slot = (size_t)entries->values[i].hash & mask;
    while (index[slot] != 0) slot = (slot + 1) & mask;
    index[slot] = (uint32_t)(i + 1);
  }
}

otv ot_table_get(ots* state, otv table, otv key, otv fallback) {
  if (table == ot_nil) return fallback;
  if (!is_type(table, OBJ_TABLE)) return fallback;
  ptrdiff_t found = table_find(state, table, key);
  return found < 0 ? fallback : as_entries(as_table(table)->entries)->values[found].value;
}

static void table_compact_or_grow(ots* state, otv* table_value) {
  /* Rebuild entries only when full or when tombstones outnumber live rows. */
  ot_table_obj* table = as_table(*table_value);
  ot_entries_obj* old = as_entries(table->entries);
  bool compact = table->used > table->length * 2;
  if (table->used < old->capacity && !compact) return;
  size_t capacity = compact ? old->capacity : old->capacity * 2;
  otv fresh = make_entries(state, capacity);
  otv fresh_index = ot_nil;
  OT_FRAME_SCOPED(state, table_value, &fresh, &fresh_index);
  fresh_index = make_bytes(state, capacity * 2 * sizeof(uint32_t));
  table = as_table(*table_value);
  old = as_entries(table->entries);
  ot_entries_obj* entries = as_entries(fresh);
  size_t next = 0;
  for (size_t i = 0; i < table->used; i++)
    if (old->values[i].live) entries->values[next++] = old->values[i];
  table->entries = fresh;
  table->index = fresh_index;
  table->used = next;
  table_rebuild_index(*table_value);
}

otv ot_table_put(ots* state, otv table, otv key, otv value) {
  OT_FRAME_SCOPED(state, &table, &key, &value);
  if (!is_type(table, OBJ_TABLE)) return ot_raise(state, "put!: expected table");
  size_t slot = 0;
  ptrdiff_t found = table_lookup(state, table, key, &slot);
  ot_table_obj* object = as_table(table);
  if (found >= 0) {
    ot_entry* entry = &as_entries(object->entries)->values[found];
    if (value == ot_nil) {
      entry->live = false;
      entry->key = ot_nil;
      entry->value = ot_nil;
      object->length--;
      ((uint32_t*)as_bytes(object->index)->data)[slot] = UINT32_MAX;
    } else {
      entry->value = value;
    }
    return table;
  }
  if (value == ot_nil) return table;
  table_compact_or_grow(state, &table);
  object = as_table(table);
  found = table_lookup(state, table, key, &slot);
  (void)found;
  size_t entry_index = object->used++;
  ot_entry* entry = &as_entries(object->entries)->values[entry_index];
  entry->key = key;
  entry->value = value;
  entry->hash = value_hash(state, key);
  entry->live = true;
  ((uint32_t*)as_bytes(object->index)->data)[slot] = (uint32_t)(entry_index + 1);
  object->length++;
  return table;
}

size_t ot_table_length(otv table) {
  return is_type(table, OBJ_TABLE) ? as_table(table)->length : 0;
}

static bool proper_list(otv value) {
  otv slow = value;
  otv fast = value;
  while (is_type(fast, OBJ_PAIR)) {
    fast = as_pair(fast)->cdr;
    if (!is_type(fast, OBJ_PAIR)) return fast == ot_null;
    fast = as_pair(fast)->cdr;
    slow = as_pair(slow)->cdr;
    if (fast == slow) return false;
  }
  return fast == ot_null;
}

static bool proper_list_length(otv value, size_t* length) {
  otv slow = value;
  bool advance_slow = false;
  *length = 0;
  while (is_type(value, OBJ_PAIR)) {
    value = as_pair(value)->cdr;
    (*length)++;
    if (advance_slow) slow = as_pair(slow)->cdr;
    advance_slow = !advance_slow;
    if (is_type(value, OBJ_PAIR) && value == slow) return false;
  }
  return value == ot_null;
}

static size_t list_length(otv value) {
  size_t length = 0;
  while (is_type(value, OBJ_PAIR)) {
    length++;
    value = as_pair(value)->cdr;
  }
  return length;
}

static otv list_from_array(ots* state, const otv* values, size_t count) {
  otv list = ot_null;
  OT_FRAME_SCOPED(state, &list);
  for (size_t i = count; i-- > 0;) list = ot_cons(state, values[i], list);
  return list;
}

static bool is_falsy(otv value) { return value == ot_nil || value == ot_false; }

/* =========================================================================
 * 3. PRINTER
 * ========================================================================= */

static void render_value(ots* state, buf* out, otv value, bool display, unsigned depth);

static void render_name(buf* out, otv value, bool keyword) {
  ot_name_obj* name = as_name(value);
  if (keyword) buf_byte(out, ':');
  buf_write(out, name->bytes, name->length);
}

static void render_string(buf* out, otv value, bool display) {
  ot_string_obj* string = as_string(value);
  const unsigned char* bytes = as_bytes(string->bytes)->data;
  if (display) {
    buf_write(out, (const char*)bytes, string->length);
    return;
  }
  buf_byte(out, '"');
  for (size_t i = 0; i < string->length; i++) {
    switch (bytes[i]) {
      case '\n': buf_cstr(out, "\\n"); break;
      case '\t': buf_cstr(out, "\\t"); break;
      case '\r': buf_cstr(out, "\\r"); break;
      case '\0': buf_cstr(out, "\\0"); break;
      case 27: buf_cstr(out, "\\e"); break;
      case '"': buf_cstr(out, "\\\""); break;
      case '\\': buf_cstr(out, "\\\\"); break;
      default: buf_byte(out, (char)bytes[i]); break;
    }
  }
  buf_byte(out, '"');
}

static void render_pair(ots* state, buf* out, otv value, bool display, unsigned depth) {
  buf_byte(out, '(');
  bool first = true;
  while (is_type(value, OBJ_PAIR)) {
    if (!first) buf_byte(out, ' ');
    render_value(state, out, as_pair(value)->car, display, depth + 1);
    value = as_pair(value)->cdr;
    first = false;
  }
  if (value != ot_null) {
    buf_cstr(out, " . ");
    render_value(state, out, value, display, depth + 1);
  }
  buf_byte(out, ')');
}

static void render_value(ots* state, buf* out, otv value, bool display, unsigned depth) {
  if (depth > 1000) {
    buf_cstr(out, "#<depth>");
    return;
  }
  if (ot_is_int(value)) {
    buf_printf(out, "%" PRIdPTR, ot_get_int(value));
    return;
  }
  if (value == ot_nil) {
    buf_cstr(out, "nil");
    return;
  }
  if (value == ot_null) {
    buf_cstr(out, "()");
    return;
  }
  if (value == ot_true || value == ot_false) {
    buf_cstr(out, value == ot_true ? "#t" : "#f");
    return;
  }
  if (!ot_is_ptr(value)) {
    buf_cstr(out, "#<internal>");
    return;
  }
  switch (ot_object_type(value)) {
    case OBJ_FLOAT: {
      double number = ((ot_float_obj*)ot_as_obj(value))->value;
      char text[64];
      snprintf(text, sizeof text, "%.17g", number);
      buf_cstr(out, text);
      if (isfinite(number) && strchr(text, '.') == NULL && strchr(text, 'e') == NULL &&
          strchr(text, 'E') == NULL)
        buf_cstr(out, ".0");
      break;
    }
    case OBJ_SYMBOL: render_name(out, value, false); break;
    case OBJ_KEYWORD: render_name(out, value, true); break;
    case OBJ_STRING: render_string(out, value, display); break;
    case OBJ_PAIR: render_pair(state, out, value, display, depth); break;
    case OBJ_ARRAY: {
      ot_array_obj* array = as_array(value);
      buf_byte(out, '[');
      for (size_t i = 0; i < array->length; i++) {
        if (i != 0) buf_byte(out, ' ');
        render_value(state, out, as_slots(array->slots)->values[i], display, depth + 1);
      }
      buf_byte(out, ']');
      break;
    }
    case OBJ_TABLE: {
      ot_table_obj* table = as_table(value);
      ot_entries_obj* entries = as_entries(table->entries);
      buf_byte(out, '{');
      bool first = true;
      for (size_t i = 0; i < table->used; i++) {
        if (!entries->values[i].live) continue;
        if (!first) buf_byte(out, ' ');
        render_value(state, out, entries->values[i].key, display, depth + 1);
        buf_byte(out, ' ');
        render_value(state, out, entries->values[i].value, display, depth + 1);
        first = false;
      }
      buf_byte(out, '}');
      break;
    }
    case OBJ_BUFFER: {
      ot_buffer_obj* buffer = (ot_buffer_obj*)ot_as_obj(value);
      if (!display) buf_cstr(out, "#<buffer \"");
      buf_write(out, (const char*)as_bytes(buffer->bytes)->data, buffer->length);
      if (!display) buf_cstr(out, "\">");
      break;
    }
    case OBJ_FUNCTION: {
      otv name = ((ot_function_obj*)ot_as_obj(value))->name;
      buf_cstr(out, "#<fn");
      if (is_type(name, OBJ_SYMBOL)) {
        buf_byte(out, ' ');
        render_name(out, name, false);
      }
      buf_byte(out, '>');
      break;
    }
    case OBJ_NAT: {
      buf_cstr(out, "#<fn ");
      render_name(out, ((ot_nat_obj*)ot_as_obj(value))->name, false);
      buf_byte(out, '>');
      break;
    }
    case OBJ_MACRO: buf_cstr(out, "#<macro>"); break;
    case OBJ_PARAM: {
      buf_cstr(out, "#<param ");
      render_name(out, ((ot_param_obj*)ot_as_obj(value))->name, false);
      buf_byte(out, '>');
      break;
    }
    case OBJ_RESTART: {
      buf_cstr(out, "#<restart ");
      render_name(out, ((ot_restart_obj*)ot_as_obj(value))->name, false);
      buf_byte(out, '>');
      break;
    }
    case OBJ_EXT: buf_cstr(out, "#<foreign>"); break;
    default: buf_cstr(out, "#<internal>"); break;
  }
}

void ot_repr_to(ots* state, otv value, bool display, void (*write)(void*, const char*, size_t),
                void* userdata) {
  buf out = {0};
  render_value(state, &out, value, display, 0);
  if (!out.failed) write(userdata, out.data, out.length);
  buf_free(&out);
}

/* =========================================================================
 * 4. READER
 * ========================================================================= */

typedef struct reader {
  ots* state;
  const char* source;
  size_t length;
  size_t offset;
  const char* name;
  bool incomplete;
} reader;

static bool delimiter(unsigned char byte) {
  return isspace(byte) || byte == '(' || byte == ')' || byte == '[' || byte == ']' || byte == '{' ||
         byte == '}' || byte == ';' || byte == '"' || byte == ',' || byte == '\'' || byte == '`';
}

static void reader_space(reader* input) {
  while (input->offset < input->length) {
    unsigned char byte = (unsigned char)input->source[input->offset];
    if (isspace(byte)) {
      input->offset++;
      continue;
    }
    if (byte == ';') {
      while (input->offset < input->length && input->source[input->offset] != '\n') input->offset++;
      continue;
    }
    break;
  }
}

static otv reader_error(reader* input, const char* message) {
  return ot_raise(input->state, "%s: read error at byte %zu: %s", input->name, input->offset,
                  message);
}

static otv read_form(reader* input);

static otv read_string(reader* input) {
  input->offset++;
  buf bytes = {0};
  while (input->offset < input->length) {
    unsigned char byte = (unsigned char)input->source[input->offset++];
    if (byte == '"') {
      otv value = ot_make_string(input->state, bytes.data, bytes.length);
      buf_free(&bytes);
      return value;
    }
    if (byte != '\\') {
      buf_byte(&bytes, (char)byte);
      continue;
    }
    if (input->offset == input->length) break;
    byte = (unsigned char)input->source[input->offset++];
    switch (byte) {
      case 'n': buf_byte(&bytes, '\n'); break;
      case 't': buf_byte(&bytes, '\t'); break;
      case 'r': buf_byte(&bytes, '\r'); break;
      case '0': buf_byte(&bytes, '\0'); break;
      case 'e': buf_byte(&bytes, 27); break;
      case '"': buf_byte(&bytes, '"'); break;
      case '\\': buf_byte(&bytes, '\\'); break;
      default: buf_free(&bytes); return reader_error(input, "unknown string escape");
    }
  }
  input->incomplete = true;
  buf_free(&bytes);
  return reader_error(input, "unterminated string");
}

static bool token_starts_number(const char* token, size_t length) {
  size_t i = 0;
  if (i < length && (token[i] == '+' || token[i] == '-')) i++;
  return i < length && (isdigit((unsigned char)token[i]) || (token[i] == '.' && i + 1 < length));
}

static otv read_atom(reader* input) {
  size_t start = input->offset;
  while (input->offset < input->length && !delimiter((unsigned char)input->source[input->offset]))
    input->offset++;
  const char* token = input->source + start;
  size_t length = input->offset - start;
  if (length == 0) return reader_error(input, "expected a value");
  if (length == 3 && memcmp(token, "nil", 3) == 0) return ot_nil;
  if ((length == 2 && memcmp(token, "#t", 2) == 0) ||
      (length == 5 && memcmp(token, "#true", 5) == 0) ||
      (length == 4 && memcmp(token, "true", 4) == 0))
    return ot_true;
  if ((length == 2 && memcmp(token, "#f", 2) == 0) ||
      (length == 6 && memcmp(token, "#false", 6) == 0) ||
      (length == 5 && memcmp(token, "false", 5) == 0))
    return ot_false;
  if (token[0] == '#') return reader_error(input, "reserved # token");
  if (token[0] == ':') {
    if (length == 1) return reader_error(input, "empty keyword");
    return ot_intern(input->state, token + 1, length - 1, true);
  }
  if (token_starts_number(token, length)) {
    buf text = {0};
    buf_write(&text, token, length);
    buf_byte(&text, '\0');
    char* end = NULL;
    errno = 0;
    if (length > 2 &&
        (token[0] == '0' ||
         ((token[0] == '+' || token[0] == '-') && length > 3 && token[1] == '0')) &&
        (token[token[0] == '+' || token[0] == '-' ? 2 : 1] == 'x' ||
         token[token[0] == '+' || token[0] == '-' ? 2 : 1] == 'X')) {
      intmax_t integer = strtoimax(text.data, &end, 0);
      if (errno == 0 && end == text.data + length) {
        buf_free(&text);
        intptr_t narrowed = (intptr_t)integer;
        if ((intmax_t)narrowed != integer) return reader_error(input, "integer out of range");
        return ot_make_int(narrowed);
      }
    } else {
      intmax_t integer = strtoimax(text.data, &end, 10);
      if (errno == 0 && end == text.data + length && strchr(text.data, '.') == NULL &&
          strchr(text.data, 'e') == NULL && strchr(text.data, 'E') == NULL) {
        buf_free(&text);
        intptr_t narrowed = (intptr_t)integer;
        if ((intmax_t)narrowed != integer) return reader_error(input, "integer out of range");
        return ot_make_int(narrowed);
      }
      errno = 0;
      double number = strtod(text.data, &end);
      if (errno == 0 && end == text.data + length) {
        buf_free(&text);
        return ot_make_float(input->state, number);
      }
    }
    buf_free(&text);
    return reader_error(input, "invalid number");
  }
  return ot_intern(input->state, token, length, false);
}

static otv read_sequence(reader* input, char closing, otv prefix) {
  input->offset++;
  otv head = ot_null;
  otv tail = ot_nil;
  otv item = ot_nil;
  OT_FRAME_SCOPED(input->state, &head, &tail, &item, &prefix);
  if (prefix != ot_nil) {
    head = tail = ot_cons(input->state, prefix, ot_null);
  }
  for (;;) {
    reader_space(input);
    if (input->offset == input->length) {
      input->incomplete = true;
      return reader_error(input, "unclosed form");
    }
    if (input->source[input->offset] == closing) {
      input->offset++;
      return head;
    }
    if (input->source[input->offset] == '.' &&
        (input->offset + 1 == input->length ||
         delimiter((unsigned char)input->source[input->offset + 1]))) {
      if (tail == ot_nil) return reader_error(input, "dot without a preceding value");
      input->offset++;
      reader_space(input);
      item = read_form(input);
      if (item == OT_UNWIND) return item;
      as_pair(tail)->cdr = item;
      reader_space(input);
      if (input->offset == input->length) {
        input->incomplete = true;
        return reader_error(input, "unclosed dotted pair");
      }
      if (input->source[input->offset] != closing)
        return reader_error(input, "content after dotted tail");
      input->offset++;
      return head;
    }
    item = read_form(input);
    if (item == OT_UNWIND) return item;
    otv cell = ot_cons(input->state, item, ot_null);
    if (head == ot_null) {
      head = cell;
      tail = cell;
    } else {
      as_pair(tail)->cdr = cell;
      tail = cell;
    }
  }
}

static otv read_quote(reader* input, const char* name, size_t name_length, size_t prefix_length) {
  input->offset += prefix_length;
  reader_space(input);
  if (input->offset == input->length) {
    input->incomplete = true;
    return reader_error(input, "quote without a value");
  }
  otv symbol = ot_intern(input->state, name, name_length, false);
  otv value = ot_nil;
  OT_FRAME_SCOPED(input->state, &symbol, &value);
  value = read_form(input);
  if (value == OT_UNWIND) return value;
  otv tail = ot_cons(input->state, value, ot_null);
  return ot_cons(input->state, symbol, tail);
}

static otv read_form(reader* input) {
  reader_space(input);
  if (input->offset == input->length) return OT_UNDEFINED;
  char byte = input->source[input->offset];
  switch (byte) {
    case '(': return read_sequence(input, ')', ot_nil);
    case '[': return read_sequence(input, ']', ot_intern(input->state, "array", 5, false));
    case '{': return read_sequence(input, '}', ot_intern(input->state, "table", 5, false));
    case '"': return read_string(input);
    case '\'': return read_quote(input, "quote", 5, 1);
    case '`': return read_quote(input, "quasiquote", 10, 1);
    case ',':
      if (input->offset + 1 < input->length && input->source[input->offset + 1] == '@')
        return read_quote(input, "unquote-splicing", 16, 2);
      return read_quote(input, "unquote", 7, 1);
    case ')':
    case ']':
    case '}': return reader_error(input, "unexpected closing delimiter");
    default: return read_atom(input);
  }
}

/* =========================================================================
 * 5. NAMESPACES AND LEXICAL ENVIRONMENTS
 * ========================================================================= */

static bool symbol_is(otv value, const char* name) {
  if (!is_type(value, OBJ_SYMBOL)) return false;
  ot_name_obj* symbol = as_name(value);
  size_t length = strlen(name);
  return symbol->length == length && memcmp(symbol->bytes, name, length) == 0;
}

static otv make_alias(ots* state, otv name, otv value, otv next) {
  OT_FRAME_SCOPED(state, &name, &value, &next);
  ot_alias_obj* alias = must_alloc(state, sizeof(*alias), OBJ_ALIAS);
  alias->name = name;
  alias->value = value;
  alias->next = next;
  return ot_from_obj(alias);
}

static otv namespace_find(ots* state, otv name, bool create) {
  OT_FRAME_SCOPED(state, &name);
  for (otv cursor = state->namespaces; is_type(cursor, OBJ_NAMESPACE);
       cursor = ((ot_namespace_obj*)ot_as_obj(cursor))->next) {
    ot_namespace_obj* space = (ot_namespace_obj*)ot_as_obj(cursor);
    if (space->name == name) return cursor;
  }
  if (!create) return ot_nil;
  otv var_index = ot_nil;
  otv refer_index = ot_nil;
  OT_FRAME_SCOPED(state, &var_index, &refer_index);
  var_index = ot_table_new(state, 16);
  refer_index = ot_table_new(state, 16);
  ot_namespace_obj* space = must_alloc(state, sizeof(*space), OBJ_NAMESPACE);
  space->name = name;
  space->vars = ot_nil;
  space->var_index = var_index;
  space->refers = ot_nil;
  space->refer_index = refer_index;
  space->aliases = ot_nil;
  space->next = state->namespaces;
  space->loaded = false;
  space->loading = false;
  otv made = ot_from_obj(space);
  state->namespaces = made;

  if (is_type(state->core_namespace, OBJ_NAMESPACE) && made != state->core_namespace) {
    otv refers = ot_nil;
    otv cursor = ((ot_namespace_obj*)ot_as_obj(state->core_namespace))->vars;
    OT_FRAME_SCOPED(state, &made, &refers, &cursor);
    ot_namespace_obj* core = (ot_namespace_obj*)ot_as_obj(state->core_namespace);
    (void)core;
    while (is_type(cursor, OBJ_VAR)) {
      ot_var_obj* var = (ot_var_obj*)ot_as_obj(cursor);
      if (!var->private_value) {
        refers = make_alias(state, var->name, cursor, refers);
        ot_alias_obj* added = (ot_alias_obj*)ot_as_obj(refers);
        ot_table_put(state, refer_index, added->name, added->value);
      }
      cursor = ((ot_var_obj*)ot_as_obj(cursor))->next;
    }
    ((ot_namespace_obj*)ot_as_obj(made))->refers = refers;
  }
  return made;
}

static otv namespace_var(ots* state, otv namespace_value, otv name, bool include_refers) {
  if (!is_type(namespace_value, OBJ_NAMESPACE)) return ot_nil;
  ot_namespace_obj* space = (ot_namespace_obj*)ot_as_obj(namespace_value);
  otv found = ot_table_get(state, space->var_index, name, ot_nil);
  if (found != ot_nil || !include_refers) return found;
  return ot_table_get(state, space->refer_index, name, ot_nil);
}

static otv define_var(ots* state, otv namespace_value, otv name, otv value, otv doc,
                      bool private_value) {
  otv made = ot_nil;
  OT_FRAME_SCOPED(state, &namespace_value, &name, &value, &doc, &made);
  otv existing = namespace_var(state, namespace_value, name, false);
  if (is_type(existing, OBJ_VAR)) {
    ot_var_obj* var = (ot_var_obj*)ot_as_obj(existing);
    var->value = value;
    var->doc = doc;
    var->private_value = private_value;
    as_name(name)->cache_namespace = namespace_value;
    as_name(name)->cache_var = existing;
    return existing;
  }
  ot_namespace_obj* space = (ot_namespace_obj*)ot_as_obj(namespace_value);
  otv next = space->vars;
  OT_FRAME_SCOPED(state, &next);
  ot_var_obj* var = must_alloc(state, sizeof(*var), OBJ_VAR);
  var->name = name;
  var->value = value;
  var->doc = doc;
  var->next = next;
  var->private_value = private_value;
  made = ot_from_obj(var);
  ((ot_namespace_obj*)ot_as_obj(namespace_value))->vars = made;
  ot_table_put(state, ((ot_namespace_obj*)ot_as_obj(namespace_value))->var_index, name, made);
  as_name(name)->cache_namespace = namespace_value;
  as_name(name)->cache_var = made;
  return made;
}

static otv make_env(ots* state, otv parent, otv namespace_value) {
  OT_FRAME_SCOPED(state, &parent, &namespace_value);
  ot_env_obj* env = must_alloc(state, sizeof(*env), OBJ_ENV);
  env->parent = parent;
  env->bindings = ot_nil;
  env->namespace_value = namespace_value;
  return ot_from_obj(env);
}

static void env_bind(ots* state, otv* env_value, otv name, otv value) {
  OT_FRAME_SCOPED(state, env_value, &name, &value);
  ot_env_obj* env = (ot_env_obj*)ot_as_obj(*env_value);
  otv next = env->bindings;
  OT_FRAME_SCOPED(state, &next);
  ot_binding_obj* binding = must_alloc(state, sizeof(*binding), OBJ_BINDING);
  binding->name = name;
  binding->value = value;
  binding->next = next;
  ((ot_env_obj*)ot_as_obj(*env_value))->bindings = ot_from_obj(binding);
}

static otv env_binding(otv env_value, otv name) {
  for (otv env_cursor = env_value; is_type(env_cursor, OBJ_ENV);
       env_cursor = ((ot_env_obj*)ot_as_obj(env_cursor))->parent) {
    ot_env_obj* env = (ot_env_obj*)ot_as_obj(env_cursor);
    for (otv cursor = env->bindings; is_type(cursor, OBJ_BINDING);
         cursor = ((ot_binding_obj*)ot_as_obj(cursor))->next) {
      ot_binding_obj* binding = (ot_binding_obj*)ot_as_obj(cursor);
      if (binding->name == name) return cursor;
    }
  }
  return ot_nil;
}

static otv env_namespace(ots* state, otv env) {
  return is_type(env, OBJ_ENV) ? ((ot_env_obj*)ot_as_obj(env))->namespace_value
                               : state->vm.current_namespace;
}

static otv namespace_alias(otv namespace_value, otv name) {
  ot_namespace_obj* space = (ot_namespace_obj*)ot_as_obj(namespace_value);
  for (otv cursor = space->aliases; is_type(cursor, OBJ_ALIAS);
       cursor = ((ot_alias_obj*)ot_as_obj(cursor))->next) {
    ot_alias_obj* alias = (ot_alias_obj*)ot_as_obj(cursor);
    if (alias->name == name) return alias->value;
  }
  return ot_nil;
}

static otv resolve_var(ots* state, otv namespace_value, otv symbol, bool report_error) {
  OT_FRAME_SCOPED(state, &namespace_value, &symbol);
  ot_name_obj* name = as_name(symbol);
  const char* slash = memchr(name->bytes, '/', name->length);
  if (slash != NULL && slash != name->bytes && slash != name->bytes + name->length - 1) {
    size_t prefix_length = (size_t)(slash - name->bytes);
    otv prefix = ot_intern(state, name->bytes, prefix_length, false);
    otv member = ot_intern(state, slash + 1, name->length - prefix_length - 1, false);
    OT_FRAME_SCOPED(state, &prefix, &member);
    otv target_name = namespace_alias(namespace_value, prefix);
    if (target_name == ot_nil) target_name = prefix;
    otv target = namespace_find(state, target_name, false);
    otv var = namespace_var(state, target, member, false);
    if (is_type(var, OBJ_VAR)) {
      ot_var_obj* cell = (ot_var_obj*)ot_as_obj(var);
      if (target == namespace_value || !cell->private_value) return var;
    }
  } else {
    if (name->cache_namespace == namespace_value && is_type(name->cache_var, OBJ_VAR))
      return name->cache_var;
    otv var = namespace_var(state, namespace_value, symbol, true);
    if (is_type(var, OBJ_VAR)) {
      name->cache_namespace = namespace_value;
      name->cache_var = var;
      return var;
    }
  }
  if (report_error) return ot_raise(state, "unbound symbol: %.*s", (int)name->length, name->bytes);
  return ot_nil;
}

/* =========================================================================
 * 6. CONDITIONS AND CALLABLES
 * ========================================================================= */

static otv compile_and_run_form(ots* state, otv form, otv env);
static otv apply_value(ots* state, otv callable, otv* args, size_t argc, bool allow_macro);
static bool eval_source(ots* state, const char* source, size_t length, const char* name,
                        bool expand, otv* out);
static otv compile_forms(ots* state, otv forms, otv env, otv params, otv name);
static bool is_catch_clause(otv form);
static otv unwrap_quote(otv value);

static otv signal_condition(ots* state, otv condition) {
  /* Handlers run with themselves hidden, which prevents recursive re-entry
   * while leaving outer handlers visible to predicates and handler bodies. */
  OT_FRAME_SCOPED(state, &condition);
  for (ot_handler_frame* frame = state->vm.handlers; frame != NULL; frame = frame->prev) {
    ot_handler_frame* saved = state->vm.handlers;
    otv pred_function = frame->pred;
    otv handler_function = frame->handler;
    OT_FRAME_SCOPED(state, &pred_function, &handler_function);
    state->vm.handlers = frame->prev;
    otv pred_args[1] = {condition};
    otv pred = apply_value(state, pred_function, pred_args, 1, false);
    if (pred == OT_UNWIND) {
      frame->pred = pred_function;
      frame->handler = handler_function;
      state->vm.handlers = saved;
      return pred;
    }
    if (!is_falsy(pred)) {
      otv handler_args[1] = {condition};
      otv result = apply_value(state, handler_function, handler_args, 1, false);
      frame->pred = pred_function;
      frame->handler = handler_function;
      state->vm.handlers = saved;
      if (result == OT_UNWIND) return result;
    } else {
      frame->pred = pred_function;
      frame->handler = handler_function;
      state->vm.handlers = saved;
    }
  }
  return ot_nil;
}

otv ot_raise_value(ots* state, otv condition) {
  OT_FRAME_SCOPED(state, &condition);
  if (state->vm.unwind_kind == UNWIND_QUIT) return OT_UNWIND;
  otv signalled = signal_condition(state, condition);
  if (signalled == OT_UNWIND) return signalled;
  state->vm.condition = condition;
  state->vm.unwind_kind = UNWIND_CONDITION;
  return OT_UNWIND;
}

otv ot_raise(ots* state, const char* format, ...) {
  buf message = {0};
  va_list args;
  va_start(args, format);
  va_list copy;
  va_copy(copy, args);
  int length = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  if (length >= 0) {
    buf_reserve(&message, (size_t)length + 1);
    if (!message.failed) vsnprintf(message.data, (size_t)length + 1, format, args);
    if (!message.failed) message.length = (size_t)length;
  }
  va_end(args);
  otv condition = ot_table_new(state, 4);
  OT_FRAME_SCOPED(state, &condition);
  otv type_key = ot_nil;
  otv message_key = ot_nil;
  otv error_type = ot_nil;
  otv text = ot_nil;
  OT_FRAME_SCOPED(state, &type_key, &message_key, &error_type, &text);
  type_key = ot_intern(state, "type", 4, true);
  message_key = ot_intern(state, "message", 7, true);
  error_type = ot_intern(state, "error", 5, false);
  text = ot_make_string(state, message.data == NULL ? "error" : message.data,
                        message.data == NULL ? 5 : message.length);
  buf_free(&message);
  ot_table_put(state, condition, type_key, error_type);
  ot_table_put(state, condition, message_key, text);
  return ot_raise_value(state, condition);
}

static otv compile_function_code(ots* state, otv params, otv body, otv env, otv namespace_value,
                                 otv name) {
  OT_FRAME_SCOPED(state, &params, &body, &env, &namespace_value, &name);
  otv compile_env = make_env(state, env, namespace_value);
  OT_FRAME_SCOPED(state, &compile_env);
  otv cursor = params;
  OT_FRAME_SCOPED(state, &cursor);
  if (is_type(cursor, OBJ_PAIR) && symbol_is(as_pair(cursor)->car, "array"))
    cursor = as_pair(cursor)->cdr;
  if (is_type(cursor, OBJ_SYMBOL)) {
    env_bind(state, &compile_env, cursor, ot_nil);
  } else {
    while (is_type(cursor, OBJ_PAIR)) {
      otv parameter = as_pair(cursor)->car;
      cursor = as_pair(cursor)->cdr;
      OT_FRAME_SCOPED(state, &parameter);
      if (!is_type(parameter, OBJ_SYMBOL)) return ot_raise(state, "parameter must be a symbol");
      if (symbol_is(parameter, "&")) {
        if (!is_type(cursor, OBJ_PAIR) || !is_type(as_pair(cursor)->car, OBJ_SYMBOL) ||
            as_pair(cursor)->cdr != ot_null)
          return ot_raise(state, "invalid & rest parameter");
        parameter = as_pair(cursor)->car;
        env_bind(state, &compile_env, parameter, ot_nil);
        cursor = ot_null;
        break;
      }
      env_bind(state, &compile_env, parameter, ot_nil);
    }
    if (is_type(cursor, OBJ_SYMBOL)) env_bind(state, &compile_env, cursor, ot_nil);
    else if (cursor != ot_null) return ot_raise(state, "invalid parameter list");
  }
  if (is_type(env, OBJ_ENV) && is_type(name, OBJ_SYMBOL))
    env_bind(state, &compile_env, name, ot_nil);
  for (cursor = body; is_type(cursor, OBJ_PAIR); cursor = as_pair(cursor)->cdr) {
    otv definition = as_pair(cursor)->car;
    if (!is_type(definition, OBJ_PAIR)) continue;
    otv definition_head = as_pair(definition)->car;
    if (!is_type(definition_head, OBJ_SYMBOL) ||
        (!symbol_is(definition_head, "define") && !symbol_is(definition_head, "def") &&
         !symbol_is(definition_head, "define-")))
      continue;
    otv definition_args = as_pair(definition)->cdr;
    if (!is_type(definition_args, OBJ_PAIR)) continue;
    otv definition_name = as_pair(definition_args)->car;
    if (is_type(definition_name, OBJ_PAIR)) definition_name = as_pair(definition_name)->car;
    if (is_type(definition_name, OBJ_SYMBOL))
      env_bind(state, &compile_env, definition_name, ot_nil);
  }
  return compile_forms(state, body, compile_env, params, name);
}

static otv make_restart(ots* state, const char* name_text, const char* description_text) {
  otv name = ot_intern(state, name_text, strlen(name_text), false);
  otv description = ot_nil;
  OT_FRAME_SCOPED(state, &name, &description);
  description = ot_make_string(state, description_text, strlen(description_text));
  ot_restart_obj* restart = must_alloc(state, sizeof(*restart), OBJ_RESTART);
  restart->name = name;
  restart->description = description;
  restart->id = ++state->vm.next_restart_id;
  return ot_from_obj(restart);
}

static otv interrupt_unwind(ots* state) {
  state->vm.unwind_kind = UNWIND_QUIT;
  state->vm.condition = ot_intern(state, "interrupt", 9, false);
  return OT_UNWIND;
}

static otv run_interrupt_hook(ots* state) {
  if (state->interrupt_hook == NULL || state->vm.in_interrupt_hook) return interrupt_unwind(state);

  otv continue_restart = make_restart(state, "continue", "Resume the interrupted evaluation.");
  OT_FRAME_SCOPED(state, &continue_restart);
  otv abort_restart = make_restart(state, "abort", "Abort the interrupted evaluation.");
  OT_FRAME_SCOPED(state, &abort_restart);
  ot_restart_clause clauses[2] = {
      {.restart = abort_restart, .params = ot_null, .body = ot_null},
      {.restart = continue_restart, .params = ot_null, .body = ot_null},
  };
  ot_restart_frame frame = {.prev = state->vm.restarts, .clauses = clauses, .count = 2};
  state->vm.restarts = &frame;
  state->vm.in_interrupt_hook = true;
  ot_interrupt_action action = state->interrupt_hook(state, state->interrupt_userdata);
  state->vm.in_interrupt_hook = false;
  state->vm.restarts = frame.prev;
  bool interrupted_again = atomic_exchange(&state->vm.interrupted, false);

  if (state->vm.unwind_kind == UNWIND_RESTART) {
    uint64_t continue_id = ((ot_restart_obj*)ot_as_obj(continue_restart))->id;
    uint64_t abort_id = ((ot_restart_obj*)ot_as_obj(abort_restart))->id;
    if (state->vm.unwind_restart_id == continue_id || state->vm.unwind_restart_id == abort_id) {
      action =
          state->vm.unwind_restart_id == continue_id ? OT_INTERRUPT_CONTINUE : OT_INTERRUPT_ABORT;
      state->vm.unwind_kind = UNWIND_NONE;
      state->vm.unwind_restart_id = 0;
      state->vm.unwind_args = ot_nil;
    } else {
      return OT_UNWIND;
    }
  } else if (state->vm.unwind_kind != UNWIND_NONE) {
    return OT_UNWIND;
  }

  if (interrupted_again) action = OT_INTERRUPT_ABORT;
  return action == OT_INTERRUPT_CONTINUE ? ot_nil : interrupt_unwind(state);
}

static bool list_nth(otv list, size_t index, otv* out) {
  while (index != 0 && is_type(list, OBJ_PAIR)) {
    list = as_pair(list)->cdr;
    index--;
  }
  if (!is_type(list, OBJ_PAIR)) return false;
  *out = as_pair(list)->car;
  return true;
}

static otv list_tail(otv list, size_t count) {
  while (count-- != 0 && is_type(list, OBJ_PAIR)) list = as_pair(list)->cdr;
  return list;
}

static otv bind_parameters(ots* state, otv function_value, otv* args, size_t argc) {
  OT_FRAME_SCOPED(state, &function_value);
  ot_function_obj* function = (ot_function_obj*)ot_as_obj(function_value);
  otv parent = function->env;
  otv namespace_value = function->namespace_value;
  if (!is_type(function->code, OBJ_CODE)) return ot_raise(state, "function has no bytecode");
  otv params = as_code(function->code)->params;
  OT_FRAME_SCOPED(state, &parent, &namespace_value, &params);
  otv env = make_env(state, parent, namespace_value);
  OT_FRAME_SCOPED(state, &env);
  if (is_type(params, OBJ_PAIR) && symbol_is(as_pair(params)->car, "array"))
    params = as_pair(params)->cdr;
  if (is_type(params, OBJ_SYMBOL)) {
    otv rest = list_from_array(state, args, argc);
    env_bind(state, &env, params, rest);
    return env;
  }
  size_t index = 0;
  while (is_type(params, OBJ_PAIR)) {
    otv name = as_pair(params)->car;
    otv next = as_pair(params)->cdr;
    OT_FRAME_SCOPED(state, &name, &next);
    if (is_type(name, OBJ_SYMBOL) && symbol_is(name, "&")) {
      if (!is_type(next, OBJ_PAIR) || !is_type(as_pair(next)->car, OBJ_SYMBOL) ||
          as_pair(next)->cdr != ot_null)
        return ot_raise(state, "invalid & rest parameter");
      otv rest = list_from_array(state, args + index, argc - index);
      OT_FRAME_SCOPED(state, &rest);
      name = as_pair(next)->car;
      env_bind(state, &env, name, rest);
      return env;
    }
    if (!is_type(name, OBJ_SYMBOL)) return ot_raise(state, "parameter must be a symbol");
    if (index == argc) return ot_raise(state, "wrong number of arguments");
    params = next;
    env_bind(state, &env, name, args[index++]);
  }
  if (is_type(params, OBJ_SYMBOL)) {
    otv rest = list_from_array(state, args + index, argc - index);
    env_bind(state, &env, params, rest);
    return env;
  }
  if (params != ot_null || index != argc) return ot_raise(state, "wrong number of arguments");
  return env;
}

/* =========================================================================
 * 7. BYTECODE COMPILER, VM, AND SPECIAL FORMS
 * ========================================================================= */

/* Bytecode is deliberately an ASCII string. Opcodes are printable bytes and
 * every operand is four hexadecimal digits, so code has no byte order and can
 * be embedded in a readable function representation without a binary escape
 * layer. Constants, including nested code and opcode descriptors, live in a
 * separate pool. */
typedef enum bytecode_op {
  BC_CONST = 'A',
  BC_GET_LEXICAL = 'B',
  BC_GET_GLOBAL = 'C',
  BC_CALL = 'E',
  BC_TAIL_CALL = 'F',
  BC_POP = 'G',
  BC_RETURN = 'H',
  BC_JUMP = 'J',
  BC_JUMP_IF_FALSE = 'K',
  BC_DUP = 'L',
  BC_JUMP_IF_TRUE = 'M',
  BC_SCOPE = 'N',
  BC_BIND = 'O',
  BC_UNSCOPE = 'P',
  BC_SET_LEXICAL = 'Q',
  BC_SET_GLOBAL = 'R',
  BC_CLOSURE = 'S',
  BC_DEFINE_LEXICAL = 'T',
  BC_DEFINE_GLOBAL = 'U',
  BC_DEFINE_PRIVATE = 'V',
  BC_MACRO = 'W',
  BC_DEF_PARAM = 'X',
  BC_NAMED_LET = 'Y',
  BC_TAIL_NAMED_LET = 'Z',
  BC_TRY = '!',
  BC_HANDLER_BIND = '"',
  BC_RESTART_CASE = '#',
  BC_UNWIND_PROTECT = '$',
  BC_WITH_PARAMS = '%',
  BC_IN_NS = '&',
  BC_REQUIRE = '\'',
  BC_CONS = '(',
  BC_APPEND = ')',
} bytecode_op;

typedef struct bytecode_compiler {
  buf bytes;
  otv constants;
} bytecode_compiler;

static char hex_digit(unsigned value) {
  return (char)(value < 10 ? '0' + value : 'a' + (value - 10));
}

static int hex_value(unsigned char byte) {
  if (byte >= '0' && byte <= '9') return byte - '0';
  if (byte >= 'a' && byte <= 'f') return byte - 'a' + 10;
  if (byte >= 'A' && byte <= 'F') return byte - 'A' + 10;
  return -1;
}

static void bytecode_emit(bytecode_compiler* compiler, bytecode_op op) {
  buf_byte(&compiler->bytes, (char)op);
}

static void bytecode_emit_u16(bytecode_compiler* compiler, size_t value) {
  if (value > UINT16_MAX) {
    compiler->bytes.failed = true;
    return;
  }
  for (unsigned shift = 12;; shift -= 4) {
    buf_byte(&compiler->bytes, hex_digit((unsigned)(value >> shift) & 15u));
    if (shift == 0) break;
  }
}

static size_t bytecode_emit_jump(bytecode_compiler* compiler, bytecode_op op) {
  bytecode_emit(compiler, op);
  size_t operand = compiler->bytes.length;
  bytecode_emit_u16(compiler, 0);
  return operand;
}

static void bytecode_patch_u16(bytecode_compiler* compiler, size_t operand, size_t value) {
  if (value > UINT16_MAX || operand > compiler->bytes.length ||
      compiler->bytes.length - operand < 4) {
    compiler->bytes.failed = true;
    return;
  }
  for (unsigned shift = 12;; shift -= 4) {
    compiler->bytes.data[operand++] = hex_digit((unsigned)(value >> shift) & 15u);
    if (shift == 0) break;
  }
}

static size_t bytecode_constant(ots* state, bytecode_compiler* compiler, otv value) {
  OT_FRAME_SCOPED(state, &compiler->constants, &value);
  ot_array_obj* constants = as_array(compiler->constants);
  otv* values = as_slots(constants->slots)->values;
  for (size_t i = 0; i < constants->length; i++)
    if (values[i] == value) return i;
  size_t index = constants->length;
  array_push(state, &compiler->constants, value);
  return index;
}

static void bytecode_emit_constant(ots* state, bytecode_compiler* compiler, bytecode_op op,
                                   otv value) {
  size_t index = bytecode_constant(state, compiler, value);
  bytecode_emit(compiler, op);
  bytecode_emit_u16(compiler, index);
}

static bool compile_expression(ots* state, bytecode_compiler* compiler, otv form, otv env,
                               bool tail);

static otv make_descriptor(ots* state, otv* values, size_t count) {
  size_t root_count = count + 1;
  otv descriptor = ot_nil;
  otv* roots[root_count];
  for (size_t i = 0; i < count; i++) roots[i] = &values[i];
  roots[count] = &descriptor;
  ot_frame values_frame;
  ot_frame_push(state, &values_frame, roots, root_count);
  descriptor = ot_array_new(state, count);
  for (size_t i = 0; i < count; i++) array_push(state, &descriptor, values[i]);
  ot_frame_pop(state, &values_frame);
  return descriptor;
}

static otv compile_expression_code(ots* state, otv form, otv env) {
  OT_FRAME_SCOPED(state, &form, &env);
  otv forms = ot_cons(state, form, ot_null);
  OT_FRAME_SCOPED(state, &forms);
  return compile_function_code(state, ot_null, forms, env, env_namespace(state, env), ot_nil);
}

static otv copy_forms_until(ots* state, otv forms, otv stop) {
  otv result = ot_null;
  otv tail = ot_nil;
  OT_FRAME_SCOPED(state, &forms, &stop, &result, &tail);
  while (forms != stop) {
    if (!is_type(forms, OBJ_PAIR)) return ot_raise(state, "body must be a proper list");
    otv cell = ot_cons(state, as_pair(forms)->car, ot_null);
    if (result == ot_null) result = cell;
    else as_pair(tail)->cdr = cell;
    tail = cell;
    forms = as_pair(forms)->cdr;
  }
  return result;
}

static bool compile_quasiquote(ots* state, bytecode_compiler* compiler, otv value, otv env,
                               unsigned depth) {
  OT_FRAME_SCOPED(state, &compiler->constants, &value, &env);
  if (!is_type(value, OBJ_PAIR)) {
    bytecode_emit_constant(state, compiler, BC_CONST, value);
    return !compiler->bytes.failed;
  }
  otv head = as_pair(value)->car;
  if (is_type(head, OBJ_SYMBOL) && symbol_is(head, "unquote")) {
    otv expression;
    if (!list_nth(value, 1, &expression) || list_tail(value, 2) != ot_null) {
      (void)ot_raise(state, "unquote: expected one argument");
      return false;
    }
    if (depth == 1) return compile_expression(state, compiler, expression, env, false);
    bytecode_emit_constant(state, compiler, BC_CONST, head);
    if (!compile_quasiquote(state, compiler, expression, env, depth - 1)) return false;
    bytecode_emit_constant(state, compiler, BC_CONST, ot_null);
    bytecode_emit(compiler, BC_CONS);
    bytecode_emit(compiler, BC_CONS);
    return !compiler->bytes.failed;
  }
  if (is_type(head, OBJ_SYMBOL) && symbol_is(head, "quasiquote")) {
    otv expression;
    if (!list_nth(value, 1, &expression) || list_tail(value, 2) != ot_null) {
      (void)ot_raise(state, "quasiquote: expected one argument");
      return false;
    }
    bytecode_emit_constant(state, compiler, BC_CONST, head);
    if (!compile_quasiquote(state, compiler, expression, env, depth + 1)) return false;
    bytecode_emit_constant(state, compiler, BC_CONST, ot_null);
    bytecode_emit(compiler, BC_CONS);
    bytecode_emit(compiler, BC_CONS);
    return !compiler->bytes.failed;
  }
  otv car = as_pair(value)->car;
  otv cdr = as_pair(value)->cdr;
  OT_FRAME_SCOPED(state, &car, &cdr);
  if (depth == 1 && is_type(car, OBJ_PAIR) && is_type(as_pair(car)->car, OBJ_SYMBOL) &&
      symbol_is(as_pair(car)->car, "unquote-splicing")) {
    otv expression;
    if (!list_nth(car, 1, &expression) || list_tail(car, 2) != ot_null) {
      (void)ot_raise(state, "unquote-splicing: expected one argument");
      return false;
    }
    if (!compile_expression(state, compiler, expression, env, false)) return false;
    if (!compile_quasiquote(state, compiler, cdr, env, depth)) return false;
    bytecode_emit(compiler, BC_APPEND);
    return !compiler->bytes.failed;
  }
  if (!compile_quasiquote(state, compiler, car, env, depth)) return false;
  if (!compile_quasiquote(state, compiler, cdr, env, depth)) return false;
  bytecode_emit(compiler, BC_CONS);
  return !compiler->bytes.failed;
}

static bool compile_sequence(ots* state, bytecode_compiler* compiler, otv forms, otv env,
                             bool tail) {
  OT_FRAME_SCOPED(state, &compiler->constants, &forms, &env);
  if (is_type(env, OBJ_ENV)) {
    for (otv scan = forms; is_type(scan, OBJ_PAIR); scan = as_pair(scan)->cdr) {
      otv definition = as_pair(scan)->car;
      if (!is_type(definition, OBJ_PAIR)) continue;
      otv definition_head = as_pair(definition)->car;
      if (!is_type(definition_head, OBJ_SYMBOL) ||
          (!symbol_is(definition_head, "define") && !symbol_is(definition_head, "def") &&
           !symbol_is(definition_head, "define-")))
        continue;
      otv definition_args = as_pair(definition)->cdr;
      if (!is_type(definition_args, OBJ_PAIR)) continue;
      otv definition_name = as_pair(definition_args)->car;
      if (is_type(definition_name, OBJ_PAIR)) definition_name = as_pair(definition_name)->car;
      if (is_type(definition_name, OBJ_SYMBOL) &&
          !is_type(env_binding(env, definition_name), OBJ_BINDING))
        env_bind(state, &env, definition_name, ot_nil);
    }
  }
  if (forms == ot_null) {
    bytecode_emit_constant(state, compiler, BC_CONST, ot_nil);
    return !compiler->bytes.failed;
  }
  while (is_type(forms, OBJ_PAIR)) {
    bool last = as_pair(forms)->cdr == ot_null;
    if (!compile_expression(state, compiler, as_pair(forms)->car, env, tail && last)) return false;
    otv next = as_pair(forms)->cdr;
    if (next != ot_null) bytecode_emit(compiler, BC_POP);
    forms = next;
  }
  if (forms != ot_null) {
    (void)ot_raise(state, "body must be a proper list");
    return false;
  }
  return !compiler->bytes.failed;
}

static bool compile_expression(ots* state, bytecode_compiler* compiler, otv form, otv env,
                               bool tail) {
  OT_FRAME_SCOPED(state, &compiler->constants, &form, &env);
  if (is_type(form, OBJ_SYMBOL)) {
    bytecode_op op = is_type(env_binding(env, form), OBJ_BINDING) ? BC_GET_LEXICAL : BC_GET_GLOBAL;
    bytecode_emit_constant(state, compiler, op, form);
    return !compiler->bytes.failed;
  }
  if (!is_type(form, OBJ_PAIR)) {
    bytecode_emit_constant(state, compiler, BC_CONST, form);
    return !compiler->bytes.failed;
  }

  otv head = as_pair(form)->car;
  OT_FRAME_SCOPED(state, &head);
  if (is_type(head, OBJ_SYMBOL) && as_name(head)->special_form) {
    otv args = as_pair(form)->cdr;
    OT_FRAME_SCOPED(state, &args);
    if (symbol_is(head, "quote")) {
      otv value;
      if (!list_nth(args, 0, &value) || list_tail(args, 1) != ot_null) {
        (void)ot_raise(state, "quote: expected one argument");
        return false;
      }
      bytecode_emit_constant(state, compiler, BC_CONST, value);
      return !compiler->bytes.failed;
    }
    if (symbol_is(head, "lambda") || symbol_is(head, "fn")) {
      otv params;
      if (!list_nth(args, 0, &params)) {
        (void)ot_raise(state, "lambda: missing parameter list");
        return false;
      }
      otv body = list_tail(args, 1);
      OT_FRAME_SCOPED(state, &params, &body);
      otv code = compile_function_code(state, params, body, env, env_namespace(state, env), ot_nil);
      if (code == OT_UNWIND) return false;
      bytecode_emit_constant(state, compiler, BC_CLOSURE, code);
      return !compiler->bytes.failed;
    }
    if (symbol_is(head, "define") || symbol_is(head, "def") || symbol_is(head, "define-")) {
      otv target;
      if (!list_nth(args, 0, &target)) {
        (void)ot_raise(state, "define: missing name");
        return false;
      }
      otv name = target;
      otv doc = ot_nil;
      otv value_form = ot_nil;
      OT_FRAME_SCOPED(state, &target, &name, &doc, &value_form);
      if (is_type(target, OBJ_PAIR)) {
        name = as_pair(target)->car;
        if (!is_type(name, OBJ_SYMBOL)) {
          (void)ot_raise(state, "define: bad name");
          return false;
        }
        otv body = list_tail(args, 1);
        if (is_type(body, OBJ_PAIR) && is_type(as_pair(body)->car, OBJ_STRING) &&
            is_type(as_pair(body)->cdr, OBJ_PAIR)) {
          doc = as_pair(body)->car;
          body = as_pair(body)->cdr;
        }
        OT_FRAME_SCOPED(state, &body);
        otv code = compile_function_code(state, as_pair(target)->cdr, body, env,
                                         env_namespace(state, env), name);
        if (code == OT_UNWIND) return false;
        bytecode_emit_constant(state, compiler, BC_CLOSURE, code);
      } else {
        if (!is_type(name, OBJ_SYMBOL)) {
          (void)ot_raise(state, "define: bad name");
          return false;
        }
        otv values = list_tail(args, 1);
        if (is_type(values, OBJ_PAIR) && is_type(as_pair(values)->car, OBJ_STRING) &&
            is_type(as_pair(values)->cdr, OBJ_PAIR)) {
          doc = as_pair(values)->car;
          values = as_pair(values)->cdr;
        }
        if (!is_type(values, OBJ_PAIR) || as_pair(values)->cdr != ot_null) {
          (void)ot_raise(state, "define: expected one value");
          return false;
        }
        value_form = as_pair(values)->car;
        if (!compile_expression(state, compiler, value_form, env, false)) return false;
      }
      otv fields[] = {name, doc};
      otv descriptor = make_descriptor(state, fields, 2);
      bytecode_emit_constant(
          state, compiler,
          is_type(env, OBJ_ENV)
              ? BC_DEFINE_LEXICAL
              : (symbol_is(head, "define-") ? BC_DEFINE_PRIVATE : BC_DEFINE_GLOBAL),
          descriptor);
      return !compiler->bytes.failed;
    }
    if (symbol_is(head, "begin") || symbol_is(head, "do"))
      return compile_sequence(state, compiler, args, env, tail);
    if (symbol_is(head, "if")) {
      size_t count = proper_list(args) ? list_length(args) : 0;
      if (count < 2 || count > 3) {
        (void)ot_raise(state, "if: expected 2 or 3 arguments");
        return false;
      }
      otv test = as_pair(args)->car;
      otv consequent = as_pair(as_pair(args)->cdr)->car;
      otv alternate = count == 3 ? as_pair(as_pair(as_pair(args)->cdr)->cdr)->car : ot_nil;
      OT_FRAME_SCOPED(state, &test, &consequent, &alternate);
      if (!compile_expression(state, compiler, test, env, false)) return false;
      size_t false_jump = bytecode_emit_jump(compiler, BC_JUMP_IF_FALSE);
      if (!compile_expression(state, compiler, consequent, env, tail)) return false;
      size_t end_jump = bytecode_emit_jump(compiler, BC_JUMP);
      bytecode_patch_u16(compiler, false_jump, compiler->bytes.length);
      if (!compile_expression(state, compiler, alternate, env, tail)) return false;
      bytecode_patch_u16(compiler, end_jump, compiler->bytes.length);
      return !compiler->bytes.failed;
    }
    if (symbol_is(head, "and") || symbol_is(head, "or")) {
      bool is_and = symbol_is(head, "and");
      if (args == ot_null) {
        bytecode_emit_constant(state, compiler, BC_CONST, is_and ? ot_true : ot_false);
        return !compiler->bytes.failed;
      }
      otv cursor = args;
      OT_FRAME_SCOPED(state, &cursor);
      size_t jumps_count = list_length(args);
      size_t jumps[jumps_count == 0 ? 1 : jumps_count];
      size_t jump_count = 0;
      while (is_type(cursor, OBJ_PAIR)) {
        bool last = as_pair(cursor)->cdr == ot_null;
        if (!compile_expression(state, compiler, as_pair(cursor)->car, env, tail && last))
          return false;
        otv next = as_pair(cursor)->cdr;
        if (next != ot_null) {
          bytecode_emit(compiler, BC_DUP);
          jumps[jump_count++] =
              bytecode_emit_jump(compiler, is_and ? BC_JUMP_IF_FALSE : BC_JUMP_IF_TRUE);
          bytecode_emit(compiler, BC_POP);
        }
        cursor = next;
      }
      if (cursor != ot_null) {
        (void)ot_raise(state, "%s: improper form", is_and ? "and" : "or");
        return false;
      }
      for (size_t i = 0; i < jump_count; i++)
        bytecode_patch_u16(compiler, jumps[i], compiler->bytes.length);
      return !compiler->bytes.failed;
    }
    if (symbol_is(head, "cond")) {
      otv clauses = args;
      OT_FRAME_SCOPED(state, &clauses);
      size_t clause_count = list_length(clauses);
      size_t end_jumps[clause_count == 0 ? 1 : clause_count];
      size_t end_count = 0;
      while (is_type(clauses, OBJ_PAIR)) {
        otv clause = as_pair(clauses)->car;
        if (!is_type(clause, OBJ_PAIR)) {
          (void)ot_raise(state, "cond: bad clause");
          return false;
        }
        otv test = as_pair(clause)->car;
        otv body = as_pair(clause)->cdr;
        OT_FRAME_SCOPED(state, &clause, &test, &body);
        bool is_else = is_type(test, OBJ_SYMBOL) && symbol_is(test, "else");
        if (body == ot_null) {
          if (is_else) {
            bytecode_emit_constant(state, compiler, BC_CONST, ot_true);
            end_jumps[end_count++] = bytecode_emit_jump(compiler, BC_JUMP);
          } else {
            if (!compile_expression(state, compiler, test, env, false)) return false;
            bytecode_emit(compiler, BC_DUP);
            size_t false_jump = bytecode_emit_jump(compiler, BC_JUMP_IF_FALSE);
            end_jumps[end_count++] = bytecode_emit_jump(compiler, BC_JUMP);
            bytecode_patch_u16(compiler, false_jump, compiler->bytes.length);
            bytecode_emit(compiler, BC_POP);
          }
          clauses = as_pair(clauses)->cdr;
          continue;
        }
        size_t next_jump = SIZE_MAX;
        if (!is_else) {
          if (!compile_expression(state, compiler, test, env, false)) return false;
          next_jump = bytecode_emit_jump(compiler, BC_JUMP_IF_FALSE);
        }
        if (!compile_sequence(state, compiler, body, env, tail)) return false;
        end_jumps[end_count++] = bytecode_emit_jump(compiler, BC_JUMP);
        if (next_jump != SIZE_MAX) bytecode_patch_u16(compiler, next_jump, compiler->bytes.length);
        clauses = as_pair(clauses)->cdr;
      }
      if (clauses != ot_null) {
        (void)ot_raise(state, "cond: improper clauses");
        return false;
      }
      bytecode_emit_constant(state, compiler, BC_CONST, ot_nil);
      for (size_t i = 0; i < end_count; i++)
        bytecode_patch_u16(compiler, end_jumps[i], compiler->bytes.length);
      return !compiler->bytes.failed;
    }
    if (symbol_is(head, "let") && is_type(args, OBJ_PAIR) &&
        is_type(as_pair(args)->car, OBJ_SYMBOL)) {
      otv name = as_pair(args)->car;
      otv rest = as_pair(args)->cdr;
      if (!is_type(rest, OBJ_PAIR)) {
        (void)ot_raise(state, "let: missing bindings");
        return false;
      }
      otv bindings = as_pair(rest)->car;
      otv body = as_pair(rest)->cdr;
      if (!proper_list(bindings)) {
        (void)ot_raise(state, "let: bad bindings");
        return false;
      }
      otv params = ot_null;
      otv params_tail = ot_nil;
      otv cursor = bindings;
      OT_FRAME_SCOPED(state, &name, &bindings, &body, &params, &params_tail, &cursor);
      size_t count = 0;
      while (is_type(cursor, OBJ_PAIR)) {
        otv binding = as_pair(cursor)->car;
        otv binding_name;
        otv expression;
        if (!list_nth(binding, 0, &binding_name) || !list_nth(binding, 1, &expression) ||
            list_tail(binding, 2) != ot_null || !is_type(binding_name, OBJ_SYMBOL)) {
          (void)ot_raise(state, "let: invalid binding");
          return false;
        }
        OT_FRAME_SCOPED(state, &binding, &binding_name, &expression);
        if (!compile_expression(state, compiler, expression, env, false)) return false;
        otv cell = ot_cons(state, binding_name, ot_null);
        if (params == ot_null) params = cell;
        else as_pair(params_tail)->cdr = cell;
        params_tail = cell;
        count++;
        cursor = as_pair(cursor)->cdr;
      }
      otv named_compile_env = make_env(state, env, env_namespace(state, env));
      OT_FRAME_SCOPED(state, &named_compile_env);
      env_bind(state, &named_compile_env, name, ot_nil);
      otv code = compile_function_code(state, params, body, named_compile_env,
                                       env_namespace(state, env), name);
      if (code == OT_UNWIND) return false;
      otv fields[] = {name, code, ot_make_int((intptr_t)count)};
      otv descriptor = make_descriptor(state, fields, 3);
      bytecode_emit_constant(state, compiler, tail ? BC_TAIL_NAMED_LET : BC_NAMED_LET, descriptor);
      return !compiler->bytes.failed;
    }
    if (symbol_is(head, "let") && is_type(args, OBJ_PAIR) &&
        !is_type(as_pair(args)->car, OBJ_SYMBOL)) {
      otv bindings = as_pair(args)->car;
      otv body = as_pair(args)->cdr;
      if (!proper_list(bindings)) {
        (void)ot_raise(state, "let: bad bindings");
        return false;
      }
      OT_FRAME_SCOPED(state, &bindings, &body);
      otv let_env = make_env(state, env, env_namespace(state, env));
      OT_FRAME_SCOPED(state, &let_env);
      bytecode_emit(compiler, BC_SCOPE);
      while (is_type(bindings, OBJ_PAIR)) {
        otv binding = as_pair(bindings)->car;
        otv name;
        otv expression;
        if (!list_nth(binding, 0, &name) || !list_nth(binding, 1, &expression) ||
            list_tail(binding, 2) != ot_null || !is_type(name, OBJ_SYMBOL)) {
          (void)ot_raise(state, "let: invalid binding");
          return false;
        }
        OT_FRAME_SCOPED(state, &binding, &name, &expression);
        if (!compile_expression(state, compiler, expression, let_env, false)) return false;
        bytecode_emit_constant(state, compiler, BC_BIND, name);
        env_bind(state, &let_env, name, ot_nil);
        bindings = as_pair(bindings)->cdr;
      }
      if (!compile_sequence(state, compiler, body, let_env, tail)) return false;
      if (!tail) bytecode_emit(compiler, BC_UNSCOPE);
      return !compiler->bytes.failed;
    }
    if (symbol_is(head, "set!")) {
      otv name;
      otv expression;
      if (!list_nth(args, 0, &name) || !list_nth(args, 1, &expression) ||
          list_tail(args, 2) != ot_null || !is_type(name, OBJ_SYMBOL)) {
        (void)ot_raise(state, "set!: expected name and value");
        return false;
      }
      OT_FRAME_SCOPED(state, &name, &expression);
      if (!compile_expression(state, compiler, expression, env, false)) return false;
      bytecode_emit_constant(
          state, compiler,
          is_type(env_binding(env, name), OBJ_BINDING) ? BC_SET_LEXICAL : BC_SET_GLOBAL, name);
      return !compiler->bytes.failed;
    }
    if (symbol_is(head, "while")) {
      otv test;
      if (!list_nth(args, 0, &test)) {
        (void)ot_raise(state, "while: missing test");
        return false;
      }
      otv body = list_tail(args, 1);
      OT_FRAME_SCOPED(state, &test, &body);
      size_t loop = compiler->bytes.length;
      if (!compile_expression(state, compiler, test, env, false)) return false;
      size_t done = bytecode_emit_jump(compiler, BC_JUMP_IF_FALSE);
      if (!compile_sequence(state, compiler, body, env, false)) return false;
      bytecode_emit(compiler, BC_POP);
      bytecode_emit(compiler, BC_JUMP);
      bytecode_emit_u16(compiler, loop);
      bytecode_patch_u16(compiler, done, compiler->bytes.length);
      bytecode_emit_constant(state, compiler, BC_CONST, ot_nil);
      return !compiler->bytes.failed;
    }
    if (symbol_is(head, "quasiquote")) {
      otv value;
      if (!list_nth(args, 0, &value) || list_tail(args, 1) != ot_null) {
        (void)ot_raise(state, "quasiquote: expected one argument");
        return false;
      }
      return compile_quasiquote(state, compiler, value, env, 1);
    }
    if (symbol_is(head, "unquote") || symbol_is(head, "unquote-splicing")) {
      (void)ot_raise(state, "%s outside quasiquote",
                     symbol_is(head, "unquote") ? "unquote" : "unquote-splicing");
      return false;
    }
    if (symbol_is(head, "defmacro")) {
      if (is_type(env, OBJ_ENV)) {
        (void)ot_raise(state, "defmacro: top level only");
        return false;
      }
      otv name;
      otv params;
      if (!list_nth(args, 0, &name) || !list_nth(args, 1, &params) || !is_type(name, OBJ_SYMBOL)) {
        (void)ot_raise(state, "defmacro: invalid definition");
        return false;
      }
      otv body = list_tail(args, 2);
      OT_FRAME_SCOPED(state, &name, &params, &body);
      otv code = compile_function_code(state, params, body, env, env_namespace(state, env), name);
      if (code == OT_UNWIND) return false;
      bytecode_emit_constant(state, compiler, BC_CLOSURE, code);
      bytecode_emit(compiler, BC_MACRO);
      otv fields[] = {name, ot_nil};
      otv descriptor = make_descriptor(state, fields, 2);
      bytecode_emit_constant(state, compiler, BC_DEFINE_GLOBAL, descriptor);
      return !compiler->bytes.failed;
    }
    if (symbol_is(head, "defparam")) {
      if (is_type(env, OBJ_ENV)) {
        (void)ot_raise(state, "defparam: top level only");
        return false;
      }
      otv name;
      if (!list_nth(args, 0, &name)) {
        (void)ot_raise(state, "defparam: invalid definition");
        return false;
      }
      otv values = list_tail(args, 1);
      otv doc = ot_nil;
      OT_FRAME_SCOPED(state, &name, &values, &doc);
      if (is_type(values, OBJ_PAIR) && is_type(as_pair(values)->car, OBJ_STRING) &&
          is_type(as_pair(values)->cdr, OBJ_PAIR)) {
        doc = as_pair(values)->car;
        values = as_pair(values)->cdr;
      }
      if (!is_type(name, OBJ_SYMBOL) || !is_type(values, OBJ_PAIR) ||
          as_pair(values)->cdr != ot_null) {
        (void)ot_raise(state, "defparam: invalid definition");
        return false;
      }
      if (!compile_expression(state, compiler, as_pair(values)->car, env, false)) return false;
      otv fields[] = {name, doc};
      otv descriptor = make_descriptor(state, fields, 2);
      bytecode_emit_constant(state, compiler, BC_DEF_PARAM, descriptor);
      return !compiler->bytes.failed;
    }
    if (symbol_is(head, "try")) {
      otv catches = ot_null;
      for (otv cursor = args; is_type(cursor, OBJ_PAIR); cursor = as_pair(cursor)->cdr)
        if (is_catch_clause(as_pair(cursor)->car)) {
          catches = cursor;
          break;
        }
      otv body = ot_nil;
      OT_FRAME_SCOPED(state, &catches, &body);
      body = copy_forms_until(state, args, catches);
      if (body == OT_UNWIND) return false;
      otv body_code =
          compile_function_code(state, ot_null, body, env, env_namespace(state, env), ot_nil);
      if (body_code == OT_UNWIND) return false;
      OT_FRAME_SCOPED(state, &body_code);
      otv compiled_catches = ot_nil;
      OT_FRAME_SCOPED(state, &compiled_catches);
      compiled_catches = ot_array_new(state, list_length(catches));
      while (is_type(catches, OBJ_PAIR)) {
        otv clause = as_pair(catches)->car;
        otv spec;
        otv pred_form;
        otv var;
        if (!list_nth(clause, 1, &spec) || !is_type(spec, OBJ_PAIR) ||
            !list_nth(spec, 0, &pred_form) || !list_nth(spec, 1, &var) ||
            list_tail(spec, 2) != ot_null || !is_type(var, OBJ_SYMBOL)) {
          (void)ot_raise(state, "try: invalid catch clause");
          return false;
        }
        otv pred_code = ot_nil;
        otv params = ot_nil;
        OT_FRAME_SCOPED(state, &clause, &spec, &pred_form, &var, &pred_code, &params);
        pred_code = compile_expression_code(state, pred_form, env);
        if (pred_code == OT_UNWIND) return false;
        params = ot_cons(state, var, ot_null);
        otv handler_code = compile_function_code(state, params, list_tail(clause, 2), env,
                                                 env_namespace(state, env), ot_nil);
        if (handler_code == OT_UNWIND) return false;
        otv fields[] = {pred_code, handler_code};
        otv compiled = make_descriptor(state, fields, 2);
        array_push(state, &compiled_catches, compiled);
        catches = as_pair(catches)->cdr;
      }
      otv fields[] = {body_code, compiled_catches};
      otv descriptor = make_descriptor(state, fields, 2);
      bytecode_emit_constant(state, compiler, BC_TRY, descriptor);
      return !compiler->bytes.failed;
    }
    if (symbol_is(head, "handler-bind") || symbol_is(head, "with-params")) {
      otv bindings;
      if (!list_nth(args, 0, &bindings) || !proper_list(bindings)) {
        (void)ot_raise(state, "%s: bad bindings",
                       symbol_is(head, "handler-bind") ? "handler-bind" : "with-params");
        return false;
      }
      otv compiled_bindings = ot_nil;
      otv cursor = bindings;
      OT_FRAME_SCOPED(state, &bindings, &compiled_bindings, &cursor);
      compiled_bindings = ot_array_new(state, list_length(bindings));
      while (is_type(cursor, OBJ_PAIR)) {
        otv binding = as_pair(cursor)->car;
        otv left;
        otv right;
        if (!list_nth(binding, 0, &left) || !list_nth(binding, 1, &right) ||
            list_tail(binding, 2) != ot_null) {
          (void)ot_raise(state, "%s: invalid binding",
                         symbol_is(head, "handler-bind") ? "handler-bind" : "with-params");
          return false;
        }
        otv left_code = ot_nil;
        OT_FRAME_SCOPED(state, &binding, &left, &right, &left_code);
        left_code = compile_expression_code(state, left, env);
        if (left_code == OT_UNWIND) return false;
        otv right_code = compile_expression_code(state, right, env);
        if (right_code == OT_UNWIND) return false;
        otv fields[] = {left_code, right_code};
        otv compiled = make_descriptor(state, fields, 2);
        array_push(state, &compiled_bindings, compiled);
        cursor = as_pair(cursor)->cdr;
      }
      otv body = list_tail(args, 1);
      OT_FRAME_SCOPED(state, &body);
      otv body_code =
          compile_function_code(state, ot_null, body, env, env_namespace(state, env), ot_nil);
      if (body_code == OT_UNWIND) return false;
      otv fields[] = {compiled_bindings, body_code};
      otv descriptor = make_descriptor(state, fields, 2);
      bytecode_emit_constant(state, compiler,
                             symbol_is(head, "handler-bind") ? BC_HANDLER_BIND : BC_WITH_PARAMS,
                             descriptor);
      return !compiler->bytes.failed;
    }
    if (symbol_is(head, "restart-case")) {
      otv expression;
      if (!list_nth(args, 0, &expression)) {
        (void)ot_raise(state, "restart-case: missing expression");
        return false;
      }
      otv expression_code = ot_nil;
      otv clauses = ot_nil;
      otv compiled_clauses = ot_nil;
      OT_FRAME_SCOPED(state, &expression, &expression_code, &clauses, &compiled_clauses);
      expression_code = compile_expression_code(state, expression, env);
      if (expression_code == OT_UNWIND) return false;
      clauses = list_tail(args, 1);
      if (!proper_list(clauses)) {
        (void)ot_raise(state, "restart-case: invalid clauses");
        return false;
      }
      compiled_clauses = ot_array_new(state, list_length(clauses));
      while (is_type(clauses, OBJ_PAIR)) {
        otv clause = as_pair(clauses)->car;
        otv name;
        if (!list_nth(clause, 0, &name) || !is_type(name, OBJ_SYMBOL)) {
          (void)ot_raise(state, "restart-case: invalid name");
          return false;
        }
        otv rest = list_tail(clause, 1);
        otv description = ot_nil;
        if (is_type(rest, OBJ_PAIR) && is_type(as_pair(rest)->car, OBJ_STRING) &&
            is_type(as_pair(rest)->cdr, OBJ_PAIR)) {
          description = as_pair(rest)->car;
          rest = as_pair(rest)->cdr;
        }
        if (!is_type(rest, OBJ_PAIR)) {
          (void)ot_raise(state, "restart-case: missing parameter list");
          return false;
        }
        otv params = as_pair(rest)->car;
        otv body = as_pair(rest)->cdr;
        OT_FRAME_SCOPED(state, &clause, &name, &rest, &description, &params, &body);
        otv clause_code =
            compile_function_code(state, params, body, env, env_namespace(state, env), ot_nil);
        if (clause_code == OT_UNWIND) return false;
        otv fields[] = {name, description, clause_code};
        otv compiled = make_descriptor(state, fields, 3);
        array_push(state, &compiled_clauses, compiled);
        clauses = as_pair(clauses)->cdr;
      }
      otv fields[] = {expression_code, compiled_clauses};
      otv descriptor = make_descriptor(state, fields, 2);
      bytecode_emit_constant(state, compiler, BC_RESTART_CASE, descriptor);
      return !compiler->bytes.failed;
    }
    if (symbol_is(head, "unwind-protect") || symbol_is(head, "defer")) {
      otv expression;
      if (!list_nth(args, 0, &expression)) {
        (void)ot_raise(state, "unwind-protect: missing expression");
        return false;
      }
      otv expression_code = ot_nil;
      otv cleanup = ot_nil;
      OT_FRAME_SCOPED(state, &expression, &expression_code, &cleanup);
      expression_code = compile_expression_code(state, expression, env);
      if (expression_code == OT_UNWIND) return false;
      cleanup = list_tail(args, 1);
      otv cleanup_code =
          compile_function_code(state, ot_null, cleanup, env, env_namespace(state, env), ot_nil);
      if (cleanup_code == OT_UNWIND) return false;
      otv fields[] = {expression_code, cleanup_code};
      otv descriptor = make_descriptor(state, fields, 2);
      bytecode_emit_constant(state, compiler, BC_UNWIND_PROTECT, descriptor);
      return !compiler->bytes.failed;
    }
    if (symbol_is(head, "in-ns") || symbol_is(head, "ns")) {
      otv name;
      if (!list_nth(args, 0, &name)) {
        (void)ot_raise(state, "%s: missing name", symbol_is(head, "ns") ? "ns" : "in-ns");
        return false;
      }
      name = unwrap_quote(name);
      if (is_type(name, OBJ_KEYWORD))
        name = ot_intern(state, as_name(name)->bytes, as_name(name)->length, false);
      else if (is_type(name, OBJ_STRING)) {
        const char* bytes;
        size_t length;
        ot_string_bytes(name, &bytes, &length);
        name = ot_intern(state, bytes, length, false);
      }
      if (!is_type(name, OBJ_SYMBOL)) {
        (void)ot_raise(state, "in-ns: invalid name");
        return false;
      }
      bytecode_emit_constant(state, compiler, BC_IN_NS, name);
      if (symbol_is(head, "ns")) {
        otv clauses = list_tail(args, 1);
        while (is_type(clauses, OBJ_PAIR)) {
          otv clause = as_pair(clauses)->car;
          if (is_type(clause, OBJ_PAIR) && is_type(as_pair(clause)->car, OBJ_KEYWORD) &&
              as_name(as_pair(clause)->car)->length == 7 &&
              memcmp(as_name(as_pair(clause)->car)->bytes, "require", 7) == 0) {
            bytecode_emit(compiler, BC_POP);
            bytecode_emit_constant(state, compiler, BC_REQUIRE, as_pair(clause)->cdr);
          }
          clauses = as_pair(clauses)->cdr;
        }
        if (clauses != ot_null) {
          (void)ot_raise(state, "ns: improper clauses");
          return false;
        }
      }
      return !compiler->bytes.failed;
    }
    if (symbol_is(head, "require")) {
      bytecode_emit_constant(state, compiler, BC_REQUIRE, args);
      return !compiler->bytes.failed;
    }
    (void)ot_raise(state, "compiler: unsupported special form");
    return false;
  }

  size_t length;
  if (!proper_list_length(form, &length)) {
    (void)ot_raise(state, "application must be a proper list");
    return false;
  }
  if (length == 0 || length - 1 > UINT16_MAX) {
    (void)ot_raise(state, "application has too many arguments");
    return false;
  }
  otv cursor = form;
  OT_FRAME_SCOPED(state, &cursor);
  while (is_type(cursor, OBJ_PAIR)) {
    if (!compile_expression(state, compiler, as_pair(cursor)->car, env, false)) return false;
    cursor = as_pair(cursor)->cdr;
  }
  bytecode_emit(compiler, tail ? BC_TAIL_CALL : BC_CALL);
  bytecode_emit_u16(compiler, length - 1);
  return !compiler->bytes.failed;
}

static otv make_code(ots* state, const buf* bytes, otv constants, otv params, otv name) {
  OT_FRAME_SCOPED(state, &constants, &params, &name);
  otv byte_values = make_bytes(state, bytes->length);
  OT_FRAME_SCOPED(state, &byte_values);
  if (bytes->length != 0) memcpy(as_bytes(byte_values)->data, bytes->data, bytes->length);
  ot_code_obj* code = must_alloc(state, sizeof(*code), OBJ_CODE);
  code->bytes = byte_values;
  code->constants = constants;
  code->params = params;
  code->name = name;
  code->length = bytes->length;
  code->constant_count = as_array(constants)->length;
  return ot_from_obj(code);
}

static otv compile_forms(ots* state, otv forms, otv env, otv params, otv name) {
  OT_FRAME_SCOPED(state, &forms, &env, &params, &name);
  bytecode_compiler compiler = {.constants = ot_nil};
  compiler.constants = ot_array_new(state, 8);
  OT_FRAME_SCOPED(state, &compiler.constants);
  if (!compile_sequence(state, &compiler, forms, env, true)) {
    buf_free(&compiler.bytes);
    return OT_UNWIND;
  }
  bytecode_emit(&compiler, BC_RETURN);
  if (compiler.bytes.failed) {
    buf_free(&compiler.bytes);
    return ot_raise(state, "bytecode exceeds implementation limits");
  }
  otv code = make_code(state, &compiler.bytes, compiler.constants, params, name);
  buf_free(&compiler.bytes);
  return code;
}

static otv compile_form(ots* state, otv form, otv env) {
  OT_FRAME_SCOPED(state, &form, &env);
  otv forms = ot_cons(state, form, ot_null);
  OT_FRAME_SCOPED(state, &forms);
  return compile_forms(state, forms, env, ot_null, ot_nil);
}

static void vm_stack_reserve(ots* state, size_t count) {
  if (count <= state->vm.vm_stack_capacity) return;
  size_t capacity = state->vm.vm_stack_capacity == 0 ? 256 : state->vm.vm_stack_capacity;
  while (capacity < count) capacity *= 2;
  void* grown = ot_host_realloc(state->vm.vm_stack, capacity * sizeof(*state->vm.vm_stack));
  if (grown == NULL) abort();
  state->vm.vm_stack = grown;
  state->vm.vm_stack_capacity = capacity;
}

static void vm_push(ots* state, otv value) {
  vm_stack_reserve(state, state->vm.vm_stack_count + 1);
  state->vm.vm_stack[state->vm.vm_stack_count++] = value;
}

static void vm_frames_reserve(ots* state, size_t count) {
  if (count <= state->vm.vm_frame_capacity) return;
  size_t capacity = state->vm.vm_frame_capacity == 0 ? 32 : state->vm.vm_frame_capacity;
  while (capacity < count) capacity *= 2;
  void* grown = ot_host_realloc(state->vm.vm_frames, capacity * sizeof(*state->vm.vm_frames));
  if (grown == NULL) abort();
  state->vm.vm_frames = grown;
  state->vm.vm_frame_capacity = capacity;
}

static void vm_push_frame(ots* state, otv function, otv env, size_t base) {
  vm_frames_reserve(state, state->vm.vm_frame_count + 1);
  state->vm.vm_frames[state->vm.vm_frame_count++] = (ot_vm_frame){
      .function = function,
      .env = env,
      .ip = 0,
      .base = base,
  };
}

static otv make_bytecode_function(ots* state, otv code, otv env, otv namespace_value, otv name) {
  OT_FRAME_SCOPED(state, &code, &env, &namespace_value, &name);
  ot_function_obj* function = must_alloc(state, sizeof(*function), OBJ_FUNCTION);
  function->code = code;
  function->env = env;
  function->namespace_value = namespace_value;
  function->name = name;
  return ot_from_obj(function);
}

static bool bytecode_read_u16(ot_code_obj* code, size_t* ip, size_t* out) {
  if (*ip > code->length || code->length - *ip < 4) return false;
  unsigned value = 0;
  unsigned char* bytes = as_bytes(code->bytes)->data;
  for (size_t i = 0; i < 4; i++) {
    int digit = hex_value(bytes[(*ip)++]);
    if (digit < 0) return false;
    value = (value << 4) | (unsigned)digit;
  }
  *out = value;
  return true;
}

static otv vm_unwind_to(ots* state, size_t floor) {
  while (state->vm.vm_frame_count > floor) {
    ot_vm_frame frame = state->vm.vm_frames[--state->vm.vm_frame_count];
    state->vm.vm_stack_count = frame.base;
  }
  return OT_UNWIND;
}

static otv vm_execute(ots* state, size_t floor);
static otv require_forms(ots* state, otv specs);
static otv vm_execute_dynamic(ots* state, unsigned char instruction, otv descriptor, otv env,
                              otv namespace_value);

static otv vm_call_function(ots* state, otv callable, otv* args, size_t argc) {
  OT_FRAME_SCOPED(state, &callable);
  otv env = bind_parameters(state, callable, args, argc);
  if (env == OT_UNWIND) return env;
  OT_FRAME_SCOPED(state, &env);
  size_t floor = state->vm.vm_frame_count;
  size_t base = state->vm.vm_stack_count;
  vm_push_frame(state, callable, env, base);
  return vm_execute(state, floor);
}

static otv descriptor_value(otv descriptor, size_t index) {
  if (!is_type(descriptor, OBJ_ARRAY) || index >= as_array(descriptor)->length) return ot_nil;
  return as_slots(as_array(descriptor)->slots)->values[index];
}

static otv append_values(ots* state, otv left, otv right) {
  OT_FRAME_SCOPED(state, &left, &right);
  if (!proper_list(left)) return ot_raise(state, "unquote-splicing: expected list");
  if (left == ot_null) return right;
  otv result = ot_null;
  otv tail = ot_nil;
  OT_FRAME_SCOPED(state, &result, &tail);
  while (is_type(left, OBJ_PAIR)) {
    otv cell = ot_cons(state, as_pair(left)->car, ot_null);
    if (result == ot_null) result = cell;
    else as_pair(tail)->cdr = cell;
    tail = cell;
    left = as_pair(left)->cdr;
  }
  as_pair(tail)->cdr = right;
  return result;
}

static otv apply_value(ots* state, otv callable, otv* args, size_t argc, bool allow_macro) {
  OT_FRAME_SCOPED(state, &callable);
  size_t root_count = argc == 0 ? 1 : argc;
  otv* roots[root_count];
  otv dummy = ot_nil;
  for (size_t i = 0; i < root_count; i++) roots[i] = argc == 0 ? &dummy : &args[i];
  ot_frame args_frame;
  ot_frame_push(state, &args_frame, roots, root_count);
  otv result;
  if (is_type(callable, OBJ_NAT)) {
    result = ((ot_nat_obj*)ot_as_obj(callable))->function(state, args, (int)argc);
  } else if (is_type(callable, OBJ_FUNCTION)) {
    result = vm_call_function(state, callable, args, argc);
  } else if (is_type(callable, OBJ_MACRO) && allow_macro) {
    result = apply_value(state, ((ot_macro_obj*)ot_as_obj(callable))->function, args, argc, false);
  } else if (is_type(callable, OBJ_TABLE)) {
    if (argc < 1 || argc > 2) result = ot_raise(state, "table call: expected 1 or 2 arguments");
    else result = ot_table_get(state, callable, args[0], argc == 2 ? args[1] : ot_nil);
  } else if (is_type(callable, OBJ_ARRAY)) {
    if (argc < 1 || argc > 2 || !ot_is_int(args[0])) {
      result = ot_raise(state, "array call: expected integer key and optional default");
    } else {
      intptr_t index = ot_get_int(args[0]);
      ot_array_obj* array = as_array(callable);
      result = index >= 0 && (size_t)index < array->length ? as_slots(array->slots)->values[index]
                                                           : (argc == 2 ? args[1] : ot_nil);
    }
  } else if (is_type(callable, OBJ_KEYWORD)) {
    if (argc < 1 || argc > 2) result = ot_raise(state, "keyword call: expected collection");
    else if (is_type(args[0], OBJ_TABLE))
      result = ot_table_get(state, args[0], callable, argc == 2 ? args[1] : ot_nil);
    else result = argc == 2 ? args[1] : ot_nil;
  } else if (is_type(callable, OBJ_PARAM)) {
    if (argc != 0) result = ot_raise(state, "param call: expected no arguments");
    else {
      result = ((ot_param_obj*)ot_as_obj(callable))->value;
      for (ot_param_frame* frame = state->vm.params; frame != NULL; frame = frame->prev)
        if (frame->param == callable) {
          result = frame->value;
          break;
        }
    }
  } else {
    result = ot_raise(state, "value is not callable");
  }
  ot_frame_pop(state, &args_frame);
  return result;
}

static bool vm_enter_call(ots* state, size_t argc, bool tail) {
  if (state->vm.vm_stack_count < argc + 1) {
    (void)ot_raise(state, "vm: operand stack underflow");
    return false;
  }
  size_t call_base = state->vm.vm_stack_count - argc - 1;
  otv callable = state->vm.vm_stack[call_base];
  OT_FRAME_SCOPED(state, &callable);
  if (is_type(callable, OBJ_FUNCTION)) {
    otv env = bind_parameters(state, callable, state->vm.vm_stack + call_base + 1, argc);
    if (env == OT_UNWIND) return false;
    OT_FRAME_SCOPED(state, &env);
    size_t base = tail ? state->vm.vm_frames[state->vm.vm_frame_count - 1].base : call_base;
    if (tail) {
      ot_vm_frame* frame = &state->vm.vm_frames[state->vm.vm_frame_count - 1];
      state->vm.vm_stack_count = base;
      frame->function = callable;
      frame->env = env;
      frame->ip = 0;
    } else {
      state->vm.vm_stack_count = call_base;
      vm_push_frame(state, callable, env, base);
    }
    return true;
  }

  size_t values_count = argc == 0 ? 1 : argc;
  otv values[values_count];
  otv* roots[values_count];
  for (size_t i = 0; i < values_count; i++) {
    values[i] = argc == 0 ? ot_nil : state->vm.vm_stack[call_base + 1 + i];
    roots[i] = &values[i];
  }
  ot_frame values_frame;
  ot_frame_push(state, &values_frame, roots, values_count);
  otv result = apply_value(state, callable, values, argc, false);
  ot_frame_pop(state, &values_frame);
  if (result == OT_UNWIND) return false;
  if (!tail) {
    state->vm.vm_stack_count = call_base;
    vm_push(state, result);
    return true;
  }

  ot_vm_frame leaving = state->vm.vm_frames[--state->vm.vm_frame_count];
  state->vm.vm_stack_count = leaving.base;
  vm_push(state, result);
  return true;
}

static otv vm_execute_loop(ots* state, size_t floor) {
  while (state->vm.vm_frame_count > floor) {
    if (state->vm.vm_frame_count > state->vm.frame_limit) {
      (void)vm_unwind_to(state, floor);
      return ot_raise(state, "maximum VM frame depth exceeded");
    }
    if (++state->vm.poll_count >= 1024) {
      state->vm.poll_count = 0;
      if (atomic_exchange(&state->vm.interrupted, false)) {
        otv interrupted = run_interrupt_hook(state);
        if (interrupted == OT_UNWIND) return vm_unwind_to(state, floor);
      }
    }

    ot_vm_frame* frame = &state->vm.vm_frames[state->vm.vm_frame_count - 1];
    ot_function_obj* function = (ot_function_obj*)ot_as_obj(frame->function);
    if (!is_type(function->code, OBJ_CODE)) {
      (void)ot_raise(state, "vm: function has no bytecode");
      return vm_unwind_to(state, floor);
    }
    ot_code_obj* code = as_code(function->code);
    if (frame->ip >= code->length) {
      (void)ot_raise(state, "vm: instruction pointer outside bytecode");
      return vm_unwind_to(state, floor);
    }
    unsigned char instruction = as_bytes(code->bytes)->data[frame->ip++];
    size_t operand = 0;
    if (instruction == BC_CONST || instruction == BC_GET_LEXICAL || instruction == BC_GET_GLOBAL ||
        instruction == BC_CALL || instruction == BC_TAIL_CALL || instruction == BC_JUMP ||
        instruction == BC_JUMP_IF_FALSE || instruction == BC_JUMP_IF_TRUE ||
        instruction == BC_BIND || instruction == BC_SET_LEXICAL || instruction == BC_SET_GLOBAL ||
        instruction == BC_CLOSURE || instruction == BC_DEFINE_LEXICAL ||
        instruction == BC_DEFINE_GLOBAL || instruction == BC_DEFINE_PRIVATE ||
        instruction == BC_DEF_PARAM || instruction == BC_NAMED_LET ||
        instruction == BC_TAIL_NAMED_LET || instruction == BC_TRY ||
        instruction == BC_HANDLER_BIND || instruction == BC_RESTART_CASE ||
        instruction == BC_UNWIND_PROTECT || instruction == BC_WITH_PARAMS ||
        instruction == BC_IN_NS || instruction == BC_REQUIRE) {
      if (!bytecode_read_u16(code, &frame->ip, &operand)) {
        (void)ot_raise(state, "vm: malformed ASCII operand");
        return vm_unwind_to(state, floor);
      }
    }

    if (instruction == BC_CONST || instruction == BC_GET_LEXICAL || instruction == BC_GET_GLOBAL) {
      if (operand >= code->constant_count) {
        (void)ot_raise(state, "vm: constant index out of range");
        return vm_unwind_to(state, floor);
      }
      otv value = as_slots(as_array(code->constants)->slots)->values[operand];
      if (instruction == BC_CONST) {
        vm_push(state, value);
        continue;
      }
      if (instruction == BC_GET_LEXICAL) {
        otv binding = env_binding(frame->env, value);
        if (!is_type(binding, OBJ_BINDING)) {
          (void)ot_raise(state, "vm: missing lexical binding");
          return vm_unwind_to(state, floor);
        }
        vm_push(state, ((ot_binding_obj*)ot_as_obj(binding))->value);
        continue;
      }
      if (instruction == BC_GET_GLOBAL) {
        otv var = resolve_var(state, function->namespace_value, value, true);
        if (var == OT_UNWIND) return vm_unwind_to(state, floor);
        vm_push(state, ((ot_var_obj*)ot_as_obj(var))->value);
        continue;
      }
    }

    if (instruction == BC_CLOSURE) {
      if (operand >= code->constant_count) {
        (void)ot_raise(state, "vm: closure constant out of range");
        return vm_unwind_to(state, floor);
      }
      otv nested_code = as_slots(as_array(code->constants)->slots)->values[operand];
      if (!is_type(nested_code, OBJ_CODE)) {
        (void)ot_raise(state, "vm: closure constant is not code");
        return vm_unwind_to(state, floor);
      }
      otv closure = make_bytecode_function(state, nested_code, frame->env,
                                           function->namespace_value, as_code(nested_code)->name);
      vm_push(state, closure);
      continue;
    }
    if (instruction == BC_DEFINE_LEXICAL || instruction == BC_DEFINE_GLOBAL ||
        instruction == BC_DEFINE_PRIVATE) {
      if (operand >= code->constant_count || state->vm.vm_stack_count <= frame->base) {
        (void)ot_raise(state, "vm: invalid definition");
        return vm_unwind_to(state, floor);
      }
      otv descriptor = as_slots(as_array(code->constants)->slots)->values[operand];
      otv name = descriptor_value(descriptor, 0);
      otv doc = descriptor_value(descriptor, 1);
      otv value = state->vm.vm_stack[state->vm.vm_stack_count - 1];
      if (!is_type(name, OBJ_SYMBOL)) {
        (void)ot_raise(state, "vm: invalid definition descriptor");
        return vm_unwind_to(state, floor);
      }
      if (is_type(value, OBJ_FUNCTION)) ((ot_function_obj*)ot_as_obj(value))->name = name;
      if (instruction == BC_DEFINE_LEXICAL) env_bind(state, &frame->env, name, value);
      else
        define_var(state, state->vm.current_namespace, name, value, doc,
                   instruction == BC_DEFINE_PRIVATE);
      continue;
    }
    if (instruction == BC_MACRO) {
      if (state->vm.vm_stack_count <= frame->base ||
          !is_type(state->vm.vm_stack[state->vm.vm_stack_count - 1], OBJ_FUNCTION)) {
        (void)ot_raise(state, "vm: macro requires a function");
        return vm_unwind_to(state, floor);
      }
      otv function_value = state->vm.vm_stack[state->vm.vm_stack_count - 1];
      OT_FRAME_SCOPED(state, &function_value);
      ot_macro_obj* macro = must_alloc(state, sizeof(*macro), OBJ_MACRO);
      macro->function = function_value;
      state->vm.vm_stack[state->vm.vm_stack_count - 1] = ot_from_obj(macro);
      continue;
    }
    if (instruction == BC_DEF_PARAM) {
      if (operand >= code->constant_count || state->vm.vm_stack_count <= frame->base) {
        (void)ot_raise(state, "vm: invalid parameter definition");
        return vm_unwind_to(state, floor);
      }
      otv descriptor = as_slots(as_array(code->constants)->slots)->values[operand];
      otv name = descriptor_value(descriptor, 0);
      otv doc = descriptor_value(descriptor, 1);
      otv value = state->vm.vm_stack[state->vm.vm_stack_count - 1];
      OT_FRAME_SCOPED(state, &descriptor, &name, &doc, &value);
      if (!is_type(name, OBJ_SYMBOL)) {
        (void)ot_raise(state, "vm: invalid parameter descriptor");
        return vm_unwind_to(state, floor);
      }
      ot_param_obj* param = must_alloc(state, sizeof(*param), OBJ_PARAM);
      param->name = name;
      param->value = value;
      otv made = ot_from_obj(param);
      state->vm.vm_stack[state->vm.vm_stack_count - 1] = made;
      define_var(state, state->vm.current_namespace, name, made, doc, false);
      continue;
    }
    if (instruction == BC_NAMED_LET || instruction == BC_TAIL_NAMED_LET) {
      if (operand >= code->constant_count) {
        (void)ot_raise(state, "vm: named let constant out of range");
        return vm_unwind_to(state, floor);
      }
      otv descriptor = as_slots(as_array(code->constants)->slots)->values[operand];
      otv name = descriptor_value(descriptor, 0);
      otv nested_code = descriptor_value(descriptor, 1);
      otv count_value = descriptor_value(descriptor, 2);
      if (!is_type(name, OBJ_SYMBOL) || !is_type(nested_code, OBJ_CODE) ||
          !ot_is_int(count_value) || ot_get_int(count_value) < 0) {
        (void)ot_raise(state, "vm: invalid named let descriptor");
        return vm_unwind_to(state, floor);
      }
      size_t count = (size_t)ot_get_int(count_value);
      if (state->vm.vm_stack_count < frame->base + count) {
        (void)ot_raise(state, "vm: named let operand stack underflow");
        return vm_unwind_to(state, floor);
      }
      size_t call_base = state->vm.vm_stack_count - count;
      otv named_env = ot_nil;
      otv named_namespace = function->namespace_value;
      OT_FRAME_SCOPED(state, &descriptor, &name, &nested_code, &named_env, &named_namespace);
      named_env = make_env(state, frame->env, named_namespace);
      otv callable = make_bytecode_function(state, nested_code, named_env, named_namespace, name);
      OT_FRAME_SCOPED(state, &callable);
      env_bind(state, &named_env, name, callable);
      otv call_env = bind_parameters(state, callable, state->vm.vm_stack + call_base, count);
      if (call_env == OT_UNWIND) return vm_unwind_to(state, floor);
      OT_FRAME_SCOPED(state, &call_env);
      if (instruction == BC_TAIL_NAMED_LET) {
        size_t base = frame->base;
        state->vm.vm_stack_count = base;
        frame->function = callable;
        frame->env = call_env;
        frame->ip = 0;
      } else {
        state->vm.vm_stack_count = call_base;
        vm_push_frame(state, callable, call_env, call_base);
      }
      continue;
    }
    if (instruction == BC_TRY || instruction == BC_HANDLER_BIND || instruction == BC_RESTART_CASE ||
        instruction == BC_UNWIND_PROTECT || instruction == BC_WITH_PARAMS) {
      if (operand >= code->constant_count) {
        (void)ot_raise(state, "vm: dynamic form constant out of range");
        return vm_unwind_to(state, floor);
      }
      otv descriptor = as_slots(as_array(code->constants)->slots)->values[operand];
      otv dynamic_env = frame->env;
      otv dynamic_namespace = function->namespace_value;
      OT_FRAME_SCOPED(state, &descriptor, &dynamic_env, &dynamic_namespace);
      otv result =
          vm_execute_dynamic(state, instruction, descriptor, dynamic_env, dynamic_namespace);
      if (result == OT_UNWIND) return vm_unwind_to(state, floor);
      vm_push(state, result);
      continue;
    }
    if (instruction == BC_IN_NS || instruction == BC_REQUIRE) {
      if (operand >= code->constant_count) {
        (void)ot_raise(state, "vm: namespace constant out of range");
        return vm_unwind_to(state, floor);
      }
      otv value = as_slots(as_array(code->constants)->slots)->values[operand];
      otv result = ot_nil;
      if (instruction == BC_IN_NS) {
        if (!is_type(value, OBJ_SYMBOL)) result = ot_raise(state, "vm: invalid namespace name");
        else state->vm.current_namespace = namespace_find(state, value, true);
      } else {
        result = require_forms(state, value);
      }
      if (result == OT_UNWIND) return vm_unwind_to(state, floor);
      vm_push(state, result);
      continue;
    }
    if (instruction == BC_CONS || instruction == BC_APPEND) {
      if (state->vm.vm_stack_count < frame->base + 2) {
        (void)ot_raise(state, "vm: constructor operand stack underflow");
        return vm_unwind_to(state, floor);
      }
      otv right = state->vm.vm_stack[--state->vm.vm_stack_count];
      otv left = state->vm.vm_stack[--state->vm.vm_stack_count];
      OT_FRAME_SCOPED(state, &left, &right);
      otv value =
          instruction == BC_CONS ? ot_cons(state, left, right) : append_values(state, left, right);
      if (value == OT_UNWIND) return vm_unwind_to(state, floor);
      vm_push(state, value);
      continue;
    }

    if (instruction == BC_CALL || instruction == BC_TAIL_CALL) {
      size_t before = state->vm.vm_frame_count;
      if (!vm_enter_call(state, operand, instruction == BC_TAIL_CALL))
        return vm_unwind_to(state, floor);
      if (instruction == BC_TAIL_CALL && state->vm.vm_frame_count < before &&
          state->vm.vm_frame_count == floor) {
        otv result = state->vm.vm_stack[state->vm.vm_stack_count - 1];
        state->vm.vm_stack_count--;
        return result;
      }
      continue;
    }
    if (instruction == BC_SET_LEXICAL || instruction == BC_SET_GLOBAL) {
      if (operand >= code->constant_count || state->vm.vm_stack_count <= frame->base) {
        (void)ot_raise(state, "vm: invalid assignment");
        return vm_unwind_to(state, floor);
      }
      otv name = as_slots(as_array(code->constants)->slots)->values[operand];
      otv assigned = state->vm.vm_stack[state->vm.vm_stack_count - 1];
      if (instruction == BC_SET_LEXICAL) {
        otv binding = env_binding(frame->env, name);
        if (!is_type(binding, OBJ_BINDING)) {
          (void)ot_raise(state, "vm: missing lexical binding");
          return vm_unwind_to(state, floor);
        }
        ((ot_binding_obj*)ot_as_obj(binding))->value = assigned;
      } else {
        otv var = resolve_var(state, function->namespace_value, name, true);
        if (var == OT_UNWIND) return vm_unwind_to(state, floor);
        ((ot_var_obj*)ot_as_obj(var))->value = assigned;
      }
      continue;
    }
    if (instruction == BC_DUP) {
      if (state->vm.vm_stack_count <= frame->base) {
        (void)ot_raise(state, "vm: operand stack underflow");
        return vm_unwind_to(state, floor);
      }
      vm_push(state, state->vm.vm_stack[state->vm.vm_stack_count - 1]);
      continue;
    }
    if (instruction == BC_SCOPE) {
      frame->env = make_env(state, frame->env, env_namespace(state, frame->env));
      continue;
    }
    if (instruction == BC_BIND) {
      if (operand >= code->constant_count || state->vm.vm_stack_count <= frame->base) {
        (void)ot_raise(state, "vm: invalid lexical bind");
        return vm_unwind_to(state, floor);
      }
      otv name = as_slots(as_array(code->constants)->slots)->values[operand];
      otv bound = state->vm.vm_stack[--state->vm.vm_stack_count];
      OT_FRAME_SCOPED(state, &name, &bound);
      env_bind(state, &frame->env, name, bound);
      continue;
    }
    if (instruction == BC_UNSCOPE) {
      if (!is_type(frame->env, OBJ_ENV)) {
        (void)ot_raise(state, "vm: lexical scope underflow");
        return vm_unwind_to(state, floor);
      }
      frame->env = ((ot_env_obj*)ot_as_obj(frame->env))->parent;
      continue;
    }
    if (instruction == BC_JUMP) {
      if (operand >= code->length) {
        (void)ot_raise(state, "vm: jump target outside bytecode");
        return vm_unwind_to(state, floor);
      }
      frame->ip = operand;
      continue;
    }
    if (instruction == BC_JUMP_IF_FALSE || instruction == BC_JUMP_IF_TRUE) {
      if (state->vm.vm_stack_count <= frame->base) {
        (void)ot_raise(state, "vm: operand stack underflow");
        return vm_unwind_to(state, floor);
      }
      otv condition = state->vm.vm_stack[--state->vm.vm_stack_count];
      if ((instruction == BC_JUMP_IF_FALSE && is_falsy(condition)) ||
          (instruction == BC_JUMP_IF_TRUE && !is_falsy(condition))) {
        if (operand >= code->length) {
          (void)ot_raise(state, "vm: jump target outside bytecode");
          return vm_unwind_to(state, floor);
        }
        frame->ip = operand;
      }
      continue;
    }
    if (instruction == BC_POP) {
      if (state->vm.vm_stack_count <= frame->base) {
        (void)ot_raise(state, "vm: operand stack underflow");
        return vm_unwind_to(state, floor);
      }
      state->vm.vm_stack_count--;
      continue;
    }
    if (instruction == BC_RETURN) {
      otv result = state->vm.vm_stack_count > frame->base
                       ? state->vm.vm_stack[state->vm.vm_stack_count - 1]
                       : ot_nil;
      ot_vm_frame leaving = state->vm.vm_frames[--state->vm.vm_frame_count];
      state->vm.vm_stack_count = leaving.base;
      if (state->vm.vm_frame_count == floor) return result;
      vm_push(state, result);
      continue;
    }
    (void)ot_raise(state, "vm: unknown opcode 0x%02x", instruction);
    return vm_unwind_to(state, floor);
  }
  return ot_nil;
}

static otv vm_execute(ots* state, size_t floor) { return vm_execute_loop(state, floor); }

static otv compile_and_run_form(ots* state, otv form, otv env) {
  OT_FRAME_SCOPED(state, &form, &env);
  otv code = compile_form(state, form, env);
  if (code == OT_UNWIND) return code;
  OT_FRAME_SCOPED(state, &code);
  otv function = make_bytecode_function(state, code, env, env_namespace(state, env), ot_nil);
  OT_FRAME_SCOPED(state, &function);
  size_t floor = state->vm.vm_frame_count;
  size_t base = state->vm.vm_stack_count;
  vm_push_frame(state, function, env, base);
  return vm_execute(state, floor);
}

static bool is_catch_clause(otv form) {
  return is_type(form, OBJ_PAIR) && is_type(as_pair(form)->car, OBJ_SYMBOL) &&
         symbol_is(as_pair(form)->car, "catch");
}

static otv unwrap_quote(otv value) {
  while (is_type(value, OBJ_PAIR) && is_type(as_pair(value)->car, OBJ_SYMBOL) &&
         symbol_is(as_pair(value)->car, "quote") && is_type(as_pair(value)->cdr, OBJ_PAIR))
    value = as_pair(as_pair(value)->cdr)->car;
  return value;
}

static ot_module* find_module(ots* state, otv name) {
  if (!is_type(name, OBJ_SYMBOL)) return NULL;
  ot_name_obj* symbol = as_name(name);
  for (ot_module* module = state->modules; module != NULL; module = module->next)
    if (strlen(module->name) == symbol->length &&
        memcmp(module->name, symbol->bytes, symbol->length) == 0)
      return module;
  return NULL;
}

static otv require_one(ots* state, otv spec, otv caller) {
  spec = unwrap_quote(spec);
  otv name = spec;
  otv options = ot_null;
  if (is_type(spec, OBJ_PAIR)) {
    name = unwrap_quote(as_pair(spec)->car);
    options = as_pair(spec)->cdr;
  }
  if (!is_type(name, OBJ_SYMBOL)) return ot_raise(state, "require: expected namespace name");
  OT_FRAME_SCOPED(state, &spec, &name, &options, &caller);
  otv target = namespace_find(state, name, true);
  OT_FRAME_SCOPED(state, &target);
  ot_namespace_obj* space = (ot_namespace_obj*)ot_as_obj(target);
  bool reload = false;
  for (otv cursor = options; is_type(cursor, OBJ_PAIR); cursor = as_pair(cursor)->cdr)
    if (is_type(as_pair(cursor)->car, OBJ_KEYWORD) &&
        symbol_is(ot_intern(state, as_name(as_pair(cursor)->car)->bytes,
                            as_name(as_pair(cursor)->car)->length, false),
                  "reload"))
      reload = true;
  if (!space->loaded || reload) {
    if (space->loading) return ot_raise(state, "require: circular load");
    space->loading = true;
    state->vm.current_namespace = target;
    ot_module* module = find_module(state, name);
    if (module != NULL && (!module->initialized || reload)) {
      module->init(state);
      module->initialized = true;
    }
    char* source = NULL;
    size_t length = 0;
    bool found = state->loader != NULL &&
                 state->loader(state->loader_userdata, as_name(name)->bytes, &source, &length);
    if (found) {
      otv ignored;
      bool ok = eval_source(state, source, length, as_name(name)->bytes, true, &ignored);
      ot_host_free(source);
      if (!ok) {
        state->vm.current_namespace = caller;
        ((ot_namespace_obj*)ot_as_obj(target))->loading = false;
        return OT_UNWIND;
      }
    } else if (module == NULL) {
      state->vm.current_namespace = caller;
      ((ot_namespace_obj*)ot_as_obj(target))->loading = false;
      return ot_raise(state, "require: namespace %.*s not found", (int)as_name(name)->length,
                      as_name(name)->bytes);
    }
    state->vm.current_namespace = caller;
    space = (ot_namespace_obj*)ot_as_obj(target);
    space->loading = false;
    space->loaded = true;
  }
  while (is_type(options, OBJ_PAIR)) {
    otv option = as_pair(options)->car;
    options = as_pair(options)->cdr;
    if (!is_type(option, OBJ_KEYWORD)) continue;
    if (as_name(option)->length == 2 && memcmp(as_name(option)->bytes, "as", 2) == 0) {
      if (!is_type(options, OBJ_PAIR)) return ot_raise(state, "require: :as needs a name");
      otv alias_name = unwrap_quote(as_pair(options)->car);
      options = as_pair(options)->cdr;
      if (!is_type(alias_name, OBJ_SYMBOL)) return ot_raise(state, "require: invalid alias");
      ot_namespace_obj* caller_space = (ot_namespace_obj*)ot_as_obj(caller);
      caller_space->aliases = make_alias(state, alias_name, name, caller_space->aliases);
    } else if (as_name(option)->length == 5 && memcmp(as_name(option)->bytes, "refer", 5) == 0) {
      if (!is_type(options, OBJ_PAIR)) return ot_raise(state, "require: :refer needs names");
      otv names = unwrap_quote(as_pair(options)->car);
      OT_FRAME_SCOPED(state, &names);
      options = as_pair(options)->cdr;
      if (is_type(names, OBJ_PAIR) && symbol_is(as_pair(names)->car, "array"))
        names = as_pair(names)->cdr;
      if (!proper_list(names)) return ot_raise(state, "require: invalid :refer list");
      while (is_type(names, OBJ_PAIR)) {
        otv member = as_pair(names)->car;
        otv var = namespace_var(state, target, member, false);
        OT_FRAME_SCOPED(state, &member, &var);
        if (!is_type(var, OBJ_VAR) || ((ot_var_obj*)ot_as_obj(var))->private_value)
          return ot_raise(state, "require: missing or private var");
        ot_namespace_obj* caller_space = (ot_namespace_obj*)ot_as_obj(caller);
        caller_space->refers = make_alias(state, member, var, caller_space->refers);
        ot_alias_obj* added =
            (ot_alias_obj*)ot_as_obj(((ot_namespace_obj*)ot_as_obj(caller))->refers);
        ot_table_put(state, ((ot_namespace_obj*)ot_as_obj(caller))->refer_index, added->name,
                     added->value);
        if (as_name(member)->cache_namespace == caller) {
          as_name(member)->cache_namespace = ot_nil;
          as_name(member)->cache_var = ot_nil;
        }
        names = as_pair(names)->cdr;
      }
    }
  }
  return ot_nil;
}

static otv require_forms(ots* state, otv specs) {
  otv caller = state->vm.current_namespace;
  OT_FRAME_SCOPED(state, &caller, &specs);
  while (is_type(specs, OBJ_PAIR)) {
    otv result = require_one(state, as_pair(specs)->car, caller);
    if (result == OT_UNWIND) return result;
    specs = as_pair(specs)->cdr;
  }
  return specs == ot_null ? ot_nil : ot_raise(state, "require: improper specs");
}

static otv vm_run_code(ots* state, otv code, otv env, otv namespace_value, otv* args, size_t argc) {
  size_t argument_count = argc == 0 ? 1 : argc;
  size_t root_count = argument_count + 5;
  otv function = ot_nil;
  otv call_env = ot_nil;
  otv dummy = ot_nil;
  otv result = ot_nil;
  otv* roots[root_count];
  roots[0] = &code;
  roots[1] = &env;
  roots[2] = &namespace_value;
  roots[3] = &function;
  roots[4] = &call_env;
  for (size_t i = 0; i < argument_count; i++) roots[i + 5] = argc == 0 ? &dummy : &args[i];
  ot_frame frame;
  ot_frame_push(state, &frame, roots, root_count);

  if (!is_type(code, OBJ_CODE)) {
    result = ot_raise(state, "vm: dynamic form requires bytecode");
    ot_frame_pop(state, &frame);
    return result;
  }
  function = make_bytecode_function(state, code, env, namespace_value, ot_nil);
  call_env = bind_parameters(state, function, args, argc);
  if (call_env == OT_UNWIND) {
    ot_frame_pop(state, &frame);
    return OT_UNWIND;
  }

  size_t floor = state->vm.vm_frame_count;
  size_t base = state->vm.vm_stack_count;
  vm_push_frame(state, function, call_env, base);
  result = vm_execute(state, floor);
  ot_frame_pop(state, &frame);
  return result;
}

static otv vm_execute_try(ots* state, otv descriptor, otv env, otv namespace_value) {
  otv body_code = descriptor_value(descriptor, 0);
  otv catches = descriptor_value(descriptor, 1);
  otv condition = ot_nil;
  otv result = ot_nil;
  otv clause = ot_nil;
  otv predicate_code = ot_nil;
  otv handler_code = ot_nil;
  otv predicate = ot_nil;
  otv match = ot_nil;
  OT_FRAME_SCOPED(state, &descriptor, &env, &namespace_value, &body_code, &catches, &condition,
                  &result, &clause, &predicate_code, &handler_code, &predicate, &match);

  if (!is_type(body_code, OBJ_CODE) || !is_type(catches, OBJ_ARRAY))
    return ot_raise(state, "vm: invalid catch descriptor");

  result = vm_run_code(state, body_code, env, namespace_value, NULL, 0);
  if (result != OT_UNWIND || state->vm.unwind_kind != UNWIND_CONDITION) return result;

  condition = state->vm.condition;
  state->vm.condition = ot_nil;
  state->vm.unwind_kind = UNWIND_NONE;
  for (size_t i = 0; i < as_array(catches)->length; i++) {
    clause = descriptor_value(catches, i);
    predicate_code = descriptor_value(clause, 0);
    handler_code = descriptor_value(clause, 1);
    predicate = vm_run_code(state, predicate_code, env, namespace_value, NULL, 0);
    if (predicate == OT_UNWIND) return predicate;
    otv arguments[] = {condition};
    match = apply_value(state, predicate, arguments, 1, false);
    if (match == OT_UNWIND) return match;
    if (!is_falsy(match))
      return vm_run_code(state, handler_code, env, namespace_value, arguments, 1);
  }

  state->vm.condition = condition;
  state->vm.unwind_kind = UNWIND_CONDITION;
  return OT_UNWIND;
}

static otv vm_execute_handler_bind(ots* state, otv descriptor, otv env, otv namespace_value) {
  otv bindings = descriptor_value(descriptor, 0);
  otv body_code = descriptor_value(descriptor, 1);
  otv binding = ot_nil;
  otv predicate_code = ot_nil;
  otv handler_code = ot_nil;
  otv predicate = ot_nil;
  otv handler = ot_nil;
  otv result = ot_nil;
  OT_FRAME_SCOPED(state, &descriptor, &env, &namespace_value, &bindings, &body_code, &binding,
                  &predicate_code, &handler_code, &predicate, &handler, &result);

  if (!is_type(bindings, OBJ_ARRAY) || !is_type(body_code, OBJ_CODE))
    return ot_raise(state, "vm: invalid handler descriptor");

  size_t count = as_array(bindings)->length;
  ot_handler_frame frames[count == 0 ? 1 : count];
  ot_handler_frame* old_handlers = state->vm.handlers;
  for (size_t i = 0; i < count; i++) {
    binding = descriptor_value(bindings, i);
    predicate_code = descriptor_value(binding, 0);
    handler_code = descriptor_value(binding, 1);
    predicate = vm_run_code(state, predicate_code, env, namespace_value, NULL, 0);
    if (predicate == OT_UNWIND) {
      state->vm.handlers = old_handlers;
      return predicate;
    }
    handler = vm_run_code(state, handler_code, env, namespace_value, NULL, 0);
    if (handler == OT_UNWIND) {
      state->vm.handlers = old_handlers;
      return handler;
    }
    frames[i] =
        (ot_handler_frame){.prev = state->vm.handlers, .pred = predicate, .handler = handler};
    state->vm.handlers = &frames[i];
  }

  result = vm_run_code(state, body_code, env, namespace_value, NULL, 0);
  state->vm.handlers = old_handlers;
  return result;
}

static otv vm_execute_restart_case(ots* state, otv descriptor, otv env, otv namespace_value) {
  otv expression_code = descriptor_value(descriptor, 0);
  otv clauses = descriptor_value(descriptor, 1);
  otv compiled = ot_nil;
  otv name = ot_nil;
  otv description = ot_nil;
  otv clause_code = ot_nil;
  otv result = ot_nil;
  otv invoke_args = ot_nil;
  OT_FRAME_SCOPED(state, &descriptor, &env, &namespace_value, &expression_code, &clauses, &compiled,
                  &name, &description, &clause_code, &result, &invoke_args);

  if (!is_type(expression_code, OBJ_CODE) || !is_type(clauses, OBJ_ARRAY))
    return ot_raise(state, "vm: invalid restart descriptor");

  size_t count = as_array(clauses)->length;
  ot_restart_clause restart_clauses[count == 0 ? 1 : count];
  ot_restart_frame frame = {
      .prev = state->vm.restarts,
      .clauses = restart_clauses,
      .count = 0,
  };
  state->vm.restarts = &frame;

  for (size_t i = 0; i < count; i++) {
    compiled = descriptor_value(clauses, i);
    name = descriptor_value(compiled, 0);
    description = descriptor_value(compiled, 1);
    clause_code = descriptor_value(compiled, 2);
    if (!is_type(name, OBJ_SYMBOL) || !is_type(clause_code, OBJ_CODE)) {
      state->vm.restarts = frame.prev;
      return ot_raise(state, "vm: invalid restart clause");
    }
    ot_restart_obj* restart = must_alloc(state, sizeof(*restart), OBJ_RESTART);
    restart->name = name;
    restart->description = description;
    restart->id = ++state->vm.next_restart_id;
    restart_clauses[i] = (ot_restart_clause){
        .restart = ot_from_obj(restart),
        .params = as_code(clause_code)->params,
        .body = clause_code,
    };
    frame.count++;
  }

  result = vm_run_code(state, expression_code, env, namespace_value, NULL, 0);
  if (result == OT_UNWIND && state->vm.unwind_kind == UNWIND_RESTART) {
    for (size_t i = 0; i < count; i++) {
      ot_restart_obj* restart = (ot_restart_obj*)ot_as_obj(restart_clauses[i].restart);
      if (restart->id != state->vm.unwind_restart_id) continue;

      invoke_args = state->vm.unwind_args;
      state->vm.unwind_kind = UNWIND_NONE;
      state->vm.unwind_restart_id = 0;
      state->vm.unwind_args = ot_nil;
      if (!proper_list(invoke_args)) {
        result = ot_raise(state, "invoke-restart: invalid argument list");
        break;
      }

      size_t argc = list_length(invoke_args);
      size_t value_count = argc == 0 ? 1 : argc;
      otv values[value_count];
      otv* roots[value_count];
      otv cursor = invoke_args;
      for (size_t j = 0; j < value_count; j++) {
        values[j] = argc == 0 ? ot_nil : as_pair(cursor)->car;
        roots[j] = &values[j];
        if (argc != 0) cursor = as_pair(cursor)->cdr;
      }
      ot_frame arguments_frame;
      ot_frame_push(state, &arguments_frame, roots, value_count);
      result = vm_run_code(state, restart_clauses[i].body, env, namespace_value, values, argc);
      ot_frame_pop(state, &arguments_frame);
      break;
    }
  }

  state->vm.restarts = frame.prev;
  return result;
}

static otv vm_execute_unwind_protect(ots* state, otv descriptor, otv env, otv namespace_value) {
  otv expression_code = descriptor_value(descriptor, 0);
  otv cleanup_code = descriptor_value(descriptor, 1);
  otv result = ot_nil;
  otv saved_condition = ot_nil;
  otv saved_args = ot_nil;
  OT_FRAME_SCOPED(state, &descriptor, &env, &namespace_value, &expression_code, &cleanup_code,
                  &result, &saved_condition, &saved_args);

  if (!is_type(expression_code, OBJ_CODE) || !is_type(cleanup_code, OBJ_CODE))
    return ot_raise(state, "vm: invalid unwind-protect descriptor");

  result = vm_run_code(state, expression_code, env, namespace_value, NULL, 0);
  saved_condition = state->vm.condition;
  saved_args = state->vm.unwind_args;
  ot_unwind_kind saved_kind = state->vm.unwind_kind;
  uint64_t saved_restart_id = state->vm.unwind_restart_id;
  state->vm.condition = ot_nil;
  state->vm.unwind_args = ot_nil;
  state->vm.unwind_kind = UNWIND_NONE;
  state->vm.unwind_restart_id = 0;

  otv cleanup = vm_run_code(state, cleanup_code, env, namespace_value, NULL, 0);
  if (cleanup == OT_UNWIND) return cleanup;

  state->vm.condition = saved_condition;
  state->vm.unwind_args = saved_args;
  state->vm.unwind_kind = saved_kind;
  state->vm.unwind_restart_id = saved_restart_id;
  return result;
}

static otv vm_execute_with_params(ots* state, otv descriptor, otv env, otv namespace_value) {
  otv bindings = descriptor_value(descriptor, 0);
  otv body_code = descriptor_value(descriptor, 1);
  otv binding = ot_nil;
  otv param_code = ot_nil;
  otv value_code = ot_nil;
  otv param = ot_nil;
  otv value = ot_nil;
  otv result = ot_nil;
  OT_FRAME_SCOPED(state, &descriptor, &env, &namespace_value, &bindings, &body_code, &binding,
                  &param_code, &value_code, &param, &value, &result);

  if (!is_type(bindings, OBJ_ARRAY) || !is_type(body_code, OBJ_CODE))
    return ot_raise(state, "vm: invalid params descriptor");

  size_t count = as_array(bindings)->length;
  ot_param_frame frames[count == 0 ? 1 : count];
  ot_param_frame* old_params = state->vm.params;
  for (size_t i = 0; i < count; i++) {
    binding = descriptor_value(bindings, i);
    param_code = descriptor_value(binding, 0);
    value_code = descriptor_value(binding, 1);
    param = vm_run_code(state, param_code, env, namespace_value, NULL, 0);
    if (param == OT_UNWIND) {
      state->vm.params = old_params;
      return param;
    }
    if (!is_type(param, OBJ_PARAM)) {
      state->vm.params = old_params;
      return ot_raise(state, "with-params: expected param");
    }
    value = vm_run_code(state, value_code, env, namespace_value, NULL, 0);
    if (value == OT_UNWIND) {
      state->vm.params = old_params;
      return value;
    }
    frames[i] = (ot_param_frame){.prev = state->vm.params, .param = param, .value = value};
    state->vm.params = &frames[i];
  }

  result = vm_run_code(state, body_code, env, namespace_value, NULL, 0);
  state->vm.params = old_params;
  return result;
}

static otv vm_execute_dynamic(ots* state, unsigned char instruction, otv descriptor, otv env,
                              otv namespace_value) {
  if (instruction == BC_TRY) return vm_execute_try(state, descriptor, env, namespace_value);
  if (instruction == BC_HANDLER_BIND)
    return vm_execute_handler_bind(state, descriptor, env, namespace_value);
  if (instruction == BC_RESTART_CASE)
    return vm_execute_restart_case(state, descriptor, env, namespace_value);
  if (instruction == BC_UNWIND_PROTECT)
    return vm_execute_unwind_protect(state, descriptor, env, namespace_value);
  if (instruction == BC_WITH_PARAMS)
    return vm_execute_with_params(state, descriptor, env, namespace_value);
  return ot_raise(state, "vm: invalid dynamic opcode");
}

/* =========================================================================
 * 8. CORE NATIVES
 * ========================================================================= */

static otv need_arity(ots* state, const char* name, int argc, int minimum, int maximum) {
  if (argc >= minimum && (maximum < 0 || argc <= maximum)) return ot_nil;
  if (minimum == maximum)
    return ot_raise(state, "%s: expected %d argument%s", name, minimum, minimum == 1 ? "" : "s");
  return ot_raise(state, "%s: expected %d to %d arguments", name, minimum, maximum);
}

static bool number_value(otv value, double* number, bool* integer) {
  if (ot_is_int(value)) {
    *number = (double)ot_get_int(value);
    *integer = true;
    return true;
  }
  if (is_type(value, OBJ_FLOAT)) {
    *number = ((ot_float_obj*)ot_as_obj(value))->value;
    *integer = false;
    return true;
  }
  return false;
}

static otv number_result(ots* state, bool integer, uintptr_t wrapped, double value) {
  return integer ? ot_make_int((intptr_t)wrapped) : ot_make_float(state, value);
}

static otv nat_add(ots* state, otv* args, int argc) {
  bool all_int = true;
  uintptr_t integer = 0;
  double number = 0;
  for (int i = 0; i < argc; i++) {
    double part;
    bool exact;
    if (!number_value(args[i], &part, &exact)) return ot_raise(state, "+: expected number");
    if (all_int && exact) integer += (uintptr_t)ot_get_int(args[i]);
    else {
      if (all_int) number = (double)(intptr_t)integer;
      all_int = false;
      number += part;
    }
  }
  return number_result(state, all_int, integer, number);
}

static otv nat_multiply(ots* state, otv* args, int argc) {
  bool all_int = true;
  uintptr_t integer = 1;
  double number = 1;
  for (int i = 0; i < argc; i++) {
    double part;
    bool exact;
    if (!number_value(args[i], &part, &exact)) return ot_raise(state, "*: expected number");
    if (all_int && exact) integer *= (uintptr_t)ot_get_int(args[i]);
    else {
      if (all_int) number = (double)(intptr_t)integer;
      all_int = false;
      number *= part;
    }
  }
  return number_result(state, all_int, integer, number);
}

static otv nat_subtract(ots* state, otv* args, int argc) {
  if (argc == 0) return ot_raise(state, "-: expected at least one argument");
  double first;
  bool exact;
  if (!number_value(args[0], &first, &exact)) return ot_raise(state, "-: expected number");
  bool all_int = exact;
  uintptr_t integer = exact ? (uintptr_t)ot_get_int(args[0]) : 0;
  double number = first;
  if (argc == 1) {
    if (all_int) integer = (uintptr_t)0 - integer;
    else number = -number;
  }
  for (int i = 1; i < argc; i++) {
    double part;
    bool part_exact;
    if (!number_value(args[i], &part, &part_exact)) return ot_raise(state, "-: expected number");
    if (all_int && part_exact) integer -= (uintptr_t)ot_get_int(args[i]);
    else {
      if (all_int) number = (double)(intptr_t)integer;
      all_int = false;
      number -= part;
    }
  }
  return number_result(state, all_int, integer, number);
}

static otv nat_divide(ots* state, otv* args, int argc) {
  if (argc == 0) return ot_raise(state, "/: expected at least one argument");
  double result;
  bool exact;
  if (!number_value(args[0], &result, &exact)) return ot_raise(state, "/: expected number");
  if (argc == 1) {
    if (result == 0) return ot_raise(state, "/: division by zero");
    return ot_make_float(state, 1.0 / result);
  }
  bool keep_int = exact;
  intptr_t integer = exact ? ot_get_int(args[0]) : 0;
  for (int i = 1; i < argc; i++) {
    double divisor;
    bool divisor_exact;
    if (!number_value(args[i], &divisor, &divisor_exact))
      return ot_raise(state, "/: expected number");
    if (divisor == 0) return ot_raise(state, "/: division by zero");
    if (keep_int && divisor_exact && integer % ot_get_int(args[i]) == 0) {
      integer /= ot_get_int(args[i]);
      result = (double)integer;
    } else {
      if (keep_int) result = (double)integer;
      keep_int = false;
      result /= divisor;
    }
  }
  return keep_int ? ot_make_int(integer) : ot_make_float(state, result);
}

static otv nat_integer_division(ots* state, otv* args, int argc, int mode) {
  otv arity = need_arity(state,
                         mode == 0   ? "quotient"
                         : mode == 1 ? "remainder"
                                     : "modulo",
                         argc, 2, 2);
  if (arity == OT_UNWIND) return arity;
  if (!ot_is_int(args[0]) || !ot_is_int(args[1])) return ot_raise(state, "expected integers");
  intptr_t left = ot_get_int(args[0]);
  intptr_t right = ot_get_int(args[1]);
  if (right == 0) return ot_raise(state, "division by zero");
  if (left == INTPTR_MIN && right == -1)
    return mode == 0 ? ot_make_int(INTPTR_MIN) : ot_make_int(0);
  intptr_t quotient = left / right;
  intptr_t remainder = left % right;
  if (mode == 0) return ot_make_int(quotient);
  if (mode == 2 && remainder != 0 && ((remainder < 0) != (right < 0))) remainder += right;
  return ot_make_int(remainder);
}

static otv nat_quotient(ots* state, otv* args, int argc) {
  return nat_integer_division(state, args, argc, 0);
}
static otv nat_remainder(ots* state, otv* args, int argc) {
  return nat_integer_division(state, args, argc, 1);
}
static otv nat_modulo(ots* state, otv* args, int argc) {
  return nat_integer_division(state, args, argc, 2);
}

static otv nat_numeric_compare(ots* state, otv* args, int argc, int mode) {
  if (argc < (mode == 0 ? 0 : 2)) return ot_raise(state, "comparison: too few arguments");
  for (int i = 1; i < argc; i++) {
    double left;
    double right;
    bool a;
    bool b;
    if (!number_value(args[i - 1], &left, &a) || !number_value(args[i], &right, &b))
      return ot_raise(state, "comparison: expected numbers");
    bool ok = mode == 0   ? left == right
              : mode == 1 ? left < right
              : mode == 2 ? left > right
              : mode == 3 ? left <= right
                          : left >= right;
    if (!ok) return ot_false;
  }
  return ot_true;
}

static otv nat_num_equal(ots* state, otv* args, int argc) {
  return nat_numeric_compare(state, args, argc, 0);
}
static otv nat_less(ots* state, otv* args, int argc) {
  return nat_numeric_compare(state, args, argc, 1);
}
static otv nat_greater(ots* state, otv* args, int argc) {
  return nat_numeric_compare(state, args, argc, 2);
}
static otv nat_less_equal(ots* state, otv* args, int argc) {
  return nat_numeric_compare(state, args, argc, 3);
}
static otv nat_greater_equal(ots* state, otv* args, int argc) {
  return nat_numeric_compare(state, args, argc, 4);
}

static otv nat_minmax(ots* state, otv* args, int argc, bool maximum) {
  if (argc == 0)
    return ot_raise(state, "%s: expected at least one argument", maximum ? "max" : "min");
  int best = 0;
  double best_value;
  bool exact;
  if (!number_value(args[0], &best_value, &exact)) return ot_raise(state, "expected number");
  for (int i = 1; i < argc; i++) {
    double value;
    if (!number_value(args[i], &value, &exact)) return ot_raise(state, "expected number");
    if ((maximum && value > best_value) || (!maximum && value < best_value)) {
      best = i;
      best_value = value;
    }
  }
  return args[best];
}

static otv nat_min(ots* state, otv* args, int argc) { return nat_minmax(state, args, argc, false); }
static otv nat_max(ots* state, otv* args, int argc) { return nat_minmax(state, args, argc, true); }

static otv nat_inc(ots* state, otv* args, int argc) {
  if (need_arity(state, "inc", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  otv one = ot_make_int(1);
  otv values[2] = {args[0], one};
  return nat_add(state, values, 2);
}

static otv nat_dec(ots* state, otv* args, int argc) {
  if (need_arity(state, "dec", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  otv one = ot_make_int(1);
  otv values[2] = {args[0], one};
  return nat_subtract(state, values, 2);
}

static otv nat_parity(ots* state, otv* args, int argc, bool odd) {
  if (need_arity(state, odd ? "odd?" : "even?", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (!ot_is_int(args[0])) return ot_raise(state, "%s: expected int", odd ? "odd?" : "even?");
  return ((ot_get_int(args[0]) & 1) != 0) == odd ? ot_true : ot_false;
}
static otv nat_even(ots* state, otv* args, int argc) {
  return nat_parity(state, args, argc, false);
}
static otv nat_odd(ots* state, otv* args, int argc) { return nat_parity(state, args, argc, true); }

static otv nat_not(ots* state, otv* args, int argc) {
  if (need_arity(state, "not", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  return is_falsy(args[0]) ? ot_true : ot_false;
}

static otv nat_type_predicate(ots* state, otv* args, int argc, ot_type type) {
  if (need_arity(state, "predicate", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  return ot_value_type(args[0]) == type ? ot_true : ot_false;
}

#define TYPE_PRED(NAME, TYPE)                                                                      \
  static otv NAME(ots* state, otv* args, int argc) {                                               \
    return nat_type_predicate(state, args, argc, TYPE);                                            \
  }
TYPE_PRED(nat_nil_p, OT_TYPE_NIL)
TYPE_PRED(nat_null_p, OT_TYPE_NULL)
TYPE_PRED(nat_boolean_p, OT_TYPE_BOOLEAN)
TYPE_PRED(nat_int_p, OT_TYPE_INT)
TYPE_PRED(nat_float_p, OT_TYPE_FLOAT)
TYPE_PRED(nat_symbol_p, OT_TYPE_SYMBOL)
TYPE_PRED(nat_keyword_p, OT_TYPE_KEYWORD)
TYPE_PRED(nat_string_p, OT_TYPE_STRING)
TYPE_PRED(nat_pair_p, OT_TYPE_PAIR)
TYPE_PRED(nat_array_p, OT_TYPE_ARRAY)
TYPE_PRED(nat_table_p, OT_TYPE_TABLE)
TYPE_PRED(nat_buffer_p, OT_TYPE_BUFFER)
TYPE_PRED(nat_macro_p, OT_TYPE_MACRO)
TYPE_PRED(nat_ext_p, OT_TYPE_EXT)
#undef TYPE_PRED

static otv nat_number_p(ots* state, otv* args, int argc) {
  if (need_arity(state, "number?", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  ot_type type = ot_value_type(args[0]);
  return type == OT_TYPE_INT || type == OT_TYPE_FLOAT ? ot_true : ot_false;
}

static otv nat_procedure_p(ots* state, otv* args, int argc) {
  if (need_arity(state, "procedure?", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  return is_type(args[0], OBJ_FUNCTION) || is_type(args[0], OBJ_NAT) ? ot_true : ot_false;
}

static otv nat_list_p(ots* state, otv* args, int argc) {
  if (need_arity(state, "list?", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  return proper_list(args[0]) ? ot_true : ot_false;
}

static otv nat_true_p(ots* state, otv* args, int argc) {
  if (need_arity(state, "true?", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  return args[0] == ot_true ? ot_true : ot_false;
}
static otv nat_false_p(ots* state, otv* args, int argc) {
  if (need_arity(state, "false?", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  return args[0] == ot_false ? ot_true : ot_false;
}

static otv nat_eq(ots* state, otv* args, int argc) {
  if (need_arity(state, "eq?", argc, 2, 2) == OT_UNWIND) return OT_UNWIND;
  if (args[0] == args[1]) return ot_true;
  if (is_type(args[0], OBJ_FLOAT) && is_type(args[1], OBJ_FLOAT))
    return float_same(((ot_float_obj*)ot_as_obj(args[0]))->value,
                      ((ot_float_obj*)ot_as_obj(args[1]))->value)
               ? ot_true
               : ot_false;
  return ot_false;
}

static otv nat_equal(ots* state, otv* args, int argc) {
  if (need_arity(state, "equal?", argc, 2, 2) == OT_UNWIND) return OT_UNWIND;
  return ot_equal(state, args[0], args[1], true) ? ot_true : ot_false;
}

static otv nat_type(ots* state, otv* args, int argc) {
  if (need_arity(state, "type", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  static const char* names[] = {"nil",      "null",   "boolean", "int",     "float",   "symbol",
                                "keyword",  "string", "pair",    "array",   "table",   "buffer",
                                "function", "macro",  "param",   "restart", "foreign", "internal"};
  ot_type type = ot_value_type(args[0]);
  return ot_intern(state, names[type], strlen(names[type]), true);
}

static otv nat_cons(ots* state, otv* args, int argc) {
  if (need_arity(state, "cons", argc, 2, 2) == OT_UNWIND) return OT_UNWIND;
  return ot_cons(state, args[0], args[1]);
}

static otv nat_car(ots* state, otv* args, int argc) {
  if (need_arity(state, "car", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_PAIR)) return ot_raise(state, "car: expected pair");
  return as_pair(args[0])->car;
}

static otv nat_cdr(ots* state, otv* args, int argc) {
  if (need_arity(state, "cdr", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_PAIR)) return ot_raise(state, "cdr: expected pair");
  return as_pair(args[0])->cdr;
}

static otv nat_set_car(ots* state, otv* args, int argc) {
  if (need_arity(state, "set-car!", argc, 2, 2) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_PAIR)) return ot_raise(state, "set-car!: expected pair");
  if (as_pair(args[0])->frozen) return ot_raise(state, "set-car!: frozen table key");
  as_pair(args[0])->car = args[1];
  return args[0];
}

static otv nat_set_cdr(ots* state, otv* args, int argc) {
  if (need_arity(state, "set-cdr!", argc, 2, 2) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_PAIR)) return ot_raise(state, "set-cdr!: expected pair");
  if (as_pair(args[0])->frozen) return ot_raise(state, "set-cdr!: frozen table key");
  as_pair(args[0])->cdr = args[1];
  return args[0];
}

static otv nat_list(ots* state, otv* args, int argc) {
  return list_from_array(state, args, (size_t)argc);
}

static otv nat_length(ots* state, otv* args, int argc) {
  if (need_arity(state, "length", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  otv value = args[0];
  if (value == ot_nil || value == ot_null) return ot_make_int(0);
  if (is_type(value, OBJ_PAIR)) {
    if (!proper_list(value)) return ot_raise(state, "length: improper list");
    return ot_make_int((intptr_t)list_length(value));
  }
  if (is_type(value, OBJ_ARRAY)) return ot_make_int((intptr_t)as_array(value)->length);
  if (is_type(value, OBJ_TABLE)) return ot_make_int((intptr_t)as_table(value)->length);
  if (is_type(value, OBJ_STRING)) return ot_make_int((intptr_t)as_string(value)->length);
  if (is_type(value, OBJ_BUFFER))
    return ot_make_int((intptr_t)((ot_buffer_obj*)ot_as_obj(value))->length);
  return ot_raise(state, "length: unsupported type");
}

static otv nat_reverse(ots* state, otv* args, int argc) {
  if (need_arity(state, "reverse", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (is_type(args[0], OBJ_ARRAY)) {
    size_t length = as_array(args[0])->length;
    otv output = ot_array_new(state, length);
    OT_FRAME_SCOPED(state, &output);
    for (size_t i = length; i-- > 0;) {
      ot_array_obj* array = as_array(args[0]);
      array_push(state, &output, as_slots(array->slots)->values[i]);
    }
    return output;
  }
  if (args[0] == ot_nil) return ot_null;
  if (!proper_list(args[0])) return ot_raise(state, "reverse: expected proper list");
  otv output = ot_null;
  otv cursor = args[0];
  OT_FRAME_SCOPED(state, &output, &cursor);
  while (is_type(cursor, OBJ_PAIR)) {
    otv element = as_pair(cursor)->car;
    cursor = as_pair(cursor)->cdr;
    output = ot_cons(state, element, output);
  }
  return output;
}

static otv nat_append(ots* state, otv* args, int argc) {
  if (argc == 0) return ot_null;
  otv output = args[argc - 1];
  OT_FRAME_SCOPED(state, &output);
  for (int i = argc - 2; i >= 0; i--) {
    if (!proper_list(args[i])) return ot_raise(state, "append: expected proper list");
    size_t count = list_length(args[i]);
    otv values[count == 0 ? 1 : count];
    otv* roots[count == 0 ? 1 : count];
    otv cursor = args[i];
    for (size_t j = 0; j < count; j++) {
      values[j] = as_pair(cursor)->car;
      roots[j] = &values[j];
      cursor = as_pair(cursor)->cdr;
    }
    if (count == 0) {
      values[0] = ot_nil;
      roots[0] = &values[0];
    }
    ot_frame values_frame;
    ot_frame_push(state, &values_frame, roots, count == 0 ? 1 : count);
    for (size_t j = count; j-- > 0;) output = ot_cons(state, values[j], output);
    ot_frame_pop(state, &values_frame);
  }
  return output;
}

static otv nat_array(ots* state, otv* args, int argc) {
  otv array = ot_array_new(state, (size_t)argc);
  OT_FRAME_SCOPED(state, &array);
  for (int i = 0; i < argc; i++) array_push(state, &array, args[i]);
  return array;
}

static otv nat_table(ots* state, otv* args, int argc) {
  if ((argc & 1) != 0) return ot_raise(state, "table: expected key/value pairs");
  otv table = ot_table_new(state, (size_t)argc / 2);
  OT_FRAME_SCOPED(state, &table);
  for (int i = 0; i < argc; i += 2) {
    otv result = ot_table_put(state, table, args[i], args[i + 1]);
    if (result == OT_UNWIND) return result;
  }
  return table;
}

static size_t utf8_advance(const unsigned char* bytes, size_t length, size_t offset) {
  if (offset >= length) return length;
  unsigned char byte = bytes[offset];
  size_t width = byte < 0x80 ? 1 : byte < 0xe0 ? 2 : byte < 0xf0 ? 3 : 4;
  return offset + width > length ? length : offset + width;
}

static size_t utf8_count(const unsigned char* bytes, size_t length) {
  size_t count = 0;
  for (size_t offset = 0; offset < length; offset = utf8_advance(bytes, length, offset)) count++;
  return count;
}

static size_t utf8_offset(const unsigned char* bytes, size_t length, size_t index) {
  size_t offset = 0;
  while (index-- != 0 && offset < length) offset = utf8_advance(bytes, length, offset);
  return offset;
}

static otv nat_get(ots* state, otv* args, int argc) {
  if (argc < 2 || argc > 3) return ot_raise(state, "get: expected 2 or 3 arguments");
  otv fallback = argc == 3 ? args[2] : ot_nil;
  if (args[0] == ot_nil) return fallback;
  if (is_type(args[0], OBJ_TABLE)) return ot_table_get(state, args[0], args[1], fallback);
  if (is_type(args[0], OBJ_ARRAY)) {
    if (!ot_is_int(args[1])) return fallback;
    intptr_t index = ot_get_int(args[1]);
    ot_array_obj* array = as_array(args[0]);
    return index >= 0 && (size_t)index < array->length ? as_slots(array->slots)->values[index]
                                                       : fallback;
  }
  if (is_type(args[0], OBJ_STRING)) {
    if (!ot_is_int(args[1])) return fallback;
    intptr_t index = ot_get_int(args[1]);
    if (index < 0) return fallback;
    ot_string_obj* string = as_string(args[0]);
    unsigned char* bytes = as_bytes(string->bytes)->data;
    size_t start = utf8_offset(bytes, string->length, (size_t)index);
    if (start == string->length) return fallback;
    size_t end = utf8_advance(bytes, string->length, start);
    return make_string_slice(state, args[0], start, end - start);
  }
  return fallback;
}

static otv nat_put(ots* state, otv* args, int argc) {
  if (argc < 3 || (argc & 1) == 0)
    return ot_raise(state, "put!: expected collection and key/value pairs");
  if (is_type(args[0], OBJ_TABLE)) {
    for (int i = 1; i < argc; i += 2) {
      otv result = ot_table_put(state, args[0], args[i], args[i + 1]);
      if (result == OT_UNWIND) return result;
    }
    return args[0];
  }
  if (is_type(args[0], OBJ_ARRAY)) {
    ot_array_obj* array = as_array(args[0]);
    for (int i = 1; i < argc; i += 2) {
      if (!ot_is_int(args[i])) return ot_raise(state, "put!: expected integer index");
      intptr_t index = ot_get_int(args[i]);
      if (index < 0 || (size_t)index >= array->length)
        return ot_raise(state, "put!: index out of range");
      as_slots(array->slots)->values[index] = args[i + 1];
    }
    return args[0];
  }
  return ot_raise(state, "put!: expected table or array");
}

static otv nat_push(ots* state, otv* args, int argc) {
  if (argc < 1 || !is_type(args[0], OBJ_ARRAY)) return ot_raise(state, "push!: expected array");
  otv array = args[0];
  for (int i = 1; i < argc; i++) array_push(state, &array, args[i]);
  return array;
}

static otv nat_pop(ots* state, otv* args, int argc) {
  if (need_arity(state, "pop!", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_ARRAY)) return ot_raise(state, "pop!: expected array");
  ot_array_obj* array = as_array(args[0]);
  if (array->length == 0) return ot_nil;
  otv value = as_slots(array->slots)->values[--array->length];
  as_slots(array->slots)->values[array->length] = ot_nil;
  return value;
}

static otv nat_keys_values(ots* state, otv* args, int argc, bool values) {
  if (need_arity(state, values ? "values" : "keys", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  otv output = ot_array_new(state, args[0] == ot_nil             ? 0
                                   : is_type(args[0], OBJ_TABLE) ? as_table(args[0])->length
                                                                 : 0);
  OT_FRAME_SCOPED(state, &output);
  if (args[0] == ot_nil) return output;
  if (!is_type(args[0], OBJ_TABLE))
    return ot_raise(state, "%s: expected table", values ? "values" : "keys");
  ot_table_obj* table = as_table(args[0]);
  size_t used = table->used;
  for (size_t i = 0; i < used; i++) {
    table = as_table(args[0]);
    ot_entries_obj* entries = as_entries(table->entries);
    if (entries->values[i].live)
      array_push(state, &output, values ? entries->values[i].value : entries->values[i].key);
  }
  return output;
}

static otv nat_keys(ots* state, otv* args, int argc) {
  return nat_keys_values(state, args, argc, false);
}
static otv nat_values(ots* state, otv* args, int argc) {
  return nat_keys_values(state, args, argc, true);
}

static otv nat_copy(ots* state, otv* args, int argc) {
  if (need_arity(state, "copy", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (args[0] == ot_nil) return ot_nil;
  if (is_type(args[0], OBJ_ARRAY)) {
    size_t length = as_array(args[0])->length;
    otv output = ot_array_new(state, length);
    OT_FRAME_SCOPED(state, &output);
    for (size_t i = 0; i < length; i++) {
      ot_array_obj* source = as_array(args[0]);
      array_push(state, &output, as_slots(source->slots)->values[i]);
    }
    return output;
  }
  if (is_type(args[0], OBJ_TABLE)) {
    size_t length = as_table(args[0])->length;
    size_t used = as_table(args[0])->used;
    otv output = ot_table_new(state, length);
    OT_FRAME_SCOPED(state, &output);
    for (size_t i = 0; i < used; i++) {
      ot_table_obj* source = as_table(args[0]);
      ot_entries_obj* entries = as_entries(source->entries);
      if (entries->values[i].live) {
        otv key = entries->values[i].key;
        otv value = entries->values[i].value;
        ot_table_put(state, output, key, value);
      }
    }
    return output;
  }
  return ot_raise(state, "copy: expected array, table, or nil");
}

static otv nat_for_each(ots* state, otv* args, int argc) {
  if (need_arity(state, "for-each", argc, 2, 2) == OT_UNWIND) return OT_UNWIND;
  if (args[1] == ot_nil || args[1] == ot_null) return ot_nil;
  if (is_type(args[1], OBJ_ARRAY)) {
    size_t count = as_array(args[1])->length;
    for (size_t i = 0; i < count; i++) {
      ot_array_obj* array = as_array(args[1]);
      otv item = i < array->length ? as_slots(array->slots)->values[i] : ot_nil;
      otv call_args[1] = {item};
      otv result = apply_value(state, args[0], call_args, 1, false);
      if (result == OT_UNWIND) return result;
    }
    return ot_nil;
  }
  if (!proper_list(args[1])) return ot_raise(state, "for-each: improper list");
  otv cursor = args[1];
  OT_FRAME_SCOPED(state, &cursor);
  while (is_type(cursor, OBJ_PAIR)) {
    otv call_args[1] = {as_pair(cursor)->car};
    otv result = apply_value(state, args[0], call_args, 1, false);
    if (result == OT_UNWIND) return result;
    cursor = as_pair(cursor)->cdr;
  }
  return ot_nil;
}

static otv make_display_string(ots* state, otv* args, int argc, const char* separator) {
  buf out = {0};
  for (int i = 0; i < argc; i++) {
    if (i != 0) buf_cstr(&out, separator);
    render_value(state, &out, args[i], true, 0);
  }
  otv string = ot_make_string(state, out.data, out.length);
  buf_free(&out);
  return string;
}

static otv nat_str(ots* state, otv* args, int argc) {
  return make_display_string(state, args, argc, "");
}

static otv nat_string_append(ots* state, otv* args, int argc) {
  buf out = {0};
  for (int i = 0; i < argc; i++) {
    if (!is_type(args[i], OBJ_STRING)) {
      buf_free(&out);
      return ot_raise(state, "string-append: expected strings");
    }
    ot_string_obj* string = as_string(args[i]);
    buf_write(&out, (const char*)as_bytes(string->bytes)->data, string->length);
  }
  otv string = ot_make_string(state, out.data, out.length);
  buf_free(&out);
  return string;
}

static otv nat_string_length(ots* state, otv* args, int argc) {
  if (need_arity(state, "string-length", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_STRING)) return ot_raise(state, "string-length: expected string");
  ot_string_obj* string = as_string(args[0]);
  return ot_make_int((intptr_t)utf8_count(as_bytes(string->bytes)->data, string->length));
}

static otv nat_substring(ots* state, otv* args, int argc) {
  if (argc < 2 || argc > 3 || !is_type(args[0], OBJ_STRING) || !ot_is_int(args[1]) ||
      (argc == 3 && !ot_is_int(args[2])))
    return ot_raise(state, "substring: expected string, start, and optional end");
  ot_string_obj* string = as_string(args[0]);
  unsigned char* bytes = as_bytes(string->bytes)->data;
  size_t count = utf8_count(bytes, string->length);
  intptr_t raw_start = ot_get_int(args[1]);
  intptr_t raw_end = argc == 3 ? ot_get_int(args[2]) : (intptr_t)count;
  size_t start = raw_start < 0 ? 0 : (size_t)raw_start > count ? count : (size_t)raw_start;
  size_t end = raw_end < 0 ? 0 : (size_t)raw_end > count ? count : (size_t)raw_end;
  if (end < start) end = start;
  size_t byte_start = utf8_offset(bytes, string->length, start);
  size_t byte_end = utf8_offset(bytes, string->length, end);
  return make_string_slice(state, args[0], byte_start, byte_end - byte_start);
}

static bool ascii_space(unsigned char byte) {
  return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n' || byte == '\f' ||
         byte == '\v';
}

static otv nat_string_split(ots* state, otv* args, int argc) {
  if (argc < 1 || argc > 2 || !is_type(args[0], OBJ_STRING) ||
      (argc == 2 && !is_type(args[1], OBJ_STRING)))
    return ot_raise(state, "string-split: expected string and optional separator");
  ot_string_obj* string = as_string(args[0]);
  const char* bytes = (const char*)as_bytes(string->bytes)->data;
  size_t source_length = string->length;
  otv output = ot_array_new(state, 4);
  OT_FRAME_SCOPED(state, &output);
  if (argc == 1) {
    size_t offset = 0;
    while (offset < source_length) {
      string = as_string(args[0]);
      bytes = (const char*)as_bytes(string->bytes)->data;
      while (offset < string->length && ascii_space((unsigned char)bytes[offset])) offset++;
      size_t start = offset;
      while (offset < string->length && !ascii_space((unsigned char)bytes[offset])) offset++;
      if (offset > start) {
        otv piece = make_string_slice(state, args[0], start, offset - start);
        array_push(state, &output, piece);
      }
    }
    return output;
  }
  ot_string_obj* separator = as_string(args[1]);
  const char* sep = (const char*)as_bytes(separator->bytes)->data;
  size_t separator_length = separator->length;
  if (separator_length == 0) return ot_raise(state, "string-split: empty separator");
  size_t start = 0;
  while (start <= source_length) {
    string = as_string(args[0]);
    separator = as_string(args[1]);
    bytes = (const char*)as_bytes(string->bytes)->data;
    sep = (const char*)as_bytes(separator->bytes)->data;
    size_t found = SIZE_MAX;
    for (size_t i = start; i + separator_length <= source_length; i++)
      if (memcmp(bytes + i, sep, separator_length) == 0) {
        found = i;
        break;
      }
    size_t end = found == SIZE_MAX ? source_length : found;
    otv piece = make_string_slice(state, args[0], start, end - start);
    array_push(state, &output, piece);
    if (found == SIZE_MAX) break;
    start = end + separator_length;
  }
  return output;
}

static otv nat_string_join(ots* state, otv* args, int argc) {
  if (need_arity(state, "string-join", argc, 2, 2) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_STRING))
    return ot_raise(state, "string-join: expected separator string");
  otv sequence = args[1];
  if (sequence != ot_nil && !is_type(sequence, OBJ_ARRAY) && !proper_list(sequence)) {
    if (is_type(sequence, OBJ_PAIR)) return ot_raise(state, "string-join: improper list");
    return ot_raise(state, "string-join: expected sequence");
  }
  ot_string_obj* separator = as_string(args[0]);
  const char* sep = (const char*)as_bytes(separator->bytes)->data;
  buf out = {0};
  size_t count = sequence == ot_nil             ? 0
                 : is_type(sequence, OBJ_ARRAY) ? as_array(sequence)->length
                                                : list_length(sequence);
  otv cursor = sequence;
  for (size_t i = 0; i < count; i++) {
    if (i != 0) buf_write(&out, sep, separator->length);
    otv item = is_type(sequence, OBJ_ARRAY) ? as_slots(as_array(sequence)->slots)->values[i]
                                            : as_pair(cursor)->car;
    render_value(state, &out, item, true, 0);
    if (!is_type(sequence, OBJ_ARRAY)) cursor = as_pair(cursor)->cdr;
  }
  otv result = ot_make_string(state, out.data, out.length);
  buf_free(&out);
  return result;
}

static otv nat_string_case(ots* state, otv* args, int argc, bool upper) {
  if (need_arity(state, upper ? "string-upcase" : "string-downcase", argc, 1, 1) == OT_UNWIND)
    return OT_UNWIND;
  if (!is_type(args[0], OBJ_STRING)) return ot_raise(state, "expected string");
  ot_string_obj* string = as_string(args[0]);
  buf out = {0};
  for (size_t i = 0; i < string->length; i++) {
    unsigned char byte = as_bytes(string->bytes)->data[i];
    buf_byte(&out, (char)(upper ? toupper(byte) : tolower(byte)));
  }
  otv result = ot_make_string(state, out.data, out.length);
  buf_free(&out);
  return result;
}
static otv nat_string_upcase(ots* state, otv* args, int argc) {
  return nat_string_case(state, args, argc, true);
}
static otv nat_string_downcase(ots* state, otv* args, int argc) {
  return nat_string_case(state, args, argc, false);
}

static otv nat_string_trim(ots* state, otv* args, int argc) {
  if (need_arity(state, "string-trim", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_STRING)) return ot_raise(state, "string-trim: expected string");
  ot_string_obj* string = as_string(args[0]);
  unsigned char* bytes = as_bytes(string->bytes)->data;
  size_t start = 0;
  size_t end = string->length;
  while (start < end && ascii_space(bytes[start])) start++;
  while (end > start && ascii_space(bytes[end - 1])) end--;
  return make_string_slice(state, args[0], start, end - start);
}

static bool string_search(otv haystack_value, otv needle_value, size_t* position) {
  ot_string_obj* haystack = as_string(haystack_value);
  ot_string_obj* needle = as_string(needle_value);
  unsigned char* h = as_bytes(haystack->bytes)->data;
  unsigned char* n = as_bytes(needle->bytes)->data;
  if (needle->length == 0) {
    *position = 0;
    return true;
  }
  for (size_t i = 0; i + needle->length <= haystack->length; i++)
    if (memcmp(h + i, n, needle->length) == 0) {
      *position = i;
      return true;
    }
  return false;
}

static otv nat_string_contains(ots* state, otv* args, int argc) {
  if (need_arity(state, "string-contains?", argc, 2, 2) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_STRING) || !is_type(args[1], OBJ_STRING))
    return ot_raise(state, "string-contains?: expected strings");
  size_t position;
  return string_search(args[0], args[1], &position) ? ot_true : ot_false;
}

static otv nat_string_edge(ots* state, otv* args, int argc, bool starts) {
  if (need_arity(state, starts ? "string-starts-with?" : "string-ends-with?", argc, 2, 2) ==
      OT_UNWIND)
    return OT_UNWIND;
  if (!is_type(args[0], OBJ_STRING) || !is_type(args[1], OBJ_STRING))
    return ot_raise(state, "expected strings");
  ot_string_obj* string = as_string(args[0]);
  ot_string_obj* part = as_string(args[1]);
  if (part->length > string->length) return ot_false;
  size_t offset = starts ? 0 : string->length - part->length;
  return memcmp(as_bytes(string->bytes)->data + offset, as_bytes(part->bytes)->data,
                part->length) == 0
             ? ot_true
             : ot_false;
}
static otv nat_string_starts(ots* state, otv* args, int argc) {
  return nat_string_edge(state, args, argc, true);
}
static otv nat_string_ends(ots* state, otv* args, int argc) {
  return nat_string_edge(state, args, argc, false);
}

static otv nat_string_replace(ots* state, otv* args, int argc) {
  if (need_arity(state, "string-replace", argc, 3, 3) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_STRING) || !is_type(args[1], OBJ_STRING) ||
      !is_type(args[2], OBJ_STRING))
    return ot_raise(state, "string-replace: expected strings");
  ot_string_obj* source = as_string(args[0]);
  ot_string_obj* from = as_string(args[1]);
  ot_string_obj* to = as_string(args[2]);
  if (from->length == 0) return args[0];
  unsigned char* source_bytes = as_bytes(source->bytes)->data;
  unsigned char* from_bytes = as_bytes(from->bytes)->data;
  unsigned char* to_bytes = as_bytes(to->bytes)->data;
  buf out = {0};
  size_t offset = 0;
  while (offset < source->length) {
    if (offset + from->length <= source->length &&
        memcmp(source_bytes + offset, from_bytes, from->length) == 0) {
      buf_write(&out, (const char*)to_bytes, to->length);
      offset += from->length;
    } else {
      buf_byte(&out, (char)source_bytes[offset++]);
    }
  }
  otv result = ot_make_string(state, out.data, out.length);
  buf_free(&out);
  return result;
}

static otv nat_string_number(ots* state, otv* args, int argc) {
  if (need_arity(state, "string->number", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_STRING)) return ot_raise(state, "string->number: expected string");
  ot_string_obj* string = as_string(args[0]);
  const char* bytes = (const char*)as_bytes(string->bytes)->data;
  size_t start = 0;
  size_t end = string->length;
  while (start < end && ascii_space((unsigned char)bytes[start])) start++;
  while (end > start && ascii_space((unsigned char)bytes[end - 1])) end--;
  reader input = {.state = state,
                  .source = bytes + start,
                  .length = end - start,
                  .offset = 0,
                  .name = "<string>"};
  otv value = read_atom(&input);
  if (value == OT_UNWIND || input.offset != input.length ||
      (!ot_is_int(value) && !is_type(value, OBJ_FLOAT))) {
    ot_clear_condition(state);
    return ot_nil;
  }
  return value;
}

static otv nat_name(ots* state, otv* args, int argc) {
  if (need_arity(state, "name", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (is_type(args[0], OBJ_STRING)) return args[0];
  if (!is_type(args[0], OBJ_SYMBOL) && !is_type(args[0], OBJ_KEYWORD))
    return ot_raise(state, "name: expected symbol, keyword, or string");
  return make_string_from_name(state, args[0]);
}

static otv coerce_name(ots* state, otv value, bool keyword, const char* who) {
  if (is_type(value, keyword ? OBJ_KEYWORD : OBJ_SYMBOL)) return value;
  const char* bytes;
  size_t length;
  if (is_type(value, OBJ_STRING)) {
    ot_string_bytes(value, &bytes, &length);
  } else if (is_type(value, OBJ_SYMBOL) || is_type(value, OBJ_KEYWORD)) {
    bytes = as_name(value)->bytes;
    length = as_name(value)->length;
  } else {
    return ot_raise(state, "%s: expected name", who);
  }
  buf copy = {0};
  buf_write(&copy, bytes, length);
  otv result = ot_intern(state, copy.data, copy.length, keyword);
  buf_free(&copy);
  return result;
}

static otv nat_symbol(ots* state, otv* args, int argc) {
  if (need_arity(state, "symbol", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  return coerce_name(state, args[0], false, "symbol");
}
static otv nat_keyword(ots* state, otv* args, int argc) {
  if (need_arity(state, "keyword", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  return coerce_name(state, args[0], true, "keyword");
}

static void buf_object_reserve(ots* state, otv* value, size_t needed) {
  ot_buffer_obj* buffer = (ot_buffer_obj*)ot_as_obj(*value);
  ot_bytes_obj* old = as_bytes(buffer->bytes);
  if (old->length >= needed) return;
  size_t capacity = old->length == 0 ? 16 : old->length;
  while (capacity < needed) capacity *= 2;
  otv bytes = make_bytes(state, capacity);
  OT_FRAME_SCOPED(state, value, &bytes);
  buffer = (ot_buffer_obj*)ot_as_obj(*value);
  old = as_bytes(buffer->bytes);
  memcpy(as_bytes(bytes)->data, old->data, buffer->length);
  buffer->bytes = bytes;
}

static otv nat_buffer(ots* state, otv* args, int argc) {
  if (argc > 1) return ot_raise(state, "buffer: expected zero or one argument");
  otv seed = argc == 0 ? ot_make_string(state, "", 0) : make_display_string(state, args, 1, "");
  OT_FRAME_SCOPED(state, &seed);
  ot_string_obj* string = as_string(seed);
  otv bytes = make_bytes(state, string->length < 16 ? 16 : string->length);
  OT_FRAME_SCOPED(state, &bytes);
  string = as_string(seed);
  memcpy(as_bytes(bytes)->data, as_bytes(string->bytes)->data, string->length);
  ot_buffer_obj* buffer = must_alloc(state, sizeof(*buffer), OBJ_BUFFER);
  buffer->bytes = bytes;
  buffer->length = string->length;
  return ot_from_obj(buffer);
}

static otv nat_buffer_push(ots* state, otv* args, int argc) {
  if (argc < 1 || !is_type(args[0], OBJ_BUFFER))
    return ot_raise(state, "buffer-push!: expected buffer");
  otv buf_value = args[0];
  OT_FRAME_SCOPED(state, &buf_value);
  for (int i = 1; i < argc; i++) {
    otv text = make_display_string(state, &args[i], 1, "");
    OT_FRAME_SCOPED(state, &text);
    ot_string_obj* string = as_string(text);
    ot_buffer_obj* buffer = (ot_buffer_obj*)ot_as_obj(buf_value);
    buf_object_reserve(state, &buf_value, buffer->length + string->length);
    buffer = (ot_buffer_obj*)ot_as_obj(buf_value);
    memcpy(as_bytes(buffer->bytes)->data + buffer->length, as_bytes(string->bytes)->data,
           string->length);
    buffer->length += string->length;
  }
  return buf_value;
}

static otv nat_buffer_string(ots* state, otv* args, int argc) {
  if (need_arity(state, "buffer->string", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_BUFFER)) return ot_raise(state, "buffer->string: expected buffer");
  ot_buffer_obj* buffer = (ot_buffer_obj*)ot_as_obj(args[0]);
  return make_string_slice(state, args[0], 0, buffer->length);
}

static otv nat_output(ots* state, otv* args, int argc, bool display, bool newline) {
  for (int i = 0; i < argc; i++) {
    if (i != 0) state->writer(state->writer_userdata, " ", 1);
    ot_repr_to(state, args[i], display, state->writer, state->writer_userdata);
  }
  if (newline) state->writer(state->writer_userdata, "\n", 1);
  return ot_nil;
}
static otv nat_display(ots* state, otv* args, int argc) {
  return nat_output(state, args, argc, true, false);
}
static otv nat_write(ots* state, otv* args, int argc) {
  return nat_output(state, args, argc, false, false);
}
static otv nat_println(ots* state, otv* args, int argc) {
  return nat_output(state, args, argc, true, true);
}
static otv nat_newline(ots* state, otv* args, int argc) {
  (void)args;
  if (need_arity(state, "newline", argc, 0, 0) == OT_UNWIND) return OT_UNWIND;
  state->writer(state->writer_userdata, "\n", 1);
  return ot_nil;
}

static otv nat_apply(ots* state, otv* args, int argc) {
  if (argc < 2) return ot_raise(state, "apply: expected function and sequence");
  otv sequence = args[argc - 1];
  if (sequence != ot_nil && !is_type(sequence, OBJ_ARRAY) && !proper_list(sequence)) {
    if (is_type(sequence, OBJ_PAIR)) return ot_raise(state, "apply: improper list");
    return ot_raise(state, "apply: expected sequence");
  }
  size_t tail_count = sequence == ot_nil             ? 0
                      : is_type(sequence, OBJ_ARRAY) ? as_array(sequence)->length
                                                     : list_length(sequence);
  size_t count = (size_t)argc - 2 + tail_count;
  otv values[count == 0 ? 1 : count];
  for (int i = 1; i < argc - 1; i++) values[i - 1] = args[i];
  otv cursor = sequence;
  for (size_t i = 0; i < tail_count; i++) {
    values[(size_t)argc - 2 + i] = is_type(sequence, OBJ_ARRAY)
                                       ? as_slots(as_array(sequence)->slots)->values[i]
                                       : as_pair(cursor)->car;
    if (!is_type(sequence, OBJ_ARRAY)) cursor = as_pair(cursor)->cdr;
  }
  return apply_value(state, args[0], values, count, true);
}

static otv nat_identity(ots* state, otv* args, int argc) {
  if (need_arity(state, "identity", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  return args[0];
}

static otv build_condition(ots* state, otv* args, int argc) {
  if (argc == 0) return ot_raise(state, "error: expected a condition or message");
  if (!is_type(args[0], OBJ_STRING)) {
    if (argc != 1) return ot_raise(state, "error: extra arguments require a string message");
    return args[0];
  }
  otv condition = ot_table_new(state, 4);
  OT_FRAME_SCOPED(state, &condition);
  ot_table_put(state, condition, ot_intern(state, "type", 4, true),
               ot_intern(state, "error", 5, false));
  ot_table_put(state, condition, ot_intern(state, "message", 7, true), args[0]);
  if (argc > 1) {
    otv data = ot_array_new(state, (size_t)argc - 1);
    OT_FRAME_SCOPED(state, &data);
    for (int i = 1; i < argc; i++) array_push(state, &data, args[i]);
    ot_table_put(state, condition, ot_intern(state, "data", 4, true), data);
  }
  return condition;
}

static otv nat_error(ots* state, otv* args, int argc) {
  otv condition = build_condition(state, args, argc);
  if (condition == OT_UNWIND) return condition;
  return ot_raise_value(state, condition);
}

static otv nat_signal(ots* state, otv* args, int argc) {
  otv condition = build_condition(state, args, argc);
  if (condition == OT_UNWIND) return condition;
  return signal_condition(state, condition);
}

static otv nat_define_condition(ots* state, otv* args, int argc) {
  if (need_arity(state, "define-condition", argc, 2, 2) == OT_UNWIND) return OT_UNWIND;
  otv type = coerce_name(state, args[0], false, "define-condition");
  if (type == OT_UNWIND) return type;
  otv parent = args[1] == ot_nil ? ot_nil : coerce_name(state, args[1], false, "define-condition");
  if (parent == OT_UNWIND) return parent;
  ot_table_put(state, state->type_parents, type, parent == ot_nil ? OT_UNDEFINED : parent);
  return type;
}

static otv condition_type_value(ots* state, otv condition) {
  if (!is_type(condition, OBJ_TABLE)) return ot_nil;
  return ot_table_get(state, condition, ot_intern(state, "type", 4, true), ot_nil);
}

static otv nat_condition_of_type(ots* state, otv* args, int argc) {
  if (need_arity(state, "condition-of-type?", argc, 2, 2) == OT_UNWIND) return OT_UNWIND;
  otv type = coerce_name(state, args[1], false, "condition-of-type?");
  if (type == OT_UNWIND) return type;
  otv actual = condition_type_value(state, args[0]);
  while (is_type(actual, OBJ_SYMBOL)) {
    if (actual == type) return ot_true;
    otv parent = ot_table_get(state, state->type_parents, actual, ot_nil);
    if (parent == OT_UNDEFINED) break;
    actual = parent;
  }
  return ot_false;
}

static otv nat_find_restart(ots* state, otv* args, int argc) {
  if (need_arity(state, "find-restart", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  otv name = args[0];
  if (!is_type(name, OBJ_RESTART)) {
    name = coerce_name(state, name, false, "find-restart");
    if (name == OT_UNWIND) return name;
  }
  for (ot_restart_frame* frame = state->vm.restarts; frame != NULL; frame = frame->prev)
    for (size_t i = frame->count; i-- > 0;) {
      otv restart = frame->clauses[i].restart;
      if ((is_type(name, OBJ_RESTART) && restart == name) ||
          (!is_type(name, OBJ_RESTART) && ((ot_restart_obj*)ot_as_obj(restart))->name == name))
        return restart;
    }
  return ot_nil;
}

static otv nat_compute_restarts(ots* state, otv* args, int argc) {
  (void)args;
  if (need_arity(state, "compute-restarts", argc, 0, 0) == OT_UNWIND) return OT_UNWIND;
  otv output = ot_array_new(state, 4);
  OT_FRAME_SCOPED(state, &output);
  for (ot_restart_frame* frame = state->vm.restarts; frame != NULL; frame = frame->prev)
    for (size_t i = frame->count; i-- > 0;) array_push(state, &output, frame->clauses[i].restart);
  return output;
}

static otv nat_invoke_restart(ots* state, otv* args, int argc) {
  if (argc < 1) return ot_raise(state, "invoke-restart: expected restart");
  otv restart_args[1] = {args[0]};
  otv restart = nat_find_restart(state, restart_args, 1);
  if (restart == OT_UNWIND) return restart;
  if (restart == ot_nil) return ot_raise(state, "invoke-restart: no active restart");
  state->vm.unwind_kind = UNWIND_RESTART;
  state->vm.unwind_restart_id = ((ot_restart_obj*)ot_as_obj(restart))->id;
  state->vm.unwind_args = list_from_array(state, args + 1, (size_t)argc - 1);
  return OT_UNWIND;
}

static otv nat_restart_name(ots* state, otv* args, int argc) {
  if (need_arity(state, "restart-name", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_RESTART)) return ot_raise(state, "restart-name: expected restart");
  return ((ot_restart_obj*)ot_as_obj(args[0]))->name;
}
static otv nat_restart_description(ots* state, otv* args, int argc) {
  if (need_arity(state, "restart-description", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_RESTART))
    return ot_raise(state, "restart-description: expected restart");
  return ((ot_restart_obj*)ot_as_obj(args[0]))->description;
}

static otv nat_gensym(ots* state, otv* args, int argc) {
  if (argc > 1) return ot_raise(state, "gensym: expected zero or one argument");
  const char* prefix = "G";
  size_t prefix_length = 1;
  if (argc == 1) {
    if (!is_type(args[0], OBJ_STRING)) return ot_raise(state, "gensym: expected string prefix");
    ot_string_bytes(args[0], &prefix, &prefix_length);
  }
  buf name = {0};
  buf_write(&name, prefix, prefix_length);
  buf_printf(&name, "__%" PRIu64, ++state->gensym_id);
  otv symbol = ot_intern(state, name.data, name.length, false);
  buf_free(&name);
  return symbol;
}

static otv nat_macro_oracle(ots* state, otv* args, int argc) {
  if (need_arity(state, "expander-macro-var", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_SYMBOL)) return ot_nil;
  otv var = resolve_var(state, state->vm.current_namespace, args[0], false);
  if (!is_type(var, OBJ_VAR)) return ot_nil;
  otv value = ((ot_var_obj*)ot_as_obj(var))->value;
  return is_type(value, OBJ_MACRO) ? value : ot_nil;
}

static otv macroexpand_once(ots* state, otv form) {
  if (!is_type(form, OBJ_PAIR) || !is_type(as_pair(form)->car, OBJ_SYMBOL)) return form;
  otv oracle_args[1] = {as_pair(form)->car};
  otv macro = nat_macro_oracle(state, oracle_args, 1);
  if (!is_type(macro, OBJ_MACRO)) return form;
  otv tail = as_pair(form)->cdr;
  if (!proper_list(tail)) return ot_raise(state, "macro call: improper list");
  size_t argc = list_length(tail);
  otv values[argc == 0 ? 1 : argc];
  for (size_t i = 0; i < argc; i++) {
    values[i] = as_pair(tail)->car;
    tail = as_pair(tail)->cdr;
  }
  return apply_value(state, macro, values, argc, true);
}

static otv nat_macroexpand_one(ots* state, otv* args, int argc) {
  if (need_arity(state, "macroexpand-1", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  return macroexpand_once(state, args[0]);
}

static otv nat_macroexpand(ots* state, otv* args, int argc) {
  if (need_arity(state, "macroexpand", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  otv form = args[0];
  OT_FRAME_SCOPED(state, &form);
  for (;;) {
    otv expanded = macroexpand_once(state, form);
    if (expanded == OT_UNWIND || expanded == form) return expanded;
    form = expanded;
  }
}

static otv nat_eval(ots* state, otv* args, int argc) {
  if (need_arity(state, "eval", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  otv form = args[0];
  OT_FRAME_SCOPED(state, &form);
  if (state->expander != ot_nil) {
    otv values[1] = {form};
    form = apply_value(state, state->expander, values, 1, false);
    if (form == OT_UNWIND) return form;
  }
  return compile_and_run_form(state, form, ot_nil);
}

static otv nat_read_string(ots* state, otv* args, int argc) {
  if (need_arity(state, "read-string", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_STRING)) return ot_raise(state, "read-string: expected string");
  const char* bytes;
  size_t length;
  ot_string_bytes(args[0], &bytes, &length);
  reader input = {.state = state, .source = bytes, .length = length, .name = "<string>"};
  otv value = read_form(&input);
  if (value == OT_UNDEFINED) return ot_raise(state, "read-string: empty input");
  if (value == OT_UNWIND) return value;
  reader_space(&input);
  if (input.offset != input.length) return ot_raise(state, "read-string: trailing input");
  return value;
}

static otv nat_string_less(ots* state, otv* args, int argc) {
  if (need_arity(state, "string<?", argc, 2, 2) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_STRING) || !is_type(args[1], OBJ_STRING))
    return ot_raise(state, "string<?: expected strings");
  ot_string_obj* a = as_string(args[0]);
  ot_string_obj* b = as_string(args[1]);
  size_t n = a->length < b->length ? a->length : b->length;
  int order = memcmp(as_bytes(a->bytes)->data, as_bytes(b->bytes)->data, n);
  return (order < 0 || (order == 0 && a->length < b->length)) ? ot_true : ot_false;
}

static otv nat_current_jiffy(ots* state, otv* args, int argc) {
  (void)args;
  if (need_arity(state, "current-jiffy", argc, 0, 0) == OT_UNWIND) return OT_UNWIND;
  return ot_make_int((intptr_t)ot_platform_monotonic_ns());
}
static otv nat_jiffies(ots* state, otv* args, int argc) {
  (void)args;
  if (need_arity(state, "jiffies-per-second", argc, 0, 0) == OT_UNWIND) return OT_UNWIND;
  return ot_make_int(INT64_C(1000000000));
}
static otv nat_current_second(ots* state, otv* args, int argc) {
  (void)args;
  if (need_arity(state, "current-second", argc, 0, 0) == OT_UNWIND) return OT_UNWIND;
  return ot_make_float(state, ot_platform_current_second());
}

static otv nat_quit(ots* state, otv* args, int argc) {
  (void)args;
  if (need_arity(state, "quit", argc, 0, 0) == OT_UNWIND) return OT_UNWIND;
  state->vm.unwind_kind = UNWIND_QUIT;
  state->quit_requested = true;
  return OT_UNWIND;
}

static otv nat_update_mutable(ots* state, otv* args, int argc) {
  if (argc < 3) return ot_raise(state, "update!: expected collection, key, and function");
  otv current_args[3] = {args[0], args[1], ot_nil};
  otv current = nat_get(state, current_args, 2);
  size_t call_count = (size_t)argc - 2;
  otv call_args[call_count];
  call_args[0] = current;
  for (int i = 3; i < argc; i++) call_args[i - 2] = args[i];
  otv value = apply_value(state, args[2], call_args, call_count, false);
  if (value == OT_UNWIND) return value;
  otv put_args[3] = {args[0], args[1], value};
  return nat_put(state, put_args, 3);
}

static otv nat_abs(ots* state, otv* args, int argc) {
  if (need_arity(state, "abs", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (ot_is_int(args[0])) {
    intptr_t value = ot_get_int(args[0]);
    return value < 0 ? ot_make_int((intptr_t)((uintptr_t)0 - (uintptr_t)value)) : args[0];
  }
  if (is_type(args[0], OBJ_FLOAT))
    return ot_make_float(state, fabs(((ot_float_obj*)ot_as_obj(args[0]))->value));
  return ot_raise(state, "abs: expected number");
}

static otv nat_rounding(ots* state, otv* args, int argc, int mode) {
  if (need_arity(state, "rounding", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (ot_is_int(args[0])) return args[0];
  if (!is_type(args[0], OBJ_FLOAT)) return ot_raise(state, "expected number");
  double value = ((ot_float_obj*)ot_as_obj(args[0]))->value;
  double rounded = mode == 0   ? floor(value)
                   : mode == 1 ? ceil(value)
                   : mode == 2 ? (value < 0 ? ceil(value - 0.5) : floor(value + 0.5))
                               : trunc(value);
  double minimum = -(double)(UINTPTR_MAX >> 1u) - 1.0;
  double maximum = (double)(UINTPTR_MAX >> 1u);
  if (!isfinite(rounded) || rounded < minimum || rounded > maximum)
    return ot_raise(state, "integer conversion out of range");
  return ot_make_int((intptr_t)rounded);
}
static otv nat_floor(ots* state, otv* args, int argc) { return nat_rounding(state, args, argc, 0); }
static otv nat_ceiling(ots* state, otv* args, int argc) {
  return nat_rounding(state, args, argc, 1);
}
static otv nat_round(ots* state, otv* args, int argc) { return nat_rounding(state, args, argc, 2); }
static otv nat_truncate(ots* state, otv* args, int argc) {
  return nat_rounding(state, args, argc, 3);
}

static otv nat_unary_math(ots* state, otv* args, int argc, double (*function)(double),
                          const char* name) {
  if (need_arity(state, name, argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  double value;
  bool exact;
  if (!number_value(args[0], &value, &exact)) return ot_raise(state, "%s: expected number", name);
  return ot_make_float(state, function(value));
}
#define UNARY_MATH(NAME, FUNCTION)                                                                 \
  static otv nat_##NAME(ots* state, otv* args, int argc) {                                         \
    return nat_unary_math(state, args, argc, FUNCTION, #NAME);                                     \
  }
UNARY_MATH(sqrt, sqrt)
UNARY_MATH(exp, exp)
UNARY_MATH(log, log)
UNARY_MATH(sin, sin)
UNARY_MATH(cos, cos)
UNARY_MATH(tan, tan)
UNARY_MATH(asin, asin)
UNARY_MATH(acos, acos)
#undef UNARY_MATH

static otv nat_atan(ots* state, otv* args, int argc) {
  if (argc < 1 || argc > 2) return ot_raise(state, "atan: expected one or two arguments");
  double y;
  double x;
  bool exact;
  if (!number_value(args[0], &y, &exact)) return ot_raise(state, "atan: expected number");
  if (argc == 1) return ot_make_float(state, atan(y));
  if (!number_value(args[1], &x, &exact)) return ot_raise(state, "atan: expected number");
  return ot_make_float(state, atan2(y, x));
}

static otv nat_expt(ots* state, otv* args, int argc) {
  if (need_arity(state, "expt", argc, 2, 2) == OT_UNWIND) return OT_UNWIND;
  if (ot_is_int(args[0]) && ot_is_int(args[1]) && ot_get_int(args[1]) >= 0) {
    uintptr_t base = (uintptr_t)ot_get_int(args[0]);
    uintptr_t result = 1;
    uintptr_t exponent = (uintptr_t)ot_get_int(args[1]);
    while (exponent != 0) {
      if ((exponent & 1u) != 0) result *= base;
      base *= base;
      exponent >>= 1u;
    }
    return ot_make_int((intptr_t)result);
  }
  double base;
  double exponent;
  bool exact;
  if (!number_value(args[0], &base, &exact) || !number_value(args[1], &exponent, &exact))
    return ot_raise(state, "expt: expected numbers");
  return ot_make_float(state, pow(base, exponent));
}

static otv nat_inexact(ots* state, otv* args, int argc) {
  if (need_arity(state, "inexact", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (is_type(args[0], OBJ_FLOAT)) return args[0];
  if (!ot_is_int(args[0])) return ot_raise(state, "inexact: expected number");
  return ot_make_float(state, (double)ot_get_int(args[0]));
}

static otv nat_exact(ots* state, otv* args, int argc) {
  if (need_arity(state, "exact", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (ot_is_int(args[0])) return args[0];
  if (!is_type(args[0], OBJ_FLOAT)) return ot_raise(state, "exact: expected number");
  double value = ((ot_float_obj*)ot_as_obj(args[0]))->value;
  if (!isfinite(value) || trunc(value) != value)
    return ot_raise(state, "exact: non-integral float");
  return nat_truncate(state, args, 1);
}

static otv nat_numeric_class(ots* state, otv* args, int argc, int mode) {
  if (need_arity(state, "numeric predicate", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (mode == 0) return ot_is_int(args[0]) ? ot_true : ot_false;
  if (mode == 1) return is_type(args[0], OBJ_FLOAT) ? ot_true : ot_false;
  if (mode == 2) {
    if (ot_is_int(args[0])) return ot_true;
    return is_type(args[0], OBJ_FLOAT) && trunc(((ot_float_obj*)ot_as_obj(args[0]))->value) ==
                                              ((ot_float_obj*)ot_as_obj(args[0]))->value
               ? ot_true
               : ot_false;
  }
  double value;
  bool exact;
  if (!number_value(args[0], &value, &exact)) return ot_raise(state, "expected number");
  bool result = mode == 3 ? isnan(value) : mode == 4 ? isinf(value) : isfinite(value);
  return result ? ot_true : ot_false;
}
#define NUM_CLASS(NAME, MODE)                                                                      \
  static otv nat_##NAME(ots* state, otv* args, int argc) {                                         \
    return nat_numeric_class(state, args, argc, MODE);                                             \
  }
NUM_CLASS(exact_p, 0)
NUM_CLASS(inexact_p, 1)
NUM_CLASS(integer_p, 2)
NUM_CLASS(nan_p, 3)
NUM_CLASS(infinite_p, 4)
NUM_CLASS(finite_p, 5)
#undef NUM_CLASS

static otv nat_sign(ots* state, otv* args, int argc, int mode) {
  if (need_arity(state, "sign predicate", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  double value;
  bool exact;
  if (!number_value(args[0], &value, &exact)) return ot_raise(state, "expected number");
  bool result = mode == 0 ? value == 0 : mode == 1 ? value > 0 : value < 0;
  return result ? ot_true : ot_false;
}
static otv nat_zero(ots* state, otv* args, int argc) { return nat_sign(state, args, argc, 0); }
static otv nat_pos(ots* state, otv* args, int argc) { return nat_sign(state, args, argc, 1); }
static otv nat_neg(ots* state, otv* args, int argc) { return nat_sign(state, args, argc, 2); }

typedef struct nat_def {
  const char* name;
  ot_nat function;
} nat_def;

static const nat_def core_nats[] = {
    {"+", nat_add},
    {"*", nat_multiply},
    {"-", nat_subtract},
    {"/", nat_divide},
    {"quotient", nat_quotient},
    {"remainder", nat_remainder},
    {"modulo", nat_modulo},
    {"=", nat_num_equal},
    {"<", nat_less},
    {">", nat_greater},
    {"<=", nat_less_equal},
    {">=", nat_greater_equal},
    {"min", nat_min},
    {"max", nat_max},
    {"inc", nat_inc},
    {"dec", nat_dec},
    {"abs", nat_abs},
    {"floor", nat_floor},
    {"ceiling", nat_ceiling},
    {"round", nat_round},
    {"truncate", nat_truncate},
    {"sqrt", nat_sqrt},
    {"exp", nat_exp},
    {"log", nat_log},
    {"sin", nat_sin},
    {"cos", nat_cos},
    {"tan", nat_tan},
    {"asin", nat_asin},
    {"acos", nat_acos},
    {"atan", nat_atan},
    {"expt", nat_expt},
    {"exact", nat_exact},
    {"inexact", nat_inexact},
    {"exact?", nat_exact_p},
    {"inexact?", nat_inexact_p},
    {"integer?", nat_integer_p},
    {"nan?", nat_nan_p},
    {"infinite?", nat_infinite_p},
    {"finite?", nat_finite_p},
    {"zero?", nat_zero},
    {"pos?", nat_pos},
    {"neg?", nat_neg},
    {"even?", nat_even},
    {"odd?", nat_odd},
    {"not", nat_not},
    {"nil?", nat_nil_p},
    {"null?", nat_null_p},
    {"boolean?", nat_boolean_p},
    {"int?", nat_int_p},
    {"float?", nat_float_p},
    {"number?", nat_number_p},
    {"symbol?", nat_symbol_p},
    {"keyword?", nat_keyword_p},
    {"string?", nat_string_p},
    {"pair?", nat_pair_p},
    {"array?", nat_array_p},
    {"table?", nat_table_p},
    {"buffer?", nat_buffer_p},
    {"macro?", nat_macro_p},
    {"procedure?", nat_procedure_p},
    {"foreign?", nat_ext_p},
    {"list?", nat_list_p},
    {"true?", nat_true_p},
    {"false?", nat_false_p},
    {"eq?", nat_eq},
    {"equal?", nat_equal},
    {"type", nat_type},
    {"cons", nat_cons},
    {"car", nat_car},
    {"cdr", nat_cdr},
    {"set-car!", nat_set_car},
    {"set-cdr!", nat_set_cdr},
    {"list", nat_list},
    {"append", nat_append},
    {"length", nat_length},
    {"reverse", nat_reverse},
    {"for-each", nat_for_each},
    {"array", nat_array},
    {"table", nat_table},
    {"get", nat_get},
    {"put!", nat_put},
    {"push!", nat_push},
    {"pop!", nat_pop},
    {"update!", nat_update_mutable},
    {"keys", nat_keys},
    {"values", nat_values},
    {"copy", nat_copy},
    {"str", nat_str},
    {"string-append", nat_string_append},
    {"string-length", nat_string_length},
    {"substring", nat_substring},
    {"string-split", nat_string_split},
    {"string-join", nat_string_join},
    {"string-upcase", nat_string_upcase},
    {"string-downcase", nat_string_downcase},
    {"string-trim", nat_string_trim},
    {"string-contains?", nat_string_contains},
    {"string-starts-with?", nat_string_starts},
    {"string-ends-with?", nat_string_ends},
    {"string-replace", nat_string_replace},
    {"string->number", nat_string_number},
    {"number->string", nat_str},
    {"name", nat_name},
    {"string->symbol", nat_symbol},
    {"symbol->string", nat_name},
    {"symbol", nat_symbol},
    {"keyword", nat_keyword},
    {"buffer", nat_buffer},
    {"buffer-push!", nat_buffer_push},
    {"buffer->string", nat_buffer_string},
    {"display", nat_display},
    {"print", nat_display},
    {"write", nat_write},
    {"println", nat_println},
    {"newline", nat_newline},
    {"apply", nat_apply},
    {"identity", nat_identity},
    {"eval", nat_eval},
    {"read-string", nat_read_string},
    {"macroexpand-1", nat_macroexpand_one},
    {"macroexpand", nat_macroexpand},
    {"gensym", nat_gensym},
    {"expander-macro-var", nat_macro_oracle},
    {"string<?", nat_string_less},
    {"error", nat_error},
    {"signal", nat_signal},
    {"define-condition", nat_define_condition},
    {"condition-of-type?", nat_condition_of_type},
    {"compute-restarts", nat_compute_restarts},
    {"find-restart", nat_find_restart},
    {"invoke-restart", nat_invoke_restart},
    {"restart-name", nat_restart_name},
    {"restart-description", nat_restart_description},
    {"current-jiffy", nat_current_jiffy},
    {"jiffies-per-second", nat_jiffies},
    {"current-second", nat_current_second},
    {"quit", nat_quit},
    {"exit", nat_quit},
};

/* =========================================================================
 * 9. BOOTSTRAP, EMBEDDING, AND EXTENSION API
 * ========================================================================= */

void ot_def_nat(ots* state, const char* name, ot_nat function) {
  otv symbol = ot_intern(state, name, strlen(name), false);
  OT_FRAME_SCOPED(state, &symbol);
  ot_nat_obj* nat = must_alloc(state, sizeof(*nat), OBJ_NAT);
  nat->function = function;
  nat->name = symbol;
  define_var(state, state->vm.current_namespace, symbol, ot_from_obj(nat), ot_nil, false);
}

static void register_core(ots* state) {
  for (size_t i = 0; i < sizeof core_nats / sizeof core_nats[0]; i++)
    ot_def_nat(state, core_nats[i].name, core_nats[i].function);
}

static bool eval_source(ots* state, const char* source, size_t length, const char* name,
                        bool expand, otv* out) {
  reader input = {.state = state, .source = source, .length = length, .name = name};
  otv result = ot_nil;
  otv form = ot_nil;
  OT_FRAME_SCOPED(state, &result, &form);
  for (;;) {
    reader_space(&input);
    form = read_form(&input);
    if (form == OT_UNDEFINED) break;
    if (form == OT_UNWIND) return false;
    if (expand && state->expander != ot_nil) {
      otv args[1] = {form};
      form = apply_value(state, state->expander, args, 1, false);
      if (form == OT_UNWIND) return false;
    }
    result = compile_and_run_form(state, form, ot_nil);
    if (result == OT_UNWIND) return false;
  }
  if (out != NULL) *out = result;
  return true;
}

bool ot_eval_partial(ots* state, const char* source, size_t length, const char* name,
                     size_t* consumed, bool* incomplete, otv* out) {
  reader input = {
      .state = state, .source = source, .length = length, .name = name == NULL ? "<input>" : name};
  otv result = ot_nil;
  otv form = ot_nil;
  OT_FRAME_SCOPED(state, &result, &form);
  *consumed = 0;
  *incomplete = false;
  for (;;) {
    reader_space(&input);
    size_t start = input.offset;
    form = read_form(&input);
    if (form == OT_UNDEFINED) {
      *consumed = input.offset;
      if (out != NULL) *out = result;
      return true;
    }
    if (form == OT_UNWIND) {
      *consumed = start;
      *incomplete = input.incomplete;
      return false;
    }
    if (state->expander != ot_nil) {
      otv args[1] = {form};
      form = apply_value(state, state->expander, args, 1, false);
      if (form == OT_UNWIND) {
        *consumed = input.offset;
        return false;
      }
    }
    result = compile_and_run_form(state, form, ot_nil);
    if (result == OT_UNWIND) {
      *consumed = input.offset;
      return false;
    }
    *consumed = input.offset;
  }
}

ot_config ot_config_default(void) {
  return (ot_config){.heap_init = OT_HEAP_INIT,
                     .heap_max = OT_HEAP_MAX,
                     .max_depth = OT_MAX_DEPTH,
                     .gc_stress = false};
}

ots* ot_create(const ot_config* configuration) {
  ot_config config = configuration == NULL ? ot_config_default() : *configuration;
  if (config.heap_init < 1024 || config.heap_max < config.heap_init ||
      config.heap_max > SIZE_MAX / 2 || config.max_depth == 0)
    return NULL;
  ots* state = ot_host_alloc(sizeof(*state));
  if (state == NULL) return NULL;
  memset(state, 0, sizeof(*state));
  state->vm.frame_limit = config.max_depth;
  state->config = config;
  state->config.gc_stress = false;
  state->reservation = ot_host_alloc(config.heap_max * 2);
  if (state->reservation == NULL) {
    ot_host_free(state);
    return NULL;
  }
  state->from_space = state->reservation;
  state->to_space = state->reservation + config.heap_max;
  state->alloc = state->from_space;
  state->capacity = config.heap_init;
  state->limit = state->from_space + state->capacity;
  state->symbols = ot_nil;
  state->namespaces = ot_nil;
  state->core_namespace = ot_nil;
  state->vm.current_namespace = ot_nil;
  state->expander = ot_nil;
  state->type_parents = ot_nil;
  state->vm.condition = ot_nil;
  state->vm.unwind_args = ot_nil;
  state->exts = ot_nil;
  state->writer = ot_default_write;
  state->next_stable_id = 1;
  atomic_init(&state->vm.interrupted, false);

  otv core_name = ot_intern(state, "otium.core", 10, false);
  state->core_namespace = namespace_find(state, core_name, true);
  state->vm.current_namespace = state->core_namespace;
  state->type_parents = ot_table_new(state, 8);
  register_core(state);
  ot_table_put(state, state->type_parents, ot_intern(state, "error", 5, false), OT_UNDEFINED);

  otv ignored;
  if (!eval_source(state, ot_expander_src, ot_expander_src_len, "<expander>", false, &ignored)) {
    ot_destroy(state);
    return NULL;
  }
  otv expander_name = ot_intern(state, "*expander*", 10, false);
  otv expander_var = namespace_var(state, state->core_namespace, expander_name, false);
  if (!is_type(expander_var, OBJ_VAR)) {
    ot_destroy(state);
    return NULL;
  }
  state->expander = ((ot_var_obj*)ot_as_obj(expander_var))->value;
  if (!eval_source(state, ot_prelude_src, ot_prelude_src_len, "<prelude>", true, &ignored)) {
    ot_destroy(state);
    return NULL;
  }
  otv user_name = ot_intern(state, "user", 4, false);
  state->vm.current_namespace = namespace_find(state, user_name, true);
  state->config.gc_stress = config.gc_stress;
  return state;
}

void ot_destroy(ots* state) {
  if (state == NULL) return;
  for (otv cursor = state->exts; is_type(cursor, OBJ_EXT);) {
    ot_ext_obj* ext = (ot_ext_obj*)ot_as_obj(cursor);
    otv next = ext->next;
    if (!ext->released && ext->pointer_payload && ext->payload.pointer != NULL && ext->type > 0 &&
        ext->type <= state->ext_type_count) {
      ot_ext_finalizer finalizer = state->ext_types[ext->type - 1].finalizer;
      if (finalizer != NULL) finalizer(state, ext->payload.pointer);
    }
    cursor = next;
  }
  for (ot_global_root* root = state->globals; root != NULL;) {
    ot_global_root* next = root->next;
    ot_host_free(root);
    root = next;
  }
  for (ot_module* module = state->modules; module != NULL;) {
    ot_module* next = module->next;
    ot_host_free(module->name);
    ot_host_free(module);
    module = next;
  }
  for (size_t i = 0; i < state->ext_type_count; i++) ot_host_free(state->ext_types[i].name);
  ot_host_free(state->ext_types);
  ot_host_free(state->vm.vm_frames);
  ot_host_free(state->vm.vm_stack);
  ot_host_free(state->reservation);
  ot_host_free(state);
}

void ot_set_writer(ots* state, ot_writer writer, void* userdata) {
  state->writer = writer == NULL ? ot_default_write : writer;
  state->writer_userdata = userdata;
}

void ot_set_loader(ots* state, ot_loader loader, void* userdata) {
  state->loader = loader;
  state->loader_userdata = userdata;
}

void ot_set_interrupt_hook(ots* state, ot_interrupt_hook hook, void* userdata) {
  state->interrupt_hook = hook;
  state->interrupt_userdata = userdata;
}

void ot_interrupt(ots* state) { atomic_store(&state->vm.interrupted, true); }

bool ot_eval_src(ots* state, const char* source, size_t length, const char* name, otv* out) {
  return eval_source(state, source, length, name == NULL ? "<input>" : name, true, out);
}

otv ot_condition(const ots* state) { return state->vm.condition; }

void ot_clear_condition(ots* state) {
  state->vm.condition = ot_nil;
  state->vm.unwind_args = ot_nil;
  state->vm.unwind_kind = UNWIND_NONE;
  state->vm.unwind_restart_id = 0;
}

void ot_register_module(ots* state, const char* name, ot_module_init init) {
  ot_module* module = ot_host_alloc(sizeof(*module));
  size_t length = strlen(name);
  char* copy = ot_host_alloc(length + 1);
  if (module == NULL || copy == NULL) abort();
  memcpy(copy, name, length + 1);
  *module = (ot_module){.next = state->modules, .name = copy, .init = init};
  state->modules = module;
}

unsigned ot_ext_type(ots* state, const char* name, ot_ext_finalizer finalizer) {
  for (size_t i = 0; i < state->ext_type_count; i++)
    if (strcmp(state->ext_types[i].name, name) == 0) return (unsigned)i + 1;
  if (state->ext_type_count == state->ext_type_capacity) {
    size_t capacity = state->ext_type_capacity == 0 ? 8 : state->ext_type_capacity * 2;
    void* grown = ot_host_realloc(state->ext_types, capacity * sizeof(*state->ext_types));
    if (grown == NULL) abort();
    state->ext_types = grown;
    state->ext_type_capacity = capacity;
  }
  size_t length = strlen(name);
  char* copy = ot_host_alloc(length + 1);
  if (copy == NULL) abort();
  memcpy(copy, name, length + 1);
  state->ext_types[state->ext_type_count] =
      (ot_ext_type_info){.name = copy, .finalizer = finalizer};
  return (unsigned)++state->ext_type_count;
}

otv ot_ext_inline(ots* state, unsigned type, const void* payload, size_t size) {
  if (type == 0 || type > state->ext_type_count) return ot_nil;
  size_t object_size = offsetof(ot_ext_obj, payload.bytes) + (size == 0 ? 1 : size);
  ot_ext_obj* ext = must_alloc(state, object_size, OBJ_EXT);
  ext->next = state->exts;
  ext->type = type;
  ext->pointer_payload = false;
  ext->released = false;
  ext->size = size;
  if (size != 0) memcpy(ext->payload.bytes, payload, size);
  state->exts = ot_from_obj(ext);
  return state->exts;
}

otv ot_ext_pointer(ots* state, unsigned type, void* payload) {
  if (type == 0 || type > state->ext_type_count) return ot_nil;
  ot_ext_obj* ext = must_alloc(state, sizeof(*ext), OBJ_EXT);
  ext->next = state->exts;
  ext->type = type;
  ext->pointer_payload = true;
  ext->released = false;
  ext->size = sizeof(void*);
  ext->payload.pointer = payload;
  state->exts = ot_from_obj(ext);
  return state->exts;
}

bool ot_ext_check(ots* state, const char* who, otv value, unsigned type, void** payload) {
  if (!is_type(value, OBJ_EXT) || ((ot_ext_obj*)ot_as_obj(value))->type != type) {
    ot_raise(state, "%s: wrong foreign type", who);
    return false;
  }
  ot_ext_obj* ext = (ot_ext_obj*)ot_as_obj(value);
  if (ext->released) {
    ot_raise(state, "%s: released foreign value", who);
    return false;
  }
  *payload = ext->pointer_payload ? ext->payload.pointer : ext->payload.bytes;
  return true;
}

otv ot_ext_release(ots* state, const char* who, otv value, unsigned type) {
  void* payload;
  if (!ot_ext_check(state, who, value, type, &payload)) return OT_UNWIND;
  ot_ext_obj* ext = (ot_ext_obj*)ot_as_obj(value);
  if (ext->pointer_payload && state->ext_types[type - 1].finalizer != NULL)
    state->ext_types[type - 1].finalizer(state, payload);
  ext->released = true;
  ext->payload.pointer = NULL;
  return value;
}
