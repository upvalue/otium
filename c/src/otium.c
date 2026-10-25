#define OT_INTERNAL
#include "otium.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static const char ot_expander_src[] = {
#embed "../../lib/otium/expander.ot"
};
static const unsigned ot_expander_src_len = sizeof ot_expander_src;
#include "ot-gc-internal.inc"
static const char ot_prelude_src[] = {
#embed "../../lib/otium/core.ot"
};
static const unsigned ot_prelude_src_len = sizeof ot_prelude_src;
static const char ot_extra_src[] = {
#embed "../../lib/otium/extra.ot"
};
static const unsigned ot_extra_src_len = sizeof ot_extra_src;

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
      "fn",
      "def",
      "def-",
      "defonce",
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
      "ns",
      "require",
      "defclass",
      "defabstract",
      "defgeneric",
      "defmethod",
      "reset-class!",
      "reset-generic!",
  };
  for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
    if (strlen(names[i]) == length && memcmp(names[i], bytes, length) == 0) return true;
  return false;
}

#define as_name(V) ((ot_name_obj*)ot_as_obj(V))
#define as_bytes(V) ((ot_bytes_obj*)ot_as_obj(V))
#define as_bytevector(V) ((ot_bytevector_obj*)ot_as_obj(V))
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

otv ot_make_bytevector(ots* state, size_t length) {
  ot_bytevector_obj* bytes = must_alloc(state, sizeof(*bytes) + length, OBJ_BYTEVECTOR);
  bytes->length = length;
  bytes->stable_id = 0;
  return ot_from_obj(bytes);
}

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
  ot_store(state, string, &string->bytes, bytes_value);
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
  ot_store(state, string, &string->bytes, bytes_value);
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
  ot_store(state, string, &string->bytes, bytes_value);
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
  ot_store(state, name, &name->next, state->symbols);
  ot_store(state, name, &name->cache_namespace, ot_nil);
  ot_store(state, name, &name->cache_var, ot_nil);
  ot_store(state, name, &name->qualified_prefix, ot_nil);
  ot_store(state, name, &name->qualified_member, ot_nil);
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

#define as_record(V) ((ot_record_obj*)ot_as_obj(V))
#define as_record_type(V) ((ot_record_type_obj*)ot_as_obj(V))
#define as_instance(V) ((ot_instance_obj*)ot_as_obj(V))
#define as_layout(V) ((ot_layout_obj*)ot_as_obj(V))

static otv instance_payload(otv object) {
  while (is_type(as_instance(object)->layout, OBJ_INSTANCE)) object = as_instance(object)->layout;
  return object;
}

ot_type ot_value_type(otv value) {
  if (ot_is_int(value)) return OT_TYPE_INT;
  if (value == ot_nil) return OT_TYPE_NIL;
  if (value == ot_null) return OT_TYPE_NULL;
  if (value == ot_true || value == ot_false) return OT_TYPE_BOOLEAN;
  if (ot_is_char(value)) return OT_TYPE_CHAR;
  if (value == ot_eof) return OT_TYPE_EOF;
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
    case OBJ_BYTEVECTOR: return OT_TYPE_BYTEVECTOR;
    case OBJ_PORT: return OT_TYPE_PORT;
    case OBJ_GENERIC:
    case OBJ_FUNCTION:
    case OBJ_NAT: return OT_TYPE_FUNCTION;
    case OBJ_MACRO: return OT_TYPE_MACRO;
    case OBJ_PARAM: return OT_TYPE_PARAM;
    case OBJ_RESTART: return OT_TYPE_RESTART;
    case OBJ_EXT: return OT_TYPE_EXT;
    case OBJ_DISPATCH_TYPE: return OT_TYPE_DESCRIPTOR;
    case OBJ_RECORD: return OT_TYPE_RECORD;
    case OBJ_RECORD_TYPE: return OT_TYPE_RECORD_TYPE;
    case OBJ_INSTANCE: return OT_TYPE_INSTANCE;
    default: return OT_TYPE_INTERNAL;
  }
}

otv ot_cons(ots* state, otv car, otv cdr) {
  OT_FRAME_SCOPED(state, &car, &cdr);
  ot_pair_obj* pair = must_alloc(state, sizeof(*pair), OBJ_PAIR);
  ot_store(state, pair, &pair->car, car);
  ot_store(state, pair, &pair->cdr, cdr);
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
  for (size_t i = 0; i < capacity; i++) ot_store(state, slots, &slots->values[i], ot_nil);
  return ot_from_obj(slots);
}

otv ot_array_new(ots* state, size_t capacity) {
  if (capacity < 4) capacity = 4;
  otv slots = make_slots(state, capacity);
  OT_FRAME_SCOPED(state, &slots);
  ot_array_obj* array = must_alloc(state, sizeof(*array), OBJ_ARRAY);
  ot_store(state, array, &array->slots, slots);
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
  ot_slots_obj* replacement = as_slots(new_slots);
  for (size_t i = 0; i < array->length; i++)
    ot_store(state, replacement, &replacement->values[i], old->values[i]);
  ot_store(state, array, &array->slots, new_slots);
}

static void array_push(ots* state, otv* array_value, otv value) {
  OT_FRAME_SCOPED(state, array_value, &value);
  ot_array_obj* array = as_array(*array_value);
  array_reserve(state, array_value, array->length + 1);
  array = as_array(*array_value);
  ot_slots_obj* slots = as_slots(array->slots);
  ot_store(state, slots, &slots->values[array->length++], value);
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
    ot_store(state, entries, &entries->values[i].key, ot_nil);
    ot_store(state, entries, &entries->values[i].value, ot_nil);
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
  ot_store(state, table, &table->entries, entries);
  ot_store(state, table, &table->index, index);
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
      ot_array_obj* a = as_array(left);
      ot_array_obj* b = as_array(right);
      if (!structural || a->length != b->length) return false;
      for (size_t i = 0; i < a->length; i++)
        if (!ot_equal(state, as_slots(a->slots)->values[i], as_slots(b->slots)->values[i], true))
          return false;
      return true;
    }
    default: return false;
  }
}

static bool key_equal(ots* state, otv left, otv right) {
  if (left == right) return true;
  if (is_type(left, OBJ_PAIR))
    return is_type(right, OBJ_PAIR) && key_equal(state, as_pair(left)->car, as_pair(right)->car) &&
           key_equal(state, as_pair(left)->cdr, as_pair(right)->cdr);
  if (is_type(left, OBJ_ARRAY) || is_type(left, OBJ_TABLE) || is_type(left, OBJ_BUFFER) ||
      is_type(left, OBJ_FUNCTION) || is_type(left, OBJ_NAT) || is_type(left, OBJ_EXT) ||
      is_type(left, OBJ_BYTEVECTOR))
    return false;
  return ot_equal(state, left, right, true);
}

static uint32_t value_hash(ots* state, otv value) {
  (void)state;
  if (ot_is_int(value)) return (uint32_t)((uint64_t)value ^ ((uint64_t)value >> 32u));
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
    case OBJ_BYTEVECTOR: {
      ot_bytevector_obj* bytes = as_bytevector(value);
      if (bytes->stable_id == 0) bytes->stable_id = ++state->next_stable_id;
      return (uint32_t)bytes->stable_id;
    }
    case OBJ_RECORD: return (uint32_t)as_record(value)->stable_id;
    case OBJ_RECORD_TYPE: return (uint32_t)as_record_type(value)->stable_id;
    case OBJ_INSTANCE: return (uint32_t)as_instance(value)->stable_id;
    case OBJ_DISPATCH_TYPE: return (uint32_t)((ot_dispatch_type_obj*)ot_as_obj(value))->stable_id;
    case OBJ_GENERIC: return (uint32_t)((ot_generic_obj*)ot_as_obj(value))->stable_id;
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
    if (old->values[i].live) {
      ot_entry* destination = &entries->values[next++];
      ot_store(state, entries, &destination->key, old->values[i].key);
      ot_store(state, entries, &destination->value, old->values[i].value);
      destination->hash = old->values[i].hash;
      destination->live = true;
    }
  ot_store(state, table, &table->entries, fresh);
  ot_store(state, table, &table->index, fresh_index);
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
      ot_entries_obj* entries = as_entries(object->entries);
      ot_store(state, entries, &entry->key, ot_nil);
      ot_store(state, entries, &entry->value, ot_nil);
      object->length--;
      ((uint32_t*)as_bytes(object->index)->data)[slot] = UINT32_MAX;
    } else {
      ot_store(state, as_entries(object->entries), &entry->value, value);
    }
    return table;
  }
  if (value == ot_nil) return table;
  table_compact_or_grow(state, &table);
  object = as_table(table);
  found = table_lookup(state, table, key, &slot);
  (void)found;
  size_t entry_index = object->used++;
  ot_entries_obj* entries = as_entries(object->entries);
  ot_entry* entry = &entries->values[entry_index];
  ot_store(state, entries, &entry->key, key);
  ot_store(state, entries, &entry->value, value);
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

static size_t utf8_encode(uint32_t scalar, char* out) {
  if (scalar < 0x80u) {
    out[0] = (char)scalar;
    return 1;
  }
  if (scalar < 0x800u) {
    out[0] = (char)(0xc0u | (scalar >> 6));
    out[1] = (char)(0x80u | (scalar & 0x3fu));
    return 2;
  }
  if (scalar < 0x10000u) {
    out[0] = (char)(0xe0u | (scalar >> 12));
    out[1] = (char)(0x80u | ((scalar >> 6) & 0x3fu));
    out[2] = (char)(0x80u | (scalar & 0x3fu));
    return 3;
  }
  out[0] = (char)(0xf0u | (scalar >> 18));
  out[1] = (char)(0x80u | ((scalar >> 12) & 0x3fu));
  out[2] = (char)(0x80u | ((scalar >> 6) & 0x3fu));
  out[3] = (char)(0x80u | (scalar & 0x3fu));
  return 4;
}

/* Decode the scalar starting at offset, trusting the string's encoding. */
static uint32_t utf8_decode(const unsigned char* bytes, size_t length, size_t offset) {
  unsigned char byte = bytes[offset];
  if (byte < 0x80u) return byte;
  size_t width = byte < 0xe0u ? 2 : byte < 0xf0u ? 3 : 4;
  uint32_t scalar = byte & (width == 2 ? 0x1fu : width == 3 ? 0x0fu : 0x07u);
  for (size_t i = 1; i < width && offset + i < length; i++)
    scalar = (scalar << 6) | (bytes[offset + i] & 0x3fu);
  return scalar;
}

static const struct {
  const char* name;
  uint32_t scalar;
} char_names[] = {{"space", ' '}, {"newline", '\n'}, {"tab", '\t'}, {"return", '\r'}, {"nul", 0}};

static void render_char(buf* out, uint32_t scalar, bool display) {
  char bytes[4];
  if (display) {
    buf_write(out, bytes, utf8_encode(scalar, bytes));
    return;
  }
  buf_cstr(out, "#\\");
  for (size_t i = 0; i < sizeof char_names / sizeof char_names[0]; i++)
    if (char_names[i].scalar == scalar) {
      buf_cstr(out, char_names[i].name);
      return;
    }
  if (scalar < 0x20u || scalar == 0x7fu) buf_printf(out, "x%" PRIx32, scalar);
  else buf_write(out, bytes, utf8_encode(scalar, bytes));
}

static void render_pair(ots* state, buf* out, otv value, unsigned depth) {
  buf_byte(out, '(');
  bool first = true;
  while (is_type(value, OBJ_PAIR)) {
    if (!first) buf_byte(out, ' ');
    render_value(state, out, as_pair(value)->car, false, depth + 1);
    value = as_pair(value)->cdr;
    first = false;
  }
  if (value != ot_null) {
    buf_cstr(out, " . ");
    render_value(state, out, value, false, depth + 1);
  }
  buf_byte(out, ')');
}

