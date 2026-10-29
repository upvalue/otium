#include "internal.h"
#include <stdlib.h>
#include <string.h>

static ot_value* items(ot_value a) {
  return oti_as_array(a)->capacity ? oti_as_array_data(oti_as_array(a)->data)->items : NULL;
}

static bool reserve(ot_state* rt, ot_value* array, size_t needed) {
  size_t capacity = oti_as_array(*array)->capacity;
  if (needed <= capacity) return true;
  if (needed > UINT32_MAX) return oti_error(rt, "array too large");
  size_t grown = capacity > UINT32_MAX / 2 ? UINT32_MAX : capacity * 2;
  if (grown < 4) grown = 4;
  if (grown < needed) grown = needed;
  oti_roots root;
  oti_roots_push(&rt->heap, &root, array, 1);
  ot_value storage = oti_alloc(rt, K_ARRAY_DATA, (uint32_t)grown, 0);
  if (storage) {
    if (oti_as_array(*array)->length)
      memcpy(oti_as_array_data(storage)->items, items(*array),
             oti_as_array(*array)->length * sizeof(ot_value));
    oti_as_array(*array)->data = storage;
    oti_as_array(*array)->capacity = grown;
  }
  oti_roots_pop(&rt->heap, &root);
  return storage != 0;
}

static ot_value array_new(ot_state* rt, size_t length) {
  ot_value a = oti_alloc(rt, K_ARRAY, 1, OTI_BYTES(oti_array_obj, 1));
  if (!a) return 0;
  oti_as_array(a)->length = 0;
  oti_as_array(a)->capacity = 0;
  if (!reserve(rt, &a, length)) return 0;
  return a;
}

static bool index_of(ot_value key, size_t length, size_t* index) {
  if (!ot_is_int(key) || ot_to_int(key) < 0) return false;
  *index = (size_t)ot_to_int(key);
  return *index < length;
}

static ot_value get(ot_state* rt, ot_value a, ot_value key, ot_value fallback) {
  if (a == OT_NIL) return fallback;
  if (oti_kind(a, K_BYTEVECTOR)) {
    size_t index;
    return index_of(key, oti_bytes_length(a), &index) ? ot_int(oti_bytes_data(a)[index]) : fallback;
  }
  if (oti_kind(a, K_STRING)) return oti_string_get(a, key, fallback);
  if (oti_kind(a, K_TABLE)) return oti_table_get(rt, a, key, fallback);
  if (!oti_kind(a, K_ARRAY)) {
    oti_type_error(rt, "get", "nil, table, array, string, or bytevector", a);
    return 0;
  }
  size_t index;
  return index_of(key, oti_as_array(a)->length, &index) ? items(a)[index] : fallback;
}

ot_value oti_collection_call(ot_state* rt, size_t callable, size_t first, size_t count) {
  if (count < 1 || count > 2) return oti_fail(rt, "array call needs an index and optional default");
  ot_value a = rt->stack[callable], b = rt->stack[first];
  ot_value fallback = count == 2 ? rt->stack[first + 1] : OT_NIL;
  if (oti_kind(a, K_KEYWORD))
    return oti_kind(b, K_TABLE) ? oti_table_get(rt, b, a, fallback) : fallback;
  if (oti_kind(a, K_ARRAY) && !ot_is_int(b)) return oti_fail(rt, "array index must be an integer");
  return get(rt, a, b, fallback);
}

typedef struct {
  ot_value a, b;
} oti_comparison;

static size_t pair_hash(oti_comparison p) {
  uintptr_t x = (p.a >> 3) ^ ((p.b >> 3) * (uintptr_t)0x9e3779b1u);
  x ^= x >> 16;
  x *= (uintptr_t)0x85ebca6bu;
  x ^= x >> 13;
  return x;
}

/* No managed allocation occurs during equality. The work list and seen set can
 * safely hold heap addresses, including cycles, until this call returns. */
