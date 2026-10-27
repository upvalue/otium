#include "internal.h"
#include <stdlib.h>
#include <string.h>

static oti_array_info* array_info(ot_value a) { return object_bytes(a); }

static ot_value* items(ot_value a) {
  return array_info(a)->capacity ? object_fields(object_fields(a)[0]) : NULL;
}

static bool reserve(ot_state* rt, ot_value* array, size_t needed) {
  size_t capacity = array_info(*array)->capacity;
  if (needed <= capacity) return true;
  if (needed > UINT32_MAX) return oti_error(rt, "array too large");
  size_t grown = capacity > UINT32_MAX / 2 ? UINT32_MAX : capacity * 2;
  if (grown < 4) grown = 4;
  if (grown < needed) grown = needed;
  oti_roots root;
  oti_roots_push(&rt->heap, &root, array, 1);
  ot_value storage = oti_alloc(rt, K_ARRAY_DATA, (uint32_t)grown, 0);
  if (storage) {
    if (array_info(*array)->length)
      memcpy(object_fields(storage), items(*array), array_info(*array)->length * sizeof(ot_value));
    object_fields(*array)[0] = storage;
    array_info(*array)->capacity = grown;
  }
  oti_roots_pop(&rt->heap, &root);
  return storage != 0;
}

static ot_value array_new(ot_state* rt, size_t length) {
  ot_value a = oti_alloc(rt, K_ARRAY, 1, sizeof(oti_array_info));
  if (!a) return 0;
  *array_info(a) = (oti_array_info){0, 0};
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
  if (!oti_kind(a, K_ARRAY)) return oti_fail(rt, "expected a collection or nil");
  size_t index;
  return index_of(key, array_info(a)->length, &index) ? items(a)[index] : fallback;
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
      double x, y;
      memcpy(&x, object_bytes(p.a), sizeof(x));
      memcpy(&y, object_bytes(p.b), sizeof(y));
      if (x != y) {
        result = OT_FALSE;
        break;
      }
      continue;
    }
    if (kind == K_STRING || kind == K_SYMBOL || kind == K_KEYWORD) {
      size_t x, y;
      memcpy(&x, object_bytes(p.a), sizeof(x));
      memcpy(&y, object_bytes(p.b), sizeof(y));
      if (x != y ||
          memcmp((char*)object_bytes(p.a) + sizeof(x), (char*)object_bytes(p.b) + sizeof(y), x)) {
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
    size_t length = kind == K_PAIR ? 2 : array_info(p.a)->length;
    if (kind == K_ARRAY && length != array_info(p.b)->length) {
      result = OT_FALSE;
      break;
    }
    if (length > SIZE_MAX - count ||
        !oti_grow(rt, (void**)&work, &capacity, count + length, sizeof(*work))) {
      result = oti_fail(rt, "equality too large");
      break;
    }
    ot_value* left = kind == K_PAIR ? object_fields(p.a) : items(p.a);
    ot_value* right = kind == K_PAIR ? object_fields(p.b) : items(p.b);
    for (size_t i = 0; i < length; i++) work[count++] = (oti_comparison){left[i], right[i]};
  }
  free(work);
  free(seen);
  return result;
}