static void format_float(char* output, size_t capacity, double number) {
  if (isnan(number)) {
    snprintf(output, capacity, "nan");
    return;
  }
  if (isinf(number)) {
    snprintf(output, capacity, signbit(number) ? "-inf" : "+inf");
    return;
  }
  char conventional[64];
  snprintf(conventional, sizeof conventional, "%.17g", number);
  bool use_exponent = strchr(conventional, 'e') != NULL || strchr(conventional, 'E') != NULL;
  char scientific[64];
  snprintf(scientific, sizeof scientific, "%.16e", number);
  char* exponent_start = strchr(scientific, 'e');
  int exponent = exponent_start == NULL ? 0 : (int)strtol(exponent_start + 1, NULL, 10);
  for (int precision = 1; precision <= 17; precision++) {
    char candidate[64];
    if (use_exponent) {
      snprintf(candidate, sizeof candidate, "%.*e", precision - 1, number);
      char* marker = strchr(candidate, 'e');
      char* digits = marker + 1;
      char sign = *digits;
      if (sign == '+' || sign == '-') digits++;
      while (digits[0] == '0' && digits[1] != '\0') digits++;
      char normalized[64];
      size_t mantissa_length = (size_t)(marker - candidate);
      memcpy(normalized, candidate, mantissa_length);
      size_t offset = mantissa_length;
      normalized[offset++] = 'e';
      if (sign == '-') normalized[offset++] = '-';
      snprintf(normalized + offset, sizeof normalized - offset, "%s", digits);
      snprintf(candidate, sizeof candidate, "%s", normalized);
    } else {
      int decimal_places = precision - 1 - exponent;
      if (decimal_places < 0) decimal_places = 0;
      snprintf(candidate, sizeof candidate, "%.*f", decimal_places, number);
    }
    char* end = NULL;
    double parsed = strtod(candidate, &end);
    uint64_t number_bits;
    uint64_t parsed_bits;
    memcpy(&number_bits, &number, sizeof number_bits);
    memcpy(&parsed_bits, &parsed, sizeof parsed_bits);
    if (end != NULL && *end == '\0' && number_bits == parsed_bits) {
      snprintf(output, capacity, "%s", candidate);
      return;
    }
  }
  snprintf(output, capacity, "%.17g", number);
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
  if (ot_is_char(value)) {
    render_char(out, ot_char_value(value), display);
    return;
  }
  if (value == ot_eof) {
    buf_cstr(out, "#<eof>");
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
      format_float(text, sizeof text, number);
      buf_cstr(out, text);
      if (isfinite(number) && strchr(text, '.') == NULL && strchr(text, 'e') == NULL &&
          strchr(text, 'E') == NULL)
        buf_cstr(out, ".0");
      break;
    }
    case OBJ_SYMBOL: render_name(out, value, false); break;
    case OBJ_KEYWORD: render_name(out, value, true); break;
    case OBJ_STRING: render_string(out, value, display); break;
    case OBJ_PAIR: render_pair(state, out, value, depth); break;
    case OBJ_ARRAY: {
      ot_array_obj* array = as_array(value);
      buf_byte(out, '[');
      for (size_t i = 0; i < array->length; i++) {
        if (i != 0) buf_byte(out, ' ');
        render_value(state, out, as_slots(array->slots)->values[i], false, depth + 1);
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
        render_value(state, out, entries->values[i].key, false, depth + 1);
        buf_byte(out, ' ');
        render_value(state, out, entries->values[i].value, false, depth + 1);
        first = false;
      }
      buf_byte(out, '}');
      break;
    }
    case OBJ_PORT: {
      ot_port_obj* port = (ot_port_obj*)ot_as_obj(value);
      buf_cstr(out, "#<port ");
      render_value(state, out, port->name, false, depth + 1);
      buf_cstr(out, port->input ? " input>" : " output>");
      break;
    }
    case OBJ_BYTEVECTOR: {
      ot_bytevector_obj* bytes = as_bytevector(value);
      buf_cstr(out, "#u8(");
      for (size_t i = 0; i < bytes->length; i++)
        buf_printf(out, i == 0 ? "%u" : " %u", bytes->data[i]);
      buf_byte(out, ')');
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
    case OBJ_RECORD:
    case OBJ_RECORD_TYPE: {
      bool instance = is_type(value, OBJ_RECORD);
      otv descriptor = instance ? as_record(value)->type : value;
      buf_cstr(out, instance ? "#<record " : "#<record-type ");
      render_name(out, as_record_type(descriptor)->name, false);
      buf_cstr(out, ">");
      break;
    }
    case OBJ_INSTANCE: {
      otv payload = instance_payload(value);
      otv type = as_layout(as_instance(payload)->layout)->type;
      buf_cstr(out, "#<");
      render_name(out, ((ot_dispatch_type_obj*)ot_as_obj(type))->name, false);
      buf_cstr(out, ">");
      break;
    }
    case OBJ_DISPATCH_TYPE: {
      ot_dispatch_type_obj* type = (ot_dispatch_type_obj*)ot_as_obj(value);
      buf_cstr(out, "#<type ");
      render_name(out, type->owner, false);
      buf_cstr(out, "/");
      render_name(out, type->name, false);
      buf_cstr(out, ">");
      break;
    }
    case OBJ_GENERIC: {
      ot_generic_obj* generic = (ot_generic_obj*)ot_as_obj(value);
      buf_cstr(out, "#<generic ");
      render_name(out, generic->owner, false);
      buf_cstr(out, "/");
      render_name(out, generic->name, false);
      buf_cstr(out, ">");
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
 * DIAGNOSTICS
 * ========================================================================= */

/* Coded conditions use the catalog in lib/catalog/diagnostics.json. A
 * condition is built as a table, given fields, then raised; raising fills its
 * message from the code's template, replacing {key} with the key's field. */

#include "diagnostic-catalog.inc"

static const char* diagnostic_template(const char* code, const char** type) {
  for (size_t i = 0; i < sizeof ot_diagnostic_entries / sizeof *ot_diagnostic_entries; i++)
    if (strcmp(ot_diagnostic_entries[i].code, code) == 0) {
      if (type != NULL) *type = ot_diagnostic_entries[i].type;
      return ot_diagnostic_entries[i].message;
    }
  return NULL;
}

static void diagnostic_put(ots* state, otv* condition, const char* key, otv value) {
  OT_FRAME_SCOPED(state, &value);
  otv keyword = ot_intern(state, key, strlen(key), true);
  OT_FRAME_SCOPED(state, &keyword);
  ot_table_put(state, *condition, keyword, value);
}

static void diagnostic_put_string(ots* state, otv* condition, const char* key, const char* bytes,
                                  size_t length) {
  diagnostic_put(state, condition, key, ot_make_string(state, bytes, length));
}

static otv diagnostic_new(ots* state, const char* code) {
  const char* type = "error";
  if (diagnostic_template(code, &type) == NULL) abort();
  otv condition = ot_table_new(state, 8);
  OT_FRAME_SCOPED(state, &condition);
  diagnostic_put(state, &condition, "type", ot_intern(state, type, strlen(type), false));
  diagnostic_put_string(state, &condition, "code", code, strlen(code));
  return condition;
}

static otv diagnostic_raise(ots* state, otv condition) {
  OT_FRAME_SCOPED(state, &condition);
  otv code_value = ot_table_get(state, condition, ot_intern(state, "code", 4, true), ot_nil);
  const char* code_bytes;
  size_t code_length;
  char code[16] = {0};
  if (ot_string_bytes(code_value, &code_bytes, &code_length) && code_length < sizeof code)
    memcpy(code, code_bytes, code_length);
  const char* template = diagnostic_template(code, NULL);
  buf message = {0};
  for (const char* cursor = template; cursor != NULL && *cursor != '\0'; cursor++) {
    const char* close = *cursor == '{' ? strchr(cursor, '}') : NULL;
    if (close == NULL) {
      buf_byte(&message, *cursor);
      continue;
    }
    otv key = ot_intern(state, cursor + 1, (size_t)(close - cursor - 1), true);
    otv field = ot_table_get(state, condition, key, OT_UNDEFINED);
    if (field == OT_UNDEFINED) buf_write(&message, cursor, (size_t)(close - cursor + 1));
    else
      render_value(state, &message, field, is_type(field, OBJ_STRING) || is_type(field, OBJ_SYMBOL),
                   0);
    cursor = close;
  }
  diagnostic_put_string(state, &condition, "message", message.data == NULL ? "" : message.data,
                        message.length);
  buf_free(&message);
  return ot_raise_value(state, condition);
}

/* Character position, line and column (counted from one) of byte offset in
 * source, counting UTF-8 code points as the Chez runtime counts characters. */
static void source_position(const char* source, size_t offset, size_t* character, size_t* line,
                            size_t* column) {
  *character = 0;
  *line = 1;
  *column = 1;
  for (size_t i = 0; i < offset; i++) {
    unsigned char byte = (unsigned char)source[i];
    if ((byte & 0xc0) == 0x80) continue;
    (*character)++;
    if (byte == '\n') {
      (*line)++;
      *column = 1;
    } else {
      (*column)++;
    }
  }
}

static otv source_span(ots* state, const char* name, const char* source, size_t start, size_t end) {
  size_t character, line, column, end_character, end_line, end_column;
  source_position(source, start, &character, &line, &column);
  source_position(source, end, &end_character, &end_line, &end_column);
  otv span = ot_table_new(state, 5);
  OT_FRAME_SCOPED(state, &span);
  diagnostic_put_string(state, &span, "path", name, strlen(name));
  diagnostic_put(state, &span, "line", ot_make_int((int64_t)line));
  diagnostic_put(state, &span, "column", ot_make_int((int64_t)column));
  diagnostic_put(state, &span, "start", ot_make_int((int64_t)character));
  diagnostic_put(state, &span, "end", ot_make_int((int64_t)end_character));
  return span;
}

/* "2", "2 or 3", "1, 2 or 3" or "at least 2", as the Chez runtime words it. */
static void arity_text(buf* out, size_t minimum, size_t maximum, bool rest) {
  if (rest) {
    buf_printf(out, "at least %zu", minimum);
    return;
  }
  for (size_t n = minimum; n <= maximum; n++) {
    if (n > minimum) buf_cstr(out, n == maximum ? " or " : ", ");
    buf_printf(out, "%zu", n);
  }
}

static otv raise_arity(ots* state, otv name, size_t minimum, size_t maximum, bool rest,
                       size_t got) {
  OT_FRAME_SCOPED(state, &name);
  otv condition = diagnostic_new(state, "E0301");
  OT_FRAME_SCOPED(state, &condition);
  if (name == ot_nil) name = ot_make_string(state, "procedure", 9);
  diagnostic_put(state, &condition, "name", name);
  buf expected = {0};
  arity_text(&expected, minimum, maximum, rest);
  diagnostic_put_string(state, &condition, "expected", expected.data, expected.length);
  diagnostic_put(state, &condition, "got", ot_make_int((int64_t)got));
  buf label = {0};
  buf_printf(&label, "expects %.*s argument%s, called with %zu", (int)expected.length,
             expected.data, !rest && minimum == 1 && maximum == 1 ? "" : "s", got);
  diagnostic_put_string(state, &condition, "label", label.data, label.length);
  buf_free(&expected);
  buf_free(&label);
  return diagnostic_raise(state, condition);
}

/* =========================================================================
 * 4. READER
 * ========================================================================= */

typedef struct reader_token {
  size_t start, end;
  const char* kind;
} reader_token;

typedef struct reader {
  ots* state;
  const char* source;
  size_t length;
  size_t offset;
  const char* name;
  bool incomplete;
  buf* tokens;
  /* Reading a port: source is text, the bytes copied from it so far. The
   * port has consumed the first consumed of them; the rest are lookahead it
   * still holds. failed means a port error is pending. */
  otv* port;
  buf* text;
  size_t consumed;
  bool failed;
} reader;

static bool reader_fill(reader* input, size_t count);

/* Whether count bytes are available at the read offset. */
static bool reader_has(reader* input, size_t count) {
  return input->offset + count <= input->length || (input->port && reader_fill(input, count));
}

/* Tool mode keeps only byte spans here. Build managed token lists after the
 * normal reader has validated every form. Ordinary reads allocate no log. */
static void reader_token_end(reader* input, size_t start, const char* kind) {
  if (input->tokens == NULL) return;
  reader_token token = {.start = start, .end = input->offset, .kind = kind};
  buf_write(input->tokens, (const char*)&token, sizeof token);
}

static void reader_delimiter(reader* input, const char* kind) {
  size_t start = input->offset++;
  reader_token_end(input, start, kind);
}

static bool delimiter(unsigned char byte) {
  return isspace(byte) || byte == '(' || byte == ')' || byte == '[' || byte == ']' || byte == '{' ||
         byte == '}' || byte == ';' || byte == '"' || byte == ',' || byte == '\'' || byte == '`';
}

static void reader_space(reader* input) {
  while (reader_has(input, 1)) {
    unsigned char byte = (unsigned char)input->source[input->offset];
    if (isspace(byte)) {
      input->offset++;
      continue;
    }
    if (byte == ';') {
      size_t start = input->offset;
      while (reader_has(input, 1) && input->source[input->offset] != '\n' &&
             input->source[input->offset] != '\r')
        input->offset++;
      reader_token_end(input, start, "comment");
      continue;
    }
    break;
  }
}

/* Raise read error code, pointing from start to where reading stopped, or at
 * the start alone for an unclosed form or string. A message that differs from
 * the code's title, such as the C runtime's integer range, becomes a note. */
static otv reader_error_at(reader* input, const char* code, const char* message, size_t start) {
  if (input->failed) return OT_UNWIND;
  ots* state = input->state;
  otv condition = diagnostic_new(state, code);
  OT_FRAME_SCOPED(state, &condition);
  size_t end = input->incomplete ? start + 1 : (input->offset > start ? input->offset : start + 1);
  if (end > input->length) end = input->length;
  diagnostic_put(state, &condition, "span",
                 source_span(state, input->name, input->source, start, end));
  if (strcmp(message, diagnostic_template(code, NULL)) != 0) {
    otv notes = ot_array_new(state, 1);
    OT_FRAME_SCOPED(state, &notes);
    otv note = ot_make_string(state, message, strlen(message));
    ot_array_append(state, notes, note);
    diagnostic_put(state, &condition, "notes", notes);
  }
  return diagnostic_raise(state, condition);
}

static otv reader_error(reader* input, const char* code, const char* message) {
  return reader_error_at(input, code, message, input->offset);
}

static otv read_form(reader* input);

static unsigned hex_digit(unsigned char c) {
  return c >= '0' && c <= '9'   ? c - '0'
         : c >= 'a' && c <= 'f' ? c - 'a' + 10u
         : c >= 'A' && c <= 'F' ? c - 'A' + 10u
                                : 16;
}

static otv read_string(reader* input) {
  size_t start = input->offset;
  input->offset++;
  buf bytes = {0};
  while (reader_has(input, 1)) {
    unsigned char byte = (unsigned char)input->source[input->offset++];
    if (byte == '"') {
      reader_token_end(input, start, "string");
      otv value = ot_make_string(input->state, bytes.data, bytes.length);
      buf_free(&bytes);
      return value;
    }
    if (byte != '\\') {
      if (byte == '\r') {
        if (reader_has(input, 1) && input->source[input->offset] == '\n') input->offset++;
        byte = '\n';
      }
      buf_byte(&bytes, (char)byte);
      continue;
    }
    if (!reader_has(input, 1)) break;
    byte = (unsigned char)input->source[input->offset++];
    switch (byte) {
      case 'x': {
        uint32_t scalar = 0;
        size_t digits = 0;
        while (reader_has(input, 1) && input->source[input->offset] != ';') {
          unsigned char c = (unsigned char)input->source[input->offset++];
          unsigned digit = hex_digit(c);
          if (digit == 16 || scalar > 0x10ffffu) {
            buf_free(&bytes);
            return reader_error(input, "E0006", "invalid hex escape");
          }
          scalar = scalar * 16u + digit;
          digits++;
        }
        if (!reader_has(input, 1)) {
          input->incomplete = true;
          buf_free(&bytes);
          return reader_error_at(input, "E0002", "unterminated hex escape", start);
        }
        input->offset++;
        if (digits == 0 || scalar > 0x10ffffu || (scalar >= 0xd800u && scalar <= 0xdfffu)) {
          buf_free(&bytes);
          return reader_error(input, "E0006", "invalid Unicode scalar");
        }
        char encoded[4];
        buf_write(&bytes, encoded, utf8_encode(scalar, encoded));
        break;
      }
      case 'n': buf_byte(&bytes, '\n'); break;
      case 't': buf_byte(&bytes, '\t'); break;
      case 'r': buf_byte(&bytes, '\r'); break;
      case '0': buf_byte(&bytes, '\0'); break;
      case 'e': buf_byte(&bytes, 27); break;
      case '"': buf_byte(&bytes, '"'); break;
      case '\\': buf_byte(&bytes, '\\'); break;
      default: buf_free(&bytes); return reader_error(input, "E0006", "unknown string escape");
    }
  }
  input->incomplete = true;
  buf_free(&bytes);
  return reader_error_at(input, "E0002", "unterminated string", start);
}

static bool token_starts_number(const char* token, size_t length) {
  size_t i = 0;
  if (i < length && (token[i] == '+' || token[i] == '-')) i++;
  return i < length && (isdigit((unsigned char)token[i]) || (token[i] == '.' && i + 1 < length));
}

static bool decimal_token(const char* token, size_t length) {
  size_t i = token[0] == '+' || token[0] == '-' ? 1 : 0;
  size_t digits = 0;
  while (i < length && isdigit((unsigned char)token[i])) {
    i++;
    digits++;
  }
  if (i < length && token[i] == '.') {
    i++;
    while (i < length && isdigit((unsigned char)token[i])) {
      i++;
      digits++;
    }
  }
  if (digits == 0) return false;
  if (i < length && (token[i] == 'e' || token[i] == 'E')) {
    i++;
    if (i < length && (token[i] == '+' || token[i] == '-')) i++;
    size_t start = i;
    while (i < length && isdigit((unsigned char)token[i])) i++;
    if (i == start) return false;
  }
  return i == length;
}

static size_t utf8_advance(const unsigned char* bytes, size_t length, size_t offset);

/* #\a, #\space or #\x41. The first character after #\ is taken even when it
 * is a delimiter, so #\( and #\  read as those characters. */
static otv read_char(reader* input) {
  input->offset += 2;
  if (!reader_has(input, 1)) {
    input->incomplete = true;
    return reader_error(input, "E0011", "character literal without a character");
  }
  reader_has(input, 4);
  size_t start = input->offset;
  input->offset = utf8_advance((const unsigned char*)input->source, input->length, start);
  while (reader_has(input, 1) && !delimiter((unsigned char)input->source[input->offset]))
    input->offset++;
  const unsigned char* source = (const unsigned char*)input->source;
  size_t length = input->offset - start;
  if (utf8_advance(source, input->length, start) == input->offset)
    return ot_make_char(utf8_decode(source, input->length, start));
  for (size_t i = 0; i < sizeof char_names / sizeof char_names[0]; i++)
    if (strlen(char_names[i].name) == length &&
        memcmp(char_names[i].name, source + start, length) == 0)
      return ot_make_char(char_names[i].scalar);
  if (source[start] == 'x') {
    uint32_t scalar = 0;
    for (size_t i = start + 1; i < input->offset; i++) {
      unsigned digit = hex_digit(source[i]);
      if (digit == 16 || scalar > 0x10ffffu)
        return reader_error(input, "E0011", "invalid character literal");
      scalar = scalar * 16u + digit;
    }
    if (!ot_char_valid((intptr_t)scalar))
      return reader_error(input, "E0011", "invalid Unicode scalar");
    return ot_make_char(scalar);
  }
  return reader_error(input, "E0011", "unknown character name");
}

static otv read_atom(reader* input) {
  size_t start = input->offset;
  if (reader_has(input, 2) && input->source[input->offset] == '#' &&
      input->source[input->offset + 1] == '\\') {
    otv value = read_char(input);
    reader_token_end(input, start, "atom");
    return value;
  }
  while (reader_has(input, 1) && !delimiter((unsigned char)input->source[input->offset]))
    input->offset++;
  reader_token_end(input, start, "atom");
  const char* token = input->source + start;
  size_t length = input->offset - start;
  if (length == 0) return reader_error(input, "E0010", "expected a value");
  if (length == 3 && memcmp(token, "nil", 3) == 0) return ot_nil;
  if ((length == 2 && memcmp(token, "#t", 2) == 0) ||
      (length == 5 && memcmp(token, "#true", 5) == 0))
    return ot_true;
  if ((length == 2 && memcmp(token, "#f", 2) == 0) ||
      (length == 6 && memcmp(token, "#false", 6) == 0))
    return ot_false;
  if (token[0] == '#') return reader_error(input, "E0005", "reserved # token");
  if (token[0] == ':') {
    if (length == 1) return reader_error(input, "E0008", "empty keyword");
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
        if ((intmax_t)narrowed != integer || !ot_int_fits(narrowed))
          return reader_error(input, "E0004", "integer out of range");
        return ot_make_int(narrowed);
      }
    } else {
      if (!decimal_token(token, length)) {
        buf_free(&text);
        return reader_error(input, "E0004", "invalid number");
      }
      intmax_t integer = strtoimax(text.data, &end, 10);
      if (errno == 0 && end == text.data + length && strchr(text.data, '.') == NULL &&
          strchr(text.data, 'e') == NULL && strchr(text.data, 'E') == NULL) {
        buf_free(&text);
        intptr_t narrowed = (intptr_t)integer;
        if ((intmax_t)narrowed != integer || !ot_int_fits(narrowed))
          return reader_error(input, "E0004", "integer out of range");
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
    return reader_error(input, "E0004", "invalid number");
  }
  return ot_intern(input->state, token, length, false);
}

static otv read_sequence(reader* input, char closing, otv prefix, size_t opening_length) {
  size_t start = input->offset;
  input->offset += opening_length;
  reader_token_end(input, start, "open");
  otv head = ot_null;
  otv tail = ot_nil;
  otv item = ot_nil;
  OT_FRAME_SCOPED(input->state, &head, &tail, &item, &prefix);
  if (prefix != ot_nil) {
    head = tail = ot_cons(input->state, prefix, ot_null);
  }
  for (;;) {
    reader_space(input);
    if (!reader_has(input, 1)) {
      input->incomplete = true;
      return reader_error_at(input, "E0001", "unclosed form", start);
    }
    if (input->source[input->offset] == closing) {
      reader_delimiter(input, "close");
      return head;
    }
    if (input->source[input->offset] == '.' &&
        (!reader_has(input, 2) || delimiter((unsigned char)input->source[input->offset + 1]))) {
      if (tail == ot_nil) return reader_error(input, "E0007", "dot without a preceding value");
      reader_delimiter(input, "atom");
      reader_space(input);
      if (!reader_has(input, 1)) {
        input->incomplete = true;
        return reader_error_at(input, "E0001", "unclosed dotted pair", start);
      }
      item = read_form(input);
      if (item == OT_UNWIND) return item;
      ot_pair_obj* pair = as_pair(tail);
      ot_store(input->state, pair, &pair->cdr, item);
      reader_space(input);
      if (!reader_has(input, 1)) {
        input->incomplete = true;
        return reader_error_at(input, "E0001", "unclosed dotted pair", start);
      }
      if (input->source[input->offset] != closing)
        return reader_error(input, "E0007", "content after dotted tail");
      reader_delimiter(input, "close");
      return head;
    }
    item = read_form(input);
    if (item == OT_UNWIND) return item;
    otv cell = ot_cons(input->state, item, ot_null);
    if (head == ot_null) {
      head = cell;
      tail = cell;
    } else {
      ot_pair_obj* pair = as_pair(tail);
      ot_store(input->state, pair, &pair->cdr, cell);
      tail = cell;
    }
  }
}

static otv read_quote(reader* input, const char* name, size_t name_length, size_t prefix_length) {
  size_t start = input->offset;
  input->offset += prefix_length;
  reader_token_end(input, start, "prefix");
  reader_space(input);
  if (!reader_has(input, 1)) {
    input->incomplete = true;
    return reader_error(input, "E0009", "quote without a value");
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
  if (!reader_has(input, 1)) return OT_UNDEFINED;
  char byte = input->source[input->offset];
  switch (byte) {
    case '(': return read_sequence(input, ')', ot_nil, 1);
    case '[': return read_sequence(input, ']', ot_intern(input->state, "array", 5, false), 1);
    case '{': return read_sequence(input, '}', ot_intern(input->state, "table", 5, false), 1);
    case '"': return read_string(input);
    case '#':
      /* #u8(...) reads as (bytevector ...), like [...] and {...}. */
      if (reader_has(input, 4) && memcmp(input->source + input->offset, "#u8(", 4) == 0) {
        return read_sequence(input, ')', ot_intern(input->state, "bytevector", 10, false), 4);
      }
      if (reader_has(input, 2) && input->source[input->offset + 1] == '`')
        return read_quote(input, "syntax-quote", 12, 2);
      return read_atom(input);
    case '\'': return read_quote(input, "quote", 5, 1);
    case '`': return read_quote(input, "quasiquote", 10, 1);
    case ',':
      if (reader_has(input, 2) && input->source[input->offset + 1] == '@')
        return read_quote(input, "unquote-splicing", 16, 2);
      return read_quote(input, "unquote", 7, 1);
    case ')':
    case ']':
    case '}':
      /* Consume the delimiter, as Chez does, so a port reader moves past it. */
      input->offset++;
      return reader_error_at(input, "E0003", "unexpected closing delimiter", input->offset - 1);
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
  ot_store(state, alias, &alias->name, name);
  ot_store(state, alias, &alias->value, value);
  ot_store(state, alias, &alias->next, next);
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
  ot_store(state, space, &space->name, name);
  ot_store(state, space, &space->vars, ot_nil);
  ot_store(state, space, &space->var_index, var_index);
  ot_store(state, space, &space->refers, ot_nil);
  ot_store(state, space, &space->refer_index, refer_index);
  ot_store(state, space, &space->aliases, ot_nil);
  ot_store(state, space, &space->next, state->namespaces);
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
    ot_namespace_obj* made_space = (ot_namespace_obj*)ot_as_obj(made);
    ot_store(state, made_space, &made_space->refers, refers);
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
    ot_store(state, var, &var->value, value);
    ot_store(state, var, &var->doc, doc);
    var->private_value = private_value;
    ot_name_obj* name_object = as_name(name);
    ot_store(state, name_object, &name_object->cache_namespace, namespace_value);
    ot_store(state, name_object, &name_object->cache_var, existing);
    return existing;
  }
  ot_namespace_obj* space = (ot_namespace_obj*)ot_as_obj(namespace_value);
  otv next = space->vars;
  OT_FRAME_SCOPED(state, &next);
  ot_var_obj* var = must_alloc(state, sizeof(*var), OBJ_VAR);
  ot_store(state, var, &var->name, name);
  ot_store(state, var, &var->value, value);
  ot_store(state, var, &var->doc, doc);
  ot_store(state, var, &var->next, next);
  var->private_value = private_value;
  made = ot_from_obj(var);
  ot_namespace_obj* namespace_object = (ot_namespace_obj*)ot_as_obj(namespace_value);
  ot_store(state, namespace_object, &namespace_object->vars, made);
  ot_table_put(state, namespace_object->var_index, name, made);
  ot_name_obj* name_object = as_name(name);
  ot_store(state, name_object, &name_object->cache_namespace, namespace_value);
  ot_store(state, name_object, &name_object->cache_var, made);
  return made;
}

typedef struct bytecode_compiler {
  buf bytes;
  otv constants;
  otv owner;
  otv captures;
  otv children;
  struct bytecode_compiler* outer;
  bool declarations_allowed;
} bytecode_compiler;

static otv make_env(ots* state, otv parent, otv namespace_value, bool compile_time,
                    size_t capacity) {
  OT_FRAME_SCOPED(state, &parent, &namespace_value);
  otv data = compile_time ? ot_nil : make_slots(state, capacity == 0 ? 1 : capacity);
  OT_FRAME_SCOPED(state, &data);
  ot_env_obj* env = must_alloc(state, sizeof(*env), OBJ_ENV);
  ot_store(state, env, &env->parent, parent);
  ot_store(state, env, &env->data, data);
  ot_store(state, env, &env->namespace_value, namespace_value);
  env->slot_count = 0;
  env->compile_time = compile_time;
  otv owner = ot_nil;
  if (compile_time) {
    if (is_type(parent, OBJ_ENV)) owner = ((ot_env_obj*)ot_as_obj(parent))->compile_owner;
    else if (state->compiler != NULL) owner = ((bytecode_compiler*)state->compiler)->owner;
  }
  ot_store(state, env, &env->compile_owner, owner);
  ot_store(state, env, &env->layout, ot_nil);
  return ot_from_obj(env);
}

static void env_bind(ots* state, otv* env_value, otv name, otv value) {
  OT_FRAME_SCOPED(state, env_value, &name, &value);
  ot_env_obj* env = (ot_env_obj*)ot_as_obj(*env_value);
  size_t slot = env->slot_count;
  if (env->compile_time) {
    otv next = env->data;
    OT_FRAME_SCOPED(state, &next);
    ot_binding_obj* binding = must_alloc(state, sizeof(*binding), OBJ_BINDING);
    env = (ot_env_obj*)ot_as_obj(*env_value);
    otv owner = env->compile_owner;
    OT_FRAME_SCOPED(state, &owner);
    binding->slot = ((ot_env_obj*)ot_as_obj(owner))->slot_count++;
    binding->assigned = false;
    binding->captured = false;
    binding->recursive = false;
    ot_store(state, binding, &binding->name, name);
    ot_store(state, binding, &binding->value, value);
    ot_store(state, binding, &binding->next, next);
    env = (ot_env_obj*)ot_as_obj(*env_value);
    ot_store(state, env, &env->data, ot_from_obj(binding));
    otv binding_value = ot_from_obj(binding);
    OT_FRAME_SCOPED(state, &binding_value);
    otv layout = ((ot_env_obj*)ot_as_obj(owner))->layout;
    OT_FRAME_SCOPED(state, &layout);
    array_push(state, &layout, binding_value);
    return;
  }
  otv slots = env->data;
  OT_FRAME_SCOPED(state, &slots);
  ot_slots_obj* slot_values = as_slots(slots);
  if (slot == slot_values->capacity) {
    otv grown = make_slots(state, slot_values->capacity * 2);
    OT_FRAME_SCOPED(state, &grown);
    slot_values = as_slots(slots);
    ot_slots_obj* replacement = as_slots(grown);
    for (size_t i = 0; i < slot; i++)
      ot_store(state, replacement, &replacement->values[i], slot_values->values[i]);
    slots = grown;
  }
  slot_values = as_slots(slots);
  ot_store(state, slot_values, &slot_values->values[slot], value);
  env = (ot_env_obj*)ot_as_obj(*env_value);
  ot_store(state, env, &env->data, slots);
  env->slot_count++;
}

static otv env_binding(otv env_value, otv name) {
  for (otv env_cursor = env_value; is_type(env_cursor, OBJ_ENV);
       env_cursor = ((ot_env_obj*)ot_as_obj(env_cursor))->parent) {
    ot_env_obj* env = (ot_env_obj*)ot_as_obj(env_cursor);
    for (otv cursor = env->compile_time ? env->data : ot_nil; is_type(cursor, OBJ_BINDING);
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
  ot_name_obj* cached = as_name(symbol);
  if (cached->cache_namespace == namespace_value && is_type(cached->cache_var, OBJ_VAR) &&
      memchr(cached->bytes, '/', cached->length) == NULL)
    return cached->cache_var;
  OT_FRAME_SCOPED(state, &namespace_value, &symbol);
  ot_name_obj* name = as_name(symbol);
  const char* slash = memchr(name->bytes, '/', name->length);
  if (slash != NULL && slash != name->bytes && slash != name->bytes + name->length - 1) {
    size_t prefix_length = (size_t)(slash - name->bytes);
    otv prefix = name->qualified_prefix;
    otv member = name->qualified_member;
    OT_FRAME_SCOPED(state, &prefix, &member);
    if (prefix == ot_nil) {
      prefix = ot_intern(state, name->bytes, prefix_length, false);
      name = as_name(symbol);
      member = ot_intern(state, name->bytes + prefix_length + 1, name->length - prefix_length - 1,
                         false);
      name = as_name(symbol);
      ot_store(state, name, &name->qualified_prefix, prefix);
      ot_store(state, name, &name->qualified_member, member);
    }
    otv target_name = namespace_alias(namespace_value, prefix);
    if (target_name == ot_nil) target_name = prefix;
    otv target = namespace_find(state, target_name, false);
    /* Qualified names reach private definitions, so macro expansions can. */
    otv var = namespace_var(state, target, member, false);
    if (is_type(var, OBJ_VAR)) return var;
  } else {
    if (name->cache_namespace == namespace_value && is_type(name->cache_var, OBJ_VAR))
      return name->cache_var;
    otv var = namespace_var(state, namespace_value, symbol, true);
    if (is_type(var, OBJ_VAR)) {
      ot_store(state, name, &name->cache_namespace, namespace_value);
      ot_store(state, name, &name->cache_var, var);
      return var;
    }
  }
  if (report_error) {
    otv condition = diagnostic_new(state, "E0201");
    OT_FRAME_SCOPED(state, &condition, &symbol);
    name = as_name(symbol);
    diagnostic_put_string(state, &condition, "name", name->bytes, name->length);
    return diagnostic_raise(state, condition);
  }
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

static otv raise_message(ots* state, const char* message, size_t length);

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
  otv result = message.data == NULL ? raise_message(state, "error", 5)
                                    : raise_message(state, message.data, message.length);
  buf_free(&message);
  return result;
}

/* Natives report a wrong argument type as "who: expected ...". Those become
 * E0401, worded "`who` expected ..." as on Chez; special forms are excluded. */
static otv raise_message(ots* state, const char* message, size_t length) {
  const char* colon = memchr(message, ':', length);
  static const char* const forms[] = {
      "require",  "ns",         "def",        "def-",    "defonce",
      "defmacro", "let",        "fn",         "set!",    "cond",
      "try",      "quote",      "quasiquote", "unquote", "unquote-splicing",
      "compiler", "macroexpand"};
  bool special = false;
  if (colon != NULL)
    for (size_t i = 0; i < sizeof forms / sizeof *forms; i++)
      if (strlen(forms[i]) == (size_t)(colon - message) &&
          memcmp(forms[i], message, (size_t)(colon - message)) == 0)
        special = true;
  if (colon == NULL || special || colon == message ||
      memchr(message, ' ', (size_t)(colon - message)) || (size_t)(colon - message) + 11 > length ||
      memcmp(colon, ": expected ", 11) != 0) {
    otv text = ot_make_string(state, message, length);
    OT_FRAME_SCOPED(state, &text);
    otv condition = ot_table_new(state, 4);
    OT_FRAME_SCOPED(state, &condition);
    diagnostic_put(state, &condition, "type", ot_intern(state, "error", 5, false));
    diagnostic_put(state, &condition, "message", text);
    return ot_raise_value(state, condition);
  }
  const char* expected = colon + 11;
  size_t expected_length = length - (size_t)(expected - message);
  const char* argument = NULL;
  for (size_t i = 0; i + 9 <= expected_length; i++)
    if (memcmp(expected + i, " argument", 9) == 0) {
      argument = expected + i;
      break;
    }
  if (argument != NULL) {
    /* "who: expected 2 or 3 arguments" is an arity error. */
    otv condition = diagnostic_new(state, "E0301");
    OT_FRAME_SCOPED(state, &condition);
    diagnostic_put_string(state, &condition, "name", message, (size_t)(colon - message));
    diagnostic_put_string(state, &condition, "expected", expected, (size_t)(argument - expected));
    buf label = {0};
    buf_printf(&label, "expects %.*s", (int)(expected_length), expected);
    diagnostic_put_string(state, &condition, "label", label.data, label.length);
    buf_free(&label);
    return diagnostic_raise(state, condition);
  }
  otv condition = diagnostic_new(state, "E0401");
  OT_FRAME_SCOPED(state, &condition);
  buf detail = {0};
  buf_printf(&detail, "`%.*s` %.*s", (int)(colon - message), message,
             (int)(length - (size_t)(colon - message) - 2), colon + 2);
  diagnostic_put_string(state, &condition, "detail", detail.data, detail.length);
  diagnostic_put_string(state, &condition, "who", message, (size_t)(colon - message));
  buf_free(&detail);
  return diagnostic_raise(state, condition);
}

static otv compile_function_code(ots* state, otv params, otv body, otv env, otv namespace_value,
                                 otv name) {
  OT_FRAME_SCOPED(state, &params, &body, &env, &namespace_value, &name);
  otv compile_env = make_env(state, env, namespace_value, true, 0);
  OT_FRAME_SCOPED(state, &compile_env);
  ot_env_obj* owner = (ot_env_obj*)ot_as_obj(compile_env);
  ot_store(state, owner, &owner->compile_owner, compile_env);
  otv layout = ot_array_new(state, 8);
  owner = (ot_env_obj*)ot_as_obj(compile_env);
  ot_store(state, owner, &owner->layout, layout);
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
  return compile_forms(state, body, compile_env, params, name);
}

static otv make_restart(ots* state, const char* name_text, const char* description_text) {
  otv name = ot_intern(state, name_text, strlen(name_text), false);
  otv description = ot_nil;
  OT_FRAME_SCOPED(state, &name, &description);
  description = ot_make_string(state, description_text, strlen(description_text));
  ot_restart_obj* restart = must_alloc(state, sizeof(*restart), OBJ_RESTART);
  ot_store(state, restart, &restart->name, name);
  ot_store(state, restart, &restart->description, description);
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
  if (action == OT_INTERRUPT_ERROR) return ot_raise(state, "evaluation aborted");
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

static otv local_value(otv value) {
  return is_type(value, OBJ_CELL) ? ((ot_cell_obj*)ot_as_obj(value))->value : value;
}

static otv make_cell(ots* state, otv value) {
  OT_FRAME_SCOPED(state, &value);
  ot_cell_obj* cell = must_alloc(state, sizeof(*cell), OBJ_CELL);
  ot_store(state, cell, &cell->value, value);
  return ot_from_obj(cell);
}

static bool local_boxed(otv code, size_t slot) {
  return ot_get_int(ot_array_get(as_code(code)->locals, slot, ot_make_int(0))) != 0;
}

static void initialize_local(ots* state, otv code, otv* env, size_t slot, otv value) {
  OT_FRAME_SCOPED(state, &code, env, &value);
  if (local_boxed(code, slot)) value = make_cell(state, value);
  ot_slots_obj* slots = as_slots(((ot_env_obj*)ot_as_obj(*env))->data);
  ot_store(state, slots, &slots->values[slot], value);
}

/* The arity error for calling the interpreted function with argc arguments. */
static otv raise_function_arity(ots* state, otv function_value, size_t argc) {
  OT_FRAME_SCOPED(state, &function_value);
  otv code = ((ot_function_obj*)ot_as_obj(function_value))->code;
  size_t fixed = as_code(code)->fixed_arity;
  if (fixed != SIZE_MAX) return raise_arity(state, as_code(code)->name, fixed, fixed, false, argc);
  otv params = as_code(code)->params;
  if (is_type(params, OBJ_PAIR) && symbol_is(as_pair(params)->car, "array"))
    params = as_pair(params)->cdr;
  size_t minimum = 0;
  while (is_type(params, OBJ_PAIR) && !symbol_is(as_pair(params)->car, "&")) {
    minimum++;
    params = as_pair(params)->cdr;
  }
  return raise_arity(state, as_code(code)->name, minimum, minimum, params != ot_null, argc);
}

static otv bind_parameters(ots* state, otv function_value, otv* args, size_t argc) {
  OT_FRAME_SCOPED(state, &function_value);
  ot_function_obj* function = (ot_function_obj*)ot_as_obj(function_value);
  otv code = function->code;
  otv namespace_value = function->namespace_value;
  otv params = as_code(code)->params;
  otv env = ot_nil;
  OT_FRAME_SCOPED(state, &code, &namespace_value, &params, &env);
  size_t count = as_code(code)->local_count;
  size_t fixed = as_code(code)->fixed_arity;
  if (fixed != SIZE_MAX && fixed != argc) return raise_function_arity(state, function_value, argc);
  env = make_env(state, ot_nil, namespace_value, false, count);
  ((ot_env_obj*)ot_as_obj(env))->slot_count = count;
  if (fixed != SIZE_MAX) {
    for (size_t i = 0; i < argc; i++) initialize_local(state, code, &env, i, args[i]);
    return env;
  }
  if (is_type(params, OBJ_PAIR) && symbol_is(as_pair(params)->car, "array"))
    params = as_pair(params)->cdr;
  size_t index = 0;
  while (is_type(params, OBJ_PAIR)) {
    otv name = as_pair(params)->car;
    if (symbol_is(name, "&")) {
      params = as_pair(as_pair(params)->cdr)->car;
      break;
    }
    if (index == argc) return raise_function_arity(state, function_value, argc);
    params = as_pair(params)->cdr;
    initialize_local(state, code, &env, index, args[index]);
    index++;
  }
  if (is_type(params, OBJ_SYMBOL)) {
    otv rest = list_from_array(state, args == NULL ? NULL : args + index, argc - index);
    initialize_local(state, code, &env, index, rest);
  } else if (params != ot_null || index != argc)
    return raise_function_arity(state, function_value, argc);
  return env;
}

/* =========================================================================
 * 7. BYTECODE COMPILER, VM, AND SPECIAL FORMS
 * ========================================================================= */

/* Bytecode uses one-byte opcodes and compact, byte-order-independent binary
 * operands. Constants, including nested code and opcode descriptors, live in a
 * separate pool. */
#define OT_BYTECODE_OPS(X)                                                                         \
  X(BC_CONST, 'A', vm_load, true)                                                                  \
  X(BC_GET_LEXICAL, 'B', vm_load, true)                                                            \
  X(BC_GET_GLOBAL, 'C', vm_load, true)                                                             \
  X(BC_CALL, 'E', vm_call, true)                                                                   \
  X(BC_TAIL_CALL, 'F', vm_call, true)                                                              \
  X(BC_POP, 'G', vm_pop, false)                                                                    \
  X(BC_RETURN, 'H', vm_return, false)                                                              \
  X(BC_JUMP, 'J', vm_jump, true)                                                                   \
  X(BC_JUMP_IF_FALSE, 'K', vm_conditional_jump, true)                                              \
  X(BC_DUP, 'L', vm_dup, false)                                                                    \
  X(BC_JUMP_IF_TRUE, 'M', vm_conditional_jump, true)                                               \
  X(BC_BIND, 'O', vm_bind, true)                                                                   \
  X(BC_SET_LEXICAL, 'Q', vm_set, true)                                                             \
  X(BC_SET_GLOBAL, 'R', vm_set, true)                                                              \
  X(BC_CLOSURE, 'S', vm_closure, true)                                                             \
  X(BC_DEFINE_LEXICAL, 'T', vm_define, true)                                                       \
  X(BC_DEFINE_GLOBAL, 'U', vm_define, true)                                                        \
  X(BC_DEFINE_PRIVATE, 'V', vm_define, true)                                                       \
  X(BC_MACRO, 'W', vm_macro, false)                                                                \
  X(BC_DEF_PARAM, 'X', vm_def_param, true)                                                         \
  X(BC_NAMED_LET, 'Y', vm_named_let, true)                                                         \
  X(BC_TAIL_NAMED_LET, 'Z', vm_named_let, true)                                                    \
  X(BC_TRY, '!', vm_dynamic, true)                                                                 \
  X(BC_HANDLER_BIND, '"', vm_dynamic, true)                                                        \
  X(BC_RESTART_CASE, '#', vm_dynamic, true)                                                        \
  X(BC_UNWIND_PROTECT, '$', vm_dynamic, true)                                                      \
  X(BC_WITH_PARAMS, '%', vm_dynamic, true)                                                         \
  X(BC_IN_NS, '&', vm_namespace, true)                                                             \
  X(BC_REQUIRE, '\'', vm_namespace, true)                                                          \
  X(BC_CONS, '(', vm_constructor, false)                                                           \
  X(BC_APPEND, ')', vm_constructor, false)                                                         \
  X(BC_GENERIC_DECL, '*', vm_namespace, true)                                                      \
  X(BC_GET_CAPTURE, 0x81, vm_capture, true)                                                        \
  X(BC_SET_CAPTURE, 0x82, vm_capture, true)                                                        \
  X(BC_INIT_LOCAL, 0x83, vm_init_local, true)                                                      \
  X(BC_CLEAR_LOCAL, 0x84, vm_clear_local, true)                                                    \
  X(BC_INTRINSIC, 0x80, vm_intrinsic, true)

#define OT_DECLARE_OPCODE(NAME, VALUE, LABEL, OPERAND) NAME = VALUE,

typedef enum bytecode_op { OT_BYTECODE_OPS(OT_DECLARE_OPCODE) } bytecode_op;

#undef OT_DECLARE_OPCODE

#ifndef OT_COMPUTED_GOTO
#if defined(__GNUC__) || defined(__clang__)
#define OT_COMPUTED_GOTO 1
#else
#define OT_COMPUTED_GOTO 0
#endif
#endif

static void bytecode_emit(bytecode_compiler* compiler, bytecode_op op) {
  buf_byte(&compiler->bytes, (char)op);
}

static void bytecode_emit_u16(bytecode_compiler* compiler, size_t value) {
  if (value > UINT16_MAX) {
    compiler->bytes.failed = true;
    return;
  }
  buf_byte(&compiler->bytes, (char)((value >> 8) & 0xff));
  buf_byte(&compiler->bytes, (char)(value & 0xff));
}

static size_t bytecode_emit_jump(bytecode_compiler* compiler, bytecode_op op) {
  bytecode_emit(compiler, op);
  size_t operand = compiler->bytes.length;
  bytecode_emit_u16(compiler, 0);
  return operand;
}

static void bytecode_patch_u16(bytecode_compiler* compiler, size_t operand, size_t value) {
  if (value > UINT16_MAX || operand > compiler->bytes.length ||
      compiler->bytes.length - operand < 2) {
    compiler->bytes.failed = true;
    return;
  }
  compiler->bytes.data[operand] = (char)((value >> 8) & 0xff);
  compiler->bytes.data[operand + 1] = (char)(value & 0xff);
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
static otv generic_error(ots* state, const char* code, const char* message);
static bool compile_generic_declaration(ots* state, bytecode_compiler* compiler, otv form, otv env);
static otv generic_select(ots* state, otv generic, otv* args, size_t argc);
static bool class_subtype(otv child, otv parent);
static otv invoke_method_chain(ots* state, otv context, otv* args, int argc);
static otv cached_read_call(ots* state, otv cache, otv* args, int argc);
static otv cached_write_call(ots* state, otv cache, otv* args, int argc);
static otv nat_set_applied(ots* state, otv* args, int argc);
static otv execute_generic_declaration(ots* state, otv descriptor);
static otv instance_read(ots* state, otv object, otv key);
static otv instance_write(ots* state, otv object, otv key, otv value);
static otv instance_copy(ots* state, otv object);
static otv nat_add(ots* state, otv* args, int argc);
static otv nat_subtract(ots* state, otv* args, int argc);
static otv nat_num_equal(ots* state, otv* args, int argc);
static otv nat_less(ots* state, otv* args, int argc);

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

static otv descriptor_value(otv descriptor, size_t index);

static int capture_binding(ots* state, bytecode_compiler* compiler, otv binding, otv owner) {
  OT_FRAME_SCOPED(state, &binding, &owner);
  ot_array_obj* captures = as_array(compiler->captures);
  for (size_t i = 0; i < captures->length; i++)
    if (descriptor_value(ot_array_get(compiler->captures, i, ot_nil), 0) == binding) return (int)i;
  if (compiler->outer == NULL) return -1;
  size_t source;
  if (compiler->outer->owner == owner) source = ((ot_binding_obj*)ot_as_obj(binding))->slot;
  else {
    int index = capture_binding(state, compiler->outer, binding, owner);
    if (index < 0) return -1;
    source = 65536u + (size_t)index;
  }
  ((ot_binding_obj*)ot_as_obj(binding))->captured = true;
  otv fields[] = {binding, ot_make_int((intptr_t)source)};
  otv descriptor = make_descriptor(state, fields, 2);
  size_t index = as_array(compiler->captures)->length;
  array_push(state, &compiler->captures, descriptor);
  if (index > UINT16_MAX) return -1;
  return (int)index;
}

static int lexical_location(ots* state, otv env, otv name, size_t* location) {
  bytecode_compiler* compiler = state->compiler;
  for (otv scope = env; is_type(scope, OBJ_ENV); scope = ((ot_env_obj*)ot_as_obj(scope))->parent) {
    ot_env_obj* lexical = (ot_env_obj*)ot_as_obj(scope);
    for (otv cursor = lexical->data; is_type(cursor, OBJ_BINDING);
         cursor = ((ot_binding_obj*)ot_as_obj(cursor))->next) {
      ot_binding_obj* binding = (ot_binding_obj*)ot_as_obj(cursor);
      if (binding->name != name) continue;
      if (lexical->compile_owner == compiler->owner) {
        *location = binding->slot;
        if (*location <= UINT16_MAX) return 1;
      } else {
        int index = capture_binding(state, compiler, cursor, lexical->compile_owner);
        if (index >= 0) {
          *location = (size_t)index;
          return 2;
        }
      }
      (void)ot_raise(state, "compiler: too many lexical bindings");
      return -1;
    }
  }
  return 0;
}

static ot_nat intrinsic_function(otv name) {
  if (symbol_is(name, "+")) return nat_add;
  if (symbol_is(name, "-")) return nat_subtract;
  if (symbol_is(name, "=")) return nat_num_equal;
  if (symbol_is(name, "<")) return nat_less;
  return NULL;
}

static int compile_intrinsic(ots* state, bytecode_compiler* compiler, otv form, otv env,
                             bool tail) {
  otv head = as_pair(form)->car;
  if (!is_type(head, OBJ_SYMBOL) || is_type(env_binding(env, head), OBJ_BINDING)) return 0;
  ot_nat implementation = intrinsic_function(head);
  if (implementation == NULL) return 0;
  otv args = as_pair(form)->cdr;
  if (!proper_list(args) || list_length(args) != 2) return 0;
  otv var = resolve_var(state, env_namespace(state, env), head, false);
  if (!is_type(var, OBJ_VAR)) return 0;
  otv expected = ((ot_var_obj*)ot_as_obj(var))->value;
  if (!is_type(expected, OBJ_NAT) || ((ot_nat_obj*)ot_as_obj(expected))->function != implementation)
    return 0;
  otv left = as_pair(args)->car;
  otv right = as_pair(as_pair(args)->cdr)->car;
  OT_FRAME_SCOPED(state, &compiler->constants, &form, &env, &head, &args, &var, &expected, &left,
                  &right);
  if (!compile_expression(state, compiler, left, env, false) ||
      !compile_expression(state, compiler, right, env, false))
    return -1;
  otv fields[] = {head, expected, tail ? ot_true : ot_false};
  otv descriptor = make_descriptor(state, fields, 3);
  bytecode_emit_constant(state, compiler, BC_INTRINSIC, descriptor);
  return compiler->bytes.failed ? -1 : 1;
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
    else {
      ot_pair_obj* pair = as_pair(tail);
      ot_store(state, pair, &pair->cdr, cell);
    }
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
    otv scan = forms;
    OT_FRAME_SCOPED(state, &scan);
    for (; is_type(scan, OBJ_PAIR); scan = as_pair(scan)->cdr) {
      otv definition = as_pair(scan)->car;
      if (!is_type(definition, OBJ_PAIR)) continue;
      otv definition_head = as_pair(definition)->car;
      if (!is_type(definition_head, OBJ_SYMBOL) ||
          (!symbol_is(definition_head, "def") && !symbol_is(definition_head, "def-")))
        continue;
      otv definition_args = as_pair(definition)->cdr;
      if (!is_type(definition_args, OBJ_PAIR)) continue;
      otv definition_name = as_pair(definition_args)->car;
      OT_FRAME_SCOPED(state, &definition_name);
      if (is_type(definition_name, OBJ_PAIR)) definition_name = as_pair(definition_name)->car;
      if (is_type(definition_name, OBJ_SYMBOL)) {
        otv existing = ot_nil;
        for (otv entry = ((ot_env_obj*)ot_as_obj(env))->data; is_type(entry, OBJ_BINDING);
             entry = ((ot_binding_obj*)ot_as_obj(entry))->next)
          if (((ot_binding_obj*)ot_as_obj(entry))->name == definition_name) {
            existing = entry;
            break;
          }
        if (existing == ot_nil) {
          env_bind(state, &env, definition_name, ot_nil);
          existing = env_binding(env, definition_name);
          ((ot_binding_obj*)ot_as_obj(existing))->recursive = true;
          bytecode_emit(compiler, BC_INIT_LOCAL);
          bytecode_emit_u16(compiler, ((ot_binding_obj*)ot_as_obj(existing))->slot);
        } else ((ot_binding_obj*)ot_as_obj(existing))->assigned = true;
      }
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

static bool compile_expression_inner(ots* state, bytecode_compiler* compiler, otv form, otv env,
                                     bool tail, bool declarations_allowed);

static bool compile_expression(ots* state, bytecode_compiler* compiler, otv form, otv env,
                               bool tail) {
  bool allowed = compiler->declarations_allowed;
  compiler->declarations_allowed = false;
  bool result = compile_expression_inner(state, compiler, form, env, tail, allowed);
  compiler->declarations_allowed = allowed;
  return result;
}

static bool compile_expression_inner(ots* state, bytecode_compiler* compiler, otv form, otv env,
                                     bool tail, bool declarations_allowed) {
  OT_FRAME_SCOPED(state, &compiler->constants, &form, &env);
  if (is_type(form, OBJ_SYMBOL)) {
    size_t location;
    int lexical = lexical_location(state, env, form, &location);
    if (lexical < 0) return false;
    if (lexical) {
      bytecode_emit(compiler, lexical == 2 ? BC_GET_CAPTURE : BC_GET_LEXICAL);
      bytecode_emit_u16(compiler, location);
    } else {
      bytecode_emit_constant(state, compiler, BC_GET_GLOBAL, form);
    }
    return !compiler->bytes.failed;
  }
  if (!is_type(form, OBJ_PAIR)) {
    bytecode_emit_constant(state, compiler, BC_CONST, form);
    return !compiler->bytes.failed;
  }

  int intrinsic = compile_intrinsic(state, compiler, form, env, tail);
  if (intrinsic != 0) return intrinsic > 0;

  otv head = as_pair(form)->car;
  OT_FRAME_SCOPED(state, &head);
  bool generic_form = is_type(head, OBJ_SYMBOL) &&
                      (symbol_is(head, "defclass") || symbol_is(head, "defabstract") ||
                       symbol_is(head, "defgeneric") || symbol_is(head, "defmethod") ||
                       symbol_is(head, "reset-class!") || symbol_is(head, "reset-generic!"));
  if (is_type(head, OBJ_SYMBOL) && as_name(head)->special_form &&
      !(generic_form && is_type(env_binding(env, head), OBJ_BINDING))) {
    otv args = as_pair(form)->cdr;
    OT_FRAME_SCOPED(state, &args);
    if (symbol_is(head, "defclass") || symbol_is(head, "defabstract") ||
        symbol_is(head, "defgeneric") || symbol_is(head, "defmethod") ||
        symbol_is(head, "reset-class!") || symbol_is(head, "reset-generic!")) {
      if (!declarations_allowed) {
        generic_error(state, "invalid-declaration", "declarations require top-level context");
        return false;
      }
      return compile_generic_declaration(state, compiler, form, env);
    }
    if (symbol_is(head, "quote")) {
      otv value;
      if (!list_nth(args, 0, &value) || list_tail(args, 1) != ot_null) {
        (void)ot_raise(state, "quote: expected one argument");
        return false;
      }
      bytecode_emit_constant(state, compiler, BC_CONST, value);
      return !compiler->bytes.failed;
    }
    if (symbol_is(head, "fn")) {
      otv params;
      if (!list_nth(args, 0, &params)) {
        (void)ot_raise(state, "fn: missing parameter list");
        return false;
      }
      otv body = list_tail(args, 1);
      OT_FRAME_SCOPED(state, &params, &body);
      otv code = compile_function_code(state, params, body, env, env_namespace(state, env), ot_nil);
      if (code == OT_UNWIND) return false;
      bytecode_emit_constant(state, compiler, BC_CLOSURE, code);
      return !compiler->bytes.failed;
    }
    if (symbol_is(head, "defonce") && !is_type(env, OBJ_ENV)) {
      otv name;
      if (list_nth(args, 0, &name) && is_type(name, OBJ_SYMBOL) &&
          is_type(namespace_var(state, env_namespace(state, env), name, false), OBJ_VAR)) {
        bytecode_emit_constant(state, compiler, BC_CONST, ot_nil);
        return !compiler->bytes.failed;
      }
    }
    if (symbol_is(head, "def") || symbol_is(head, "def-") || symbol_is(head, "defonce")) {
      otv target;
      if (!list_nth(args, 0, &target)) {
        (void)ot_raise(state, "def: missing name");
        return false;
      }
      otv name = target;
      otv doc = ot_nil;
      otv value_form = ot_nil;
      OT_FRAME_SCOPED(state, &target, &name, &doc, &value_form);
      if (is_type(target, OBJ_PAIR)) {
        name = as_pair(target)->car;
        if (!is_type(name, OBJ_SYMBOL)) {
          (void)ot_raise(state, "def: bad name");
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
          (void)ot_raise(state, "def: bad name");
          return false;
        }
        otv values = list_tail(args, 1);
        if (is_type(values, OBJ_PAIR) && is_type(as_pair(values)->car, OBJ_STRING) &&
            is_type(as_pair(values)->cdr, OBJ_PAIR)) {
          doc = as_pair(values)->car;
          values = as_pair(values)->cdr;
        }
        if (!is_type(values, OBJ_PAIR) || as_pair(values)->cdr != ot_null) {
          (void)ot_raise(state, "def: expected one value");
          return false;
        }
        value_form = as_pair(values)->car;
        if (!compile_expression(state, compiler, value_form, env, false)) return false;
      }
      otv local = is_type(env, OBJ_ENV) ? env_binding(env, name) : ot_nil;
      otv slot = is_type(local, OBJ_BINDING)
                     ? ot_make_int((intptr_t)((ot_binding_obj*)ot_as_obj(local))->slot)
                     : ot_nil;
      otv fields[] = {name, doc, slot};
      otv descriptor = make_descriptor(state, fields, 3);
      bytecode_emit_constant(state, compiler,
                             is_type(env, OBJ_ENV)
                                 ? BC_DEFINE_LEXICAL
                                 : (symbol_is(head, "def-") ? BC_DEFINE_PRIVATE : BC_DEFINE_GLOBAL),
                             descriptor);
      return !compiler->bytes.failed;
    }
    if (symbol_is(head, "begin")) {
      compiler->declarations_allowed = declarations_allowed;
      return compile_sequence(state, compiler, args, env, tail);
    }
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
      for (otv cursor = clauses; cursor != ot_null; cursor = as_pair(cursor)->cdr) {
        if (!is_type(cursor, OBJ_PAIR)) {
          (void)ot_raise(state, "cond: improper clauses");
          return false;
        }
        otv clause = as_pair(cursor)->car;
        if (!is_type(clause, OBJ_PAIR) || !proper_list(clause)) {
          (void)ot_raise(state, "cond: bad clause");
          return false;
        }
        otv test = as_pair(clause)->car;
        if (is_type(test, OBJ_SYMBOL) && symbol_is(test, "else") &&
            as_pair(clause)->cdr == ot_null) {
          (void)ot_raise(state, "cond: else requires a body");
          return false;
        }
      }
      size_t clause_count = list_length(clauses);
      size_t end_jumps[clause_count == 0 ? 1 : clause_count];
      size_t end_count = 0;
      while (is_type(clauses, OBJ_PAIR)) {
        otv clause = as_pair(clauses)->car;
        otv test = as_pair(clause)->car;
        otv body = as_pair(clause)->cdr;
        OT_FRAME_SCOPED(state, &clause, &test, &body);
        bool is_else = is_type(test, OBJ_SYMBOL) && symbol_is(test, "else");
        if (body == ot_null) {
          if (!compile_expression(state, compiler, test, env, false)) return false;
          bytecode_emit(compiler, BC_DUP);
          size_t false_jump = bytecode_emit_jump(compiler, BC_JUMP_IF_FALSE);
          end_jumps[end_count++] = bytecode_emit_jump(compiler, BC_JUMP);
          bytecode_patch_u16(compiler, false_jump, compiler->bytes.length);
          bytecode_emit(compiler, BC_POP);
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
        else {
          ot_pair_obj* pair = as_pair(params_tail);
          ot_store(state, pair, &pair->cdr, cell);
        }
        params_tail = cell;
        count++;
        cursor = as_pair(cursor)->cdr;
      }
      otv named_compile_env = make_env(state, env, env_namespace(state, env), true, 0);
      OT_FRAME_SCOPED(state, &named_compile_env);
      env_bind(state, &named_compile_env, name, ot_nil);
      otv loop_binding = env_binding(named_compile_env, name);
      size_t loop_slot = ((ot_binding_obj*)ot_as_obj(loop_binding))->slot;
      ((ot_binding_obj*)ot_as_obj(loop_binding))->recursive = true;
      bytecode_emit(compiler, BC_INIT_LOCAL);
      bytecode_emit_u16(compiler, loop_slot);
      otv code = compile_function_code(state, params, body, named_compile_env,
                                       env_namespace(state, env), name);
      if (code == OT_UNWIND) return false;
      otv fields[] = {name, code, ot_make_int((intptr_t)count), ot_make_int((intptr_t)loop_slot)};
      otv descriptor = make_descriptor(state, fields, 4);
      bytecode_emit_constant(state, compiler, tail ? BC_TAIL_NAMED_LET : BC_NAMED_LET, descriptor);
      if (!tail) {
        bytecode_emit(compiler, BC_CLEAR_LOCAL);
        bytecode_emit_u16(compiler, loop_slot);
      }
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
      otv let_env = make_env(state, env, env_namespace(state, env), true, 0);
      OT_FRAME_SCOPED(state, &let_env);

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
        env_bind(state, &let_env, name, ot_nil);
        otv binding_metadata = env_binding(let_env, name);
        bytecode_emit(compiler, BC_BIND);
        bytecode_emit_u16(compiler, ((ot_binding_obj*)ot_as_obj(binding_metadata))->slot);
        bindings = as_pair(bindings)->cdr;
      }
      if (!compile_sequence(state, compiler, body, let_env, tail)) return false;
      for (otv binding = ((ot_env_obj*)ot_as_obj(let_env))->data; is_type(binding, OBJ_BINDING);
           binding = ((ot_binding_obj*)ot_as_obj(binding))->next) {
        bytecode_emit(compiler, BC_CLEAR_LOCAL);
        bytecode_emit_u16(compiler, ((ot_binding_obj*)ot_as_obj(binding))->slot);
      }
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
      size_t location;
      int lexical = lexical_location(state, env, name, &location);
      if (lexical < 0) return false;
      if (lexical) {
        otv binding = env_binding(env, name);
        ((ot_binding_obj*)ot_as_obj(binding))->assigned = true;
        bytecode_emit(compiler, lexical == 2 ? BC_SET_CAPTURE : BC_SET_LEXICAL);
        bytecode_emit_u16(compiler, location);
      } else {
        bytecode_emit_constant(state, compiler, BC_SET_GLOBAL, name);
      }
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
      otv doc = ot_nil;
      OT_FRAME_SCOPED(state, &name, &params, &body, &doc);
      if (is_type(body, OBJ_PAIR) && is_type(as_pair(body)->car, OBJ_STRING) &&
          is_type(as_pair(body)->cdr, OBJ_PAIR)) {
        doc = as_pair(body)->car;
        body = as_pair(body)->cdr;
      }
      otv code = compile_function_code(state, params, body, env, env_namespace(state, env), name);
      if (code == OT_UNWIND) return false;
      bytecode_emit_constant(state, compiler, BC_CLOSURE, code);
      bytecode_emit(compiler, BC_MACRO);
      otv fields[] = {name, doc};
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
    if (symbol_is(head, "ns")) {
      otv name;
      if (!list_nth(args, 0, &name)) {
        (void)ot_raise(state, "ns: missing name");
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
        (void)ot_raise(state, "ns: invalid name");
        return false;
      }
      otv clauses = list_tail(args, 1);
      while (is_type(clauses, OBJ_PAIR)) {
        otv clause = as_pair(clauses)->car;
        if (!is_type(clause, OBJ_PAIR) || !proper_list(clause) ||
            !is_type(as_pair(clause)->car, OBJ_KEYWORD)) {
          (void)ot_raise(state, "ns: bad clause");
          return false;
        }
        ot_name_obj* clause_name = as_name(as_pair(clause)->car);
        if (clause_name->length != 7 || memcmp(clause_name->bytes, "require", 7) != 0) {
          (void)ot_raise(state, "ns: unknown clause");
          return false;
        }
        clauses = as_pair(clauses)->cdr;
      }
      if (clauses != ot_null) {
        (void)ot_raise(state, "ns: improper clauses");
        return false;
      }
      bytecode_emit_constant(state, compiler, BC_IN_NS, name);
      clauses = list_tail(args, 1);
      OT_FRAME_SCOPED(state, &clauses);
      while (is_type(clauses, OBJ_PAIR)) {
        otv clause = as_pair(clauses)->car;
        bytecode_emit(compiler, BC_POP);
        bytecode_emit_constant(state, compiler, BC_REQUIRE, as_pair(clause)->cdr);
        clauses = as_pair(clauses)->cdr;
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
  ot_store(state, code, &code->bytes, byte_values);
  ot_store(state, code, &code->constants, constants);
  ot_store(state, code, &code->params, params);
  ot_store(state, code, &code->name, name);
  bytecode_compiler* compiler = state->compiler;
  ot_store(state, code, &code->locals, ((ot_env_obj*)ot_as_obj(compiler->owner))->layout);
  ot_store(state, code, &code->captures, compiler->captures);
  ot_store(state, code, &code->children, compiler->children);
  code->local_count = as_array(code->locals)->length;
  code->boxed_parameters = false;
  code->length = bytes->length;
  code->constant_count = as_array(constants)->length;
  code->fixed_arity = 0;
  otv cursor = params;
  if (is_type(cursor, OBJ_PAIR) && symbol_is(as_pair(cursor)->car, "array"))
    cursor = as_pair(cursor)->cdr;
  while (is_type(cursor, OBJ_PAIR)) {
    otv parameter = as_pair(cursor)->car;
    if (!is_type(parameter, OBJ_SYMBOL) || symbol_is(parameter, "&")) break;
    code->fixed_arity++;
    cursor = as_pair(cursor)->cdr;
  }
  if (cursor != ot_null) code->fixed_arity = SIZE_MAX;
  return ot_from_obj(code);
}

/* All nested bodies have been compiled before capture representation is fixed.
 * Binding identities, rather than symbol names, carry assignment information. */
static void finalize_code(ots* state, otv value) {
  ot_code_obj* code = as_code(value);
  otv children = code->children;
  for (size_t i = 0; i < as_array(children)->length; i++)
    finalize_code(state, ot_array_get(children, i, ot_nil));
  ot_slots_obj* captures = as_slots(as_array(code->captures)->slots);
  for (size_t i = 0; i < as_array(code->captures)->length; i++) {
    otv descriptor = captures->values[i];
    ot_binding_obj* binding = (ot_binding_obj*)ot_as_obj(descriptor_value(descriptor, 0));
    bool boxed = binding->captured && (binding->assigned || binding->recursive);
    ot_slots_obj* fields = as_slots(as_array(descriptor)->slots);
    ot_store(state, fields, &fields->values[0], boxed ? ot_true : ot_false);
  }
  ot_slots_obj* locals = as_slots(as_array(code->locals)->slots);
  for (size_t i = 0; i < as_array(code->locals)->length; i++) {
    ot_binding_obj* binding = (ot_binding_obj*)ot_as_obj(locals->values[i]);
    bool boxed = binding->captured && (binding->assigned || binding->recursive);
    if (i < code->fixed_arity && boxed) code->boxed_parameters = true;
    ot_store(state, locals, &locals->values[i], ot_make_int(boxed ? 1 : 0));
  }
  ot_store(state, code, &code->children, ot_nil);
}

static otv compile_forms(ots* state, otv forms, otv env, otv params, otv name) {
  OT_FRAME_SCOPED(state, &forms, &env, &params, &name);
  bytecode_compiler compiler = {.constants = ot_nil,
                                .owner = ot_nil,
                                .captures = ot_nil,
                                .children = ot_nil,
                                .outer = state->compiler,
                                .declarations_allowed = env == ot_nil};
  OT_FRAME_SCOPED(state, &compiler.constants, &compiler.owner, &compiler.captures,
                  &compiler.children);
  if (is_type(env, OBJ_ENV)) compiler.owner = ((ot_env_obj*)ot_as_obj(env))->compile_owner;
  else {
    compiler.owner = make_env(state, ot_nil, state->vm.current_namespace, true, 0);
    ot_env_obj* owner = (ot_env_obj*)ot_as_obj(compiler.owner);
    ot_store(state, owner, &owner->compile_owner, compiler.owner);
    otv layout = ot_array_new(state, 8);
    owner = (ot_env_obj*)ot_as_obj(compiler.owner);
    ot_store(state, owner, &owner->layout, layout);
  }
  state->compiler = &compiler;
  compiler.constants = ot_array_new(state, 8);
  compiler.captures = ot_array_new(state, 4);
  compiler.children = ot_array_new(state, 4);
  otv code = OT_UNWIND;
  OT_FRAME_SCOPED(state, &code);
  if (compile_sequence(state, &compiler, forms, env, true)) {
    bytecode_emit(&compiler, BC_RETURN);
    if (compiler.bytes.failed) (void)ot_raise(state, "bytecode exceeds implementation limits");
    else code = make_code(state, &compiler.bytes, compiler.constants, params, name);
  }
  buf_free(&compiler.bytes);
  state->compiler = compiler.outer;
  if (code != OT_UNWIND) {
    if (compiler.outer != NULL) array_push(state, &compiler.outer->children, code);
    else finalize_code(state, code);
  }
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
  otv captures = ot_nil;
  otv source = state->vm.vm_frame_count == 0
                   ? ot_nil
                   : state->vm.vm_frames[state->vm.vm_frame_count - 1].function;
  OT_FRAME_SCOPED(state, &code, &env, &namespace_value, &name, &captures, &source);
  size_t count = as_array(as_code(code)->captures)->length;
  if (count != 0) captures = make_slots(state, count);
  for (size_t i = 0; i < count; i++) {
    otv descriptor = ot_array_get(as_code(code)->captures, i, ot_nil);
    size_t location = (size_t)ot_get_int(descriptor_value(descriptor, 1));
    otv value;
    if (location >= 65536u)
      value = as_slots(((ot_function_obj*)ot_as_obj(source))->captures)->values[location - 65536u];
    else {
      ot_vm_frame* active = &state->vm.vm_frames[state->vm.vm_frame_count - 1];
      value = active->stack_locals
                  ? state->vm.vm_stack[active->base + location]
                  : as_slots(((ot_env_obj*)ot_as_obj(env))->data)->values[location];
    }
    if (descriptor_value(descriptor, 0) == ot_false) value = local_value(value);
    ot_slots_obj* slots = as_slots(captures);
    ot_store(state, slots, &slots->values[i], value);
  }
  ot_function_obj* function = must_alloc(state, sizeof(*function), OBJ_FUNCTION);
  ot_store(state, function, &function->code, code);
  ot_store(state, function, &function->namespace_value, namespace_value);
  ot_store(state, function, &function->name, name);
  ot_store(state, function, &function->captures, captures);
  return ot_from_obj(function);
}

static bool bytecode_read_u16(ot_code_obj* code, size_t* ip, size_t* out) {
  if (*ip > code->length || code->length - *ip < 2) return false;
  unsigned char* bytes = as_bytes(code->bytes)->data;
  *out = ((size_t)bytes[*ip] << 8) | bytes[*ip + 1];
  *ip += 2;
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

static bool vm_lexical_slot(otv env_value, size_t location, ot_env_obj** env_out,
                            size_t* slot_out) {
  if (!is_type(env_value, OBJ_ENV)) return false;
  ot_env_obj* env = (ot_env_obj*)ot_as_obj(env_value);
  if (!is_type(env->data, OBJ_SLOTS) || location >= env->slot_count) return false;
  *env_out = env;
  *slot_out = location;
  return true;
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
    else {
      ot_pair_obj* pair = as_pair(tail);
      ot_store(state, pair, &pair->cdr, cell);
    }
    tail = cell;
    left = as_pair(left)->cdr;
  }
  ot_pair_obj* pair = as_pair(tail);
  ot_store(state, pair, &pair->cdr, right);
  return result;
}

/* Fixed-layout records. Declaration identity is maintained by the Otium prelude. */
static otv nat_make_record_type(ots* state, otv* args, int argc) {
  if (argc != 3 || !is_type(args[0], OBJ_SYMBOL) || !proper_list(args[1]) ||
      (args[2] != ot_true && args[2] != ot_false))
    return ot_raise(state, "defrecord: expected name, field list and boolean immutability");
  otv fields = args[1], keys = ot_nil, key = ot_nil;
  OT_FRAME_SCOPED(state, &fields, &keys, &key);
  keys = ot_array_new(state, list_length(fields));
  while (is_type(fields, OBJ_PAIR)) {
    otv field = as_pair(fields)->car;
    if (!is_type(field, OBJ_SYMBOL)) return ot_raise(state, "defrecord: expected field symbol");
    buf name = {0};
    buf_write(&name, as_name(field)->bytes, as_name(field)->length);
    if (name.failed) {
      buf_free(&name);
      return ot_raise(state, "defrecord: field name too large");
    }
    key = ot_intern(state, name.data, name.length, true);
    buf_free(&name);
    for (size_t i = 0; i < ot_array_length(keys); i++)
      if (ot_array_get(keys, i, ot_nil) == key)
        return ot_raise(state, "defrecord: duplicate field");
    ot_array_append(state, keys, key);
    fields = as_pair(fields)->cdr;
  }
  ot_record_type_obj* type = must_alloc(state, sizeof(*type), OBJ_RECORD_TYPE);
  ot_store(state, type, &type->name, args[0]);
  ot_store(state, type, &type->keys, keys);
  type->immutable = args[2] == ot_true;
  type->stable_id = ++state->next_stable_id;
  return ot_from_obj(type);
}

static otv nat_record_new(ots* state, otv* args, int argc) {
  if (argc < 1 || !is_type(args[0], OBJ_RECORD_TYPE))
    return ot_raise(state, "record-new: expected record type");
  size_t count = ot_array_length(as_record_type(args[0])->keys);
  if ((size_t)(argc - 1) != count) return ot_raise(state, "record-new: wrong field count");
  ot_record_obj* record = must_alloc(state, sizeof(*record) + count * sizeof(otv), OBJ_RECORD);
  ot_store(state, record, &record->type, args[0]);
  record->stable_id = ++state->next_stable_id;
  for (size_t i = 0; i < count; i++) ot_store(state, record, &record->values[i], args[i + 1]);
  return ot_from_obj(record);
}

static size_t record_index(otv record, otv key) {
  otv keys = as_record_type(as_record(record)->type)->keys;
  for (size_t i = 0; i < ot_array_length(keys); i++)
    if (ot_array_get(keys, i, ot_nil) == key) return i;
  return SIZE_MAX;
}

static otv record_read(ots* state, otv record, otv key) {
  if (!is_type(record, OBJ_RECORD)) return ot_raise(state, "record-ref: expected record");
  size_t index = record_index(record, key);
  if (index == SIZE_MAX) return ot_raise(state, "record-ref: unknown record field");
  return as_record(record)->values[index];
}

static otv record_write(ots* state, otv record, otv key, otv value) {
  if (!is_type(record, OBJ_RECORD)) return ot_raise(state, "record-set!: expected record");
  if (as_record_type(as_record(record)->type)->immutable)
    return ot_raise(state, "record-set!: immutable record");
  size_t index = record_index(record, key);
  if (index == SIZE_MAX) return ot_raise(state, "record-set!: unknown record field");
  ot_record_obj* object = as_record(record);
  ot_store(state, object, &object->values[index], value);
  return value;
}

static otv nat_record_ref(ots* state, otv* args, int argc) {
  if (argc != 2) return ot_raise(state, "record-ref: expected record and key");
  return record_read(state, args[0], args[1]);
}

static otv nat_record_set(ots* state, otv* args, int argc) {
  if (argc != 3) return ot_raise(state, "record-set!: expected record, key and value");
  return record_write(state, args[0], args[1], args[2]);
}

static otv nat_record_p(ots* state, otv* args, int argc) {
  if (argc != 1) return ot_raise(state, "record?: expected one value");
  return is_type(args[0], OBJ_RECORD) ? ot_true : ot_false;
}

static otv nat_record_type_p(ots* state, otv* args, int argc) {
  if (argc != 1) return ot_raise(state, "record-type?: expected one value");
  return is_type(args[0], OBJ_RECORD_TYPE) ? ot_true : ot_false;
}

static otv nat_record_type(ots* state, otv* args, int argc) {
  if (argc != 1 || !is_type(args[0], OBJ_RECORD))
    return ot_raise(state, "record-type: expected record");
  return as_record(args[0])->type;
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
  if (is_type(callable, OBJ_GENERIC)) {
    callable = generic_select(state, callable, args, argc);
    if (callable == OT_UNWIND) {
      ot_frame_pop(state, &args_frame);
      return OT_UNWIND;
    }
  }
  if (is_type(callable, OBJ_NAT)) {
    ot_nat_obj* native = (ot_nat_obj*)ot_as_obj(callable);
    if (native->context_function != NULL)
      result = native->context_function(state, native->context, args, (int)argc);
    else result = native->function(state, args, (int)argc);
  } else if (is_type(callable, OBJ_FUNCTION)) {
    result = vm_call_function(state, callable, args, argc);
  } else if (is_type(callable, OBJ_MACRO) && allow_macro) {
    result = apply_value(state, ((ot_macro_obj*)ot_as_obj(callable))->function, args, argc, false);
  } else if (is_type(callable, OBJ_RECORD)) {
    result = argc == 1 ? record_read(state, callable, args[0])
                       : ot_raise(state, "record read: expected one keyword");
  } else if (is_type(callable, OBJ_INSTANCE)) {
    result = argc == 1 ? instance_read(state, callable, args[0])
                       : ot_raise(state, "slot read: expected one keyword");
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
    otv condition = diagnostic_new(state, "E0302");
    OT_FRAME_SCOPED(state, &condition, &callable);
    buf text = {0};
    render_value(state, &text, callable, false, 0);
    diagnostic_put_string(state, &condition, "value", text.data, text.length);
    buf_free(&text);
    result = diagnostic_raise(state, condition);
  }
  ot_frame_pop(state, &args_frame);
  return result;
}

otv ot_apply(ots* state, otv callable, otv* args, size_t argc) {
  return apply_value(state, callable, args, argc, false);
}

static bool vm_enter_call(ots* state, size_t argc, bool tail) {
  if (state->vm.vm_stack_count < argc + 1) {
    (void)ot_raise(state, "vm: operand stack underflow");
    return false;
  }
  size_t call_base = state->vm.vm_stack_count - argc - 1;
  otv callable = state->vm.vm_stack[call_base];
  OT_FRAME_SCOPED(state, &callable);
  if (is_type(callable, OBJ_NAT)) {
    ot_context_nat adapter = ((ot_nat_obj*)ot_as_obj(callable))->context_function;
    bool fallback = adapter == cached_read_call && argc == 2 &&
                    !is_type(state->vm.vm_stack[call_base + 1], OBJ_INSTANCE);
    if (adapter == cached_write_call && argc == 4) {
      otv setter = state->vm.vm_stack[call_base + 1];
      fallback = !is_type(setter, OBJ_NAT) ||
                 ((ot_nat_obj*)ot_as_obj(setter))->function != nat_set_applied ||
                 !is_type(state->vm.vm_stack[call_base + 2], OBJ_INSTANCE);
    }
    if (fallback) {
      memmove(state->vm.vm_stack + call_base, state->vm.vm_stack + call_base + 1,
              argc * sizeof(otv));
      state->vm.vm_stack_count--;
      argc--;
      callable = state->vm.vm_stack[call_base];
    }
  }
  if (is_type(callable, OBJ_GENERIC)) {
    callable = generic_select(state, callable, state->vm.vm_stack + call_base + 1, argc);
    if (callable == OT_UNWIND) return false;
  }
  if (is_type(callable, OBJ_FUNCTION)) {
    ot_function_obj* candidate = (ot_function_obj*)ot_as_obj(callable);
    if (is_type(candidate->code, OBJ_CODE) && as_code(candidate->code)->fixed_arity == argc) {
      otv parent = ot_nil;
      size_t local_count = as_code(candidate->code)->local_count;
      size_t base = tail ? state->vm.vm_frames[state->vm.vm_frame_count - 1].base : call_base;
      vm_stack_reserve(state, base + local_count);
      memmove(state->vm.vm_stack + base, state->vm.vm_stack + call_base + 1, argc * sizeof(otv));
      for (size_t i = argc; i < local_count; i++) state->vm.vm_stack[base + i] = ot_nil;
      state->vm.vm_stack_count = base + local_count;
      if (!tail) {
        vm_push_frame(state, callable, parent, base);
      }
      ot_vm_frame* target = &state->vm.vm_frames[state->vm.vm_frame_count - 1];
      target->function = callable;
      target->env = parent;
      target->ip = 0;
      target->local_count = local_count;
      target->stack_locals = true;
      /* Publish the complete frame before allocating cells: arguments are roots. */
      for (size_t i = 0; i < argc; i++) {
        otv target_code = ((ot_function_obj*)ot_as_obj(callable))->code;
        if (local_boxed(target_code, i)) {
          otv cell = make_cell(state, state->vm.vm_stack[base + i]);
          state->vm.vm_stack[base + i] = cell;
        }
      }
      return true;
    }
    otv env = bind_parameters(state, callable, state->vm.vm_stack + call_base + 1, argc);
    if (env == OT_UNWIND) return false;
    OT_FRAME_SCOPED(state, &env);
    size_t base = tail ? state->vm.vm_frames[state->vm.vm_frame_count - 1].base : call_base;
    if (tail) {
      ot_vm_frame* frame = &state->vm.vm_frames[state->vm.vm_frame_count - 1];
      state->vm.vm_stack_count = base;
      frame->function = callable;
      frame->env = env;
      frame->stack_locals = false;
      frame->ip = 0;
    } else {
      state->vm.vm_stack_count = call_base;
      vm_push_frame(state, callable, env, base);
    }
    return true;
  }

  if (is_type(callable, OBJ_NAT)) {
    ot_nat_obj* native = (ot_nat_obj*)ot_as_obj(callable);
    if (native->vm_direct) {
      ot_nat function = native->function;
      otv result = function(state, state->vm.vm_stack + call_base + 1, (int)argc);
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

#if OT_COMPUTED_GOTO
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#define VM_LABEL(NAME)                                                                             \
  NAME:
#define VM_NEXT() goto vm_fast_dispatch
#else
#define VM_LABEL(NAME)
#define VM_NEXT() continue
#endif

static void vm_materialize_locals(ots* state) {
  size_t index = state->vm.vm_frame_count - 1;
  ot_vm_frame* active = &state->vm.vm_frames[index];
  size_t count = active->local_count;
  size_t base = active->base;
  otv namespace_value = ((ot_function_obj*)ot_as_obj(active->function))->namespace_value;
  otv env = make_env(state, active->env, namespace_value, false, count + 1);
  active = &state->vm.vm_frames[index];
  ot_env_obj* target = (ot_env_obj*)ot_as_obj(env);
  ot_slots_obj* slots = as_slots(target->data);
  for (size_t i = 0; i < count; i++) {
    ot_store(state, slots, &slots->values[i], state->vm.vm_stack[base + i]);
    state->vm.vm_stack[base + i] = ot_nil;
  }
  target->slot_count = count;
  active->env = env;
  active->stack_locals = false;
}

static otv frame_local(ots* state, ot_vm_frame* frame, size_t slot) {
  return frame->stack_locals ? state->vm.vm_stack[frame->base + slot]
                             : as_slots(((ot_env_obj*)ot_as_obj(frame->env))->data)->values[slot];
}

static void store_frame_local(ots* state, ot_vm_frame* frame, size_t slot, otv value) {
  if (frame->stack_locals) state->vm.vm_stack[frame->base + slot] = value;
  else {
    ot_slots_obj* slots = as_slots(((ot_env_obj*)ot_as_obj(frame->env))->data);
    ot_store(state, slots, &slots->values[slot], value);
  }
}

static void initialize_frame_local(ots* state, size_t slot, otv value) {
  OT_FRAME_SCOPED(state, &value);
  size_t index = state->vm.vm_frame_count - 1;
  ot_vm_frame* frame = &state->vm.vm_frames[index];
  otv code = ((ot_function_obj*)ot_as_obj(frame->function))->code;
  if (local_boxed(code, slot)) value = make_cell(state, value);
  store_frame_local(state, &state->vm.vm_frames[index], slot, value);
}

static otv vm_execute_loop(ots* state, size_t floor) {
#define OT_OPERAND_ENTRY(NAME, VALUE, LABEL, OPERAND) [NAME] = OPERAND,
  static const bool has_operand[256] = {OT_BYTECODE_OPS(OT_OPERAND_ENTRY)};
#undef OT_OPERAND_ENTRY
#if OT_COMPUTED_GOTO
#define OT_LABEL_ENTRY(NAME, VALUE, LABEL, OPERAND) [NAME] = &&LABEL,
  static void* const labels[256] = {OT_BYTECODE_OPS(OT_LABEL_ENTRY)};
#undef OT_LABEL_ENTRY
#endif
  while (state->vm.vm_frame_count > floor) {
  vm_hot_reenter:
    if (state->vm.vm_frame_count > state->vm.frame_limit) {
      (void)vm_unwind_to(state, floor);
      return ot_raise(state, "maximum VM frame depth exceeded");
    }
    ot_vm_frame* frame = &state->vm.vm_frames[state->vm.vm_frame_count - 1];
    ot_function_obj* function = (ot_function_obj*)ot_as_obj(frame->function);
    if (!is_type(function->code, OBJ_CODE)) {
      (void)ot_raise(state, "vm: function has no bytecode");
      return vm_unwind_to(state, floor);
    }
    ot_code_obj* code = as_code(function->code);
    VM_LABEL(vm_fast_dispatch) {
      size_t hot_sp = state->vm.vm_stack_count;
      size_t hot_ip = frame->ip;
      otv* hot_stack = state->vm.vm_stack;
      const unsigned char* hot_bytes = as_bytes(code->bytes)->data;
      for (;;) {
        if (state->vm.poll_count >= 1023 || hot_ip >= code->length) break;
        unsigned char hot_op = hot_bytes[hot_ip];
        size_t next_ip = hot_ip + 1;
        size_t hot_arg = 0;
        if (has_operand[hot_op]) {
          if (next_ip + 1 >= code->length) break;
          hot_arg = ((size_t)hot_bytes[next_ip] << 8) | hot_bytes[next_ip + 1];
          next_ip += 2;
        }
        if (hot_sp >= state->vm.vm_stack_capacity) break;
        if (hot_op == BC_CONST) {
          if (hot_arg >= code->constant_count) break;
          hot_stack[hot_sp++] = as_slots(as_array(code->constants)->slots)->values[hot_arg];
        } else if (hot_op == BC_GET_LEXICAL) {
          if (frame->stack_locals && hot_arg < frame->local_count)
            hot_stack[hot_sp++] = local_value(hot_stack[frame->base + hot_arg]);
          else {
            size_t location = hot_arg;
            if (frame->stack_locals) break;
            ot_env_obj* lexical;
            size_t slot;
            if (!vm_lexical_slot(frame->env, location, &lexical, &slot)) break;
            hot_stack[hot_sp++] = local_value(as_slots(lexical->data)->values[slot]);
          }
        } else if (hot_op == BC_CLEAR_LOCAL) {
          store_frame_local(state, frame, hot_arg, ot_nil);
        } else if (hot_op == BC_GET_CAPTURE) {
          hot_stack[hot_sp++] = local_value(as_slots(function->captures)->values[hot_arg]);
        } else if (hot_op == BC_BIND && !local_boxed(function->code, hot_arg)) {
          if (hot_sp <= frame->base || !frame->stack_locals) break;
          hot_stack[frame->base + hot_arg] = hot_stack[--hot_sp];
        } else if (hot_op == BC_SET_LEXICAL) {
          if (hot_sp <= frame->base || !frame->stack_locals || hot_arg >= frame->local_count ||
              is_type(hot_stack[frame->base + hot_arg], OBJ_CELL))
            break;
          hot_stack[frame->base + hot_arg] = hot_stack[hot_sp - 1];
        } else if (hot_op == BC_GET_GLOBAL) {
          if (hot_arg >= code->constant_count) break;
          otv symbol = as_slots(as_array(code->constants)->slots)->values[hot_arg];
          if (!is_type(symbol, OBJ_SYMBOL)) break;
          ot_name_obj* symbol_object = as_name(symbol);
          if (symbol_object->cache_namespace != function->namespace_value ||
              !is_type(symbol_object->cache_var, OBJ_VAR) ||
              memchr(symbol_object->bytes, '/', symbol_object->length) != NULL)
            break;
          hot_stack[hot_sp++] = ((ot_var_obj*)ot_as_obj(symbol_object->cache_var))->value;
        } else if (hot_op == BC_DUP) {
          if (hot_sp <= frame->base) break;
          hot_stack[hot_sp] = hot_stack[hot_sp - 1];
          hot_sp++;
        } else if (hot_op == BC_POP) {
          if (hot_sp <= frame->base) break;
          hot_sp--;
        } else if (hot_op == BC_JUMP) {
          if (hot_arg >= code->length) break;
          next_ip = hot_arg;
        } else if (hot_op == BC_JUMP_IF_FALSE || hot_op == BC_JUMP_IF_TRUE) {
          if (hot_sp <= frame->base || hot_arg >= code->length) break;
          bool false_value = is_falsy(hot_stack[--hot_sp]);
          if ((hot_op == BC_JUMP_IF_FALSE && false_value) ||
              (hot_op == BC_JUMP_IF_TRUE && !false_value))
            next_ip = hot_arg;
        } else if (hot_op == BC_INTRINSIC) {
          if (hot_arg >= code->constant_count || hot_sp < frame->base + 2) break;
          otv descriptor = as_slots(as_array(code->constants)->slots)->values[hot_arg];
          otv name = descriptor_value(descriptor, 0);
          otv expected = descriptor_value(descriptor, 1);
          if (!is_type(name, OBJ_SYMBOL) || !is_type(expected, OBJ_NAT)) break;
          ot_name_obj* symbol = as_name(name);
          if (symbol->cache_namespace != function->namespace_value ||
              !is_type(symbol->cache_var, OBJ_VAR) ||
              ((ot_var_obj*)ot_as_obj(symbol->cache_var))->value != expected)
            break;
          otv left = hot_stack[hot_sp - 2], right = hot_stack[hot_sp - 1];
          if (!ot_is_int(left) || !ot_is_int(right)) break;
          ot_nat implementation = ((ot_nat_obj*)ot_as_obj(expected))->function;
          otv result;
          if (implementation == nat_add) result = left + right - 1u;
          else if (implementation == nat_subtract) result = left - right + 1u;
          else if (implementation == nat_num_equal) result = left == right ? ot_true : ot_false;
          else if (implementation == nat_less)
            result = ot_get_int(left) < ot_get_int(right) ? ot_true : ot_false;
          else break;
          if (descriptor_value(descriptor, 2) == ot_true) {
            size_t base = frame->base;
            state->vm.vm_frame_count--;
            state->vm.vm_stack_count = base;
            if (state->vm.vm_frame_count == floor) return result;
            hot_stack[base] = result;
            state->vm.vm_stack_count++;
            goto vm_hot_reenter;
          }
          hot_stack[hot_sp - 2] = result;
          hot_sp--;
        } else if (hot_op == BC_CALL || hot_op == BC_TAIL_CALL) {
          if (hot_sp < hot_arg + 1) break;
          size_t call_base = hot_sp - hot_arg - 1;
          otv callable = hot_stack[call_base];
          if (is_type(callable, OBJ_NAT) && ((ot_nat_obj*)ot_as_obj(callable))->vm_direct) {
            ot_nat implementation = ((ot_nat_obj*)ot_as_obj(callable))->function;
            bool native_tail = hot_op == BC_TAIL_CALL;
            size_t native_base = native_tail ? frame->base : call_base;
            frame->ip = next_ip;
            state->vm.vm_stack_count = hot_sp;
            otv result = implementation(state, hot_stack + call_base + 1, (int)hot_arg);
            if (result == OT_UNWIND) return vm_unwind_to(state, floor);
            if (native_tail) state->vm.vm_frame_count--;
            state->vm.vm_stack_count = native_base;
            if (native_tail && state->vm.vm_frame_count == floor) return result;
            state->vm.vm_stack[state->vm.vm_stack_count++] = result;
            state->vm.poll_count++;
            goto vm_hot_reenter;
          }
          if (!is_type(callable, OBJ_FUNCTION)) break;
          ot_function_obj* target_function = (ot_function_obj*)ot_as_obj(callable);
          if (!is_type(target_function->code, OBJ_CODE) ||
              as_code(target_function->code)->fixed_arity != hot_arg ||
              as_code(target_function->code)->boxed_parameters)
            break;
          size_t local_count = as_code(target_function->code)->local_count;
          otv parent = ot_nil;
          bool tail = hot_op == BC_TAIL_CALL;
          size_t base = tail ? frame->base : call_base;
          if (base + local_count > state->vm.vm_stack_capacity) break;
          frame->ip = next_ip;
          memmove(hot_stack + base, hot_stack + call_base + 1, hot_arg * sizeof(otv));
          for (size_t i = hot_arg; i < local_count; i++) hot_stack[base + i] = ot_nil;
          state->vm.vm_stack_count = base + local_count;
          if (!tail) vm_push_frame(state, callable, parent, base);
          ot_vm_frame* target = &state->vm.vm_frames[state->vm.vm_frame_count - 1];
          target->function = callable;
          target->env = parent;
          target->ip = 0;
          target->local_count = local_count;
          target->stack_locals = true;
          state->vm.poll_count++;
          goto vm_hot_reenter;
        } else if (hot_op == BC_RETURN) {
          otv result = hot_sp > frame->base ? hot_stack[hot_sp - 1] : ot_nil;
          size_t base = frame->base;
          state->vm.vm_frame_count--;
          state->vm.vm_stack_count = base;
          if (state->vm.vm_frame_count == floor) return result;
          hot_stack[base] = result;
          state->vm.vm_stack_count++;
          goto vm_hot_reenter;
        } else break;
        hot_ip = next_ip;
        state->vm.poll_count++;
      }
      state->vm.vm_stack_count = hot_sp;
      frame->ip = hot_ip;
    }
    if (++state->vm.poll_count >= 1024) {
      state->vm.poll_count = 0;
      bool dev_interrupt =
          state->poll_hook && !state->vm.in_interrupt_hook && ++state->dev_poll_count >= 16;
      bool dev_polled = dev_interrupt;
      if (dev_interrupt) {
        state->dev_poll_count = 0;
        dev_interrupt = state->poll_hook(state->interrupt_userdata);
        frame = &state->vm.vm_frames[state->vm.vm_frame_count - 1];
        function = (ot_function_obj*)ot_as_obj(frame->function);
        code = as_code(function->code);
      }
      if (atomic_exchange(&state->vm.interrupted, false) || dev_interrupt) {
        otv interrupted = run_interrupt_hook(state);
        if (interrupted == OT_UNWIND) return vm_unwind_to(state, floor);
        continue;
      }
      if (dev_polled) continue;
    }
    if (frame->ip >= code->length) {
      (void)ot_raise(state, "vm: instruction pointer outside bytecode");
      return vm_unwind_to(state, floor);
    }
    unsigned char instruction = as_bytes(code->bytes)->data[frame->ip++];
    size_t operand = 0;
    if (has_operand[instruction]) {
      if (!bytecode_read_u16(code, &frame->ip, &operand)) {
        (void)ot_raise(state, "vm: truncated operand");
        return vm_unwind_to(state, floor);
      }
    }

    if (frame->stack_locals &&
        (instruction == BC_NAMED_LET || instruction == BC_TAIL_NAMED_LET || instruction == BC_TRY ||
         instruction == BC_HANDLER_BIND || instruction == BC_RESTART_CASE ||
         instruction == BC_UNWIND_PROTECT || instruction == BC_WITH_PARAMS)) {
      vm_materialize_locals(state);
      frame = &state->vm.vm_frames[state->vm.vm_frame_count - 1];
      function = (ot_function_obj*)ot_as_obj(frame->function);
      code = as_code(function->code);
    }

#if OT_COMPUTED_GOTO
    if (labels[instruction] == NULL) goto vm_invalid;
    goto* labels[instruction];
#endif

    VM_LABEL(vm_load)
    if (instruction == BC_CONST || instruction == BC_GET_LEXICAL || instruction == BC_GET_GLOBAL) {
      if (instruction != BC_GET_LEXICAL && operand >= code->constant_count) {
        (void)ot_raise(state, "vm: constant index out of range");
        return vm_unwind_to(state, floor);
      }
      otv value = instruction == BC_GET_LEXICAL
                      ? ot_nil
                      : as_slots(as_array(code->constants)->slots)->values[operand];
      if (instruction == BC_CONST) {
        vm_push(state, value);
        VM_NEXT();
      }
      if (instruction == BC_GET_LEXICAL) {
        if (frame->stack_locals && operand < frame->local_count) {
          vm_push(state, local_value(state->vm.vm_stack[frame->base + operand]));
          VM_NEXT();
        }

        ot_env_obj* lexical_env;
        size_t slot;
        if (!vm_lexical_slot(frame->env, operand, &lexical_env, &slot)) {
          (void)ot_raise(state, "vm: missing lexical binding at depth %zu slot %zu", operand >> 8,
                         operand & UINT8_MAX);
          return vm_unwind_to(state, floor);
        }
        vm_push(state, local_value(as_slots(lexical_env->data)->values[slot]));
        VM_NEXT();
      }
      if (instruction == BC_GET_GLOBAL) {
        otv var = resolve_var(state, function->namespace_value, value, true);
        if (var == OT_UNWIND) return vm_unwind_to(state, floor);
        vm_push(state, ((ot_var_obj*)ot_as_obj(var))->value);
        continue;
      }
    }

    VM_LABEL(vm_closure)
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
    VM_LABEL(vm_define)
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
      if (is_type(value, OBJ_FUNCTION)) {
        ot_function_obj* named = (ot_function_obj*)ot_as_obj(value);
        ot_store(state, named, &named->name, name);
      }
      if (instruction == BC_DEFINE_LEXICAL) {
        size_t slot = (size_t)ot_get_int(descriptor_value(descriptor, 2));
        otv previous = frame_local(state, frame, slot);
        if (is_type(previous, OBJ_CELL)) {
          ot_cell_obj* cell = (ot_cell_obj*)ot_as_obj(previous);
          ot_store(state, cell, &cell->value, value);
        } else store_frame_local(state, frame, slot, value);
      } else
        define_var(state, state->vm.current_namespace, name, value, doc,
                   instruction == BC_DEFINE_PRIVATE);
      continue;
    }
    VM_LABEL(vm_macro)
    if (instruction == BC_MACRO) {
      if (state->vm.vm_stack_count <= frame->base ||
          !is_type(state->vm.vm_stack[state->vm.vm_stack_count - 1], OBJ_FUNCTION)) {
        (void)ot_raise(state, "vm: macro requires a function");
        return vm_unwind_to(state, floor);
      }
      otv function_value = state->vm.vm_stack[state->vm.vm_stack_count - 1];
      OT_FRAME_SCOPED(state, &function_value);
      ot_macro_obj* macro = must_alloc(state, sizeof(*macro), OBJ_MACRO);
      ot_store(state, macro, &macro->function, function_value);
      state->vm.vm_stack[state->vm.vm_stack_count - 1] = ot_from_obj(macro);
      continue;
    }
    VM_LABEL(vm_def_param)
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
      ot_store(state, param, &param->name, name);
      ot_store(state, param, &param->value, value);
      otv made = ot_from_obj(param);
      state->vm.vm_stack[state->vm.vm_stack_count - 1] = made;
      define_var(state, state->vm.current_namespace, name, made, doc, false);
      continue;
    }
    VM_LABEL(vm_named_let)
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
      named_env = frame->env;
      otv callable = make_bytecode_function(state, nested_code, named_env, named_namespace, name);
      OT_FRAME_SCOPED(state, &callable);
      size_t loop_slot = (size_t)ot_get_int(descriptor_value(descriptor, 3));
      ot_slots_obj* named_slots = as_slots(((ot_env_obj*)ot_as_obj(named_env))->data);
      if (is_type(named_slots->values[loop_slot], OBJ_CELL)) {
        ot_cell_obj* cell = (ot_cell_obj*)ot_as_obj(named_slots->values[loop_slot]);
        ot_store(state, cell, &cell->value, callable);
      } else ot_store(state, named_slots, &named_slots->values[loop_slot], callable);
      otv call_env = bind_parameters(state, callable, state->vm.vm_stack + call_base, count);
      if (call_env == OT_UNWIND) return vm_unwind_to(state, floor);
      OT_FRAME_SCOPED(state, &call_env);
      if (instruction == BC_TAIL_NAMED_LET) {
        size_t base = frame->base;
        state->vm.vm_stack_count = base;
        frame->function = callable;
        frame->env = call_env;
        frame->stack_locals = false;
        frame->ip = 0;
      } else {
        state->vm.vm_stack_count = call_base;
        vm_push_frame(state, callable, call_env, call_base);
      }
      continue;
    }
    VM_LABEL(vm_dynamic)
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
    VM_LABEL(vm_namespace)
    if (instruction == BC_IN_NS || instruction == BC_REQUIRE || instruction == BC_GENERIC_DECL) {
      if (operand >= code->constant_count) {
        (void)ot_raise(state, "vm: namespace constant out of range");
        return vm_unwind_to(state, floor);
      }
      otv value = as_slots(as_array(code->constants)->slots)->values[operand];
      otv result = ot_nil;
      if (instruction == BC_GENERIC_DECL) {
        result = execute_generic_declaration(state, value);
      } else if (instruction == BC_IN_NS) {
        if (!is_type(value, OBJ_SYMBOL)) result = ot_raise(state, "vm: invalid namespace name");
        else {
          ot_namespace_obj* current = (ot_namespace_obj*)ot_as_obj(state->vm.current_namespace);
          if (current->loading && current->name != value) {
            otv condition = diagnostic_new(state, "E0206");
            OT_FRAME_SCOPED(state, &condition, &value);
            diagnostic_put(state, &condition, "declared", value);
            current = (ot_namespace_obj*)ot_as_obj(state->vm.current_namespace);
            diagnostic_put(state, &condition, "namespace", current->name);
            result = diagnostic_raise(state, condition);
          } else state->vm.current_namespace = namespace_find(state, value, true);
        }
      } else {
        result = require_forms(state, value);
      }
      if (result == OT_UNWIND) return vm_unwind_to(state, floor);
      vm_push(state, result);
      continue;
    }
    VM_LABEL(vm_constructor)
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

    VM_LABEL(vm_intrinsic)
    if (instruction == BC_INTRINSIC) {
      if (operand >= code->constant_count || state->vm.vm_stack_count < frame->base + 2) {
        (void)ot_raise(state, "vm: invalid intrinsic application");
        return vm_unwind_to(state, floor);
      }
      otv descriptor = as_slots(as_array(code->constants)->slots)->values[operand];
      otv name = descriptor_value(descriptor, 0);
      otv expected = descriptor_value(descriptor, 1);
      bool tail = descriptor_value(descriptor, 2) == ot_true;
      if (!is_type(name, OBJ_SYMBOL) || !is_type(expected, OBJ_NAT)) {
        (void)ot_raise(state, "vm: invalid intrinsic descriptor");
        return vm_unwind_to(state, floor);
      }
      size_t call_base = state->vm.vm_stack_count - 2;
      otv var = resolve_var(state, function->namespace_value, name, true);
      if (var == OT_UNWIND) return vm_unwind_to(state, floor);
      otv callable = ((ot_var_obj*)ot_as_obj(var))->value;
      if (callable == expected) {
        ot_nat implementation = ((ot_nat_obj*)ot_as_obj(expected))->function;
        otv left = state->vm.vm_stack[call_base];
        otv right = state->vm.vm_stack[call_base + 1];
        otv result;
        if (ot_is_int(left) && ot_is_int(right)) {
          if (implementation == nat_add) result = left + right - 1u;
          else if (implementation == nat_subtract) result = left - right + 1u;
          else if (implementation == nat_num_equal) result = left == right ? ot_true : ot_false;
          else if (implementation == nat_less)
            result = ot_get_int(left) < ot_get_int(right) ? ot_true : ot_false;
          else result = implementation(state, state->vm.vm_stack + call_base, 2);
        } else result = implementation(state, state->vm.vm_stack + call_base, 2);
        if (result == OT_UNWIND) return vm_unwind_to(state, floor);
        if (!tail) {
          state->vm.vm_stack_count = call_base;
          vm_push(state, result);
          continue;
        }
        ot_vm_frame leaving = state->vm.vm_frames[--state->vm.vm_frame_count];
        state->vm.vm_stack_count = leaving.base;
        if (state->vm.vm_frame_count == floor) return result;
        vm_push(state, result);
        continue;
      }

      vm_push(state, ot_nil);
      memmove(state->vm.vm_stack + call_base + 1, state->vm.vm_stack + call_base,
              2 * sizeof(*state->vm.vm_stack));
      state->vm.vm_stack[call_base] = callable;
      size_t before = state->vm.vm_frame_count;
      if (!vm_enter_call(state, 2, tail)) return vm_unwind_to(state, floor);
      if (tail && state->vm.vm_frame_count < before && state->vm.vm_frame_count == floor) {
        otv result = state->vm.vm_stack[state->vm.vm_stack_count - 1];
        state->vm.vm_stack_count--;
        return result;
      }
      continue;
    }

    VM_LABEL(vm_call)
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
    VM_LABEL(vm_set)
    if (instruction == BC_SET_LEXICAL || instruction == BC_SET_GLOBAL) {
      if ((instruction == BC_SET_GLOBAL && operand >= code->constant_count) ||
          state->vm.vm_stack_count <= frame->base) {
        (void)ot_raise(state, "vm: invalid assignment");
        return vm_unwind_to(state, floor);
      }
      otv name = instruction == BC_SET_GLOBAL
                     ? as_slots(as_array(code->constants)->slots)->values[operand]
                     : ot_nil;
      otv assigned = state->vm.vm_stack[state->vm.vm_stack_count - 1];
      if (instruction == BC_SET_LEXICAL) {
        if (frame->stack_locals && operand < frame->local_count) {
          otv current = state->vm.vm_stack[frame->base + operand];
          if (is_type(current, OBJ_CELL)) {
            ot_cell_obj* cell = (ot_cell_obj*)ot_as_obj(current);
            ot_store(state, cell, &cell->value, assigned);
          } else state->vm.vm_stack[frame->base + operand] = assigned;
          VM_NEXT();
        }

        ot_env_obj* lexical_env;
        size_t slot;
        if (!vm_lexical_slot(frame->env, operand, &lexical_env, &slot)) {
          (void)ot_raise(state, "vm: missing lexical binding");
          return vm_unwind_to(state, floor);
        }
        ot_slots_obj* slots = as_slots(lexical_env->data);
        if (is_type(slots->values[slot], OBJ_CELL)) {
          ot_cell_obj* cell = (ot_cell_obj*)ot_as_obj(slots->values[slot]);
          ot_store(state, cell, &cell->value, assigned);
        } else ot_store(state, slots, &slots->values[slot], assigned);
        VM_NEXT();
      } else {
        otv var = resolve_var(state, function->namespace_value, name, true);
        if (var == OT_UNWIND) return vm_unwind_to(state, floor);
        ot_var_obj* var_object = (ot_var_obj*)ot_as_obj(var);
        ot_store(state, var_object, &var_object->value, assigned);
      }
      continue;
    }
    VM_LABEL(vm_dup)
    if (instruction == BC_DUP) {
      if (state->vm.vm_stack_count <= frame->base) {
        (void)ot_raise(state, "vm: operand stack underflow");
        return vm_unwind_to(state, floor);
      }
      vm_push(state, state->vm.vm_stack[state->vm.vm_stack_count - 1]);
      VM_NEXT();
    }
    VM_LABEL(vm_clear_local)
    if (instruction == BC_CLEAR_LOCAL) {
      store_frame_local(state, frame, operand, ot_nil);
      VM_NEXT();
    }
    VM_LABEL(vm_init_local)
    if (instruction == BC_INIT_LOCAL) {
      initialize_frame_local(state, operand, ot_nil);
      continue;
    }
    VM_LABEL(vm_capture)
    if (instruction == BC_GET_CAPTURE || instruction == BC_SET_CAPTURE) {
      otv value = as_slots(function->captures)->values[operand];
      if (instruction == BC_GET_CAPTURE) vm_push(state, local_value(value));
      else {
        if (!is_type(value, OBJ_CELL)) {
          (void)ot_raise(state, "vm: assignment to immutable capture");
          return vm_unwind_to(state, floor);
        }
        ot_cell_obj* cell = (ot_cell_obj*)ot_as_obj(value);
        ot_store(state, cell, &cell->value, state->vm.vm_stack[state->vm.vm_stack_count - 1]);
      }
      VM_NEXT();
    }
    VM_LABEL(vm_bind)
    if (instruction == BC_BIND) {
      otv value = state->vm.vm_stack[--state->vm.vm_stack_count];
      initialize_frame_local(state, operand, value);
      continue;
    }
    VM_LABEL(vm_jump)
    if (instruction == BC_JUMP) {
      if (operand >= code->length) {
        (void)ot_raise(state, "vm: jump target outside bytecode");
        return vm_unwind_to(state, floor);
      }
      frame->ip = operand;
      VM_NEXT();
    }
    VM_LABEL(vm_conditional_jump)
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
      VM_NEXT();
    }
    VM_LABEL(vm_pop)
    if (instruction == BC_POP) {
      if (state->vm.vm_stack_count <= frame->base) {
        (void)ot_raise(state, "vm: operand stack underflow");
        return vm_unwind_to(state, floor);
      }
      state->vm.vm_stack_count--;
      VM_NEXT();
    }
    VM_LABEL(vm_return)
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
    VM_LABEL(vm_invalid)
    (void)ot_raise(state, "vm: unknown opcode 0x%02x", instruction);
    return vm_unwind_to(state, floor);
  }
  return ot_nil;
}

#undef VM_LABEL
#undef VM_NEXT
#if OT_COMPUTED_GOTO
#pragma GCC diagnostic pop
#endif

static otv vm_execute(ots* state, size_t floor) {
  otv result = vm_execute_loop(state, floor);
  return result;
}

static otv compile_and_run_form(ots* state, otv form, otv env) {
  OT_FRAME_SCOPED(state, &form, &env);
  otv code = compile_form(state, form, env);
  if (code == OT_UNWIND) return code;
  OT_FRAME_SCOPED(state, &code);
  otv function = make_bytecode_function(state, code, env, env_namespace(state, env), ot_nil);
  OT_FRAME_SCOPED(state, &function);
  return vm_call_function(state, function, NULL, 0);
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

static bool keyword_is(otv value, const char* name) {
  return is_type(value, OBJ_KEYWORD) && as_name(value)->length == strlen(name) &&
         memcmp(as_name(value)->bytes, name, as_name(value)->length) == 0;
}

static otv validate_require_options(ots* state, otv options, bool* reload) {
  otv cursor = options;
  while (cursor != ot_null) {
    if (!is_type(cursor, OBJ_PAIR)) return ot_raise(state, "require: improper options");
    otv option = as_pair(cursor)->car;
    cursor = as_pair(cursor)->cdr;
    if (!is_type(option, OBJ_KEYWORD)) return ot_raise(state, "require: expected option keyword");
    if (keyword_is(option, "reload")) {
      *reload = true;
      continue;
    }
    if (keyword_is(option, "as")) {
      if (!is_type(cursor, OBJ_PAIR)) return ot_raise(state, "require: :as needs a name");
      otv alias_name = unwrap_quote(as_pair(cursor)->car);
      if (!is_type(alias_name, OBJ_SYMBOL)) return ot_raise(state, "require: invalid alias");
      cursor = as_pair(cursor)->cdr;
      continue;
    }
    if (keyword_is(option, "refer")) {
      if (!is_type(cursor, OBJ_PAIR)) return ot_raise(state, "require: :refer needs names");
      otv names = unwrap_quote(as_pair(cursor)->car);
      if (is_type(names, OBJ_PAIR) && symbol_is(as_pair(names)->car, "array"))
        names = as_pair(names)->cdr;
      if (!proper_list(names)) return ot_raise(state, "require: invalid :refer list");
      for (otv rest = names; is_type(rest, OBJ_PAIR); rest = as_pair(rest)->cdr)
        if (!is_type(as_pair(rest)->car, OBJ_SYMBOL))
          return ot_raise(state, "require: invalid :refer name");
      cursor = as_pair(cursor)->cdr;
      continue;
    }
    return ot_raise(state, "require: unknown option");
  }
  return ot_nil;
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
  bool reload = false;
  if (validate_require_options(state, options, &reload) == OT_UNWIND) return OT_UNWIND;
  otv target = namespace_find(state, name, true);
  OT_FRAME_SCOPED(state, &target);
  ot_namespace_obj* space = (ot_namespace_obj*)ot_as_obj(target);
  space = (ot_namespace_obj*)ot_as_obj(target);
  if (!space->loaded || (reload && !symbol_is(name, "otium.generic"))) {
    if (space->loading) {
      otv condition = diagnostic_new(state, "E0205");
      OT_FRAME_SCOPED(state, &condition);
      diagnostic_put(state, &condition, "namespace", name);
      return diagnostic_raise(state, condition);
    }
    space->loading = true;
    state->vm.current_namespace = target;
    ot_module* module = find_module(state, name);
    if (module != NULL && (!module->initialized || reload)) {
      module->init(state);
      if (state->vm.unwind_kind != UNWIND_NONE) {
        state->vm.current_namespace = caller;
        ((ot_namespace_obj*)ot_as_obj(target))->loading = false;
        return OT_UNWIND;
      }
      module->initialized = true;
    }
    char* source = NULL;
    size_t length = 0;
    ot_host_free(state->loaded_source_name);
    state->loaded_source_name = NULL;
    bool found = state->loader != NULL &&
                 state->loader(state->loader_userdata, as_name(name)->bytes, &source, &length);
    if (state->vm.unwind_kind != UNWIND_NONE) {
      ot_host_free(source);
      state->vm.current_namespace = caller;
      ((ot_namespace_obj*)ot_as_obj(target))->loading = false;
      return OT_UNWIND;
    }
    if (found) {
      otv ignored;
      char* source_name = state->loaded_source_name;
      state->loaded_source_name = NULL;
      bool ok =
          eval_source(state, source, length,
                      source_name != NULL ? source_name : as_name(name)->bytes, true, &ignored);
      ot_host_free(source_name);
      ot_host_free(source);
      if (!ok) {
        state->vm.current_namespace = caller;
        ((ot_namespace_obj*)ot_as_obj(target))->loading = false;
        return OT_UNWIND;
      }
    } else if (module == NULL) {
      state->vm.current_namespace = caller;
      ((ot_namespace_obj*)ot_as_obj(target))->loading = false;
      otv condition = diagnostic_new(state, "E0202");
      OT_FRAME_SCOPED(state, &condition);
      diagnostic_put(state, &condition, "namespace", name);
      return diagnostic_raise(state, condition);
    }
    state->vm.current_namespace = caller;
    space = (ot_namespace_obj*)ot_as_obj(target);
    space->loading = false;
    space->loaded = true;
  }
  while (is_type(options, OBJ_PAIR)) {
    otv option = as_pair(options)->car;
    options = as_pair(options)->cdr;
    if (keyword_is(option, "reload")) continue;
    if (keyword_is(option, "as")) {
      if (!is_type(options, OBJ_PAIR)) return ot_raise(state, "require: :as needs a name");
      otv alias_name = unwrap_quote(as_pair(options)->car);
      options = as_pair(options)->cdr;
      if (!is_type(alias_name, OBJ_SYMBOL)) return ot_raise(state, "require: invalid alias");
      if (alias_name != name && namespace_find(state, alias_name, false) != ot_nil) {
        otv condition = diagnostic_new(state, "E0208");
        OT_FRAME_SCOPED(state, &condition);
        diagnostic_put(state, &condition, "alias", alias_name);
        return diagnostic_raise(state, condition);
      }
      ot_namespace_obj* caller_space = (ot_namespace_obj*)ot_as_obj(caller);
      otv aliases = make_alias(state, alias_name, name, caller_space->aliases);
      caller_space = (ot_namespace_obj*)ot_as_obj(caller);
      ot_store(state, caller_space, &caller_space->aliases, aliases);
    } else if (keyword_is(option, "refer")) {
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
        if (!is_type(var, OBJ_VAR) || ((ot_var_obj*)ot_as_obj(var))->private_value) {
          otv condition = diagnostic_new(state, is_type(var, OBJ_VAR) ? "E0204" : "E0203");
          OT_FRAME_SCOPED(state, &condition);
          diagnostic_put(state, &condition, "namespace", name);
          diagnostic_put(state, &condition, "name", member);
          return diagnostic_raise(state, condition);
        }
        ot_namespace_obj* caller_space = (ot_namespace_obj*)ot_as_obj(caller);
        otv refers = make_alias(state, member, var, caller_space->refers);
        caller_space = (ot_namespace_obj*)ot_as_obj(caller);
        ot_store(state, caller_space, &caller_space->refers, refers);
        ot_alias_obj* added =
            (ot_alias_obj*)ot_as_obj(((ot_namespace_obj*)ot_as_obj(caller))->refers);
        ot_table_put(state, ((ot_namespace_obj*)ot_as_obj(caller))->refer_index, added->name,
                     added->value);
        if (as_name(member)->cache_namespace == caller) {
          ot_name_obj* member_name = as_name(member);
          ot_store(state, member_name, &member_name->cache_namespace, ot_nil);
          ot_store(state, member_name, &member_name->cache_var, ot_nil);
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
  otv* binding_roots[count == 0 ? 1 : count * 2];
  ot_handler_frame* old_handlers = state->vm.handlers;
  for (size_t i = 0; i < count; i++) {
    frames[i] = (ot_handler_frame){
        .prev = i == 0 ? old_handlers : &frames[i - 1], .pred = ot_nil, .handler = ot_nil};
    binding_roots[i * 2] = &frames[i].pred;
    binding_roots[i * 2 + 1] = &frames[i].handler;
  }
  ot_frame bindings_frame;
  ot_frame_push(state, &bindings_frame, binding_roots, count * 2);
  for (size_t i = 0; i < count; i++) {
    binding = descriptor_value(bindings, i);
    predicate_code = descriptor_value(binding, 0);
    handler_code = descriptor_value(binding, 1);
    predicate = vm_run_code(state, predicate_code, env, namespace_value, NULL, 0);
    if (predicate == OT_UNWIND) {
      ot_frame_pop(state, &bindings_frame);
      return predicate;
    }
    handler = vm_run_code(state, handler_code, env, namespace_value, NULL, 0);
    if (handler == OT_UNWIND) {
      ot_frame_pop(state, &bindings_frame);
      return handler;
    }
    frames[i].pred = predicate;
    frames[i].handler = handler;
  }
  ot_frame_pop(state, &bindings_frame);
  if (count != 0) state->vm.handlers = &frames[count - 1];

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
    ot_store(state, restart, &restart->name, name);
    ot_store(state, restart, &restart->description, description);
    restart->id = ++state->vm.next_restart_id;
    restart_clauses[i] = (ot_restart_clause){
        .restart = ot_from_obj(restart),
        .params = as_code(clause_code)->params,
        .body = clause_code,
    };
    frame.count++;
  }

  result = vm_run_code(state, expression_code, env, namespace_value, NULL, 0);
  while (result == OT_UNWIND && state->vm.unwind_kind == UNWIND_RESTART) {
    bool matched = false;
    for (size_t i = 0; i < count; i++) {
      ot_restart_obj* restart = (ot_restart_obj*)ot_as_obj(restart_clauses[i].restart);
      if (restart->id != state->vm.unwind_restart_id) continue;

      matched = true;
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
    if (!matched) break;
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
  bool saved_quit_requested = state->quit_requested;
  int saved_quit_status = state->quit_status;
  state->quit_requested = false;
  state->quit_status = 0;
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
  state->quit_requested = saved_quit_requested;
  state->quit_status = saved_quit_status;
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
  return raise_arity(state, ot_make_string(state, name, strlen(name)), (size_t)minimum,
                     (size_t)(maximum < 0 ? minimum : maximum), maximum < 0, (size_t)argc);
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

static otv make_wrapped_int(uintptr_t value) { return (value << 1u) | 1u; }

static intptr_t wrapped_int_value(uintptr_t value) { return ot_get_int(make_wrapped_int(value)); }

static otv number_result(ots* state, bool integer, uintptr_t wrapped, double value) {
  return integer ? make_wrapped_int(wrapped) : ot_make_float(state, value);
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
      if (all_int) number = (double)wrapped_int_value(integer);
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
      if (all_int) number = (double)wrapped_int_value(integer);
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
      if (all_int) number = (double)wrapped_int_value(integer);
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
      integer = wrapped_int_value((uintptr_t)integer);
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
  if (left == OT_INT_MIN && right == -1)
    return mode == 0 ? ot_make_int(OT_INT_MIN) : ot_make_int(0);
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

static otv nat_bitwise(ots* state, otv* args, int argc, int mode) {
  intptr_t result = mode == 0 ? -1 : 0;
  for (int i = 0; i < argc; i++) {
    if (!ot_is_int(args[i])) return ot_raise(state, "bitwise: expected exact integers");
    intptr_t value = ot_get_int(args[i]);
    if (mode == 0) result &= value;
    else if (mode == 1) result |= value;
    else result ^= value;
  }
  return ot_make_int(result);
}

static otv nat_bitwise_and(ots* state, otv* args, int argc) {
  return nat_bitwise(state, args, argc, 0);
}

static otv nat_bitwise_ior(ots* state, otv* args, int argc) {
  return nat_bitwise(state, args, argc, 1);
}

static otv nat_bitwise_xor(ots* state, otv* args, int argc) {
  return nat_bitwise(state, args, argc, 2);
}

static otv nat_bitwise_not(ots* state, otv* args, int argc) {
  if (need_arity(state, "bitwise-not", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (!ot_is_int(args[0])) return ot_raise(state, "bitwise-not: expected exact integer");
  return ot_make_int(~ot_get_int(args[0]));
}

static otv nat_bitwise_shift(ots* state, otv* args, int argc) {
  if (need_arity(state, "bitwise-arithmetic-shift", argc, 2, 2) == OT_UNWIND) return OT_UNWIND;
  if (!ot_is_int(args[0]) || !ot_is_int(args[1]))
    return ot_raise(state, "bitwise-arithmetic-shift: expected exact integers");
  intptr_t value = ot_get_int(args[0]);
  intptr_t shift = ot_get_int(args[1]);
  if (value == 0) return ot_make_int(0);
  if (shift < 0) {
    if (shift <= -(intptr_t)(sizeof(intptr_t) * CHAR_BIT)) return ot_make_int(value < 0 ? -1 : 0);
    return ot_make_int(value < 0 ? ~(~value >> -shift) : value >> -shift);
  }
  while (shift-- > 0) {
    if (value > OT_INT_MAX / 2 || value < OT_INT_MIN / 2)
      return ot_raise(state, "bitwise-arithmetic-shift: integer out of range");
    value *= 2;
  }
  return ot_make_int(value);
}

static bool compare_int_float(intptr_t integer, double number, int* order) {
  if (isnan(number)) return false;
  double lower = (double)OT_INT_MIN;
  double upper_exclusive = -lower;
  if (number < lower) {
    *order = 1;
    return true;
  }
  if (number >= upper_exclusive) {
    *order = -1;
    return true;
  }

  intptr_t truncated = (intptr_t)number;
  if (integer < truncated) *order = -1;
  else if (integer > truncated) *order = 1;
  else if ((double)truncated < number) *order = -1;
  else if ((double)truncated > number) *order = 1;
  else *order = 0;
  return true;
}

static bool numeric_order(otv left, otv right, int* order) {
  if (ot_is_int(left) && ot_is_int(right)) {
    intptr_t a = ot_get_int(left);
    intptr_t b = ot_get_int(right);
    *order = a < b ? -1 : a > b ? 1 : 0;
    return true;
  }
  if (ot_is_int(left) && is_type(right, OBJ_FLOAT))
    return compare_int_float(ot_get_int(left), ((ot_float_obj*)ot_as_obj(right))->value, order);
  if (is_type(left, OBJ_FLOAT) && ot_is_int(right)) {
    if (!compare_int_float(ot_get_int(right), ((ot_float_obj*)ot_as_obj(left))->value, order))
      return false;
    *order = -*order;
    return true;
  }
  if (is_type(left, OBJ_FLOAT) && is_type(right, OBJ_FLOAT)) {
    double a = ((ot_float_obj*)ot_as_obj(left))->value;
    double b = ((ot_float_obj*)ot_as_obj(right))->value;
    if (isnan(a) || isnan(b)) return false;
    *order = a < b ? -1 : a > b ? 1 : 0;
    return true;
  }
  return false;
}

static otv nat_numeric_compare(ots* state, otv* args, int argc, int mode) {
  if (argc < (mode == 0 ? 0 : 2)) return ot_raise(state, "comparison: too few arguments");
  for (int i = 1; i < argc; i++) {
    bool left_number = ot_is_int(args[i - 1]) || is_type(args[i - 1], OBJ_FLOAT);
    bool right_number = ot_is_int(args[i]) || is_type(args[i], OBJ_FLOAT);
    if (!left_number || !right_number) return ot_raise(state, "comparison: expected numbers");
    int order = 0;
    bool ordered = numeric_order(args[i - 1], args[i], &order);
    bool ok = ordered && (mode == 0   ? order == 0
                          : mode == 1 ? order < 0
                          : mode == 2 ? order > 0
                          : mode == 3 ? order <= 0
                                      : order >= 0);
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
  if (!ot_is_int(args[0]) && !is_type(args[0], OBJ_FLOAT))
    return ot_raise(state, "expected number");
  for (int i = 1; i < argc; i++) {
    if (!ot_is_int(args[i]) && !is_type(args[i], OBJ_FLOAT))
      return ot_raise(state, "expected number");
    int order = 0;
    if (numeric_order(args[i], args[best], &order) &&
        ((maximum && order > 0) || (!maximum && order < 0))) {
      best = i;
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
TYPE_PRED(nat_char_p, OT_TYPE_CHAR)
TYPE_PRED(nat_eof_p, OT_TYPE_EOF)
TYPE_PRED(nat_bytevector_p, OT_TYPE_BYTEVECTOR)
#undef TYPE_PRED

static otv nat_number_p(ots* state, otv* args, int argc) {
  if (need_arity(state, "number?", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  ot_type type = ot_value_type(args[0]);
  return type == OT_TYPE_INT || type == OT_TYPE_FLOAT ? ot_true : ot_false;
}

static otv nat_procedure_p(ots* state, otv* args, int argc) {
  if (need_arity(state, "procedure?", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  return is_type(args[0], OBJ_FUNCTION) || is_type(args[0], OBJ_NAT) ||
                 is_type(args[0], OBJ_GENERIC)
             ? ot_true
             : ot_false;
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

static const char* value_type_name(otv value) {
  static const char* names[] = {
      "nil",         "null",    "boolean", "int",        "float",   "symbol",
      "keyword",     "string",  "pair",    "array",      "table",   "buffer",
      "function",    "macro",   "param",   "restart",    "foreign", "type-descriptor",
      "instance",    "char",    "eof",     "bytevector", "port",    "record",
      "record-type", "internal"};
  return names[ot_value_type(value)];
}

static otv nat_type(ots* state, otv* args, int argc) {
  if (need_arity(state, "type", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  const char* name = value_type_name(args[0]);
  return ot_intern(state, name, strlen(name), true);
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

typedef struct value_set {
  otv* slots;
  size_t capacity;
  size_t count;
} value_set;

static bool value_set_grow(value_set* set) {
  size_t capacity = set->capacity == 0 ? 32 : set->capacity * 2;
  if (capacity < set->capacity || capacity > SIZE_MAX / sizeof(*set->slots)) return false;
  otv* slots = ot_host_alloc(capacity * sizeof(*slots));
  if (slots == NULL) return false;
  memset(slots, 0, capacity * sizeof(*slots));
  for (size_t i = 0; i < set->capacity; i++) {
    otv value = set->slots[i];
    if (value == 0) continue;
    size_t slot = ((size_t)value >> 4u) & (capacity - 1u);
    while (slots[slot] != 0) slot = (slot + 1u) & (capacity - 1u);
    slots[slot] = value;
  }
  ot_host_free(set->slots);
  set->slots = slots;
  set->capacity = capacity;
  return true;
}

static int value_set_insert(value_set* set, otv value) {
  if (set->count + 1u > set->capacity / 2u && !value_set_grow(set)) return -1;
  size_t slot = ((size_t)value >> 4u) & (set->capacity - 1u);
  while (set->slots[slot] != 0) {
    if (set->slots[slot] == value) return 0;
    slot = (slot + 1u) & (set->capacity - 1u);
  }
  set->slots[slot] = value;
  set->count++;
  return 1;
}

static bool value_stack_push(otv** values, size_t* count, size_t* capacity, otv value) {
  if (*count == *capacity) {
    size_t grown_capacity = *capacity == 0 ? 32 : *capacity * 2;
    if (grown_capacity < *capacity || grown_capacity > SIZE_MAX / sizeof(**values)) return false;
    void* grown = ot_host_realloc(*values, grown_capacity * sizeof(**values));
    if (grown == NULL) return false;
    *values = grown;
    *capacity = grown_capacity;
  }
  (*values)[(*count)++] = value;
  return true;
}

static bool value_reaches(otv target, otv value, bool* failed) {
  value_set seen = {0};
  otv* pending = NULL;
  size_t pending_count = 0;
  size_t pending_capacity = 0;
  bool found = false;
  if (!value_stack_push(&pending, &pending_count, &pending_capacity, value)) *failed = true;

  while (!*failed && pending_count != 0) {
    otv current = pending[--pending_count];
    if (current == target) {
      found = true;
      break;
    }
    if (!ot_is_ptr(current)) continue;
    ot_obj_type type = ot_object_type(current);
    if (type != OBJ_PAIR && type != OBJ_ARRAY && type != OBJ_TABLE) continue;
    int inserted = value_set_insert(&seen, current);
    if (inserted < 0) {
      *failed = true;
      break;
    }
    if (inserted == 0) continue;

    if (type == OBJ_PAIR) {
      ot_pair_obj* pair = as_pair(current);
      if (!value_stack_push(&pending, &pending_count, &pending_capacity, pair->car) ||
          !value_stack_push(&pending, &pending_count, &pending_capacity, pair->cdr))
        *failed = true;
    } else if (type == OBJ_ARRAY) {
      ot_array_obj* array = as_array(current);
      ot_slots_obj* slots = as_slots(array->slots);
      for (size_t i = 0; i < array->length; i++) {
        if (!value_stack_push(&pending, &pending_count, &pending_capacity, slots->values[i])) {
          *failed = true;
          break;
        }
      }
    } else {
      ot_table_obj* table = as_table(current);
      ot_entries_obj* entries = as_entries(table->entries);
      for (size_t i = 0; i < table->used; i++) {
        if (!entries->values[i].live) continue;
        if (!value_stack_push(&pending, &pending_count, &pending_capacity,
                              entries->values[i].key) ||
            !value_stack_push(&pending, &pending_count, &pending_capacity,
                              entries->values[i].value)) {
          *failed = true;
          break;
        }
      }
    }
  }

  ot_host_free(pending);
  ot_host_free(seen.slots);
  return found;
}

static otv reject_pair_cycle(ots* state, const char* name, otv pair, otv value) {
  bool failed = false;
  bool cycle = value_reaches(pair, value, &failed);
  if (failed) return ot_raise(state, "%s: unable to check for cycle", name);
  if (cycle) return ot_raise(state, "%s: cycle", name);
  return ot_nil;
}

static otv nat_set_car(ots* state, otv* args, int argc) {
  if (need_arity(state, "set-car!", argc, 2, 2) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_PAIR)) return ot_raise(state, "set-car!: expected pair");
  if (as_pair(args[0])->frozen) return ot_raise(state, "set-car!: frozen table key");
  if (reject_pair_cycle(state, "set-car!", args[0], args[1]) == OT_UNWIND) return OT_UNWIND;
  ot_pair_obj* pair = as_pair(args[0]);
  ot_store(state, pair, &pair->car, args[1]);
  return args[0];
}

static otv nat_set_cdr(ots* state, otv* args, int argc) {
  if (need_arity(state, "set-cdr!", argc, 2, 2) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_PAIR)) return ot_raise(state, "set-cdr!: expected pair");
  if (as_pair(args[0])->frozen) return ot_raise(state, "set-cdr!: frozen table key");
  if (reject_pair_cycle(state, "set-cdr!", args[0], args[1]) == OT_UNWIND) return OT_UNWIND;
  ot_pair_obj* pair = as_pair(args[0]);
  ot_store(state, pair, &pair->cdr, args[1]);
  return args[0];
}

static otv nat_list(ots* state, otv* args, int argc) {
  return list_from_array(state, args, (size_t)argc);
}

static size_t utf8_count(const unsigned char* bytes, size_t length);

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
  if (is_type(value, OBJ_STRING)) {
    ot_string_obj* string = as_string(value);
    return ot_make_int((intptr_t)utf8_count(as_bytes(string->bytes)->data, string->length));
  }
  if (is_type(value, OBJ_BUFFER)) {
    ot_buffer_obj* buffer = (ot_buffer_obj*)ot_as_obj(value);
    return ot_make_int((intptr_t)utf8_count(as_bytes(buffer->bytes)->data, buffer->length));
  }
  if (is_type(value, OBJ_BYTEVECTOR)) return ot_make_int((intptr_t)as_bytevector(value)->length);
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
  for (int i = 0; i < argc; i++)
    if (!proper_list(args[i])) return ot_raise(state, "append: expected proper list");
  otv output = args[argc - 1];
  OT_FRAME_SCOPED(state, &output);
  for (int i = argc - 2; i >= 0; i--) {
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
    return ot_make_char(utf8_decode(bytes, string->length, start));
  }
  if (is_type(args[0], OBJ_BYTEVECTOR)) {
    if (!ot_is_int(args[1])) return fallback;
    intptr_t index = ot_get_int(args[1]);
    ot_bytevector_obj* bytes = as_bytevector(args[0]);
    return index >= 0 && (size_t)index < bytes->length ? ot_make_int(bytes->data[index]) : fallback;
  }
  return ot_raise(state, "get: expected nil, table, array, string, or bytevector");
}

static otv nat_eof_object(ots* state, otv* args, int argc) {
  (void)args;
  if (need_arity(state, "eof-object", argc, 0, 0) == OT_UNWIND) return OT_UNWIND;
  return ot_eof;
}

static otv nat_char_integer(ots* state, otv* args, int argc) {
  if (need_arity(state, "char->integer", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (!ot_is_char(args[0])) return ot_raise(state, "char->integer: expected char");
  return ot_make_int((intptr_t)ot_char_value(args[0]));
}

static otv nat_integer_char(ots* state, otv* args, int argc) {
  if (need_arity(state, "integer->char", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (!ot_is_int(args[0]) || !ot_char_valid(ot_get_int(args[0])))
    return ot_raise(state, "integer->char: expected a Unicode scalar value");
  return ot_make_char((uint32_t)ot_get_int(args[0]));
}

/* Character classes and case are ASCII-only, like the string functions. */
static otv char_argument(ots* state, const char* who, otv* args, int argc, uint32_t* scalar) {
  if (need_arity(state, who, argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (!ot_is_char(args[0])) return ot_raise(state, "%s: expected char", who);
  *scalar = ot_char_value(args[0]);
  return ot_nil;
}

static otv nat_char_case(ots* state, otv* args, int argc, bool upper) {
  uint32_t c = 0;
  if (char_argument(state, upper ? "char-upcase" : "char-downcase", args, argc, &c) == OT_UNWIND)
    return OT_UNWIND;
  if (upper && c >= 'a' && c <= 'z') c -= 32u;
  if (!upper && c >= 'A' && c <= 'Z') c += 32u;
  return ot_make_char(c);
}

static otv nat_char_upcase(ots* state, otv* args, int argc) {
  return nat_char_case(state, args, argc, true);
}

static otv nat_char_downcase(ots* state, otv* args, int argc) {
  return nat_char_case(state, args, argc, false);
}

static otv nat_char_alphabetic(ots* state, otv* args, int argc) {
  uint32_t c = 0;
  if (char_argument(state, "char-alphabetic?", args, argc, &c) == OT_UNWIND) return OT_UNWIND;
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ? ot_true : ot_false;
}

static otv nat_char_numeric(ots* state, otv* args, int argc) {
  uint32_t c = 0;
  if (char_argument(state, "char-numeric?", args, argc, &c) == OT_UNWIND) return OT_UNWIND;
  return c >= '0' && c <= '9' ? ot_true : ot_false;
}

static bool ascii_space(unsigned char byte);

static otv nat_char_whitespace(ots* state, otv* args, int argc) {
  uint32_t c = 0;
  if (char_argument(state, "char-whitespace?", args, argc, &c) == OT_UNWIND) return OT_UNWIND;
  return c < 0x80u && ascii_space((unsigned char)c) ? ot_true : ot_false;
}

/* Encode a run of chars as a string; who names the caller in errors. */
static otv string_from_chars(ots* state, const char* who, otv* chars, size_t count, otv list) {
  buf bytes = {0};
  char encoded[4];
  for (size_t i = 0; i < count; i++) {
    if (!ot_is_char(chars[i])) {
      buf_free(&bytes);
      return ot_raise(state, "%s: expected chars", who);
    }
    buf_write(&bytes, encoded, utf8_encode(ot_char_value(chars[i]), encoded));
  }
  for (; is_type(list, OBJ_PAIR); list = as_pair(list)->cdr) {
    if (!ot_is_char(as_pair(list)->car)) {
      buf_free(&bytes);
      return ot_raise(state, "%s: expected chars", who);
    }
    buf_write(&bytes, encoded, utf8_encode(ot_char_value(as_pair(list)->car), encoded));
  }
  otv result = bytes.failed ? ot_raise(state, "%s: out of memory", who)
                            : ot_make_string(state, bytes.data, bytes.length);
  buf_free(&bytes);
  return result;
}

static otv nat_string(ots* state, otv* args, int argc) {
  return string_from_chars(state, "string", args, (size_t)argc, ot_null);
}

static otv nat_list_string(ots* state, otv* args, int argc) {
  if (need_arity(state, "list->string", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (!proper_list(args[0])) return ot_raise(state, "list->string: expected list");
  return string_from_chars(state, "list->string", NULL, 0, args[0]);
}

static otv nat_string_list(ots* state, otv* args, int argc) {
  if (need_arity(state, "string->list", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_STRING)) return ot_raise(state, "string->list: expected string");
  otv string = args[0], result = ot_null;
  OT_FRAME_SCOPED(state, &string, &result);
  /* Decode backwards so the list is built without reversing. */
  size_t offset = as_string(string)->length;
  while (offset > 0) {
    const unsigned char* bytes = as_bytes(as_string(string)->bytes)->data;
    do offset--;
    while (offset > 0 && (bytes[offset] & 0xc0u) == 0x80u);
    result =
        ot_cons(state, ot_make_char(utf8_decode(bytes, as_string(string)->length, offset)), result);
  }
  return result;
}

static otv nat_string_ref(ots* state, otv* args, int argc) {
  if (need_arity(state, "string-ref", argc, 2, 2) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_STRING) || !ot_is_int(args[1]))
    return ot_raise(state, "string-ref: expected string and integer index");
  otv result = nat_get(state, args, 2);
  if (result == ot_nil) return ot_raise(state, "string-ref: index out of range");
  return result;
}

static bool byte_value(otv value) {
  return ot_is_int(value) && ot_get_int(value) >= 0 && ot_get_int(value) <= 255;
}

static otv bytevector_set(ots* state, const char* who, otv bytes, otv index, otv value) {
  if (!ot_is_int(index) || ot_get_int(index) < 0 ||
      (size_t)ot_get_int(index) >= as_bytevector(bytes)->length)
    return ot_raise(state, "%s: index out of range", who);
  if (!byte_value(value)) return ot_raise(state, "%s: expected a byte from 0 to 255", who);
  as_bytevector(bytes)->data[ot_get_int(index)] = (unsigned char)ot_get_int(value);
  return ot_nil;
}

/* Resolve optional [start [end]] arguments at args[first..] against length. */
static otv span_arguments(ots* state, const char* who, otv* args, int argc, int first,
                          size_t length, size_t* start, size_t* end) {
  *start = 0;
  *end = length;
  if (argc > first) {
    if (!ot_is_int(args[first]) || ot_get_int(args[first]) < 0 ||
        (size_t)ot_get_int(args[first]) > length)
      return ot_raise(state, "%s: start out of range", who);
    *start = (size_t)ot_get_int(args[first]);
  }
  if (argc > first + 1) {
    if (!ot_is_int(args[first + 1]) || ot_get_int(args[first + 1]) < (intptr_t)*start ||
        (size_t)ot_get_int(args[first + 1]) > length)
      return ot_raise(state, "%s: end out of range", who);
    *end = (size_t)ot_get_int(args[first + 1]);
  }
  return ot_nil;
}

static otv nat_bytevector(ots* state, otv* args, int argc) {
  for (int i = 0; i < argc; i++)
    if (!byte_value(args[i])) return ot_raise(state, "bytevector: expected bytes from 0 to 255");
  otv bytes = ot_make_bytevector(state, (size_t)argc);
  for (int i = 0; i < argc; i++) as_bytevector(bytes)->data[i] = (unsigned char)ot_get_int(args[i]);
  return bytes;
}

static otv nat_make_bytevector(ots* state, otv* args, int argc) {
  if (need_arity(state, "make-bytevector", argc, 1, 2) == OT_UNWIND) return OT_UNWIND;
  if (!ot_is_int(args[0]) || ot_get_int(args[0]) < 0)
    return ot_raise(state, "make-bytevector: expected a non-negative length");
  if (argc == 2 && !byte_value(args[1]))
    return ot_raise(state, "make-bytevector: expected a fill byte from 0 to 255");
  unsigned char fill = argc == 2 ? (unsigned char)ot_get_int(args[1]) : 0;
  otv bytes = ot_make_bytevector(state, (size_t)ot_get_int(args[0]));
  memset(as_bytevector(bytes)->data, fill, as_bytevector(bytes)->length);
  return bytes;
}

static otv bytevector_argument(ots* state, const char* who, otv value) {
  return is_type(value, OBJ_BYTEVECTOR) ? ot_nil : ot_raise(state, "%s: expected bytevector", who);
}

static otv nat_bytevector_length(ots* state, otv* args, int argc) {
  if (need_arity(state, "bytevector-length", argc, 1, 1) == OT_UNWIND ||
      bytevector_argument(state, "bytevector-length", args[0]) == OT_UNWIND)
    return OT_UNWIND;
  return ot_make_int((intptr_t)as_bytevector(args[0])->length);
}

static otv nat_bytevector_ref(ots* state, otv* args, int argc) {
  if (need_arity(state, "bytevector-u8-ref", argc, 2, 2) == OT_UNWIND ||
      bytevector_argument(state, "bytevector-u8-ref", args[0]) == OT_UNWIND)
    return OT_UNWIND;
  ot_bytevector_obj* bytes = as_bytevector(args[0]);
  if (!ot_is_int(args[1]) || ot_get_int(args[1]) < 0 ||
      (size_t)ot_get_int(args[1]) >= bytes->length)
    return ot_raise(state, "bytevector-u8-ref: index out of range");
  return ot_make_int(bytes->data[ot_get_int(args[1])]);
}

static otv nat_bytevector_set(ots* state, otv* args, int argc) {
  if (need_arity(state, "bytevector-u8-set!", argc, 3, 3) == OT_UNWIND ||
      bytevector_argument(state, "bytevector-u8-set!", args[0]) == OT_UNWIND ||
      bytevector_set(state, "bytevector-u8-set!", args[0], args[1], args[2]) == OT_UNWIND)
    return OT_UNWIND;
  return ot_nil;
}

static otv nat_bytevector_copy(ots* state, otv* args, int argc) {
  size_t start = 0, end = 0;
  if (need_arity(state, "bytevector-copy", argc, 1, 3) == OT_UNWIND ||
      bytevector_argument(state, "bytevector-copy", args[0]) == OT_UNWIND ||
      span_arguments(state, "bytevector-copy", args, argc, 1, as_bytevector(args[0])->length,
                     &start, &end) == OT_UNWIND)
    return OT_UNWIND;
  otv source = args[0];
  OT_FRAME_SCOPED(state, &source);
  otv copy = ot_make_bytevector(state, end - start);
  memcpy(as_bytevector(copy)->data, as_bytevector(source)->data + start, end - start);
  return copy;
}

static otv nat_bytevector_copy_into(ots* state, otv* args, int argc) {
  size_t start = 0, end = 0;
  if (need_arity(state, "bytevector-copy!", argc, 3, 5) == OT_UNWIND ||
      bytevector_argument(state, "bytevector-copy!", args[0]) == OT_UNWIND ||
      bytevector_argument(state, "bytevector-copy!", args[2]) == OT_UNWIND ||
      span_arguments(state, "bytevector-copy!", args, argc, 3, as_bytevector(args[2])->length,
                     &start, &end) == OT_UNWIND)
    return OT_UNWIND;
  ot_bytevector_obj* to = as_bytevector(args[0]);
  if (!ot_is_int(args[1]) || ot_get_int(args[1]) < 0 || (size_t)ot_get_int(args[1]) > to->length ||
      to->length - (size_t)ot_get_int(args[1]) < end - start)
    return ot_raise(state, "bytevector-copy!: destination out of range");
  memmove(to->data + ot_get_int(args[1]), as_bytevector(args[2])->data + start, end - start);
  return ot_nil;
}

static otv nat_bytevector_append(ots* state, otv* args, int argc) {
  size_t length = 0;
  for (int i = 0; i < argc; i++) {
    if (bytevector_argument(state, "bytevector-append", args[i]) == OT_UNWIND) return OT_UNWIND;
    length += as_bytevector(args[i])->length;
  }
  /* The argument array is rooted by the caller for the duration of the call. */
  otv result = ot_make_bytevector(state, length);
  size_t offset = 0;
  for (int i = 0; i < argc; i++) {
    ot_bytevector_obj* part = as_bytevector(args[i]);
    memcpy(as_bytevector(result)->data + offset, part->data, part->length);
    offset += part->length;
  }
  return result;
}

/* Raise a condition of an io-error type: {:type type :message ... :op op}
 * plus one extra key and value when key is not NULL. */
static otv io_error(ots* state, const char* type, const char* op, const char* message,
                    const char* key, otv value) {
  otv condition = ot_nil, field = ot_nil, item = ot_nil;
  OT_FRAME_SCOPED(state, &condition, &field, &item, &value);
  condition = ot_table_new(state, 4);
  field = ot_intern(state, "type", 4, true);
  item = ot_intern(state, type, strlen(type), false);
  ot_table_put(state, condition, field, item);
  field = ot_intern(state, "message", 7, true);
  item = ot_make_string(state, message, strlen(message));
  ot_table_put(state, condition, field, item);
  field = ot_intern(state, "op", 2, true);
  item = ot_intern(state, op, strlen(op), false);
  ot_table_put(state, condition, field, item);
  if (key != NULL) {
    field = ot_intern(state, key, strlen(key), true);
    ot_table_put(state, condition, field, value);
  }
  return ot_raise_value(state, condition);
}

/* Return the length of the valid UTF-8 sequence at bytes[0..length), or 0. */
static size_t utf8_valid_sequence(const unsigned char* bytes, size_t length) {
  unsigned char byte = bytes[0];
  if (byte < 0x80u) return 1;
  size_t width = byte >= 0xc2u && byte <= 0xdfu   ? 2
                 : byte >= 0xe0u && byte <= 0xefu ? 3
                 : byte >= 0xf0u && byte <= 0xf4u ? 4
                                                  : 0;
  if (width == 0 || width > length) return 0;
  unsigned char second = bytes[1];
  unsigned char low = byte == 0xe0u ? 0xa0u : byte == 0xf0u ? 0x90u : 0x80u;
  unsigned char high = byte == 0xedu ? 0x9fu : byte == 0xf4u ? 0x8fu : 0xbfu;
  if (second < low || second > high) return 0;
  for (size_t i = 2; i < width; i++)
    if ((bytes[i] & 0xc0u) != 0x80u) return 0;
  return width;
}

static otv nat_utf8_string(ots* state, otv* args, int argc) {
  size_t start = 0, end = 0;
  if (need_arity(state, "utf8->string", argc, 1, 3) == OT_UNWIND ||
      bytevector_argument(state, "utf8->string", args[0]) == OT_UNWIND ||
      span_arguments(state, "utf8->string", args, argc, 1, as_bytevector(args[0])->length, &start,
                     &end) == OT_UNWIND)
    return OT_UNWIND;
  const unsigned char* data = as_bytevector(args[0])->data;
  for (size_t offset = start; offset < end;) {
    size_t width = utf8_valid_sequence(data + offset, end - offset);
    if (width == 0) {
      char message[64];
      snprintf(message, sizeof message, "utf8->string: invalid UTF-8 at byte %zu", offset);
      return io_error(state, "decode-error", "utf8->string", message, "position",
                      ot_make_int((intptr_t)offset));
    }
    offset += width;
  }
  buf bytes = {0};
  buf_write(&bytes, (const char*)data + start, end - start);
  otv result = bytes.failed ? ot_raise(state, "utf8->string: out of memory")
                            : ot_make_string(state, bytes.data, bytes.length);
  buf_free(&bytes);
  return result;
}

static otv nat_string_utf8(ots* state, otv* args, int argc) {
  if (need_arity(state, "string->utf8", argc, 1, 3) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_STRING)) return ot_raise(state, "string->utf8: expected string");
  otv string = args[0];
  const unsigned char* data = as_bytes(as_string(string)->bytes)->data;
  size_t length = as_string(string)->length;
  size_t start = 0, end = 0;
  if (span_arguments(state, "string->utf8", args, argc, 1, utf8_count(data, length), &start,
                     &end) == OT_UNWIND)
    return OT_UNWIND;
  size_t byte_start = utf8_offset(data, length, start);
  size_t byte_end = utf8_offset(data, length, end);
  OT_FRAME_SCOPED(state, &string);
  otv result = ot_make_bytevector(state, byte_end - byte_start);
  memcpy(as_bytevector(result)->data, as_bytes(as_string(string)->bytes)->data + byte_start,
         byte_end - byte_start);
  return result;
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
    ot_slots_obj* slots = as_slots(array->slots);
    for (int i = 1; i < argc; i += 2) {
      if (!ot_is_int(args[i])) return ot_raise(state, "put!: expected integer index");
      intptr_t index = ot_get_int(args[i]);
      if (index < 0 || (size_t)index >= array->length)
        return ot_raise(state, "put!: index out of range");
      ot_store(state, slots, &slots->values[index], args[i + 1]);
    }
    return args[0];
  }
  if (is_type(args[0], OBJ_BYTEVECTOR)) {
    for (int i = 1; i < argc; i += 2)
      if (bytevector_set(state, "put!", args[0], args[i], args[i + 1]) == OT_UNWIND)
        return OT_UNWIND;
    return args[0];
  }
  return ot_raise(state, "put!: expected table, array, or bytevector");
}

static otv nat_set_applied(ots* state, otv* args, int argc) {
  if (need_arity(state, "set-applied!", argc, 3, 3) == OT_UNWIND) return OT_UNWIND;
  if (is_type(args[0], OBJ_RECORD)) return record_write(state, args[0], args[1], args[2]);
  if (is_type(args[0], OBJ_INSTANCE)) return instance_write(state, args[0], args[1], args[2]);
  if (!is_type(args[0], OBJ_TABLE) && !is_type(args[0], OBJ_ARRAY))
    return ot_raise(state, "set-applied!: cannot assign through %s", value_type_name(args[0]));
  if (nat_put(state, args, argc) == OT_UNWIND) return OT_UNWIND;
  return args[2];
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
  ot_slots_obj* slots = as_slots(array->slots);
  otv value = slots->values[--array->length];
  ot_store(state, slots, &slots->values[array->length], ot_nil);
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
  if (is_type(args[0], OBJ_INSTANCE)) return instance_copy(state, args[0]);
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

static otv nat_read_file(ots* state, otv* args, int argc) {
  if (need_arity(state, "read-file", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  const char* bytes;
  size_t length;
  if (!ot_string_bytes(args[0], &bytes, &length) || memchr(bytes, 0, length) != NULL)
    return ot_raise(state, "read-file: expected path without NUL");
  char* path = ot_host_alloc(length + 1);
  if (path == NULL) return ot_raise(state, "read-file: out of memory");
  memcpy(path, bytes, length);
  path[length] = 0;
  char* source = NULL;
  size_t size = 0;
  bool ok = ot_platform_read_file(path, &source, &size);
  ot_host_free(path);
  if (!ok) return ot_raise(state, "read-file: cannot read file");
  otv result = ot_make_string(state, source, size);
  ot_host_free(source);
  return result;
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
  ot_store(state, buffer, &buffer->bytes, bytes);
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
  string = as_string(seed);
  ot_store(state, buffer, &buffer->bytes, bytes);
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
    string = as_string(text);
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

static otv port_output(ots*, otv, otv*, int, bool, bool);
static otv port_current(ots*, bool);

static otv nat_output(ots* state, otv* args, int argc, bool display, bool newline) {
  return port_output(state, port_current(state, false), args, argc, display, newline);
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
  if (need_arity(state, "newline", argc, 0, 1) == OT_UNWIND) return OT_UNWIND;
  return port_output(state, argc ? args[0] : port_current(state, false), NULL, 0, true, true);
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
  return apply_value(state, args[0], values, count, false);
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
  otv parent = ot_nil;
  OT_FRAME_SCOPED(state, &type, &parent);
  parent = args[1] == ot_nil ? ot_nil : coerce_name(state, args[1], false, "define-condition");
  if (parent == OT_UNWIND) return parent;
  for (otv ancestor = parent; is_type(ancestor, OBJ_SYMBOL);) {
    if (ancestor == type) return ot_raise(state, "define-condition: cyclic hierarchy");
    ancestor = ot_table_get(state, state->type_parents, ancestor, ot_nil);
  }
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

/* The namespace whose own definitions hold var, or nil. */
static otv var_owner(ots* state, otv var) {
  otv name = ((ot_var_obj*)ot_as_obj(var))->name;
  for (otv cursor = state->namespaces; is_type(cursor, OBJ_NAMESPACE);
       cursor = ((ot_namespace_obj*)ot_as_obj(cursor))->next)
    if (namespace_var(state, cursor, name, false) == var) return cursor;
  return ot_nil;
}

static otv qualified_symbol(ots* state, otv namespace_value, otv member) {
  OT_FRAME_SCOPED(state, &namespace_value, &member);
  buf text = {0};
  ot_name_obj* space = as_name(((ot_namespace_obj*)ot_as_obj(namespace_value))->name);
  buf_write(&text, space->bytes, space->length);
  buf_write(&text, "/", 1);
  buf_write(&text, as_name(member)->bytes, as_name(member)->length);
  otv symbol = ot_intern(state, text.data, text.length, false);
  buf_free(&text);
  return symbol;
}

static bool symbol_qualified(otv symbol) {
  ot_name_obj* name = as_name(symbol);
  const char* slash = name->length > 2 ? memchr(name->bytes + 1, '/', name->length - 2) : NULL;
  return slash != NULL;
}

static otv nat_expander_qualify(ots* state, otv* args, int argc) {
  if (need_arity(state, "expander-qualify", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_SYMBOL)) return ot_false;
  otv space = state->vm.current_namespace;
  otv symbol = args[0];
  otv var = ot_nil;
  OT_FRAME_SCOPED(state, &space, &symbol, &var);
  var = resolve_var(state, space, symbol, false);
  if (var == OT_UNWIND) return var;
  if (!is_type(var, OBJ_VAR))
    return symbol_qualified(symbol) ? ot_false : qualified_symbol(state, space, symbol);
  otv owner = var_owner(state, var);
  if (!is_type(owner, OBJ_NAMESPACE)) return ot_false;
  return qualified_symbol(state, owner, ((ot_var_obj*)ot_as_obj(var))->name);
}

static otv nat_expander_local_name(ots* state, otv* args, int argc) {
  if (need_arity(state, "expander-local-name", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_SYMBOL) || !symbol_qualified(args[0])) return ot_false;
  otv space = state->vm.current_namespace;
  otv symbol = args[0];
  otv var = ot_nil;
  OT_FRAME_SCOPED(state, &space, &symbol, &var);
  var = resolve_var(state, space, symbol, false);
  if (!is_type(var, OBJ_VAR)) return ot_false;
  otv member = as_name(symbol)->qualified_member;
  if (!is_type(member, OBJ_SYMBOL)) return ot_false;
  return resolve_var(state, space, member, false) == var ? member : ot_false;
}

static otv nat_expander_invoke_macro(ots* state, otv* args, int argc) {
  if (need_arity(state, "expander-invoke-macro", argc, 2, 2) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_MACRO)) return ot_raise(state, "expander-invoke-macro: expected macro");
  otv cursor = args[1];
  if (!proper_list(cursor)) return ot_raise(state, "expander-invoke-macro: expected argument list");
  size_t count = list_length(cursor);
  otv values[count == 0 ? 1 : count];
  for (size_t i = 0; i < count; i++) {
    values[i] = as_pair(cursor)->car;
    cursor = as_pair(cursor)->cdr;
  }
  return apply_value(state, args[0], values, count, true);
}

static otv nat_expander_application(ots* state, otv* args, int argc) {
  if (need_arity(state, "expander-application", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (state->plain_expansion) return args[0];
  otv form = args[0], cache = ot_nil, callable = ot_nil;
  OT_FRAME_SCOPED(state, &form, &cache, &callable);
  size_t count = list_length(form);
  bool write = count == 4 && symbol_is(as_pair(form)->car, "set-applied!");
  bool read = false;
  if (count == 2) {
    otv key = as_pair(as_pair(form)->cdr)->car;
    read = is_type(key, OBJ_PAIR) && symbol_is(as_pair(key)->car, "quote") &&
           list_length(key) == 2 && is_type(as_pair(as_pair(key)->cdr)->car, OBJ_KEYWORD);
  }
  if (!read && !write) return form;
  cache = make_slots(state, 3);
  callable = ot_make_native(state, write ? "cached slot assignment" : "cached slot read",
                            write ? cached_write_call : cached_read_call, cache);
  return ot_cons(state, callable, form);
}

static otv nat_expander_optional_function(ots* state, otv* args, int argc) {
  if (need_arity(state, "expander-optional-function", argc, 2, 2) == OT_UNWIND) return OT_UNWIND;
  return args[state->plain_expansion ? 0 : 1];
}

// C has no host syntax that the expander must leave unwalked.
static otv nat_expander_native_syntax(ots* state, otv* args, int argc) {
  (void)args;
  if (need_arity(state, "expander-native-syntax?", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  return ot_false;
}

static otv nat_expander_fail(ots* state, otv* args, int argc) {
  if (need_arity(state, "expander-fail", argc, 3, 3) == OT_UNWIND) return OT_UNWIND;
  const char* bytes;
  size_t length;
  char code[16] = {0};
  if (!ot_string_bytes(args[0], &bytes, &length) || length >= sizeof code ||
      diagnostic_template((memcpy(code, bytes, length), code), NULL) == NULL)
    return ot_raise(state, "expander-fail: unknown code");
  otv detail = args[1], form = args[2];
  OT_FRAME_SCOPED(state, &detail, &form);
  otv condition = diagnostic_new(state, code);
  OT_FRAME_SCOPED(state, &condition);
  diagnostic_put(state, &condition, "detail", detail);
  diagnostic_put(state, &condition, "form", form);
  return diagnostic_raise(state, condition);
}

// C does not track source positions through expansion yet.
static otv nat_expander_locate(ots* state, otv* args, int argc) {
  if (need_arity(state, "expander-locate", argc, 2, 2) == OT_UNWIND) return OT_UNWIND;
  return args[1];
}

static otv nat_current_ns(ots* state, otv* args, int argc) {
  (void)args;
  if (need_arity(state, "current-ns", argc, 0, 0) == OT_UNWIND) return OT_UNWIND;
  return ((ot_namespace_obj*)ot_as_obj(state->vm.current_namespace))->name;
}

#include "runtime-reference.inc"

void ot_binding_documentation(otv var_value, const char** text, size_t* length) {
  ot_var_obj* var = (ot_var_obj*)ot_as_obj(var_value);
  *text = "";
  *length = 0;
  if (ot_string_bytes(var->doc, text, length)) return;
  if (is_type(var->value, OBJ_NAT)) {
    otv name = ((ot_nat_obj*)ot_as_obj(var->value))->name;
    *text = ot_runtime_documentation(as_name(name)->bytes);
    *length = strlen(*text);
  }
}

static otv nat_describe(ots* state, otv* args, int argc) {
  if (need_arity(state, "describe", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_SYMBOL)) return ot_raise(state, "describe: expected symbol");
  otv var_value = resolve_var(state, state->vm.current_namespace, args[0], true);
  if (!is_type(var_value, OBJ_VAR)) {
    const char* text = ot_runtime_documentation(as_name(args[0])->bytes);
    return *text ? ot_make_string(state, text, strlen(text)) : ot_nil;
  }
  ot_var_obj* var = (ot_var_obj*)ot_as_obj(var_value);
  ot_namespace_obj* owner = NULL;
  for (otv cursor = state->namespaces; is_type(cursor, OBJ_NAMESPACE);
       cursor = ((ot_namespace_obj*)ot_as_obj(cursor))->next) {
    ot_namespace_obj* candidate = (ot_namespace_obj*)ot_as_obj(cursor);
    for (otv vars = candidate->vars; is_type(vars, OBJ_VAR);
         vars = ((ot_var_obj*)ot_as_obj(vars))->next)
      if (vars == var_value) {
        owner = candidate;
        break;
      }
    if (owner != NULL) break;
  }
  if (owner == NULL) return ot_nil;

  ot_name_obj* namespace_name = as_name(owner->name);
  ot_name_obj* var_name = as_name(var->name);
  buf description = {0};
  buf_write(&description, namespace_name->bytes, namespace_name->length);
  buf_byte(&description, '/');
  buf_write(&description, var_name->bytes, var_name->length);
  buf_cstr(&description, ": ");
  const char* text;
  size_t length;
  ot_binding_documentation(var_value, &text, &length);
  if (length) buf_write(&description, text, length);
  else buf_cstr(&description, "no documentation");
  if (description.failed) {
    buf_free(&description);
    return ot_raise(state, "describe: host allocation failed");
  }
  otv result = ot_make_string(state, description.data, description.length);
  buf_free(&description);
  return result;
}

// macroexpand-1 and macroexpand use the shared expander's head expansion.
static otv expand_head(ots* state, otv form, bool once) {
  if (state->expand_head == ot_nil) return form;
  otv values[2] = {form, once ? ot_true : ot_false};
  return apply_value(state, state->expand_head, values, 2, false);
}

static otv nat_macroexpand_one(ots* state, otv* args, int argc) {
  if (need_arity(state, "macroexpand-1", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  return expand_head(state, args[0], true);
}

// macroexpand-all runs the whole expander, leaving applications unlowered.
static otv nat_macroexpand_all(ots* state, otv* args, int argc) {
  if (need_arity(state, "macroexpand-all", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (state->expander == ot_nil) return args[0];
  bool saved = state->plain_expansion;
  state->plain_expansion = true;
  otv result = apply_value(state, state->expander, args, 1, false);
  state->plain_expansion = saved;
  return result;
}

static otv nat_macroexpand(ots* state, otv* args, int argc) {
  if (need_arity(state, "macroexpand", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  return expand_head(state, args[0], false);
}

// Top-level begin is split before expansion, so a macro defined by one
// child is available to the next.
static otv run_top_form(ots* state, otv form) {
  OT_FRAME_SCOPED(state, &form);
  if (state->expand_head != ot_nil) {
    form = expand_head(state, form, false);
    if (form == OT_UNWIND) return form;
    if (is_type(form, OBJ_PAIR) && is_type(as_pair(form)->car, OBJ_SYMBOL) &&
        symbol_is(as_pair(form)->car, "begin")) {
      otv rest = as_pair(form)->cdr;
      otv result = ot_nil;
      OT_FRAME_SCOPED(state, &rest, &result);
      if (!proper_list(rest)) return ot_raise(state, "begin: body must be a proper list");
      while (is_type(rest, OBJ_PAIR)) {
        result = run_top_form(state, as_pair(rest)->car);
        if (result == OT_UNWIND) return result;
        rest = as_pair(rest)->cdr;
      }
      return result;
    }
  }
  if (state->expander != ot_nil) {
    otv values[1] = {form};
    form = apply_value(state, state->expander, values, 1, false);
    if (form == OT_UNWIND) return form;
  }
  return compile_and_run_form(state, form, ot_nil);
}

static otv nat_eval(ots* state, otv* args, int argc) {
  if (need_arity(state, "eval", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  return run_top_form(state, args[0]);
}

static otv nat_read_string(ots* state, otv* args, int argc) {
  if (need_arity(state, "read-string", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_STRING)) return ot_raise(state, "read-string: expected string");
  const char* bytes;
  size_t length;
  ot_string_bytes(args[0], &bytes, &length);
  /* Reading allocates. Keep source bytes outside the moving heap. */
  buf source = {0};
  buf_write(&source, bytes, length);
  reader input = {.state = state, .source = source.data, .length = length, .name = "<string>"};
  otv value = read_form(&input);
  if (value != OT_UNWIND) {
    if (value == OT_UNDEFINED) value = reader_error(&input, "E0010", "read-string: empty input");
    else {
      reader_space(&input);
      if (input.offset != input.length)
        value = reader_error(&input, "E0010", "read-string: trailing input");
    }
  }
  buf_free(&source);
  return value;
}

/* Advance once through the source, counting characters rather than UTF-8 bytes.
 * CRLF is one line break; a bare CR is also a line break, as on Chez. */
static void token_position(const char* source, size_t length, size_t* offset, size_t end,
                           size_t* line, size_t* column) {
  while (*offset < end) {
    unsigned char byte = (unsigned char)source[(*offset)++];
    if ((byte & 0xc0) == 0x80) continue;
    if (byte == '\n' || (byte == '\r' && (*offset == length || source[*offset] != '\n'))) {
      (*line)++;
      *column = 0;
    } else {
      (*column)++;
    }
  }
}

static otv nat_reader_tokens(ots* state, otv* args, int argc) {
  if (need_arity(state, "tokens", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_STRING)) return ot_raise(state, "tokens: expected string");
  const char* bytes;
  size_t length;
  ot_string_bytes(args[0], &bytes, &length);
  buf source = {0}, tokens = {0};
  buf_write(&source, bytes, length);
  reader input = {.state = state,
                  .source = source.data,
                  .length = length,
                  .name = "<string>",
                  .tokens = &tokens};
  otv value;
  do {
    value = read_form(&input);
  } while (value != OT_UNWIND && value != OT_UNDEFINED);
  otv result = value == OT_UNWIND ? OT_UNWIND : ot_null;
  otv entry = ot_null, text = ot_nil, kind = ot_nil, tail = ot_nil;
  OT_FRAME_SCOPED(state, &result, &entry, &text, &kind, &tail);
  if (value != OT_UNWIND) {
    size_t offset = 0, line = 0, column = 0, previous_line = 0;
    for (size_t i = 0; i < tokens.length; i += sizeof(reader_token)) {
      reader_token token;
      memcpy(&token, tokens.data + i, sizeof token);
      token_position(source.data, length, &offset, token.start, &line, &column);
      entry = ot_cons(state, ot_make_int((intptr_t)(line - previous_line)), ot_null);
      entry = ot_cons(state, ot_make_int((intptr_t)column), entry);
      entry = ot_cons(state, ot_make_int((intptr_t)line), entry);
      text = ot_make_string(state, source.data + token.start, token.end - token.start);
      entry = ot_cons(state, text, entry);
      kind = ot_intern(state, token.kind, strlen(token.kind), true);
      entry = ot_cons(state, kind, entry);
      otv cell = ot_cons(state, entry, ot_null);
      if (result == ot_null) result = cell;
      else ot_store(state, as_pair(tail), &as_pair(tail)->cdr, cell);
      tail = cell;
      token_position(source.data, length, &offset, token.end, &line, &column);
      previous_line = line;
    }
  }
  buf_free(&tokens);
  buf_free(&source);
  return result;
}

static void reader_module_init(ots* state) { ot_def_nat(state, "tokens", nat_reader_tokens); }

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

static otv nat_request_exit(ots* state, otv* args, int argc, const char* name) {
  if (need_arity(state, name, argc, 0, 1) == OT_UNWIND) return OT_UNWIND;
  if (argc == 1 && (!ot_is_int(args[0]) || ot_get_int(args[0]) < 0 || ot_get_int(args[0]) > 255))
    return ot_raise(state, "%s: expected status from 0 to 255", name);
  state->vm.unwind_kind = UNWIND_QUIT;
  state->quit_requested = true;
  state->quit_status = argc == 0 ? 0 : (int)ot_get_int(args[0]);
  return OT_UNWIND;
}

static otv nat_quit(ots* state, otv* args, int argc) {
  return nat_request_exit(state, args, argc, "quit");
}

static otv nat_exit(ots* state, otv* args, int argc) {
  return nat_request_exit(state, args, argc, "exit");
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
    return value < 0 ? make_wrapped_int((uintptr_t)0 - (uintptr_t)value) : args[0];
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
  double minimum = (double)OT_INT_MIN;
  double maximum_exclusive = -minimum;
  if (!isfinite(rounded) || rounded < minimum || rounded >= maximum_exclusive)
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
    return make_wrapped_int(result);
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

/* Classes and generic dispatch. */
#define as_dispatch_type(V) ((ot_dispatch_type_obj*)ot_as_obj(V))
#define as_generic(V) ((ot_generic_obj*)ot_as_obj(V))

static otv generic_error(ots* state, const char* code, const char* message) {
  otv condition = ot_table_new(state, 4);
  otv key = ot_nil, value = ot_nil;
  OT_FRAME_SCOPED(state, &condition, &key, &value);
  key = ot_intern(state, "type", 4, true);
  value = ot_intern(state, "error", 5, false);
  ot_table_put(state, condition, key, value);
  key = ot_intern(state, "code", 4, true);
  value = ot_intern(state, code, strlen(code), false);
  ot_table_put(state, condition, key, value);
  key = ot_intern(state, "message", 7, true);
  value = ot_make_string(state, message, strlen(message));
  ot_table_put(state, condition, key, value);
  return ot_raise_value(state, condition);
}

static otv new_dispatch_type(ots* state, otv owner, otv name, bool builtin) {
  OT_FRAME_SCOPED(state, &owner, &name);
  ot_dispatch_type_obj* type = must_alloc(state, sizeof(*type), OBJ_DISPATCH_TYPE);
  ot_store(state, type, &type->owner, owner);
  ot_store(state, type, &type->name, name);
  ot_store(state, type, &type->parent, state->object_class);
  ot_store(state, type, &type->layout, ot_nil);
  type->abstract = false;
  type->builtin = builtin;
  type->stable_id = ++state->next_stable_id;
  return ot_from_obj(type);
}

static otv dispatch_type(ots* state, otv value) {
  if (is_type(value, OBJ_INSTANCE))
    return as_layout(as_instance(instance_payload(value))->layout)->type;
#ifdef OT_WITH_FFI
  if (ot_ffi_owning_pointer(state, value))
    return as_slots(state->dispatch_types)->values[OT_TYPE_INTERNAL + 1];
#endif
  return as_slots(state->dispatch_types)->values[ot_value_type(value)];
}

static bool same_signature(otv a, otv b) {
  size_t n = as_array(a)->length;
  if (n != as_array(b)->length) return false;
  for (size_t i = 0; i < n; i++)
    if (as_slots(as_array(a)->slots)->values[i] != as_slots(as_array(b)->slots)->values[i])
      return false;
  return true;
}

static bool signature_applies(ots* state, otv signature, otv* args, size_t argc) {
  for (size_t i = 0; i < argc; i++) {
    otv type = as_slots(as_array(signature)->slots)->values[i];
    if (type != ot_false && !class_subtype(dispatch_type(state, args[i]), type)) return false;
  }
  return true;
}

static bool signature_dominates(otv a, otv b) {
  bool strict = false;
  for (size_t i = 0; i < as_array(a)->length; i++) {
    otv x = as_slots(as_array(a)->slots)->values[i];
    otv y = as_slots(as_array(b)->slots)->values[i];
    if (x == y) continue;
    if (x == ot_false || (y != ot_false && !class_subtype(x, y))) return false;
    strict = true;
  }
  return strict;
}

/* Effective method chains and per-generic dispatch caches. */
static otv method_qualifier(otv method) { return as_pair(as_pair(method)->cdr)->car; }

static otv method_function(otv method) { return as_pair(as_pair(as_pair(method)->cdr)->cdr)->car; }

static bool method_has_next(otv method) {
  return as_pair(as_pair(as_pair(method)->cdr)->cdr)->cdr == ot_true;
}

static otv method_field(otv context, size_t index) {
  return as_slots(as_array(context)->slots)->values[index];
}

static void invalidate_generic(ots* state, otv value) {
  ot_generic_obj* generic = as_generic(value);
  ot_store(state, generic, &generic->cache, ot_nil);
  ot_store(state, generic, &generic->positions, ot_nil);
  generic->epoch = state->class_epoch;
}

static otv effective_method(ots* state, otv generic, otv methods, otv* args, size_t argc);

static otv invoke_method_chain(ots* state, otv context, otv* args, int argc) {
  otv generic = method_field(context, 0), function = method_field(context, 1);
  otv remaining = method_field(context, 2);
  OT_FRAME_SCOPED(state, &context, &generic, &function, &remaining);
  if (function == ot_false) {
    buf message = {0};
    buf_cstr(&message,
             remaining == ot_null ? "no applicable method for " : "ambiguous methods for ");
    render_value(state, &message, generic, false, 0);
    for (otv p = remaining; p != ot_null; p = as_pair(p)->cdr) {
      buf_cstr(&message, " ");
      render_value(state, &message, as_pair(as_pair(p)->car)->car, false, 0);
    }
    buf_byte(&message, '\0');
    otv result = generic_error(state, remaining == ot_null ? "missing-method" : "ambiguous-method",
                               message.failed ? "method selection failed" : message.data);
    buf_free(&message);
    return result;
  }
  return apply_value(state, function, args, (size_t)argc, false);
}

static otv invoke_next_method(ots* state, otv context, otv* args, int argc) {
  otv generic = method_field(context, 0);
  if ((size_t)argc != as_generic(generic)->arity)
    return generic_error(state, "wrong-arity", "incorrect next method arity");
  OT_FRAME_SCOPED(state, &context, &generic);
  otv values[argc == 0 ? 1 : argc];
  for (int i = 0; i < argc; i++) values[i] = args[i];
  otv* roots[argc == 0 ? 1 : argc];
  for (int i = 0; i < argc; i++) roots[i] = &values[i];
  ot_frame arguments_frame [[gnu::cleanup(ot_frame_cleanup)]];
  ot_frame_push(state, &arguments_frame, roots, (size_t)argc);
  bool same = true;
  for (int i = 0; i < argc; i++)
    if (dispatch_type(state, values[i]) != method_field(method_field(context, 3), (size_t)i)) {
      same = false;
      break;
    }
  otv next = same
                 ? method_field(context, 1)
                 : effective_method(state, generic, method_field(context, 2), values, (size_t)argc);
  if (next == OT_UNWIND) return next;
  return apply_value(state, next, values, (size_t)argc, false);
}

static otv effective_method(ots* state, otv generic, otv methods, otv* args, size_t argc) {
  otv applicable = ot_null, best = ot_nil, remaining = ot_null, next = ot_nil;
  otv cursor = methods, function = ot_false;
  OT_FRAME_SCOPED(state, &generic, &methods, &applicable, &best, &remaining, &next, &cursor,
                  &function);
  bool around = false;
  while (cursor != ot_null) {
    otv method = as_pair(cursor)->car;
    if (signature_applies(state, as_pair(method)->car, args, argc)) {
      if (method_qualifier(method) == ot_true) around = true;
      applicable = ot_cons(state, method, applicable);
    }
    cursor = as_pair(cursor)->cdr;
  }
  size_t count = 0;
  for (otv p = applicable; p != ot_null; p = as_pair(p)->cdr) {
    otv method = as_pair(p)->car;
    if ((method_qualifier(method) == ot_true) != around) continue;
    bool dominated = false;
    for (otv q = applicable; q != ot_null; q = as_pair(q)->cdr) {
      otv other = as_pair(q)->car;
      if (method_qualifier(other) == method_qualifier(method) &&
          signature_dominates(as_pair(other)->car, as_pair(method)->car)) {
        dominated = true;
        break;
      }
    }
    if (!dominated) {
      best = method;
      count++;
    }
  }
  if (count == 1) {
    function = method_function(best);
    cursor = applicable;
    while (cursor != ot_null) {
      otv method = as_pair(cursor)->car;
      if (method != best) remaining = ot_cons(state, method, remaining);
      cursor = as_pair(cursor)->cdr;
    }
    if (!method_has_next(best)) return function;
    next = ot_false;
    if (remaining != ot_null) {
      otv types = ot_array_new(state, argc);
      OT_FRAME_SCOPED(state, &types);
      for (size_t i = 0; i < argc; i++) array_push(state, &types, dispatch_type(state, args[i]));
      next = effective_method(state, generic, remaining, args, argc);
      if (next == OT_UNWIND) return next;
      otv fields[] = {generic, next, remaining, types};
      otv context = make_descriptor(state, fields, 4);
      next = ot_make_native(state, "next method", invoke_next_method, context);
    }
    return apply_value(state, function, &next, 1, false);
  } else remaining = applicable;
  otv fields[] = {generic, function, remaining, next};
  otv context = make_descriptor(state, fields, 4);
  return ot_make_native(state, "effective method", invoke_method_chain, context);
}

static otv generic_select(ots* state, otv generic, otv* args, size_t argc) {
  if (argc != as_generic(generic)->arity)
    return generic_error(state, "wrong-arity", "generic called with incorrect arity");
  OT_FRAME_SCOPED(state, &generic);
  if (as_generic(generic)->epoch != state->class_epoch) invalidate_generic(state, generic);
  if (as_generic(generic)->cache == ot_nil) {
    otv cache = ot_nil, positions = ot_nil;
    OT_FRAME_SCOPED(state, &cache, &positions);
    cache = ot_table_new(state, 8);
    positions = ot_array_new(state, argc);
    for (size_t i = 0; i < argc; i++) {
      bool specialized = false;
      for (otv p = as_generic(generic)->methods; p != ot_null; p = as_pair(p)->cdr)
        if (method_field(as_pair(as_pair(p)->car)->car, i) != ot_false) {
          specialized = true;
          break;
        }
      if (specialized) array_push(state, &positions, ot_make_int((intptr_t)i));
    }
    ot_generic_obj* target = as_generic(generic);
    ot_store(state, target, &target->cache, cache);
    ot_store(state, target, &target->positions, positions);
  }
  otv cache = as_generic(generic)->cache, positions = as_generic(generic)->positions;
  otv key = ot_false, found = ot_nil;
  OT_FRAME_SCOPED(state, &cache, &positions, &key, &found);
  size_t count = as_array(positions)->length;
  for (size_t i = 0; i < (count == 0 ? 1 : count); i++) {
    key = count == 0 ? state->object_class
                     : dispatch_type(state, args[ot_get_int(method_field(positions, i))]);
    found = ot_table_get(state, cache, key, ot_nil);
    if (i + 1 >= count) {
      if (found == ot_nil) {
        found = effective_method(state, generic, as_generic(generic)->methods, args, argc);
        if (found == OT_UNWIND) return found;
        if (ot_table_put(state, cache, key, found) == OT_UNWIND) return OT_UNWIND;
      }
      return found;
    }
    if (found == ot_nil) {
      found = ot_table_new(state, 4);
      if (ot_table_put(state, cache, key, found) == OT_UNWIND) return OT_UNWIND;
    }
    cache = found;
  }
  return OT_UNWIND;
}

static otv default_update_instance(ots* state, otv context, otv* args, int argc) {
  (void)state;
  (void)context;
  (void)args;
  (void)argc;
  return ot_nil;
}

static otv signature_from_list(ots* state, otv generic, otv list) {
  OT_FRAME_SCOPED(state, &generic, &list);
  if (!is_type(generic, OBJ_GENERIC))
    return generic_error(state, "not-generic", "expected generic");
  size_t length;
  if (!proper_list_length(list, &length) || length != as_generic(generic)->arity)
    return generic_error(state, "invalid-signature", "signature has incorrect arity");
  for (otv p = list; p != ot_null; p = as_pair(p)->cdr)
    if (as_pair(p)->car != ot_false && !is_type(as_pair(p)->car, OBJ_DISPATCH_TYPE))
      return generic_error(state, "invalid-signature", "expected descriptor or #f");
  otv signature = ot_array_new(state, length);
  OT_FRAME_SCOPED(state, &signature);
  while (list != ot_null) {
    array_push(state, &signature,
               as_pair(list)->car == state->object_class ? ot_false : as_pair(list)->car);
    list = as_pair(list)->cdr;
  }
  return signature;
}

static otv register_method(ots* state, otv generic, otv signature, otv function, otv qualifier,
                           otv has_next) {
  otv methods = ot_null, cursor = as_generic(generic)->methods;
  OT_FRAME_SCOPED(state, &generic, &signature, &function, &qualifier, &has_next, &methods, &cursor);
  invalidate_generic(state, generic);
  /* Keep old method lists intact for escaped next-method closures. */
  while (cursor != ot_null) {
    otv method = as_pair(cursor)->car;
    if (!(same_signature(as_pair(method)->car, signature) && method_qualifier(method) == qualifier))
      methods = ot_cons(state, method, methods);
    cursor = as_pair(cursor)->cdr;
  }
  otv method = ot_cons(state, function, has_next);
  OT_FRAME_SCOPED(state, &method);
  method = ot_cons(state, qualifier, method);
  method = ot_cons(state, signature, method);
  methods = ot_cons(state, method, methods);
  ot_generic_obj* object = as_generic(generic);
  ot_store(state, object, &object->methods, methods);
  return ot_nil;
}

/* Class storage and declarations. */
static otv array_item(otv array, size_t index) {
  return as_slots(as_array(array)->slots)->values[index];
}

static bool class_subtype(otv child, otv parent) {
  while (is_type(child, OBJ_DISPATCH_TYPE)) {
    if (child == parent) return true;
    child = as_dispatch_type(child)->parent;
  }
  return false;
}

static otv class_slot_error(ots* state, const char* code, const char* text, otv type, otv key) {
  buf message = {0};
  buf_cstr(&message, text);
  buf_cstr(&message, ": ");
  render_value(state, &message, type, false, 0);
  buf_cstr(&message, " ");
  render_value(state, &message, key, false, 0);
  buf_byte(&message, '\0');
  otv result = generic_error(state, code, message.failed ? text : message.data);
  buf_free(&message);
  return result;
}

static otv allocate_instance(ots* state, otv layout) {
  OT_FRAME_SCOPED(state, &layout);
  size_t count = as_array(as_layout(layout)->keys)->length;
  ot_instance_obj* object = must_alloc(state, sizeof(*object) + count * sizeof(otv), OBJ_INSTANCE);
  ot_store(state, object, &object->layout, layout);
  object->stable_id = ++state->next_stable_id;
  for (size_t i = 0; i < count; i++) ot_store(state, object, &object->values[i], OT_UNDEFINED);
  return ot_from_obj(object);
}

static otv check_instance_slots(ots* state, otv object) {
  otv layout = as_instance(object)->layout;
  for (size_t i = 0; i < as_array(as_layout(layout)->keys)->length; i++)
    if (as_instance(object)->values[i] == OT_UNDEFINED)
      return class_slot_error(state, "uninitialized-slot", "required slot has no value",
                              as_layout(layout)->type, array_item(as_layout(layout)->keys, i));
  return object;
}

static otv instance_current_properties(ots* state, otv original, otv properties) {
  if (!is_type(original, OBJ_INSTANCE))
    return generic_error(state, "invalid-slot-operation", "expected instance");
  otv object = instance_payload(original);
  otv old_layout = as_instance(object)->layout;
  otv type = as_layout(old_layout)->type;
  otv layout = as_dispatch_type(type)->layout;
  if (old_layout == layout) return object;
  otv fresh = ot_nil, old_slots = ot_nil, result = ot_nil;
  OT_FRAME_SCOPED(state, &original, &object, &old_layout, &type, &layout, &fresh, &old_slots,
                  &result, &properties);
  old_slots = ot_table_new(state, as_array(as_layout(old_layout)->keys)->length);
  for (size_t i = 0; i < as_array(as_layout(old_layout)->keys)->length; i++) {
    result = ot_table_put(state, old_slots, array_item(as_layout(old_layout)->keys, i),
                          as_instance(object)->values[i]);
    if (result == OT_UNWIND) return result;
  }
  fresh = allocate_instance(state, layout);
  /* Publish the new layout before defaults and update-instance can access this object. */
  ot_instance_obj* previous = as_instance(object);
  ot_store(state, previous, &previous->layout, fresh);
  for (size_t i = 0; i < as_array(as_layout(layout)->keys)->length; i++) {
    otv index = ot_table_get(state, as_layout(old_layout)->indices,
                             array_item(as_layout(layout)->keys, i), OT_UNDEFINED);
    if (properties != ot_nil && array_item(as_layout(layout)->states, i) == ot_false)
      result = as_instance(properties)->values[i];
    else if (index != OT_UNDEFINED &&
             array_item(as_layout(old_layout)->states, ot_get_int(index)) ==
                 array_item(as_layout(layout)->states, i))
      result = as_instance(object)->values[ot_get_int(index)];
    else {
      otv initializer = array_item(as_layout(layout)->defaults, i);
      result =
          initializer == ot_false ? OT_UNDEFINED : apply_value(state, initializer, NULL, 0, false);
      if (result == OT_UNWIND) goto failed;
    }
    ot_instance_obj* target = as_instance(fresh);
    ot_store(state, target, &target->values[i], result);
  }
  if (state->update_instance != ot_nil) {
    otv args[] = {original, old_slots};
    result = apply_value(state, state->update_instance, args, 2, false);
    if (result == OT_UNWIND) goto failed;
  }
  result = check_instance_slots(state, fresh);
  if (result == OT_UNWIND) goto failed;
  for (size_t i = 0; i < as_array(as_layout(old_layout)->keys)->length; i++) {
    ot_instance_obj* stale = as_instance(object);
    ot_store(state, stale, &stale->values[i], ot_nil);
  }
  return fresh;
failed:;
  ot_instance_obj* rollback = as_instance(object);
  ot_store(state, rollback, &rollback->layout, old_layout);
  return OT_UNWIND;
}

static otv instance_current(ots* state, otv original) {
  return instance_current_properties(state, original, ot_nil);
}

static otv instance_index(ots* state, otv object, otv key) {
  otv layout = as_instance(object)->layout;
  otv index = is_type(key, OBJ_KEYWORD)
                  ? ot_table_get(state, as_layout(layout)->indices, key, OT_UNDEFINED)
                  : OT_UNDEFINED;
  if (index == OT_UNDEFINED)
    return class_slot_error(state, "missing-slot", "class has no such slot",
                            as_layout(layout)->type, key);
  return index;
}

static otv instance_read(ots* state, otv object, otv key) {
  OT_FRAME_SCOPED(state, &object, &key);
  object = instance_current(state, object);
  if (object == OT_UNWIND) return object;
  otv index = instance_index(state, object, key);
  if (index == OT_UNWIND) return index;
  otv value = as_instance(object)->values[ot_get_int(index)];
  if (value == OT_UNDEFINED)
    return class_slot_error(state, "uninitialized-slot", "slot has no value",
                            as_layout(as_instance(object)->layout)->type, key);
  return value;
}

static otv instance_write(ots* state, otv object, otv key, otv value) {
  OT_FRAME_SCOPED(state, &object, &key, &value);
  object = instance_current(state, object);
  if (object == OT_UNWIND) return object;
  otv index = instance_index(state, object, key);
  if (index == OT_UNWIND) return index;
  ot_instance_obj* target = as_instance(object);
  ot_store(state, target, &target->values[ot_get_int(index)], value);
  return value;
}

static otv instance_copy(ots* state, otv object) {
  OT_FRAME_SCOPED(state, &object);
  object = instance_current(state, object);
  if (object == OT_UNWIND) return object;
  otv copy = allocate_instance(state, as_instance(object)->layout);
  size_t count = as_array(as_layout(as_instance(object)->layout)->keys)->length;
  ot_instance_obj* target = as_instance(copy);
  for (size_t i = 0; i < count; i++)
    ot_store(state, target, &target->values[i], as_instance(object)->values[i]);
  return copy;
}

static otv make_instance_properties(ots* state, otv* args, int argc, bool properties_only) {
  if (argc < 1 || !is_type(args[0], OBJ_DISPATCH_TYPE) || as_dispatch_type(args[0])->builtin ||
      as_dispatch_type(args[0])->abstract)
    return generic_error(state, "invalid-class", "make expects a concrete user class");
  if (argc % 2 != 1)
    return generic_error(state, "invalid-slot-operation", "make expects keyword/value pairs");
  otv layout = as_dispatch_type(args[0])->layout;
  otv object = ot_nil, result = ot_nil;
  OT_FRAME_SCOPED(state, &layout, &object, &result);
  object = allocate_instance(state, layout);
  for (int i = 1; i < argc; i += 2) {
    otv index = instance_index(state, object, args[i]);
    if (index == OT_UNWIND) return index;
    if (properties_only && array_item(as_layout(layout)->states, ot_get_int(index)) == ot_true)
      return generic_error(state, "invalid-slot-operation",
                           "state slot cannot be supplied as a property");
    ot_instance_obj* target = as_instance(object);
    ot_store(state, target, &target->values[ot_get_int(index)], args[i + 1]);
  }
  for (size_t i = 0; i < as_array(as_layout(layout)->keys)->length; i++) {
    if (properties_only && array_item(as_layout(layout)->states, i) == ot_true) {
      ot_instance_obj* target = as_instance(object);
      ot_store(state, target, &target->values[i], ot_nil);
      continue;
    }
    if (as_instance(object)->values[i] != OT_UNDEFINED) continue;
    otv initializer = array_item(as_layout(layout)->defaults, i);
    if (initializer == ot_false) continue;
    result = apply_value(state, initializer, NULL, 0, false);
    if (result == OT_UNWIND) return result;
    ot_instance_obj* target = as_instance(object);
    ot_store(state, target, &target->values[i], result);
  }
  return check_instance_slots(state, object);
}

static otv nat_make_instance(ots* state, otv* args, int argc) {
  return make_instance_properties(state, args, argc, false);
}

static otv nat_class_properties(ots* state, otv* args, int argc) {
  return make_instance_properties(state, args, argc, true);
}

/* Descriptions must be rebuilt after class redefinition, never lazily migrated. */
static otv current_properties(ots* state, otv object) {
  if (!is_type(object, OBJ_INSTANCE))
    return generic_error(state, "invalid-slot-operation", "expected property instance");
  object = instance_payload(object);
  otv layout = as_instance(object)->layout;
  if (layout != as_dispatch_type(as_layout(layout)->type)->layout)
    return generic_error(state, "stale-properties", "rebuild description after class redefinition");
  return object;
}

static otv nat_class_mount(ots* state, otv* args, int argc) {
  if (need_arity(state, "class-mount", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  otv source = current_properties(state, args[0]);
  if (source == OT_UNWIND) return source;
  otv object = ot_nil, layout = as_instance(source)->layout, result = ot_nil;
  OT_FRAME_SCOPED(state, &source, &object, &layout, &result);
  object = allocate_instance(state, layout);
  for (size_t i = 0; i < as_array(as_layout(layout)->keys)->length; i++) {
    result = as_instance(source)->values[i];
    if (array_item(as_layout(layout)->states, i) == ot_true)
      result = apply_value(state, array_item(as_layout(layout)->defaults, i), NULL, 0, false);
    if (result == OT_UNWIND) return result;
    ot_instance_obj* target = as_instance(object);
    ot_store(state, target, &target->values[i], result);
  }
  return check_instance_slots(state, object);
}

static otv nat_class_update_properties(ots* state, otv* args, int argc) {
  if (need_arity(state, "class-update-properties!", argc, 2, 2) == OT_UNWIND) return OT_UNWIND;
  otv source = current_properties(state, args[1]), object = args[0];
  if (source == OT_UNWIND) return source;
  if (!is_type(object, OBJ_INSTANCE) ||
      as_layout(as_instance(instance_payload(object))->layout)->type !=
          as_layout(as_instance(source)->layout)->type)
    return generic_error(state, "invalid-class",
                         "properties must have the same class as the instance");
  OT_FRAME_SCOPED(state, &source, &object);
  object = instance_current_properties(state, object, source);
  if (object == OT_UNWIND) return object;
  otv layout = as_instance(object)->layout;
  for (size_t i = 0; i < as_array(as_layout(layout)->keys)->length; i++) {
    if (array_item(as_layout(layout)->states, i) == ot_false) {
      ot_instance_obj* target = as_instance(object);
      ot_store(state, target, &target->values[i], as_instance(source)->values[i]);
    }
  }
  return args[0];
}

static otv nat_class_subtype_p(ots* state, otv* args, int argc) {
  if (need_arity(state, "class-subtype?", argc, 2, 2) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_DISPATCH_TYPE) || !is_type(args[1], OBJ_DISPATCH_TYPE))
    return generic_error(state, "invalid-class", "class-subtype? expects classes");
  return class_subtype(args[0], args[1]) ? ot_true : ot_false;
}

static otv nat_instance_p(ots* state, otv* args, int argc) {
  if (need_arity(state, "instance?", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  return is_type(args[0], OBJ_INSTANCE) ? ot_true : ot_false;
}

static otv nat_slot_set(ots* state, otv* args, int argc) {
  if (need_arity(state, "slot-set!", argc, 3, 3) == OT_UNWIND) return OT_UNWIND;
  return instance_write(state, args[0], args[1], args[2]);
}

static bool class_option(otv value, const char* name) {
  return is_type(value, OBJ_KEYWORD) && as_name(value)->length == strlen(name) &&
         memcmp(as_name(value)->bytes, name, strlen(name)) == 0;
}

static bool local_class_name(otv name) {
  return is_type(name, OBJ_SYMBOL) && !as_name(name)->special_form &&
         !memchr(as_name(name)->bytes, '/', as_name(name)->length);
}

static bool compile_class_declaration(ots* state, bytecode_compiler* compiler, otv form) {
  otv kind = as_pair(form)->car, name = as_pair(as_pair(form)->cdr)->car;
  otv rest = list_tail(form, 2), parent = ot_false, specs = ot_nil;
  OT_FRAME_SCOPED(state, &form, &kind, &name, &rest, &parent, &specs);
  if (!local_class_name(name)) goto invalid;
  if (is_type(rest, OBJ_PAIR) && class_option(as_pair(rest)->car, "is")) {
    if (!list_nth(rest, 1, &parent) || !is_type(parent, OBJ_SYMBOL)) goto invalid;
    rest = list_tail(rest, 2);
  }
  if (symbol_is(kind, "defabstract") && rest != ot_null) goto invalid;
  specs = ot_array_new(state, list_length(rest));
  while (rest != ot_null) {
    otv slot = as_pair(rest)->car, options = ot_null, key = ot_nil;
    otv init = ot_false, accessor = ot_false, state_slot = ot_false;
    OT_FRAME_SCOPED(state, &slot, &options, &key, &init, &accessor, &state_slot);
    if (is_type(slot, OBJ_PAIR)) {
      options = as_pair(slot)->cdr;
      slot = as_pair(slot)->car;
    }
    if (!is_type(slot, OBJ_SYMBOL) || !proper_list(options) || list_length(options) % 2 != 0)
      goto invalid;
    key = coerce_name(state, slot, true, "defclass");
    for (size_t i = 0; i < as_array(specs)->length; i++)
      if (array_item(array_item(specs, i), 0) == key) goto invalid;
    bool seen_init = false, seen_accessor = false, seen_state = false;
    while (options != ot_null) {
      otv option = as_pair(options)->car;
      otv value = as_pair(as_pair(options)->cdr)->car;
      OT_FRAME_SCOPED(state, &option, &value);
      if (class_option(option, "init") && !seen_init) {
        seen_init = true;
        otv fn = ot_nil, expanded = ot_nil;
        OT_FRAME_SCOPED(state, &fn, &expanded);
        fn = ot_cons(state, value, ot_null);
        fn = ot_cons(state, ot_null, fn);
        otv fn_name = ot_intern(state, "fn", 2, false);
        fn = ot_cons(state, fn_name, fn);
        expanded = apply_value(state, state->expander, &fn, 1, false);
        if (expanded == OT_UNWIND) return false;
        init = compile_function_code(state, ot_null, list_tail(expanded, 2), ot_nil,
                                     state->vm.current_namespace, slot);
        if (init == OT_UNWIND) return false;
      } else if (class_option(option, "accessor") && !seen_accessor && local_class_name(value)) {
        seen_accessor = true;
        accessor = value;
      } else if (class_option(option, "state") && !seen_state &&
                 (value == ot_true || value == ot_false)) {
        seen_state = true;
        state_slot = value;
      } else goto invalid;
      options = list_tail(options, 2);
    }
    if (state_slot == ot_true && !seen_init) goto invalid;
    otv fields[] = {key, init, accessor, state_slot};
    otv spec = make_descriptor(state, fields, 4);
    array_push(state, &specs, spec);
    rest = as_pair(rest)->cdr;
  }
  otv fields[] = {kind, name, parent, specs};
  otv descriptor = make_descriptor(state, fields, 4);
  bytecode_emit_constant(state, compiler, BC_GENERIC_DECL, descriptor);
  return !compiler->bytes.failed;
invalid:
  generic_error(state, "invalid-class-definition",
                "invalid class name, parent, slot, or slot options");
  return false;
}

static otv new_generic(ots* state, otv owner, otv name, size_t arity) {
  OT_FRAME_SCOPED(state, &owner, &name);
  ot_generic_obj* generic = must_alloc(state, sizeof(*generic), OBJ_GENERIC);
  ot_store(state, generic, &generic->owner, owner);
  ot_store(state, generic, &generic->name, name);
  ot_store(state, generic, &generic->methods, ot_null);
  ot_store(state, generic, &generic->cache, ot_nil);
  ot_store(state, generic, &generic->positions, ot_nil);
  generic->epoch = state->class_epoch;
  generic->arity = arity;
  generic->stable_id = ++state->next_stable_id;
  return ot_from_obj(generic);
}

static otv slot_accessor(ots* state, otv key, otv* args, int argc) {
  if (need_arity(state, "slot accessor", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  return instance_read(state, args[0], key);
}

static otv execute_class_declaration(ots* state, otv declaration) {
  otv kind = array_item(declaration, 0), name = array_item(declaration, 1);
  otv parent = array_item(declaration, 2), specs = array_item(declaration, 3);
  otv space = state->vm.current_namespace;
  otv owner = ((ot_namespace_obj*)ot_as_obj(space))->name;
  otv type = ot_nil, existing = ot_nil, layout = ot_nil;
  otv keys = ot_nil, defaults = ot_nil, states = ot_nil, indices = ot_nil;
  OT_FRAME_SCOPED(state, &declaration, &kind, &name, &parent, &specs, &space, &owner, &type,
                  &existing, &layout, &keys, &defaults, &states, &indices);
  bool abstract = symbol_is(kind, "defabstract");
  bool reset = symbol_is(kind, "reset-class!");
  existing = namespace_var(state, space, name, false);
  otv referred = namespace_var(state, space, name, true);
  if (symbol_is(owner, "otium.generic") || (existing == ot_nil && referred != ot_nil) ||
      (reset && existing == ot_nil))
    return generic_error(state, "binding-conflict", "class declaration conflicts with binding");
  if (existing != ot_nil && !reset) {
    type = ((ot_var_obj*)ot_as_obj(existing))->value;
    if (!is_type(type, OBJ_DISPATCH_TYPE) || as_dispatch_type(type)->owner != owner ||
        as_dispatch_type(type)->name != name || as_dispatch_type(type)->abstract != abstract)
      return generic_error(state, "binding-conflict", "incompatible class declaration");
  }
  if (parent == ot_false) parent = state->object_class;
  else {
    otv var = resolve_var(state, space, parent, false);
    parent = is_type(var, OBJ_VAR) ? ((ot_var_obj*)ot_as_obj(var))->value : ot_nil;
  }
  if (!is_type(parent, OBJ_DISPATCH_TYPE) || !as_dispatch_type(parent)->abstract ||
      (type != ot_nil && class_subtype(parent, type)))
    return generic_error(state, "invalid-class-definition", "expected acyclic abstract parent");
  for (size_t i = 0; i < as_array(specs)->length; i++) {
    otv accessor = array_item(array_item(specs, i), 2);
    if (accessor == ot_false) continue;
    otv var = namespace_var(state, space, accessor, false);
    otv imported = namespace_var(state, space, accessor, true);
    if (accessor == name || (var == ot_nil && imported != ot_nil))
      return generic_error(state, "binding-conflict", "accessor conflicts with binding");
    if (var != ot_nil) {
      otv value = ((ot_var_obj*)ot_as_obj(var))->value;
      if (!is_type(value, OBJ_GENERIC) || as_generic(value)->arity != 1 ||
          as_generic(value)->owner != owner || as_generic(value)->name != accessor)
        return generic_error(state, "binding-conflict", "accessor conflicts with binding");
    }
  }
  if (type == ot_nil) type = new_dispatch_type(state, owner, name, false);
  size_t count = as_array(specs)->length;
  keys = ot_array_new(state, count);
  defaults = ot_array_new(state, count);
  states = ot_array_new(state, count);
  indices = ot_table_new(state, count);
  for (size_t i = 0; i < count; i++) {
    otv spec = array_item(specs, i), init = array_item(spec, 1);
    OT_FRAME_SCOPED(state, &spec, &init);
    array_push(state, &keys, array_item(spec, 0));
    if (init != ot_false) init = make_bytecode_function(state, init, ot_nil, space, name);
    array_push(state, &defaults, init);
    array_push(state, &states, array_item(spec, 3));
    otv result = ot_table_put(state, indices, array_item(spec, 0), ot_make_int((intptr_t)i));
    if (result == OT_UNWIND) return result;
  }
  ot_layout_obj* made = must_alloc(state, sizeof(*made), OBJ_LAYOUT);
  ot_store(state, made, &made->type, type);
  ot_store(state, made, &made->keys, keys);
  ot_store(state, made, &made->defaults, defaults);
  ot_store(state, made, &made->states, states);
  ot_store(state, made, &made->indices, indices);
  layout = ot_from_obj(made);
  ot_dispatch_type_obj* descriptor = as_dispatch_type(type);
  descriptor->abstract = abstract;
  ot_store(state, descriptor, &descriptor->parent, parent);
  ot_store(state, descriptor, &descriptor->layout, layout);
  state->class_epoch++;
  define_var(state, space, name, type, ot_nil, false);
  for (size_t i = 0; i < count; i++) {
    otv accessor = array_item(array_item(specs, i), 2);
    if (accessor == ot_false) continue;
    otv generic = ot_nil, function = ot_nil, signature = ot_nil;
    OT_FRAME_SCOPED(state, &accessor, &generic, &function, &signature);
    otv var = namespace_var(state, space, accessor, false);
    generic = var != ot_nil ? ((ot_var_obj*)ot_as_obj(var))->value
                            : new_generic(state, owner, accessor, 1);
    function =
        ot_make_native(state, "slot accessor", slot_accessor, array_item(array_item(specs, i), 0));
    signature = ot_array_new(state, 1);
    array_push(state, &signature, type);
    register_method(state, generic, signature, function, ot_false, ot_false);
    define_var(state, space, accessor, generic, ot_nil, false);
  }
  return ot_nil;
}

static otv cached_instance_index(ots* state, otv cache, otv object, otv key) {
  otv layout = as_instance(object)->layout;
  if (as_slots(cache)->values[0] == layout && as_slots(cache)->values[1] == key)
    return as_slots(cache)->values[2];
  otv index = instance_index(state, object, key);
  if (index == OT_UNWIND) return index;
  ot_slots_obj* slots = as_slots(cache);
  ot_store(state, slots, &slots->values[0], layout);
  ot_store(state, slots, &slots->values[1], key);
  ot_store(state, slots, &slots->values[2], index);
  return index;
}

static otv cached_read_call(ots* state, otv cache, otv* args, int argc) {
  if (need_arity(state, "cached slot read", argc, 2, 2) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_INSTANCE)) return apply_value(state, args[0], args + 1, 1, false);
  OT_FRAME_SCOPED(state, &cache);
  otv object = instance_current(state, args[0]);
  if (object == OT_UNWIND) return object;
  otv index = cached_instance_index(state, cache, object, args[1]);
  if (index == OT_UNWIND) return index;
  otv value = as_instance(object)->values[ot_get_int(index)];
  if (value == OT_UNDEFINED)
    return class_slot_error(state, "uninitialized-slot", "slot has no value",
                            as_layout(as_instance(object)->layout)->type, args[1]);
  return value;
}

static otv cached_write_call(ots* state, otv cache, otv* args, int argc) {
  if (need_arity(state, "cached slot assignment", argc, 4, 4) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_NAT) ||
      ((ot_nat_obj*)ot_as_obj(args[0]))->function != nat_set_applied ||
      !is_type(args[1], OBJ_INSTANCE))
    return apply_value(state, args[0], args + 1, 3, false);
  OT_FRAME_SCOPED(state, &cache);
  otv object = instance_current(state, args[1]);
  if (object == OT_UNWIND) return object;
  otv index = cached_instance_index(state, cache, object, args[2]);
  if (index == OT_UNWIND) return index;
  ot_instance_obj* target = as_instance(object);
  ot_store(state, target, &target->values[ot_get_int(index)], args[3]);
  return args[3];
}

static bool compile_generic_declaration(ots* state, bytecode_compiler* compiler, otv form,
                                        otv env) {
  OT_FRAME_SCOPED(state, &compiler->constants, &form, &env);
  if (env != ot_nil) {
    generic_error(state, "invalid-declaration", "generic declarations require top-level context");
    return false;
  }
  if (!proper_list(form) || list_length(form) < 2) {
    generic_error(state, "invalid-declaration", "malformed generic declaration");
    return false;
  }
  otv declaration_kind = as_pair(form)->car;
  if (symbol_is(declaration_kind, "defclass") || symbol_is(declaration_kind, "defabstract") ||
      symbol_is(declaration_kind, "reset-class!"))
    return compile_class_declaration(state, compiler, form);
  otv kind = as_pair(form)->car, head = as_pair(as_pair(form)->cdr)->car;
  otv name = head, params = ot_null, signature = ot_null, code = ot_nil;
  otv qualifier = ot_false;
  OT_FRAME_SCOPED(state, &kind, &head, &name, &params, &signature, &code, &qualifier);
  if (symbol_is(kind, "defmethod") && class_option(head, "around")) {
    qualifier = ot_true;
    form = ot_cons(state, kind, list_tail(form, 2));
    if (list_length(form) < 3) {
      generic_error(state, "invalid-declaration", "around method needs a signature and body");
      return false;
    }
    head = as_pair(as_pair(form)->cdr)->car;
    name = head;
  }
  bool method = symbol_is(kind, "defmethod");
  bool type = symbol_is(kind, "defclass") || symbol_is(kind, "reset-class!");
  if (!type) {
    if (!is_type(head, OBJ_PAIR) || !proper_list(head)) {
      generic_error(state, "invalid-declaration", "expected fixed parameter list");
      return false;
    }
    name = as_pair(head)->car;
  }
  if (!is_type(name, OBJ_SYMBOL) ||
      (!method && memchr(as_name(name)->bytes, '/', as_name(name)->length)) ||
      (!method && list_length(form) != 2) || (method && list_length(form) < 3)) {
    generic_error(state, "invalid-declaration", "invalid declaration name or body");
    return false;
  }
  if (!type) {
    otv rest = as_pair(head)->cdr;
    OT_FRAME_SCOPED(state, &rest);
    while (rest != ot_null) {
      otv parameter = as_pair(rest)->car, specializer = ot_false;
      OT_FRAME_SCOPED(state, &parameter, &specializer);
      if (method && is_type(parameter, OBJ_PAIR) && list_length(parameter) == 2 &&
          proper_list(parameter)) {
        specializer = as_pair(as_pair(parameter)->cdr)->car;
        parameter = as_pair(parameter)->car;
        if (!is_type(specializer, OBJ_SYMBOL)) parameter = ot_nil;
      }
      if (!is_type(parameter, OBJ_SYMBOL) || symbol_is(parameter, "&")) {
        generic_error(state, "invalid-declaration", "expected parameter or (parameter Type)");
        return false;
      }
      for (otv p = params; p != ot_null; p = as_pair(p)->cdr)
        if (as_pair(p)->car == parameter) {
          generic_error(state, "invalid-declaration", "duplicate parameter");
          return false;
        }
      params = ot_cons(state, parameter, params);
      signature = ot_cons(state, specializer, signature);
      rest = as_pair(rest)->cdr;
    }
    params = nat_reverse(state, &params, 1);
    signature = nat_reverse(state, &signature, 1);
  }
  otv has_next = ot_false;
  if (method) {
    otv body = list_tail(form, 2), next_name = ot_nil;
    OT_FRAME_SCOPED(state, &body, &next_name);
    if (class_option(as_pair(body)->car, "next")) {
      if (list_length(body) < 3) {
        generic_error(state, "invalid-declaration", "expected :next name followed by method body");
        return false;
      }
      next_name = as_pair(as_pair(body)->cdr)->car;
      if (!is_type(next_name, OBJ_SYMBOL) || symbol_is(next_name, "&") ||
          memchr(as_name(next_name)->bytes, '/', as_name(next_name)->length)) {
        generic_error(state, "invalid-declaration", "expected local next-method name");
        return false;
      }
      for (otv p = params; p != ot_null; p = as_pair(p)->cdr)
        if (as_pair(p)->car == next_name) {
          generic_error(state, "invalid-declaration", "next-method name duplicates parameter");
          return false;
        }
      otv binding = ot_cons(state, next_name, ot_null);
      OT_FRAME_SCOPED(state, &binding);
      binding = ot_cons(state, next_name, binding);
      binding = ot_cons(state, binding, ot_null);
      body = ot_cons(state, binding, list_tail(body, 2));
      body = ot_cons(state, ot_intern(state, "let", 3, false), body);
      body = ot_cons(state, body, ot_null);
      body = ot_cons(state, params, body);
      body = ot_cons(state, ot_intern(state, "fn", 2, false), body);
      body = ot_cons(state, body, ot_null);
      params = ot_cons(state, next_name, ot_null);
      has_next = ot_true;
    }
    code = compile_function_code(state, params, body, ot_nil, state->vm.current_namespace, name);
    if (code == OT_UNWIND) return false;
  }
  otv fields[] = {kind, name, signature, code, qualifier, has_next};
  otv descriptor = make_descriptor(state, fields, 6);
  bytecode_emit_constant(state, compiler, BC_GENERIC_DECL, descriptor);
  return !compiler->bytes.failed;
}

static otv execute_generic_declaration(ots* state, otv descriptor) {
  otv declaration_kind = array_item(descriptor, 0);
  if (symbol_is(declaration_kind, "defclass") || symbol_is(declaration_kind, "defabstract") ||
      symbol_is(declaration_kind, "reset-class!"))
    return execute_class_declaration(state, descriptor);
  otv kind = as_slots(as_array(descriptor)->slots)->values[0];
  otv name = as_slots(as_array(descriptor)->slots)->values[1];
  otv signature = as_slots(as_array(descriptor)->slots)->values[2];
  otv code = as_slots(as_array(descriptor)->slots)->values[3];
  otv space = state->vm.current_namespace;
  otv owner = ((ot_namespace_obj*)ot_as_obj(space))->name;
  OT_FRAME_SCOPED(state, &descriptor, &kind, &name, &signature, &code, &space, &owner);
  if (symbol_is(kind, "defmethod")) {
    otv var = resolve_var(state, space, name, false);
    if (!is_type(var, OBJ_VAR))
      return generic_error(state, "not-generic", "method target is not defined");
    otv generic = ((ot_var_obj*)ot_as_obj(var))->value;
    OT_FRAME_SCOPED(state, &generic);
    if (!is_type(generic, OBJ_GENERIC))
      return generic_error(state, "not-generic", "method target is not generic");
    if (list_length(signature) != as_generic(generic)->arity)
      return generic_error(state, "wrong-arity", "method arity differs from generic");
    otv resolved = ot_array_new(state, as_generic(generic)->arity);
    OT_FRAME_SCOPED(state, &resolved);
    while (signature != ot_null) {
      otv type = as_pair(signature)->car;
      if (type != ot_false) {
        otv type_var = resolve_var(state, space, type, false);
        if (!is_type(type_var, OBJ_VAR) ||
            !is_type(((ot_var_obj*)ot_as_obj(type_var))->value, OBJ_DISPATCH_TYPE))
          return generic_error(state, "invalid-specializer", "expected type descriptor");
        type = ((ot_var_obj*)ot_as_obj(type_var))->value;
      }
      array_push(state, &resolved, type == state->object_class ? ot_false : type);
      signature = as_pair(signature)->cdr;
    }
    otv function = make_bytecode_function(state, code, ot_nil, space, name);
    return register_method(state, generic, resolved, function, array_item(descriptor, 4),
                           array_item(descriptor, 5));
  }
  bool type = symbol_is(kind, "defclass") || symbol_is(kind, "reset-class!");
  bool reset = symbol_is(kind, "reset-class!") || symbol_is(kind, "reset-generic!");
  otv existing = namespace_var(state, space, name, false);
  otv referred = namespace_var(state, space, name, true);
  if (symbol_is(owner, "otium.generic") || (existing == ot_nil && referred != ot_nil) ||
      (reset && existing == ot_nil) || as_name(name)->special_form)
    return generic_error(state, "binding-conflict", "declaration conflicts with existing binding");
  size_t arity = list_length(signature);
  if (existing != ot_nil && !reset) {
    otv old = ((ot_var_obj*)ot_as_obj(existing))->value;
    bool valid = type ? is_type(old, OBJ_DISPATCH_TYPE) && as_dispatch_type(old)->owner == owner &&
                            as_dispatch_type(old)->name == name
                      : is_type(old, OBJ_GENERIC) && as_generic(old)->owner == owner &&
                            as_generic(old)->name == name && as_generic(old)->arity == arity;
    if (!valid)
      return generic_error(state, "binding-conflict",
                           "incompatible declaration; use explicit reset");
    return ot_nil;
  }
  otv made = ot_nil;
  OT_FRAME_SCOPED(state, &made);
  if (type) made = new_dispatch_type(state, owner, name, false);
  else {
    ot_generic_obj* generic = must_alloc(state, sizeof(*generic), OBJ_GENERIC);
    ot_store(state, generic, &generic->owner, owner);
    ot_store(state, generic, &generic->name, name);
    ot_store(state, generic, &generic->methods, ot_null);
    ot_store(state, generic, &generic->cache, ot_nil);
    ot_store(state, generic, &generic->positions, ot_nil);
    generic->epoch = state->class_epoch;
    generic->arity = arity;
    generic->stable_id = ++state->next_stable_id;
    made = ot_from_obj(generic);
  }
  define_var(state, space, name, made, ot_nil, false);
  return ot_nil;
}

static otv nat_dispatch_type(ots* state, otv* args, int argc) {
  if (need_arity(state, "dispatch-type", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  return dispatch_type(state, args[0]);
}

static otv nat_type_descriptor_p(ots* state, otv* args, int argc) {
  if (need_arity(state, "type-descriptor?", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  return is_type(args[0], OBJ_DISPATCH_TYPE) ? ot_true : ot_false;
}

static otv nat_generic_p(ots* state, otv* args, int argc) {
  if (need_arity(state, "generic?", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  return is_type(args[0], OBJ_GENERIC) ? ot_true : ot_false;
}

static otv nat_type_name(ots* state, otv* args, int argc) {
  if (need_arity(state, "type-name", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_DISPATCH_TYPE))
    return generic_error(state, "invalid-specializer", "expected descriptor");
  buf name = {0};
  render_name(&name, as_dispatch_type(args[0])->owner, false);
  buf_cstr(&name, "/");
  render_name(&name, as_dispatch_type(args[0])->name, false);
  otv result = ot_intern(state, name.data, name.length, false);
  buf_free(&name);
  return result;
}

static otv nat_generic_methods(ots* state, otv* args, int argc) {
  if (need_arity(state, "generic-methods", argc, 1, 1) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(args[0], OBJ_GENERIC))
    return generic_error(state, "not-generic", "expected generic");
  otv result = ot_null, cursor = as_generic(args[0])->methods, signature = ot_nil, copy = ot_null;
  OT_FRAME_SCOPED(state, &result, &cursor, &signature, &copy);
  while (cursor != ot_null) {
    signature = as_pair(as_pair(cursor)->car)->car;
    copy = ot_null;
    for (size_t i = as_array(signature)->length; i > 0; i--)
      copy = ot_cons(state,
                     as_slots(as_array(signature)->slots)->values[i - 1] == ot_false
                         ? state->object_class
                         : as_slots(as_array(signature)->slots)->values[i - 1],
                     copy);
    otv qualifier = method_qualifier(as_pair(cursor)->car) == ot_true
                        ? ot_intern(state, "around", 6, true)
                        : ot_intern(state, "primary", 7, false);
    otv entry = ot_nil;
    OT_FRAME_SCOPED(state, &qualifier, &entry);
    entry = ot_cons(state, copy, ot_null);
    entry = ot_cons(state, qualifier, entry);
    result = ot_cons(state, entry, result);
    cursor = as_pair(cursor)->cdr;
  }
  return result;
}

static otv nat_remove_method(ots* state, otv* args, int argc) {
  if (need_arity(state, "remove-method!", argc, 2, 3) == OT_UNWIND) return OT_UNWIND;
  otv qualifier = ot_false;
  if (argc == 3) {
    if (class_option(args[2], "around")) qualifier = ot_true;
    else if (!symbol_is(args[2], "primary"))
      return generic_error(state, "invalid-qualifier", "expected :around or primary");
  }
  otv signature = signature_from_list(state, args[0], args[1]);
  if (signature == OT_UNWIND) return signature;
  otv cursor = as_generic(args[0])->methods, previous = ot_nil;
  while (cursor != ot_null) {
    if (same_signature(as_pair(as_pair(cursor)->car)->car, signature) &&
        method_qualifier(as_pair(cursor)->car) == qualifier) {
      invalidate_generic(state, args[0]);
      if (previous == ot_nil) {
        ot_generic_obj* generic = as_generic(args[0]);
        ot_store(state, generic, &generic->methods, as_pair(cursor)->cdr);
      } else {
        ot_pair_obj* pair = as_pair(previous);
        ot_store(state, pair, &pair->cdr, as_pair(cursor)->cdr);
      }
      return ot_true;
    }
    previous = cursor;
    cursor = as_pair(cursor)->cdr;
  }
  return ot_false;
}

static void generic_module_init(ots* state) {
  if (state->dispatch_types != ot_nil) return;
  state->dispatch_types = make_slots(state, OT_TYPE_INTERNAL + 2);
  static const char* names[] = {
      "Nil",        "Null",    "Boolean", "Number",         "Number",   "Symbol",    "Keyword",
      "String",     "Pair",    "Vector",  "Hashtable",      "Buffer",   "Procedure", "Macro",
      "Parameter",  "Restart", "Foreign", "TypeDescriptor", "Instance", "Character", "Other",
      "Bytevector", "Port",    "Other",   "Other",          "Other"};
  otv owner = ((ot_namespace_obj*)ot_as_obj(state->vm.current_namespace))->name;
  otv name = ot_nil, type = ot_nil;
  OT_FRAME_SCOPED(state, &owner, &name, &type);
  name = ot_intern(state, "Object", 6, false);
  state->object_class = new_dispatch_type(state, owner, name, true);
  as_dispatch_type(state->object_class)->abstract = true;
  define_var(state, state->vm.current_namespace, name, state->object_class, ot_nil, false);
  for (size_t i = 0; i <= OT_TYPE_INTERNAL; i++) {
    name = ot_intern(state, names[i], strlen(names[i]), false);
    if (i == OT_TYPE_FLOAT) type = as_slots(state->dispatch_types)->values[OT_TYPE_INT];
    else if (i == OT_TYPE_INTERNAL || i == OT_TYPE_RECORD || i == OT_TYPE_RECORD_TYPE)
      type = as_slots(state->dispatch_types)->values[OT_TYPE_EOF];
    else type = new_dispatch_type(state, owner, name, true);
    ot_slots_obj* slots = as_slots(state->dispatch_types);
    ot_store(state, slots, &slots->values[i], type);
    define_var(state, state->vm.current_namespace, name, type, ot_nil, false);
  }
  name = ot_intern(state, "CPointer", 8, false);
  type = new_dispatch_type(state, owner, name, true);
  ot_store(state, as_slots(state->dispatch_types),
           &as_slots(state->dispatch_types)->values[OT_TYPE_INTERNAL + 1], type);
  define_var(state, state->vm.current_namespace, name, type, ot_nil, false);
  ot_def_nat(state, "%make-record-type", nat_make_record_type);
  ot_def_nat(state, "record-new", nat_record_new);
  ot_def_nat(state, "record-ref", nat_record_ref);
  ot_def_nat(state, "record-set!", nat_record_set);
  ot_def_nat(state, "record?", nat_record_p);
  ot_def_nat(state, "record-type?", nat_record_type_p);
  ot_def_nat(state, "record-type", nat_record_type);
  ot_def_nat(state, "make", nat_make_instance);
  ot_def_nat(state, "class-properties", nat_class_properties);
  ot_def_nat(state, "class-mount", nat_class_mount);
  ot_def_nat(state, "class-update-properties!", nat_class_update_properties);
  ot_def_nat(state, "class-subtype?", nat_class_subtype_p);
  ot_def_nat(state, "dispatch-type", nat_dispatch_type);
  ot_def_nat(state, "type-descriptor?", nat_type_descriptor_p);
  ot_def_nat(state, "type-name", nat_type_name);
  ot_def_nat(state, "instance?", nat_instance_p);
  ot_def_nat(state, "slot-set!", nat_slot_set);
  ot_def_nat(state, "generic?", nat_generic_p);
  ot_def_nat(state, "generic-methods", nat_generic_methods);
  ot_def_nat(state, "remove-method!", nat_remove_method);
  name = ot_intern(state, "update-instance", 15, false);
  state->update_instance = new_generic(state, owner, name, 2);
  otv signature = ot_nil, function = ot_nil;
  OT_FRAME_SCOPED(state, &signature, &function);
  signature = ot_array_new(state, 2);
  array_push(state, &signature, ot_false);
  array_push(state, &signature, ot_false);
  function = ot_make_native(state, "update-instance", default_update_instance, ot_nil);
  register_method(state, state->update_instance, signature, function, ot_false, ot_false);
  define_var(state, state->vm.current_namespace, name, state->update_instance, ot_nil, false);
}

/* Run a cleanup procedure with the outcome of leaving the body. */
static otv nat_call_with_cleanup(ots* state, otv* args, int argc) {
  if (need_arity(state, "call-with-cleanup", argc, 2, 2) == OT_UNWIND) return OT_UNWIND;
  otv body = args[0], release = args[1], result = ot_nil;
  otv condition = ot_nil, restart_args = ot_nil, outcome = ot_nil;
  OT_FRAME_SCOPED(state, &body, &release, &result, &condition, &restart_args, &outcome);
  result = apply_value(state, body, NULL, 0, false);
  condition = state->vm.condition;
  restart_args = state->vm.unwind_args;
  ot_unwind_kind kind = state->vm.unwind_kind;
  uint64_t restart_id = state->vm.unwind_restart_id;
  bool quit = state->quit_requested;
  int status = state->quit_status;
  state->vm.condition = ot_nil;
  state->vm.unwind_args = ot_nil;
  state->vm.unwind_kind = UNWIND_NONE;
  state->vm.unwind_restart_id = 0;
  state->quit_requested = false;
  state->quit_status = 0;
  if (kind == UNWIND_CONDITION) outcome = condition;
  else if (kind == UNWIND_QUIT) outcome = ot_intern(state, "quit", 4, true);
  if (apply_value(state, release, &outcome, 1, false) == OT_UNWIND) return OT_UNWIND;
  state->vm.condition = condition;
  state->vm.unwind_args = restart_args;
  state->vm.unwind_kind = kind;
  state->vm.unwind_restart_id = restart_id;
  state->quit_requested = quit;
  state->quit_status = status;
  return result;
}

/* Buffered blocking ports. Managed bytevectors keep storage movable between
 * calls; a host read/write cannot enter Otium or trigger its collector. */
#define as_port(V) ((ot_port_obj*)ot_as_obj(V))

enum {
  PORT_INPUT_MEMORY = 1,
  PORT_STRING,
  PORT_BYTES,
  PORT_FILE,
  PORT_CUSTOM,
  PORT_WRITER,
  PORT_STANDARD
};

void ot_set_io_host(ots* state, const ot_io_host* host, void* userdata) {
  state->io_host = host ? *host : *ot_platform_io_host();
  state->io_userdata = userdata;
}

static otv port_error(ots* s, otv p, const char* op, int reason) {
  static const char* reasons[] = {"other",        "not-found",     "exists",   "permission",
                                  "is-directory", "not-directory", "no-space", "closed",
                                  "other",        "other"};
  if (reason < 0 || reason > OT_IO_INTERRUPTED) reason = OT_IO_OTHER;
  OT_FRAME_SCOPED(s, &p);
  otv condition = ot_table_new(s, 6), key = ot_nil, value = ot_nil;
  OT_FRAME_SCOPED(s, &condition, &key, &value);
  const char* fields[] = {"type", "op", "reason", "message", "path", "detail"};
  for (size_t i = 0; i < 6; i++) {
    key = ot_intern(s, fields[i], strlen(fields[i]), true);
    if (i == 0)
      value = ot_intern(s, reason == OT_IO_CLOSED ? "io-error" : "file-error",
                        reason == OT_IO_CLOSED ? 8 : 10, false);
    else if (i == 1) value = ot_intern(s, op, strlen(op), false);
    else if (i == 2) value = ot_intern(s, reasons[reason], strlen(reasons[reason]), true);
    else if (i == 4) value = is_type(p, OBJ_PORT) ? as_port(p)->name : p;
    else value = ot_make_string(s, reasons[reason], strlen(reasons[reason]));
    ot_table_put(s, condition, key, value);
  }
  return ot_raise_value(s, condition);
}

static otv port_check(ots* s, otv p, bool input, const char* op) {
  if (!is_type(p, OBJ_PORT)) return ot_raise(s, "%s: expected port", op);
  if (as_port(p)->closed) return port_error(s, p, op, OT_IO_CLOSED);
  if (as_port(p)->input != input) return ot_raise(s, "%s: wrong port direction", op);
  return ot_nil;
}

static otv port_callback(ots* s, otv p, const char* name) {
  OT_FRAME_SCOPED(s, &p);
  otv key = ot_intern(s, name, strlen(name), true);
  return ot_table_get(s, as_port(p)->callbacks, key, ot_nil);
}

static otv port_new(ots* s, otv name, bool input, unsigned kind, intptr_t handle, otv buffer,
                    otv callbacks) {
  OT_FRAME_SCOPED(s, &name, &buffer, &callbacks);
  ot_port_obj* p = must_alloc(s, sizeof(*p), OBJ_PORT);
  ot_store(s, p, &p->name, name);
  ot_store(s, p, &p->buffer, buffer);
  ot_store(s, p, &p->callbacks, callbacks);
  p->input = input;
  p->kind = kind;
  p->handle = handle;
  p->at = p->position = 0;
  p->closed = false;
  p->eof = kind == PORT_INPUT_MEMORY;
  p->end = p->eof ? as_bytevector(buffer)->length : 0;
  otv value = ot_from_obj(p);
  OT_FRAME_SCOPED(s, &value);
  if (kind == PORT_FILE || kind == PORT_CUSTOM) s->open_ports = ot_cons(s, value, s->open_ports);
  return value;
}

static otv port_flush(ots* s, otv value) {
  if (port_check(s, value, false, "flush-output-port") == OT_UNWIND) return OT_UNWIND;
  OT_FRAME_SCOPED(s, &value);
  unsigned kind = as_port(value)->kind;
  if (kind == PORT_STRING || kind == PORT_BYTES) return ot_nil;
  while (as_port(value)->end) {
    ot_port_obj* p = as_port(value);
    size_t end = p->end;
    long count;
    if (kind == PORT_WRITER) {
      s->writer(s->writer_userdata, (char*)as_bytevector(p->buffer)->data, end);
      count = (long)end;
    } else if (kind == PORT_CUSTOM) {
      otv callback = port_callback(s, value, "write");
      otv args[] = {as_port(value)->buffer, ot_make_int(0), ot_make_int((intptr_t)end)};
      otv result = apply_value(s, callback, args, 3, false);
      if (result == OT_UNWIND) return result;
      if (!ot_is_int(result) || ot_get_int(result) <= 0 || (size_t)ot_get_int(result) > end)
        return ot_raise(s, "write-bytevector: invalid custom write count");
      count = ot_get_int(result);
    } else {
      count = s->io_host.write
                  ? s->io_host.write(s->io_userdata, p->handle, as_bytevector(p->buffer)->data, end)
                  : -OT_IO_OTHER;
      if (count == -OT_IO_INTERRUPTED) {
        if (atomic_exchange(&s->vm.interrupted, false) && run_interrupt_hook(s) == OT_UNWIND)
          return OT_UNWIND;
        continue;
      }
      if (count < 0) return port_error(s, value, "write-bytevector", (int)-count);
      if (count == 0 || (size_t)count > end)
        return ot_raise(s, "write-bytevector: invalid host write count");
    }
    p = as_port(value);
    memmove(as_bytevector(p->buffer)->data, as_bytevector(p->buffer)->data + count,
            end - (size_t)count);
    p->end -= (size_t)count;
  }
  if (kind == PORT_WRITER && s->writer == ot_default_write)
    fflush(s->writer_userdata ? (FILE*)s->writer_userdata : stdout);
  if (kind == PORT_CUSTOM) {
    otv callback = port_callback(s, value, "flush");
    if (!is_falsy(callback) && apply_value(s, callback, NULL, 0, false) == OT_UNWIND)
      return OT_UNWIND;
  }
  return ot_nil;
}

static otv port_ensure(ots* s, otv value, size_t needed) {
  if (port_check(s, value, true, "read") == OT_UNWIND) return OT_UNWIND;
  OT_FRAME_SCOPED(s, &value);
  if (as_port(value)->kind == PORT_STANDARD && as_port(value)->handle == 0) {
    otv output = ((ot_param_obj*)ot_as_obj(s->output_port_param))->value;
    if (!as_port(output)->closed && port_flush(s, output) == OT_UNWIND) return OT_UNWIND;
  }
  for (;;) {
    ot_port_obj* p = as_port(value);
    if (p->end - p->at >= needed || p->eof) return ot_nil;
    ot_bytevector_obj* buffer = as_bytevector(p->buffer);
    memmove(buffer->data, buffer->data + p->at, p->end - p->at);
    p->end -= p->at;
    p->at = 0;
    size_t end = p->end, capacity = buffer->length;
    long count;
    if (p->kind == PORT_CUSTOM) {
      otv callback = port_callback(s, value, "read");
      otv args[] = {as_port(value)->buffer, ot_make_int((intptr_t)end),
                    ot_make_int((intptr_t)capacity)};
      otv result = apply_value(s, callback, args, 3, false);
      if (result == OT_UNWIND) return result;
      if (result == ot_eof) count = 0;
      else if (ot_is_int(result) && ot_get_int(result) > 0 &&
               (size_t)ot_get_int(result) <= capacity - end)
        count = ot_get_int(result);
      else return ot_raise(s, "read-bytevector: invalid custom read count");
    } else {
      if (atomic_exchange(&s->vm.interrupted, false) && run_interrupt_hook(s) == OT_UNWIND)
        return OT_UNWIND;
      p = as_port(value);
      buffer = as_bytevector(p->buffer);
      count = s->io_host.read
                  ? s->io_host.read(s->io_userdata, p->handle, buffer->data + end, capacity - end)
                  : -OT_IO_OTHER;
      if (count == -OT_IO_INTERRUPTED) {
        if (atomic_exchange(&s->vm.interrupted, false) && run_interrupt_hook(s) == OT_UNWIND)
          return OT_UNWIND;
        continue;
      }
      if (count < 0) return port_error(s, value, "read-bytevector", (int)-count);
      if ((size_t)count > capacity - end)
        return ot_raise(s, "read-bytevector: invalid host read count");
    }
    p = as_port(value);
    if (!count) p->eof = true;
    p->end += (size_t)count;
  }
}

static otv port_peek(ots* s, otv value, bool character, bool consume) {
  OT_FRAME_SCOPED(s, &value);
  if (port_ensure(s, value, 1) == OT_UNWIND) return OT_UNWIND;
  ot_port_obj* p = as_port(value);
  if (p->at == p->end) return ot_eof;
  unsigned char byte = as_bytevector(p->buffer)->data[p->at];
  size_t width = 1;
  otv result = ot_make_int(byte);
  if (character) {
    width = byte < 128                     ? 1
            : byte >= 0xc2 && byte <= 0xdf ? 2
            : byte >= 0xe0 && byte <= 0xef ? 3
            : byte >= 0xf0 && byte <= 0xf4 ? 4
                                           : 1;
    if (port_ensure(s, value, width) == OT_UNWIND) return OT_UNWIND;
    p = as_port(value);
    const unsigned char* data = as_bytevector(p->buffer)->data + p->at;
    if (!utf8_valid_sequence(data, p->end - p->at))
      return io_error(s, "decode-error", "read-char", "read-char: invalid UTF-8", "position",
                      ot_make_int((intptr_t)p->position));
    result = ot_make_char(utf8_decode(data, width, 0));
  }
  p = as_port(value);
  if (consume) {
    p->at += width;
    p->position += width;
  }
  return result;
}

static otv port_current(ots* s, bool input) {
  otv param = input ? s->input_port_param : s->output_port_param;
  return apply_value(s, param, NULL, 0, false);
}

static otv port_optional(ots* s, otv* args, int argc, int index, bool input) {
  return argc > index ? args[index] : port_current(s, input);
}

/* Hand the port the bytes before the read offset, then copy count bytes of
 * lookahead from its buffer without consuming them. Lookahead is at most four
 * bytes, and port buffers hold at least four. */
static bool reader_fill(reader* input, size_t count) {
  if (input->failed) return false;
  ot_port_obj* p = as_port(*input->port);
  p->at += input->offset - input->consumed;
  p->position += input->offset - input->consumed;
  input->consumed = input->offset;
  if (port_ensure(input->state, *input->port, count) == OT_UNWIND) {
    input->failed = true;
    return false;
  }
  p = as_port(*input->port);
  size_t held = input->length - input->consumed;
  size_t available = p->end - p->at < count ? p->end - p->at : count;
  if (available > held)
    buf_write(input->text, (char*)as_bytevector(p->buffer)->data + p->at + held, available - held);
  if (input->text->failed) {
    ot_raise(input->state, "read: out of memory");
    input->failed = true;
    return false;
  }
  input->source = input->text->data;
  input->length = input->text->length;
  return input->offset + count <= input->length;
}

static otv n_read(ots* s, otv* a, int n) {
  if (need_arity(s, "read", n, 0, 1) == OT_UNWIND) return OT_UNWIND;
  otv p = port_optional(s, a, n, 0, true);
  OT_FRAME_SCOPED(s, &p);
  if (p == OT_UNWIND || port_check(s, p, true, "read") == OT_UNWIND) return OT_UNWIND;
  buf name = {0}, text = {0};
  if (is_type(as_port(p)->name, OBJ_STRING))
    buf_write(&name, (char*)as_bytes(as_string(as_port(p)->name)->bytes)->data,
              as_string(as_port(p)->name)->length);
  else buf_cstr(&name, "<port>");
  buf_byte(&name, '\0');
  size_t position = as_port(p)->position;
  reader input = {.state = s, .source = "", .name = name.data, .port = &p, .text = &text};
  otv value = read_form(&input);
  if (input.failed) value = OT_UNWIND;
  else if (value == OT_UNDEFINED) value = ot_eof;
  /* The reader works on bytes; ports promise characters. */
  for (size_t i = 0; value != OT_UNWIND && i < input.offset;) {
    size_t width = utf8_valid_sequence((const unsigned char*)text.data + i, input.offset - i);
    if (!width)
      value = io_error(s, "decode-error", "read-char", "read-char: invalid UTF-8", "position",
                       ot_make_int((intptr_t)(position + i)));
    i += width ? width : input.offset;
  }
  if (!input.failed) {
    ot_port_obj* port = as_port(p);
    port->at += input.offset - input.consumed;
    port->position += input.offset - input.consumed;
  }
  buf_free(&name);
  buf_free(&text);
  return value;
}

static otv port_write_bytes(ots* s, otv value, otv bytes, size_t start, size_t end) {
  if (port_check(s, value, false, "write-bytevector") == OT_UNWIND) return OT_UNWIND;
  OT_FRAME_SCOPED(s, &value, &bytes);
  while (start < end) {
    ot_port_obj* p = as_port(value);
    size_t capacity = as_bytevector(p->buffer)->length;
    if (p->end == capacity) {
      if (p->kind == PORT_STRING || p->kind == PORT_BYTES) {
        otv grown = ot_make_bytevector(s, capacity * 2);
        p = as_port(value);
        memcpy(as_bytevector(grown)->data, as_bytevector(p->buffer)->data, p->end);
        ot_store(s, p, &p->buffer, grown);
      } else if (port_flush(s, value) == OT_UNWIND) return OT_UNWIND;
      continue;
    }
    size_t count = end - start;
    if (count > capacity - p->end) count = capacity - p->end;
    memcpy(as_bytevector(p->buffer)->data + p->end, as_bytevector(bytes)->data + start, count);
    p->end += count;
    start += count;
  }
  if (as_port(value)->kind == PORT_WRITER || as_port(value)->kind == PORT_STANDARD)
    return port_flush(s, value);
  return ot_nil;
}

static otv port_output(ots* s, otv p, otv* args, int argc, bool display, bool newline) {
  OT_FRAME_SCOPED(s, &p);
  buf text = {0};
  for (int i = 0; i < argc; i++) {
    if (i) buf_byte(&text, ' ');
    render_value(s, &text, args[i], display, 0);
  }
  if (newline) buf_byte(&text, '\n');
  if (text.failed) {
    buf_free(&text);
    return ot_raise(s, "output: out of memory");
  }
  otv bytes = ot_make_bytevector(s, text.length);
  memcpy(as_bytevector(bytes)->data, text.data, text.length);
  buf_free(&text);
  return port_write_bytes(s, p, bytes, 0, as_bytevector(bytes)->length);
}

static otv n_display_to(ots* s, otv* a, int n) {
  if (n < 1) return ot_raise(s, "display-to: expected port");
  return port_output(s, a[0], a + 1, n - 1, true, false);
}

static otv n_write_to(ots* s, otv* a, int n) {
  if (n < 1) return ot_raise(s, "write-to: expected port");
  return port_output(s, a[0], a + 1, n - 1, false, false);
}

static otv n_port_p(ots* s, otv* a, int n) {
  if (need_arity(s, "port?", n, 1, 1) == OT_UNWIND) return OT_UNWIND;
  return (is_type(a[0], OBJ_PORT) ? ot_true : ot_false);
}

#define PORT_PRED(NAME, EXPR)                                                                      \
  static otv NAME(ots* s, otv* a, int n) {                                                         \
    if (need_arity(s, #NAME, n, 1, 1) == OT_UNWIND) return OT_UNWIND;                              \
    return (is_type(a[0], OBJ_PORT) && (EXPR) ? ot_true : ot_false);                               \
  }
PORT_PRED(n_input_p, as_port(a[0])->input)
PORT_PRED(n_output_p, !as_port(a[0])->input)
PORT_PRED(n_input_open_p, as_port(a[0])->input && !as_port(a[0])->closed)
PORT_PRED(n_output_open_p, !as_port(a[0])->input && !as_port(a[0])->closed)
#undef PORT_PRED
#define PORT_READ(NAME, CHAR, CONSUME)                                                             \
  static otv NAME(ots* s, otv* a, int n) {                                                         \
    if (need_arity(s, #NAME, n, 0, 1) == OT_UNWIND) return OT_UNWIND;                              \
    return port_peek(s, port_optional(s, a, n, 0, true), CHAR, CONSUME);                           \
  }
PORT_READ(n_read_char, true, true)
PORT_READ(n_peek_char, true, false)
PORT_READ(n_read_u8, false, true)
PORT_READ(n_peek_u8, false, false)
#undef PORT_READ

static otv n_flush(ots* s, otv* a, int n) {
  if (need_arity(s, "flush-output-port", n, 0, 1) == OT_UNWIND) return OT_UNWIND;
  return port_flush(s, port_optional(s, a, n, 0, false));
}

static otv port_close(ots* s, otv value, int direction) {
  if (!is_type(value, OBJ_PORT)) return ot_raise(s, "close-port: expected port");
  if (direction && as_port(value)->input != (direction == 1))
    return ot_raise(s, "close-port: wrong port direction");
  if (as_port(value)->closed) return ot_nil;
  OT_FRAME_SCOPED(s, &value);
  if (!as_port(value)->input && port_flush(s, value) == OT_UNWIND) return OT_UNWIND;
  as_port(value)->closed = true;
  otv cursor = s->open_ports, previous = ot_nil;
  while (is_type(cursor, OBJ_PAIR)) {
    if (as_pair(cursor)->car == value) {
      if (previous == ot_nil) s->open_ports = as_pair(cursor)->cdr;
      else ot_store(s, as_pair(previous), &as_pair(previous)->cdr, as_pair(cursor)->cdr);
      break;
    }
    previous = cursor;
    cursor = as_pair(cursor)->cdr;
  }
  if (as_port(value)->kind == PORT_CUSTOM) {
    otv callback = port_callback(s, value, "close");
    if (!is_falsy(callback) && apply_value(s, callback, NULL, 0, false) == OT_UNWIND)
      return OT_UNWIND;
  } else if (as_port(value)->kind == PORT_FILE) {
    int reason =
        s->io_host.close ? s->io_host.close(s->io_userdata, as_port(value)->handle) : OT_IO_OTHER;
    if (reason) return port_error(s, value, "close-port", reason);
  }
  return ot_nil;
}

#define PORT_CLOSE(NAME, DIR)                                                                      \
  static otv NAME(ots* s, otv* a, int n) {                                                         \
    if (need_arity(s, #NAME, n, 1, 1) == OT_UNWIND) return OT_UNWIND;                              \
    return port_close(s, a[0], DIR);                                                               \
  }
PORT_CLOSE(n_close_port, 0)
PORT_CLOSE(n_close_input, 1)
PORT_CLOSE(n_close_output, 2)
#undef PORT_CLOSE

static otv port_open_file(ots* s, otv* a, int n, bool input) {
  if (need_arity(s, "open-file", n, 1, 2) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(a[0], OBJ_STRING) || (n == 2 && !is_type(a[1], OBJ_TABLE)))
    return ot_raise(s, "open-file: expected path and optional options table");
  otv path = a[0], opts = n == 2 ? a[1] : ot_nil, buffer = ot_nil;
  OT_FRAME_SCOPED(s, &path, &opts, &buffer);
  otv key = ot_intern(s, "buffer-size", 11, true);
  otv size = ot_table_get(s, opts, key, ot_make_int(4096));
  if (!ot_is_int(size) || ot_get_int(size) <= 0)
    return ot_raise(s, "open-file: invalid buffer-size");
  size_t capacity = (size_t)ot_get_int(size);
  if (capacity < 4) capacity = 4;
  unsigned flags = input ? OT_IO_INPUT : OT_IO_OUTPUT;
  if (!input) {
    key = ot_intern(s, "if-exists", 9, true);
    otv mode = ot_table_get(s, opts, key, ot_nil);
    if (mode != ot_nil) {
      if ((is_type(mode, OBJ_KEYWORD) && as_name(mode)->length == 6 &&
           memcmp(as_name(mode)->bytes, "append", 6) == 0))
        flags |= OT_IO_APPEND;
      else if ((is_type(mode, OBJ_KEYWORD) && as_name(mode)->length == 5 &&
                memcmp(as_name(mode)->bytes, "error", 5) == 0))
        flags |= OT_IO_EXCLUSIVE;
      else if (!(is_type(mode, OBJ_KEYWORD) && as_name(mode)->length == 8 &&
                 memcmp(as_name(mode)->bytes, "truncate", 8) == 0))
        return ot_raise(s, "open-output-file: invalid :if-exists");
    }
  }
  buffer = ot_make_bytevector(s, capacity);
  const char* bytes;
  size_t length;
  ot_string_bytes(path, &bytes, &length);
  if (memchr(bytes, 0, length)) return ot_raise(s, "open-file: NUL in path");
  char* filename = ot_host_alloc(length + 1);
  if (!filename) return ot_raise(s, "open-file: out of memory");
  memcpy(filename, bytes, length);
  intptr_t handle = 0;
  int reason =
      s->io_host.open ? s->io_host.open(s->io_userdata, filename, flags, &handle) : OT_IO_NOT_FOUND;
  ot_host_free(filename);
  if (reason) return port_error(s, path, input ? "open-input-file" : "open-output-file", reason);
  return port_new(s, path, input, PORT_FILE, handle, buffer, ot_nil);
}

static otv n_open_input(ots* s, otv* a, int n) { return port_open_file(s, a, n, true); }

static otv n_open_output(ots* s, otv* a, int n) { return port_open_file(s, a, n, false); }

static otv port_memory(ots* s, otv* a, int n, unsigned kind) {
  bool input = kind == PORT_INPUT_MEMORY;
  if (need_arity(s, "open-memory-port", n, input ? 1 : 0, input ? 1 : 0) == OT_UNWIND)
    return OT_UNWIND;
  otv buffer = input ? a[0] : ot_nil, name = ot_nil;
  OT_FRAME_SCOPED(s, &buffer, &name);
  if (input) {
    if (is_type(buffer, OBJ_STRING)) buffer = nat_string_utf8(s, &buffer, 1);
    else if (is_type(buffer, OBJ_BYTEVECTOR)) buffer = nat_bytevector_copy(s, &buffer, 1);
    else return ot_raise(s, "open-input-port: expected string or bytevector");
  } else buffer = ot_make_bytevector(s, 256);
  if (buffer == OT_UNWIND) return buffer;
  const char* label = kind == PORT_STRING ? "string" : "bytevector";
  name = ot_make_string(s, label, strlen(label));
  return port_new(s, name, input, kind, 0, buffer, ot_nil);
}

static otv n_input_string(ots* s, otv* a, int n) {
  if (n != 1 || !is_type(a[0], OBJ_STRING))
    return ot_raise(s, "open-input-string: expected string");
  return port_memory(s, a, n, PORT_INPUT_MEMORY);
}

static otv n_input_bytes(ots* s, otv* a, int n) {
  if (n != 1 || !is_type(a[0], OBJ_BYTEVECTOR))
    return ot_raise(s, "open-input-bytevector: expected bytevector");
  return port_memory(s, a, n, PORT_INPUT_MEMORY);
}

static otv n_output_string(ots* s, otv* a, int n) { return port_memory(s, a, n, PORT_STRING); }

static otv n_output_bytes(ots* s, otv* a, int n) { return port_memory(s, a, n, PORT_BYTES); }

static otv port_extract(ots* s, otv* a, int n, unsigned kind) {
  if (need_arity(s, "get-output", n, 1, 1) == OT_UNWIND ||
      port_check(s, a[0], false, "get-output") == OT_UNWIND)
    return OT_UNWIND;
  if (as_port(a[0])->kind != kind) return ot_raise(s, "get-output: wrong port kind");
  otv args[] = {as_port(a[0])->buffer, ot_make_int(0), ot_make_int((intptr_t)as_port(a[0])->end)};
  return kind == PORT_STRING ? nat_utf8_string(s, args, 3) : nat_bytevector_copy(s, args, 3);
}

static otv n_get_string(ots* s, otv* a, int n) { return port_extract(s, a, n, PORT_STRING); }

static otv n_get_bytes(ots* s, otv* a, int n) { return port_extract(s, a, n, PORT_BYTES); }

static otv n_write_bytes(ots* s, otv* a, int n) {
  if (need_arity(s, "write-bytevector", n, 1, 4) == OT_UNWIND ||
      bytevector_argument(s, "write-bytevector", a[0]) == OT_UNWIND)
    return OT_UNWIND;
  size_t start, end;
  if (span_arguments(s, "write-bytevector", a, n, 2, as_bytevector(a[0])->length, &start, &end) ==
      OT_UNWIND)
    return OT_UNWIND;
  return port_write_bytes(s, port_optional(s, a, n, 1, false), a[0], start, end);
}

static otv n_write_string(ots* s, otv* a, int n) {
  if (need_arity(s, "write-string", n, 1, 4) == OT_UNWIND) return OT_UNWIND;
  otv p = port_optional(s, a, n, 1, false), bytes = ot_nil;
  OT_FRAME_SCOPED(s, &p, &bytes);
  otv args[] = {a[0], n > 2 ? a[2] : ot_nil, n > 3 ? a[3] : ot_nil};
  bytes = nat_string_utf8(s, args, n > 1 ? n - 1 : 1);
  if (bytes == OT_UNWIND) return bytes;
  return port_write_bytes(s, p, bytes, 0, as_bytevector(bytes)->length);
}

static otv n_write_char(ots* s, otv* a, int n) {
  if (need_arity(s, "write-char", n, 1, 2) == OT_UNWIND) return OT_UNWIND;
  if (!ot_is_char(a[0])) return ot_raise(s, "write-char: expected character");
  otv p = port_optional(s, a, n, 1, false);
  OT_FRAME_SCOPED(s, &p);
  otv text = nat_string(s, a, 1);
  if (text == OT_UNWIND) return text;
  otv args[] = {text, p};
  return n_write_string(s, args, 2);
}

static otv n_write_u8(ots* s, otv* a, int n) {
  if (need_arity(s, "write-u8", n, 1, 2) == OT_UNWIND) return OT_UNWIND;
  if (!byte_value(a[0])) return ot_raise(s, "write-u8: expected byte");
  otv p = port_optional(s, a, n, 1, false);
  OT_FRAME_SCOPED(s, &p);
  unsigned char byte = (unsigned char)ot_get_int(a[0]);
  otv bytes = ot_make_bytevector(s, 1);
  as_bytevector(bytes)->data[0] = byte;
  return port_write_bytes(s, p, bytes, 0, 1);
}

static otv port_read_into(ots* s, otv bytes, otv value, size_t start, size_t end) {
  if (port_check(s, value, true, "read-bytevector!") == OT_UNWIND) return OT_UNWIND;
  OT_FRAME_SCOPED(s, &value, &bytes);
  size_t at = start;
  while (at < end) {
    if (port_ensure(s, value, 1) == OT_UNWIND) return OT_UNWIND;
    ot_port_obj* p = as_port(value);
    size_t count = p->end - p->at;
    if (!count) return at == start ? ot_eof : ot_make_int((intptr_t)(at - start));
    if (count > end - at) count = end - at;
    memcpy(as_bytevector(bytes)->data + at, as_bytevector(p->buffer)->data + p->at, count);
    p->at += count;
    p->position += count;
    at += count;
  }
  return ot_make_int((intptr_t)(at - start));
}

static otv n_read_into(ots* s, otv* a, int n) {
  if (need_arity(s, "read-bytevector!", n, 1, 4) == OT_UNWIND ||
      bytevector_argument(s, "read-bytevector!", a[0]) == OT_UNWIND)
    return OT_UNWIND;
  size_t start, end;
  if (span_arguments(s, "read-bytevector!", a, n, 2, as_bytevector(a[0])->length, &start, &end) ==
      OT_UNWIND)
    return OT_UNWIND;
  return port_read_into(s, a[0], port_optional(s, a, n, 1, true), start, end);
}

static otv n_read_bytes(ots* s, otv* a, int n) {
  if (need_arity(s, "read-bytevector", n, 1, 2) == OT_UNWIND) return OT_UNWIND;
  if (!ot_is_int(a[0]) || ot_get_int(a[0]) < 0)
    return ot_raise(s, "read-bytevector: invalid count");
  size_t size = (size_t)ot_get_int(a[0]);
  otv p = port_optional(s, a, n, 1, true), bytes = ot_nil;
  OT_FRAME_SCOPED(s, &p, &bytes);
  bytes = ot_make_bytevector(s, size);
  otv got = port_read_into(s, bytes, p, 0, size);
  if (got == OT_UNWIND || got == ot_eof) return got;
  if ((size_t)ot_get_int(got) == size) return bytes;
  otv args[] = {bytes, ot_make_int(0), got};
  return nat_bytevector_copy(s, args, 3);
}

static otv port_scan(ots* s, otv p, otv stops, bool until, bool skip, size_t limit,
                     bool eof_result) {
  OT_FRAME_SCOPED(s, &p, &stops);
  if (port_check(s, p, true, "read-string") == OT_UNWIND) return OT_UNWIND;
  bool ascii[128] = {false};
  if (stops != ot_nil) {
    const unsigned char* set = as_bytes(as_string(stops)->bytes)->data;
    for (size_t i = 0; i < as_string(stops)->length; i++)
      if (set[i] < 128) ascii[set[i]] = true;
  }
  buf out = {0};
  size_t count = 0;
  otv result = ot_nil;
  while (count < limit) {
    otv c = port_peek(s, p, true, false);
    if (c == OT_UNWIND) {
      result = c;
      break;
    }
    if (c == ot_eof) {
      if (!count && eof_result) result = c;
      break;
    }
    bool member = false;
    if (stops != ot_nil) {
      const unsigned char* set = as_bytes(as_string(stops)->bytes)->data;
      size_t length = as_string(stops)->length;
      if (ot_char_value(c) < 128) member = ascii[ot_char_value(c)];
      else
        for (size_t i = 0; i < length;) {
          if (utf8_decode(set, length, i) == ot_char_value(c)) {
            member = true;
            break;
          }
          i++;
          while (i < length && (set[i] & 0xc0) == 0x80) i++;
        }
      if (member == until) break;
    }
    ot_port_obj* port = as_port(p);
    size_t width =
        utf8_valid_sequence(as_bytevector(port->buffer)->data + port->at, port->end - port->at);
    if (!skip) buf_write(&out, (char*)as_bytevector(port->buffer)->data + port->at, width);
    port->at += width;
    port->position += width;
    count++;
  }
  if (result == ot_nil)
    result = skip         ? ot_make_int((intptr_t)count)
             : out.failed ? ot_raise(s, "read: out of memory")
                          : ot_make_string(s, out.data, out.length);
  buf_free(&out);
  return result;
}

static otv n_read_characters(ots* s, otv* a, int n) {
  if (need_arity(s, "read-string", n, 1, 2) == OT_UNWIND) return OT_UNWIND;
  if (!ot_is_int(a[0]) || ot_get_int(a[0]) < 0) return ot_raise(s, "read-string: invalid count");
  return port_scan(s, port_optional(s, a, n, 1, true), ot_nil, false, false,
                   (size_t)ot_get_int(a[0]), true);
}

static otv n_read_all(ots* s, otv* a, int n) {
  if (need_arity(s, "read-all", n, 0, 1) == OT_UNWIND) return OT_UNWIND;
  return port_scan(s, port_optional(s, a, n, 0, true), ot_nil, false, false, SIZE_MAX, false);
}

static otv port_scan_args(ots* s, otv* a, int n, bool until, bool skip) {
  if (need_arity(s, "scan", n, 1, 2) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(a[0], OBJ_STRING)) return ot_raise(s, "scan: expected character set string");
  return port_scan(s, port_optional(s, a, n, 1, true), a[0], until, skip, SIZE_MAX, false);
}

static otv n_read_until(ots* s, otv* a, int n) { return port_scan_args(s, a, n, true, false); }

static otv n_read_while(ots* s, otv* a, int n) { return port_scan_args(s, a, n, false, false); }

static otv n_skip_while(ots* s, otv* a, int n) { return port_scan_args(s, a, n, false, true); }

static otv n_read_line(ots* s, otv* a, int n) {
  if (need_arity(s, "read-line", n, 0, 1) == OT_UNWIND) return OT_UNWIND;
  otv p = port_optional(s, a, n, 0, true), stops = ot_nil, text = ot_nil;
  OT_FRAME_SCOPED(s, &p, &stops, &text);
  otv first = port_peek(s, p, true, false);
  if (first == ot_eof || first == OT_UNWIND) return first;
  stops = ot_make_string(s, "\n", 1);
  text = port_scan(s, p, stops, true, false, SIZE_MAX, false);
  if (text == OT_UNWIND) return text;
  otv next = port_peek(s, p, true, true);
  if (next == OT_UNWIND) return next;
  size_t length = as_string(text)->length;
  if (next != ot_eof && length && as_bytes(as_string(text)->bytes)->data[length - 1] == '\r') {
    buf copy = {0};
    buf_write(&copy, (char*)as_bytes(as_string(text)->bytes)->data, length - 1);
    text = ot_make_string(s, copy.data, copy.length);
    buf_free(&copy);
  }
  return text;
}

static otv port_custom(ots* s, otv* a, int n, bool input) {
  if (need_arity(s, "make-port", n, 2, 2) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(a[0], OBJ_STRING) || !is_type(a[1], OBJ_TABLE))
    return ot_raise(s, "make-port: expected name and callbacks");
  otv name = a[0], callbacks = a[1], buffer = ot_nil;
  OT_FRAME_SCOPED(s, &name, &callbacks, &buffer);
  const char* keys[] = {input ? "read" : "write", "close", "flush"};
  for (size_t i = 0; i < 3; i++) {
    otv key = ot_intern(s, keys[i], strlen(keys[i]), true);
    otv f = ot_table_get(s, callbacks, key, ot_nil);
    if ((i == 0 || !is_falsy(f)) && ot_value_type(f) != OT_TYPE_FUNCTION)
      return ot_raise(s, "make-port: expected callback procedure");
  }
  buffer = ot_make_bytevector(s, 4096);
  return port_new(s, name, input, PORT_CUSTOM, 0, buffer, callbacks);
}

static otv n_make_input(ots* s, otv* a, int n) { return port_custom(s, a, n, true); }

static otv n_make_output(ots* s, otv* a, int n) { return port_custom(s, a, n, false); }

static otv n_open_ports(ots* s, otv* a, int n) {
  (void)a;
  if (need_arity(s, "open-ports", n, 0, 0) == OT_UNWIND) return OT_UNWIND;
  otv result = ot_array_new(s, 4), cursor = s->open_ports;
  OT_FRAME_SCOPED(s, &result, &cursor);
  while (is_type(cursor, OBJ_PAIR)) {
    array_push(s, &result, as_pair(cursor)->car);
    cursor = as_pair(cursor)->cdr;
  }
  for (size_t i = 0; i < 3; i++) {
    otv param = i == 0 ? s->input_port_param : i == 1 ? s->output_port_param : s->error_port_param;
    otv port = ((ot_param_obj*)ot_as_obj(param))->value;
    if (!as_port(port)->closed) array_push(s, &result, port);
  }
  return result;
}

static otv n_flush_all(ots* s, otv* a, int n) {
  (void)a;
  if (need_arity(s, "flush-ports!", n, 0, 0) == OT_UNWIND) return OT_UNWIND;
  otv cursor = s->open_ports, p = ot_nil;
  OT_FRAME_SCOPED(s, &cursor, &p);
  while (is_type(cursor, OBJ_PAIR)) {
    p = as_pair(cursor)->car;
    cursor = as_pair(cursor)->cdr;
    if (!as_port(p)->input && !as_port(p)->closed && port_flush(s, p) == OT_UNWIND)
      return OT_UNWIND;
  }
  p = ((ot_param_obj*)ot_as_obj(s->output_port_param))->value;
  if (!as_port(p)->closed && port_flush(s, p) == OT_UNWIND) return OT_UNWIND;
  p = ((ot_param_obj*)ot_as_obj(s->error_port_param))->value;
  if (!as_port(p)->closed && port_flush(s, p) == OT_UNWIND) return OT_UNWIND;
  return ot_nil;
}

bool ot_flush_ports(ots* s) {
  if (!is_type(s->output_port_param, OBJ_PARAM)) return true;
  otv condition = s->vm.condition, args = s->vm.unwind_args;
  OT_FRAME_SCOPED(s, &condition, &args);
  ot_unwind_kind kind = s->vm.unwind_kind;
  uint64_t restart = s->vm.unwind_restart_id;
  bool quit = s->quit_requested;
  int status = s->quit_status;
  s->vm.condition = s->vm.unwind_args = ot_nil;
  s->vm.unwind_kind = UNWIND_NONE;
  s->vm.unwind_restart_id = 0;
  s->quit_requested = false;
  if (n_flush_all(s, NULL, 0) == OT_UNWIND) return false;
  s->vm.condition = condition;
  s->vm.unwind_args = args;
  s->vm.unwind_kind = kind;
  s->vm.unwind_restart_id = restart;
  s->quit_requested = quit;
  s->quit_status = status;
  return true;
}

static char* port_path(ots* s, otv path, const char* op) {
  if (!is_type(path, OBJ_STRING)) {
    ot_raise(s, "%s: expected path string", op);
    return NULL;
  }
  const char* text;
  size_t size;
  ot_string_bytes(path, &text, &size);
  if (memchr(text, 0, size)) {
    ot_raise(s, "%s: NUL in path", op);
    return NULL;
  }
  char* copy = ot_host_alloc(size + 1);
  if (!copy) {
    ot_raise(s, "%s: out of memory", op);
    return NULL;
  }
  memcpy(copy, text, size);
  return copy;
}

static otv port_file_info(ots* s, otv* a, int n, bool exists) {
  const char* op = exists ? "file-exists?" : "file-info";
  if (need_arity(s, op, n, 1, 1) == OT_UNWIND) return OT_UNWIND;
  char* path = port_path(s, a[0], op);
  if (!path) return OT_UNWIND;
  ot_io_file_info info = {0};
  int reason = s->io_host.info ? s->io_host.info(s->io_userdata, path, &info) : OT_IO_OTHER;
  ot_host_free(path);
  if (exists && (reason == OT_IO_NOT_FOUND || reason == OT_IO_NOT_DIRECTORY)) return ot_false;
  if (reason) return port_error(s, a[0], op, reason);
  if (exists) return ot_true;
  otv result = ot_table_new(s, 3), key = ot_nil, value = ot_nil;
  OT_FRAME_SCOPED(s, &result, &key, &value);
  key = ot_intern(s, "type", 4, true);
  const char* kind = info.kind == 1 ? "file" : info.kind == 2 ? "directory" : "other";
  value = ot_intern(s, kind, strlen(kind), true);
  ot_table_put(s, result, key, value);
  key = ot_intern(s, "size", 4, true);
  value = info.size <= (uint64_t)OT_INT_MAX ? ot_make_int((intptr_t)info.size)
                                            : ot_make_float(s, (double)info.size);
  ot_table_put(s, result, key, value);
  key = ot_intern(s, "modified", 8, true);
  value = ot_make_float(s, info.modified);
  ot_table_put(s, result, key, value);
  return result;
}

static otv n_file_info(ots* s, otv* a, int n) { return port_file_info(s, a, n, false); }

static otv n_file_exists(ots* s, otv* a, int n) { return port_file_info(s, a, n, true); }

static otv port_file_change(ots* s, otv* a, int n, int operation) {
  const char* op = operation == 0   ? "delete-file"
                   : operation == 1 ? "make-directory"
                                    : "rename-file";
  int argc = operation == 2 ? 2 : 1;
  if (need_arity(s, op, n, argc, argc) == OT_UNWIND) return OT_UNWIND;
  char* path = port_path(s, a[0], op);
  if (!path) return OT_UNWIND;
  int reason = OT_IO_OTHER;
  if (operation == 0 && s->io_host.remove) reason = s->io_host.remove(s->io_userdata, path);
  if (operation == 1 && s->io_host.mkdir) reason = s->io_host.mkdir(s->io_userdata, path);
  if (operation == 2) {
    char* to = port_path(s, a[1], op);
    if (!to) {
      ot_host_free(path);
      return OT_UNWIND;
    }
    if (s->io_host.rename) reason = s->io_host.rename(s->io_userdata, path, to);
    ot_host_free(to);
  }
  ot_host_free(path);
  return reason ? port_error(s, a[0], op, reason) : ot_nil;
}

static otv n_delete_file(ots* s, otv* a, int n) { return port_file_change(s, a, n, 0); }

static otv n_mkdir(ots* s, otv* a, int n) { return port_file_change(s, a, n, 1); }

static otv n_rename_file(ots* s, otv* a, int n) { return port_file_change(s, a, n, 2); }

typedef struct port_directory {
  ots* state;
  otv result;
} port_directory;

static void port_directory_entry(void* user, const char* name) {
  port_directory* d = user;
  otv text = ot_make_string(d->state, name, strlen(name));
  array_push(d->state, &d->result, text);
}

static otv n_list_directory(ots* s, otv* a, int n) {
  if (need_arity(s, "list-directory", n, 1, 1) == OT_UNWIND) return OT_UNWIND;
  char* path = port_path(s, a[0], "list-directory");
  if (!path) return OT_UNWIND;
  port_directory d = {.state = s, .result = ot_array_new(s, 8)};
  OT_FRAME_SCOPED(s, &d.result);
  int reason = s->io_host.list ? s->io_host.list(s->io_userdata, path, port_directory_entry, &d)
                               : OT_IO_OTHER;
  ot_host_free(path);
  if (reason) return port_error(s, a[0], "list-directory", reason);
  /* The shared sort preserves array kind and Unicode string ordering. */
  otv symbol = ot_intern(s, "sort", 4, false);
  otv variable = namespace_var(s, s->core_namespace, symbol, false);
  return apply_value(s, ((ot_var_obj*)ot_as_obj(variable))->value, &d.result, 1, false);
}

static otv n_file_symbolic_link(ots* s, otv* a, int n) {
  const char* op = "file-symbolic-link?";
  if (need_arity(s, op, n, 1, 1) == OT_UNWIND) return OT_UNWIND;
  char* path = port_path(s, a[0], op);
  if (!path) return OT_UNWIND;
  bool result = false;
  int reason = s->io_host.symbolic_link ? s->io_host.symbolic_link(s->io_userdata, path, &result)
                                        : OT_IO_OTHER;
  ot_host_free(path);
  if (reason == OT_IO_NOT_FOUND || reason == OT_IO_NOT_DIRECTORY) return ot_false;
  if (reason) return port_error(s, a[0], op, reason);
  return result ? ot_true : ot_false;
}

static otv n_current_directory(ots* s, otv* a, int n) {
  (void)a;
  if (need_arity(s, "current-directory", n, 0, 0) == OT_UNWIND) return OT_UNWIND;
  char* path = NULL;
  int reason = s->io_host.cwd ? s->io_host.cwd(s->io_userdata, &path) : OT_IO_OTHER;
  if (reason) return port_error(s, ot_nil, "current-directory", reason);
  otv result = ot_make_string(s, path, strlen(path));
  ot_host_free(path);
  return result;
}

static const nat_def port_natives[] = {{"flush-ports!", n_flush_all},
                                       {"file-exists?", n_file_exists},
                                       {"file-symbolic-link?", n_file_symbolic_link},
                                       {"current-directory", n_current_directory},
                                       {"file-info", n_file_info},
                                       {"delete-file", n_delete_file},
                                       {"rename-file", n_rename_file},
                                       {"make-directory", n_mkdir},
                                       {"list-directory", n_list_directory},
                                       {"display-to", n_display_to},
                                       {"write-to", n_write_to},
                                       {"port?", n_port_p},
                                       {"input-port?", n_input_p},
                                       {"output-port?", n_output_p},
                                       {"input-port-open?", n_input_open_p},
                                       {"output-port-open?", n_output_open_p},
                                       {"open-input-file", n_open_input},
                                       {"open-output-file", n_open_output},
                                       {"open-binary-input-file", n_open_input},
                                       {"open-binary-output-file", n_open_output},
                                       {"open-input-string", n_input_string},
                                       {"open-input-bytevector", n_input_bytes},
                                       {"open-output-string", n_output_string},
                                       {"open-output-bytevector", n_output_bytes},
                                       {"get-output-string", n_get_string},
                                       {"get-output-bytevector", n_get_bytes},
                                       {"close-port", n_close_port},
                                       {"close-input-port", n_close_input},
                                       {"close-output-port", n_close_output},
                                       {"read-char", n_read_char},
                                       {"peek-char", n_peek_char},
                                       {"read-u8", n_read_u8},
                                       {"peek-u8", n_peek_u8},
                                       {"read-bytevector", n_read_bytes},
                                       {"read-bytevector!", n_read_into},
                                       {"%read-characters", n_read_characters},
                                       {"read-all", n_read_all},
                                       {"read", n_read},
                                       {"read-line", n_read_line},
                                       {"read-until", n_read_until},
                                       {"read-while", n_read_while},
                                       {"skip-while", n_skip_while},
                                       {"write-string", n_write_string},
                                       {"write-char", n_write_char},
                                       {"write-u8", n_write_u8},
                                       {"write-bytevector", n_write_bytes},
                                       {"flush-output-port", n_flush},
                                       {"make-input-port", n_make_input},
                                       {"make-output-port", n_make_output},
                                       {"open-ports", n_open_ports}};

static otv port_param(ots* s, const char* name, const char* label, bool input, unsigned kind,
                      intptr_t handle) {
  otv symbol = ot_intern(s, name, strlen(name), false), value = ot_nil, buffer = ot_nil;
  OT_FRAME_SCOPED(s, &symbol, &value, &buffer);
  buffer = ot_make_bytevector(s, 4096);
  value = ot_make_string(s, label, strlen(label));
  value = port_new(s, value, input, kind, handle, buffer, ot_nil);
  ot_param_obj* param = must_alloc(s, sizeof(*param), OBJ_PARAM);
  ot_store(s, param, &param->name, symbol);
  ot_store(s, param, &param->value, value);
  otv result = ot_from_obj(param);
  OT_FRAME_SCOPED(s, &result);
  define_var(s, s->core_namespace, symbol, result, ot_nil, false);
  return result;
}

static void ports_init(ots* s) {
  ot_set_io_host(s, NULL, NULL);
  s->open_ports = ot_null;
  s->input_port_param = port_param(s, "current-input-port", "stdin", true, PORT_STANDARD, 0);
  s->output_port_param = port_param(s, "current-output-port", "stdout", false, PORT_WRITER, 1);
  s->error_port_param = port_param(s, "current-error-port", "stderr", false, PORT_STANDARD, 2);
  otv old = s->vm.current_namespace;
  OT_FRAME_SCOPED(s, &old);
  s->vm.current_namespace = s->core_namespace;
  for (size_t i = 0; i < sizeof port_natives / sizeof *port_natives; i++)
    ot_def_nat(s, port_natives[i].name, port_natives[i].function);
  s->vm.current_namespace = old;
}

static const nat_def core_nats[] = {
    {"call-with-cleanup", nat_call_with_cleanup},
    {"+", nat_add},
    {"*", nat_multiply},
    {"-", nat_subtract},
    {"/", nat_divide},
    {"bitwise-and", nat_bitwise_and},
    {"bitwise-ior", nat_bitwise_ior},
    {"bitwise-xor", nat_bitwise_xor},
    {"bitwise-not", nat_bitwise_not},
    {"bitwise-arithmetic-shift", nat_bitwise_shift},
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
    {"char?", nat_char_p},
    {"eof-object?", nat_eof_p},
    {"eof-object", nat_eof_object},
    {"char->integer", nat_char_integer},
    {"integer->char", nat_integer_char},
    {"char-upcase", nat_char_upcase},
    {"char-downcase", nat_char_downcase},
    {"char-alphabetic?", nat_char_alphabetic},
    {"char-numeric?", nat_char_numeric},
    {"char-whitespace?", nat_char_whitespace},
    {"string", nat_string},
    {"string->list", nat_string_list},
    {"list->string", nat_list_string},
    {"string-ref", nat_string_ref},
    {"bytevector?", nat_bytevector_p},
    {"bytevector", nat_bytevector},
    {"make-bytevector", nat_make_bytevector},
    {"bytevector-length", nat_bytevector_length},
    {"bytevector-u8-ref", nat_bytevector_ref},
    {"bytevector-u8-set!", nat_bytevector_set},
    {"bytevector-copy", nat_bytevector_copy},
    {"bytevector-copy!", nat_bytevector_copy_into},
    {"bytevector-append", nat_bytevector_append},
    {"utf8->string", nat_utf8_string},
    {"string->utf8", nat_string_utf8},
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
    {"set-applied!", nat_set_applied},
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
    {"read-file", nat_read_file},
    {"macroexpand-1", nat_macroexpand_one},
    {"macroexpand", nat_macroexpand},
    {"macroexpand-all", nat_macroexpand_all},
    {"gensym", nat_gensym},
    {"current-ns", nat_current_ns},
    {"describe", nat_describe},
    {"expander-macro-var", nat_macro_oracle},
    {"expander-invoke-macro", nat_expander_invoke_macro},
    {"expander-qualify", nat_expander_qualify},
    {"expander-local-name", nat_expander_local_name},
    {"expander-native-syntax?", nat_expander_native_syntax},
    {"expander-application", nat_expander_application},
    {"expander-optional-function", nat_expander_optional_function},
    {"expander-locate", nat_expander_locate},
    {"expander-fail", nat_expander_fail},
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
    {"exit", nat_exit},
};

/* =========================================================================
 * 9. BOOTSTRAP, EMBEDDING, AND EXTENSION API
 * ========================================================================= */

static void define_native(ots* state, const char* name, ot_nat function, bool vm_direct) {
  otv symbol = ot_intern(state, name, strlen(name), false);
  OT_FRAME_SCOPED(state, &symbol);
  ot_nat_obj* nat = must_alloc(state, sizeof(*nat), OBJ_NAT);
  nat->function = function;
  nat->context_function = NULL;
  ot_store(state, nat, &nat->context, ot_false);
  nat->vm_direct = vm_direct;
  ot_store(state, nat, &nat->name, symbol);
  define_var(state, state->vm.current_namespace, symbol, ot_from_obj(nat), ot_nil, false);
}

void ot_def_nat(ots* state, const char* name, ot_nat function) {
  define_native(state, name, function, false);
}

otv ot_make_native(ots* state, const char* name, ot_context_nat function, otv context) {
  otv symbol = ot_false;
  OT_FRAME_SCOPED(state, &symbol, &context);
  symbol = ot_intern(state, name, strlen(name), false);
  ot_nat_obj* nat = must_alloc(state, sizeof(*nat), OBJ_NAT);
  nat->function = NULL;
  nat->context_function = function;
  nat->vm_direct = false;
  ot_store(state, nat, &nat->name, symbol);
  ot_store(state, nat, &nat->context, context);
  return ot_from_obj(nat);
}

static void register_core(ots* state) {
  for (size_t i = 0; i < sizeof core_nats / sizeof core_nats[0]; i++) {
    const char* name = core_nats[i].name;
    /* Direct natives borrow VM argument slots. Only opt in functions which do
     * not call back into the VM or a host writer while using those slots. */
    static const ot_nat direct[] = {
        nat_add,        nat_subtract,      nat_multiply,    nat_divide,   nat_quotient,
        nat_remainder,  nat_modulo,        nat_num_equal,   nat_less,     nat_greater,
        nat_less_equal, nat_greater_equal, nat_min,         nat_max,      nat_inc,
        nat_dec,        nat_abs,           nat_not,         nat_nil_p,    nat_null_p,
        nat_boolean_p,  nat_int_p,         nat_float_p,     nat_number_p, nat_symbol_p,
        nat_keyword_p,  nat_string_p,      nat_pair_p,      nat_array_p,  nat_table_p,
        nat_buffer_p,   nat_macro_p,       nat_procedure_p, nat_ext_p,    nat_list_p,
        nat_true_p,     nat_false_p,       nat_eq,          nat_equal,    nat_cons,
        nat_car,        nat_cdr,           nat_set_car,     nat_set_cdr,  nat_list,
        nat_append,     nat_length,        nat_reverse,     nat_identity,
    };
    bool vm_direct = false;
    for (size_t j = 0; j < sizeof direct / sizeof direct[0]; j++)
      if (core_nats[i].function == direct[j]) {
        vm_direct = true;
        break;
      }
    define_native(state, name, core_nats[i].function, vm_direct);
  }
}

static bool eval_source(ots* state, const char* source, size_t length, const char* name,
                        bool expand, otv* out) {
  reader input = {.state = state, .source = source, .length = length, .name = name};
  otv result = ot_nil;
  otv form = ot_nil;
  OT_FRAME_SCOPED(state, &result, &form);
  for (;;) {
    reader_space(&input);
    size_t start = input.offset;
    form = read_form(&input);
    if (form == OT_UNDEFINED) break;
    if (form == OT_UNWIND) return false;
    result = expand ? run_top_form(state, form) : compile_and_run_form(state, form, ot_nil);
    if (result == OT_UNWIND) {
      /* Point an error that has no location at the top-level form. */
      otv condition = state->vm.condition;
      otv span_key = ot_intern(state, "span", 4, true);
      if (state->vm.unwind_kind == UNWIND_CONDITION && is_type(condition, OBJ_TABLE) &&
          ot_table_get(state, condition, span_key, ot_nil) == ot_nil) {
        OT_FRAME_SCOPED(state, &condition);
        otv span = source_span(state, name, source, start, input.offset);
        OT_FRAME_SCOPED(state, &span);
        ot_table_put(state, condition, ot_intern(state, "span", 4, true), span);
        state->vm.condition = condition;
      }
      return false;
    }
  }
  if (out != NULL) *out = result;
  return true;
}

void ot_select_namespace(ots* state, const char* name, size_t length) {
  otv symbol = ot_intern(state, name, length, false);
  state->vm.current_namespace = namespace_find(state, symbol, true);
}

bool ot_eval_results(ots* state, const char* source, size_t length, const char* name, otv* results,
                     size_t* line) {
  size_t counted = 0;
  *line = 1;
  reader input = {.state = state, .source = source, .length = length, .name = name};
  otv form = ot_nil, result = ot_nil;
  OT_FRAME_SCOPED(state, &form, &result);
  for (;;) {
    reader_space(&input);
    while (counted < input.offset)
      if (source[counted++] == '\n') (*line)++;
    form = read_form(&input);
    if (form == OT_UNDEFINED) return true;
    if (form == OT_UNWIND) return false;
    result = run_top_form(state, form);
    if (result == OT_UNWIND) return false;
    buf rendered = {0};
    render_value(state, &rendered, result, false, 0);
    result = ot_make_string(state, rendered.data, rendered.length);
    buf_free(&rendered);
    *results = ot_array_append(state, *results, result);
  }
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
    result = run_top_form(state, form);
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
                     .gc_stress = false,
                     .gc_force_compact = false};
}

static void extra_module_init(ots* state) {
  otv ignored;
  (void)eval_source(state, ot_extra_src, ot_extra_src_len, "<otium.extra>", true, &ignored);
}

ots* ot_create(const ot_config* configuration) {
  ot_config config = configuration == NULL ? ot_config_default() : *configuration;
  if (config.heap_init < 1024 || config.heap_max < config.heap_init ||
      config.heap_max > (SIZE_MAX - 7) / 2 || config.max_depth == 0)
    return NULL;
  ots* state = ot_host_alloc(sizeof(*state));
  if (state == NULL) return NULL;
  memset(state, 0, sizeof(*state));
  state->vm.frame_limit = config.max_depth;
  state->config = config;
  state->config.gc_stress = false;
  if (!ot_gc_heap_init(state)) {
    ot_host_free(state);
    return NULL;
  }
  state->symbols = ot_nil;
  state->namespaces = ot_nil;
  state->core_namespace = ot_nil;
  state->vm.current_namespace = ot_nil;
  state->expander = ot_nil;
  state->expand_head = ot_nil;
  state->type_parents = ot_nil;
  state->dispatch_types = ot_nil;
  state->object_class = ot_nil;
  state->update_instance = ot_nil;
  state->input_port_param = state->output_port_param = state->error_port_param = state->open_ports =
      ot_nil;
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
  /* The diagnostic catalog's condition types are all errors. */
  for (size_t i = 0; i < sizeof ot_diagnostic_entries / sizeof *ot_diagnostic_entries; i++) {
    otv type = ot_intern(state, ot_diagnostic_entries[i].type,
                         strlen(ot_diagnostic_entries[i].type), false);
    OT_FRAME_SCOPED(state, &type);
    otv error = ot_intern(state, "error", 5, false);
    ot_table_put(state, state->type_parents, type, error);
  }

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
  otv head_name = ot_intern(state, "expander-expand-head", 20, false);
  otv head_var = namespace_var(state, state->core_namespace, head_name, false);
  if (!is_type(head_var, OBJ_VAR)) {
    ot_destroy(state);
    return NULL;
  }
  state->expand_head = ((ot_var_obj*)ot_as_obj(head_var))->value;
  ot_register_module(state, "otium.reader", reader_module_init);
  ot_register_module(state, "otium.extra", extra_module_init);
  ot_register_module(state, "otium.generic", generic_module_init);
  otv generic_name = ot_intern(state, "otium.generic", 13, false);
  state->vm.current_namespace = namespace_find(state, generic_name, true);
  generic_module_init(state);
  otv public_var = ((ot_namespace_obj*)ot_as_obj(state->vm.current_namespace))->vars;
  OT_FRAME_SCOPED(state, &public_var);
  while (is_type(public_var, OBJ_VAR)) {
    ot_var_obj* var = (ot_var_obj*)ot_as_obj(public_var);
    define_var(state, state->core_namespace, var->name, var->value, ot_nil, false);
    public_var = ((ot_var_obj*)ot_as_obj(public_var))->next;
  }
  ((ot_namespace_obj*)ot_as_obj(state->vm.current_namespace))->loaded = true;
  state->vm.current_namespace = state->core_namespace;
  if (!eval_source(state, ot_prelude_src, ot_prelude_src_len, "<prelude>", true, &ignored)) {
    ot_destroy(state);
    return NULL;
  }
  ports_init(state);
  otv user_name = ot_intern(state, "user", 4, false);
  state->vm.current_namespace = namespace_find(state, user_name, true);
  ot_register_ffi(state);
  ot_register_cli(state);
  state->config.gc_stress = config.gc_stress;
  return state;
}

void ot_destroy(ots* state) {
  if (state == NULL) return;
  (void)ot_flush_ports(state);
  ot_host_free(state->loaded_source_name);
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
#ifdef OT_WITH_FFI
  ot_destroy_ffi(state);
#endif
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
  ot_gc_heap_destroy(state);
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

void ot_set_source_name(ots* state, const char* name) {
  ot_host_free(state->loaded_source_name);
  size_t length = strlen(name);
  state->loaded_source_name = ot_host_alloc(length + 1);
  if (state->loaded_source_name != NULL) memcpy(state->loaded_source_name, name, length + 1);
}

void ot_clear_condition(ots* state) {
  state->vm.condition = ot_nil;
  state->vm.unwind_args = ot_nil;
  state->vm.unwind_kind = UNWIND_NONE;
  state->vm.unwind_restart_id = 0;
}

bool ot_exit_status(const ots* state, int* status) {
  if (!state->quit_requested) return false;
  if (status != NULL) *status = state->quit_status;
  return true;
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
  ot_store(state, ext, &ext->next, state->exts);
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
  ot_store(state, ext, &ext->next, state->exts);
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
  OT_FRAME_SCOPED(state, &value);
  void* payload;
  if (!ot_ext_check(state, who, value, type, &payload)) return OT_UNWIND;
  ot_ext_obj* ext = (ot_ext_obj*)ot_as_obj(value);
  if (ext->pointer_payload && state->ext_types[type - 1].finalizer != NULL)
    state->ext_types[type - 1].finalizer(state, payload);
  ext = (ot_ext_obj*)ot_as_obj(value);
  ext->released = true;
  ext->payload.pointer = NULL;
  return value;
}