ot_value oti_equal(ot_state* rt, ot_value a, ot_value b, bool keys) {
  if (a == b) return OT_TRUE;
  if (!value_is_ptr(a) || !value_is_ptr(b) || object_kind(a) != object_kind(b)) return OT_FALSE;
  oti_comparison *work = NULL, *seen = NULL;
  size_t capacity = 0, count = 0, seen_capacity = 0, seen_count = 0;
  ot_value result = OT_TRUE;
  if (!oti_grow(rt, (void**)&work, &capacity, 1, sizeof(*work))) return 0;
  work[count++] = (oti_comparison){a, b};
  while (count) {
    oti_comparison p = work[--count];
    if (p.a == p.b) continue;
    if (!value_is_ptr(p.a) || !value_is_ptr(p.b) || object_kind(p.a) != object_kind(p.b)) {
      result = OT_FALSE;
      break;
    }
    unsigned kind = object_kind(p.a);
    if (kind == K_FLOAT) {
      if (oti_as_float(p.a)->value != oti_as_float(p.b)->value) {
        result = OT_FALSE;
        break;
      }
      continue;
    }
    if (kind == K_STRING || kind == K_SYMBOL || kind == K_KEYWORD) {
      size_t x = oti_bytes_length(p.a), y = oti_bytes_length(p.b);
      if (x != y || memcmp(oti_bytes_data(p.a), oti_bytes_data(p.b), x)) {
        result = OT_FALSE;
        break;
      }
      continue;
    }
    if (kind != K_PAIR && (kind != K_ARRAY || keys)) {
      result = OT_FALSE;
      break;
    }
    if (seen_count >= seen_capacity / 2) {
      size_t next = seen_capacity ? seen_capacity * 2 : 64;
      if (next < seen_capacity || next > SIZE_MAX / sizeof(*seen)) {
        result = oti_fail(rt, "equality too large");
        break;
      }
      oti_comparison* grown = calloc(next, sizeof(*grown));
      if (!grown) {
        result = oti_fail(rt, "out of host memory");
        break;
      }
      for (size_t i = 0; i < seen_capacity; i++)
        if (seen[i].a) {
          size_t slot = pair_hash(seen[i]) & (next - 1);
          while (grown[slot].a) slot = (slot + 1) & (next - 1);
          grown[slot] = seen[i];
        }
      free(seen);
      seen = grown;
      seen_capacity = next;
    }
    size_t slot = pair_hash(p) & (seen_capacity - 1);
    while (seen[slot].a && (seen[slot].a != p.a || seen[slot].b != p.b))
      slot = (slot + 1) & (seen_capacity - 1);
    if (seen[slot].a) continue;
    seen[slot] = p;
    seen_count++;
    size_t length = kind == K_PAIR ? 2 : oti_as_array(p.a)->length;
    if (kind == K_ARRAY && length != oti_as_array(p.b)->length) {
      result = OT_FALSE;
      break;
    }
    if (length > SIZE_MAX - count ||
        !oti_grow(rt, (void**)&work, &capacity, count + length, sizeof(*work))) {
      result = oti_fail(rt, "equality too large");
      break;
    }
    if (kind == K_PAIR) {
      work[count++] = (oti_comparison){oti_car(p.a), oti_car(p.b)};
      work[count++] = (oti_comparison){oti_cdr(p.a), oti_cdr(p.b)};
      continue;
    }
    ot_value *left = items(p.a), *right = items(p.b);
    for (size_t i = 0; i < length; i++) work[count++] = (oti_comparison){left[i], right[i]};
  }
  free(work);
  free(seen);
  return result;
}

ot_value oti_array_from(ot_state* rt, size_t first, size_t count) {
  ot_value a = array_new(rt, count);
  if (!a) return 0;
  oti_copy_words(items(a), rt->stack + first, count);
  oti_as_array(a)->length = count;
  return a;
}

/* Most collection builtins hand a table first argument to the table module. */
static bool collection_on_table(ot_state* rt, size_t first, size_t count) {
  return count && oti_kind(rt->stack[first], K_TABLE);
}

static bool collection_arity(ot_state* rt, size_t count, size_t min, size_t max) {
  if (count >= min && count <= max) return true;
  return oti_error(rt, "wrong number of collection arguments");
}

static ot_value collection_array_p(ot_state* rt, size_t first, size_t count) {
  if (!collection_arity(rt, count, 1, 1)) return 0;
  return oti_kind(rt->stack[first], K_ARRAY) ? OT_TRUE : OT_FALSE;
}

static ot_value collection_equal(ot_state* rt, size_t first, size_t count) {
  if (!collection_arity(rt, count, 2, 2)) return 0;
  return oti_equal(rt, rt->stack[first], rt->stack[first + 1], false);
}

static ot_value collection_get(ot_state* rt, size_t first, size_t count) {
  if (collection_on_table(rt, first, count)) return oti_table_get_builtin(rt, first, count);
  if (!collection_arity(rt, count, 2, 3)) return 0;
  return get(rt, rt->stack[first], rt->stack[first + 1],
             count == 3 ? rt->stack[first + 2] : OT_NIL);
}

static ot_value collection_length(ot_state* rt, size_t first, size_t count) {
  if (collection_on_table(rt, first, count)) return oti_table_length_builtin(rt, first, count);
  if (!collection_arity(rt, count, 1, 1)) return 0;
  ot_value a = rt->stack[first];
  if (a == OT_NIL || a == OT_EMPTY) return ot_int(0);
  if (oti_kind(a, K_BUFFER)) return ot_int((intptr_t)oti_as_buffer(a)->characters);
  if (oti_kind(a, K_STRING)) return oti_string_length_builtin(rt, first, count);
  if (oti_kind(a, K_PAIR)) {
    /* Count with one pointer and check for a cycle with another at half
     * its speed: they meet only in a cycle. */
    size_t length = 0;
    ot_value slow = a;
    while (oti_kind(a, K_PAIR)) {
      a = oti_cdr(a);
      length++;
      if (!oti_kind(a, K_PAIR)) break;
      a = oti_cdr(a);
      length++;
      slow = oti_cdr(slow);
      if (a == slow) return oti_fail(rt, "length needs an acyclic list");
    }
    if (a != OT_EMPTY) return oti_fail(rt, "length needs a proper list");
    return ot_int((intptr_t)length);
  }
  if (oti_kind(a, K_BYTEVECTOR)) return ot_int((intptr_t)oti_bytes_length(a));
  if (!oti_kind(a, K_ARRAY)) return oti_fail(rt, "expected an array");
  return ot_int((intptr_t)oti_as_array(a)->length);
}