ot_value oti_collection_builtin(ot_state* rt, unsigned id, size_t first, size_t count) {
  if (id >= B_TABLE || (count && oti_kind(rt->stack[first], K_TABLE) && id != B_ARRAY &&
                        id != B_EQUAL && id != B_ARRAY_P))
    return oti_table_builtin(rt, id, first, count);
  if (id == B_ARRAY) {
    ot_value a = array_new(rt, count);
    if (!a) return 0;
    if (count) memcpy(items(a), rt->stack + first, count * sizeof(ot_value));
    array_info(a)->length = count;
    return a;
  }
  size_t min = id == B_GET || id == B_EQUAL || id == B_CONTAINS ? 2 : id == B_PUT_MUT ? 3 : 1;
  size_t max = id == B_GET ? 3 : id == B_PUSH || id == B_PUT_MUT ? SIZE_MAX : min;
  if (count < min || count > max || (id == B_PUT_MUT && !(count % 2)))
    return oti_fail(rt, "wrong number of collection arguments");
  ot_value a = rt->stack[first], b = count > 1 ? rt->stack[first + 1] : OT_NIL;
  if (id == B_COPY && oti_kind(a, K_INSTANCE)) return oti_instance_copy(rt, a);
  if (id == B_COPY && a == OT_NIL) return OT_NIL;
  if (id == B_CONTAINS && a == OT_NIL) return OT_FALSE;
  if (id == B_CONTAINS && oti_kind(a, K_STRING)) {
    if (!oti_kind(b, K_STRING)) return oti_fail(rt, "expected substring");
    size_t n, m;
    memcpy(&n, object_bytes(a), sizeof(n));
    memcpy(&m, object_bytes(b), sizeof(m));
    const char *s = (char*)object_bytes(a) + sizeof(n), *part = (char*)object_bytes(b) + sizeof(m);
    if (m > n) return OT_FALSE;
    for (size_t i = 0; i <= n - m; i++)
      if (!memcmp(s + i, part, m)) return OT_TRUE;
    return OT_FALSE;
  }
  if (id == B_CONTAINS && (a == OT_EMPTY || oti_kind(a, K_PAIR))) {
    ot_value slow = a, fast = a;
    while (oti_kind(a, K_PAIR)) {
      ot_value equal = oti_equal(rt, object_fields(a)[0], b, false);
      if (equal != OT_FALSE) return equal;
      a = object_fields(a)[1];
      if (oti_kind(slow, K_PAIR)) slow = object_fields(slow)[1];
      for (unsigned i = 0; i < 2 && oti_kind(fast, K_PAIR); i++) fast = object_fields(fast)[1];
      if (oti_kind(fast, K_PAIR) && fast == slow) return oti_fail(rt, "cyclic list");
    }
    return a == OT_EMPTY ? OT_FALSE : oti_fail(rt, "expected proper list");
  }
  if (id == B_ARRAY_P) return oti_kind(a, K_ARRAY) ? OT_TRUE : OT_FALSE;
  if (id == B_EQUAL) return oti_equal(rt, a, b, false);
  if (id == B_GET) return get(rt, a, b, count == 3 ? rt->stack[first + 2] : OT_NIL);
  if (id == B_LENGTH && (a == OT_NIL || a == OT_EMPTY)) return ot_int(0);
  if (id == B_LENGTH && oti_kind(a, K_BUFFER))
    return ot_int((intptr_t)((oti_buffer_info*)object_bytes(a))->characters);
  if (id == B_LENGTH && oti_kind(a, K_STRING))
    return oti_support_builtin(rt, B_STRING_LENGTH, first, count);
  if (id == B_LENGTH && oti_kind(a, K_PAIR)) {
    size_t length = 0;
    ot_value slow = a, fast = a;
    while (oti_kind(a, K_PAIR)) {
      length++;
      a = object_fields(a)[1];
      if (oti_kind(slow, K_PAIR)) slow = object_fields(slow)[1];
      for (unsigned i = 0; i < 2 && oti_kind(fast, K_PAIR); i++) fast = object_fields(fast)[1];
      if (oti_kind(fast, K_PAIR) && fast == slow)
        return oti_fail(rt, "length needs an acyclic list");
    }
    if (a != OT_EMPTY) return oti_fail(rt, "length needs a proper list");
    return ot_int((intptr_t)length);
  }
  if (oti_kind(a, K_BYTEVECTOR)) {
    if (id == B_LENGTH) return ot_int((intptr_t)oti_bytes_length(a));
    if (id == B_COPY) return oti_bytes_builtin(rt, B_BYTES_COPY, first, count);
    if (id == B_PUT_MUT) {
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
  }
  if (!oti_kind(a, K_ARRAY)) return oti_fail(rt, "expected an array");
  switch (id) {
    case B_LENGTH: return ot_int((intptr_t)array_info(a)->length);
    case B_PUT_MUT: {
      for (size_t i = 1; i < count; i += 2) {
        size_t index;
        if (!index_of(rt->stack[first + i], array_info(a)->length, &index))
          return oti_fail(rt, "array index out of bounds");
        items(a)[index] = rt->stack[first + i + 1];
      }
      return a;
    }
    case B_PUSH: {
      size_t length = array_info(a)->length;
      if (count - 1 > UINT32_MAX - length) return oti_fail(rt, "array too large");
      if (!reserve(rt, &a, length + count - 1)) return 0;
      if (count > 1)
        memcpy(items(a) + length, rt->stack + first + 1, (count - 1) * sizeof(ot_value));
      array_info(a)->length = length + count - 1;
      return a;
    }
    case B_POP: {
      if (!array_info(a)->length) return OT_NIL;
      size_t index = --array_info(a)->length;
      ot_value result = items(a)[index];
      items(a)[index] = OT_NIL;
      return result;
    }
    case B_COPY: {
      size_t length = array_info(a)->length;
      ot_value copy = array_new(rt, length);
      if (!copy) return 0;
      a = rt->stack[first];
      if (length) memcpy(items(copy), items(a), length * sizeof(ot_value));
      array_info(copy)->length = length;
      return copy;
    }
    case B_CONTAINS:
      for (size_t i = 0; i < array_info(a)->length; i++) {
        ot_value result = oti_equal(rt, items(a)[i], b, false);
        if (result != OT_FALSE) return result;
      }
      return OT_FALSE;
  }
  return oti_fail(rt, "unknown collection operation");
}