static ot_value collection_put_mut(ot_state* rt, size_t first, size_t count) {
  if (collection_on_table(rt, first, count)) return oti_table_put_builtin(rt, first, count);
  if (!collection_arity(rt, count, 3, SIZE_MAX)) return 0;
  if (!(count % 2)) return oti_fail(rt, "wrong number of collection arguments");
  ot_value a = rt->stack[first];
  if (oti_kind(a, K_BYTEVECTOR)) {
    for (size_t i = 1; i < count; i += 2) {
      size_t index;
      ot_value byte = rt->stack[first + i + 1];
      if (!index_of(rt->stack[first + i], oti_bytes_length(a), &index))
        return oti_fail(rt, "bytevector index out of bounds");
      if (!ot_is_int(byte) || ot_to_int(byte) < 0 || ot_to_int(byte) > 255)
        return oti_fail(rt, "expected byte");
      oti_bytes_data(a)[index] = (unsigned char)ot_to_int(byte);
    }
    return a;
  }
  if (!oti_kind(a, K_ARRAY)) return oti_fail(rt, "expected an array");
  for (size_t i = 1; i < count; i += 2) {
    size_t index;
    if (!index_of(rt->stack[first + i], oti_as_array(a)->length, &index))
      return oti_fail(rt, "array index out of bounds");
    items(a)[index] = rt->stack[first + i + 1];
  }
  return a;
}

static ot_value collection_push(ot_state* rt, size_t first, size_t count) {
  if (collection_on_table(rt, first, count)) return oti_table_push_builtin(rt, first, count);
  if (!collection_arity(rt, count, 1, SIZE_MAX)) return 0;
  ot_value a = rt->stack[first];
  if (!oti_kind(a, K_ARRAY)) return oti_fail(rt, "expected an array");
  size_t length = oti_as_array(a)->length;
  if (count - 1 > UINT32_MAX - length) return oti_fail(rt, "array too large");
  if (!reserve(rt, &a, length + count - 1)) return 0;
  if (count > 1) memcpy(items(a) + length, rt->stack + first + 1, (count - 1) * sizeof(ot_value));
  oti_as_array(a)->length = length + count - 1;
  return a;
}

static ot_value collection_pop(ot_state* rt, size_t first, size_t count) {
  if (collection_on_table(rt, first, count)) return oti_table_pop_builtin(rt, first, count);
  if (!collection_arity(rt, count, 1, 1)) return 0;
  ot_value a = rt->stack[first];
  if (!oti_kind(a, K_ARRAY)) return oti_fail(rt, "expected an array");
  if (!oti_as_array(a)->length) return OT_NIL;
  size_t index = --oti_as_array(a)->length;
  ot_value result = items(a)[index];
  items(a)[index] = OT_NIL;
  return result;
}

static ot_value collection_copy(ot_state* rt, size_t first, size_t count) {
  if (collection_on_table(rt, first, count)) return oti_table_copy_builtin(rt, first, count);
  if (!collection_arity(rt, count, 1, 1)) return 0;
  ot_value a = rt->stack[first];
  if (oti_kind(a, K_INSTANCE)) return oti_instance_copy(rt, a);
  if (oti_kind(a, K_RECORD)) return oti_record_copy(rt, a);
  if (a == OT_NIL) return OT_NIL;
  if (oti_kind(a, K_BYTEVECTOR)) return oti_bytes_copy_builtin(rt, first, count);
  if (!oti_kind(a, K_ARRAY)) return oti_fail(rt, "expected an array");
  size_t length = oti_as_array(a)->length;
  ot_value copy = array_new(rt, length);
  if (!copy) return 0;
  a = rt->stack[first];
  if (length) memcpy(items(copy), items(a), length * sizeof(ot_value));
  oti_as_array(copy)->length = length;
  return copy;
}

const oti_builtin_def oti_collection_builtins[] = {
    {B_ARRAY, "array", oti_array_from},      {B_ARRAY_P, "array?", collection_array_p},
    {B_LENGTH, "length", collection_length}, {B_GET, "get", collection_get},
    {B_PUT_MUT, "put!", collection_put_mut}, {B_PUSH, "push!", collection_push},
    {B_POP, "pop!", collection_pop},         {B_COPY, "copy", collection_copy},
    {B_EQUAL, "equal?", collection_equal},   {0, NULL, NULL},
};

