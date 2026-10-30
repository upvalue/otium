#include "internal.h"
#include <stdlib.h>
#include <string.h>

static bool byte(ot_value v) { return ot_is_int(v) && ot_to_int(v) >= 0 && ot_to_int(v) <= 255; }

ot_value oti_bytes_new(ot_state* rt, size_t n) {
  if (n > UINT32_MAX - sizeof(size_t) - 32) return oti_fail(rt, "bytevector too large");
  ot_value v = oti_alloc(rt, K_BYTEVECTOR, 0, sizeof(size_t) + n);
  if (v) oti_as_bytes(v)->length = n;
  return v;
}

static bool slice(ot_state* rt, size_t first, size_t count, size_t n, size_t* start, size_t* end) {
  *start = 0;
  *end = n;
  if ((count && !oti_natural(rt->stack[first], start)) ||
      (count > 1 && !oti_natural(rt->stack[first + 1], end)) || *start > *end || *end > n)
    return oti_error(rt, "invalid bytevector or string range");
  return true;
}

/* Validate one scalar without reading beyond the requested slice. */
size_t oti_utf8_scalar(const unsigned char* s, size_t p, size_t end) {
  unsigned c = s[p];
  size_t width;
  if (c < 128) return p + 1;
  if (c >= 0xc2 && c <= 0xdf) width = 2;
  else if (c >= 0xe0 && c <= 0xef) width = 3;
  else if (c >= 0xf0 && c <= 0xf4) width = 4;
  else return 0;
  if (width > end - p) return 0;
  for (size_t j = 1; j < width; j++)
    if ((s[p + j] & 0xc0) != 0x80) return 0;
  if ((c == 0xe0 && s[p + 1] < 0xa0) || (c == 0xed && s[p + 1] >= 0xa0) ||
      (c == 0xf0 && s[p + 1] < 0x90) || (c == 0xf4 && s[p + 1] >= 0x90))
    return 0;
  return p + width;
}

/* The bytevector first argument, after checking the arity. */
static bool bytes_target(ot_state* rt, const char* who, size_t first, size_t count, size_t min,
                         size_t max) {
  if (!oti_check_arity(rt, who, count, min, max)) return false;
  if (oti_kind(rt->stack[first], K_BYTEVECTOR)) return true;
  oti_fail(rt, "expected bytevector");
  return false;
}

static ot_value bytes_p(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "bytevector?", count, 1, 1)) return 0;
  return oti_kind(rt->stack[first], K_BYTEVECTOR) ? OT_TRUE : OT_FALSE;
}

static ot_value bytes_bytevector(ot_state* rt, size_t first, size_t count) {
  for (size_t i = 0; i < count; i++)
    if (!byte(rt->stack[first + i])) return oti_fail(rt, "expected byte");
  ot_value result = oti_bytes_new(rt, count);
  if (!result) return 0;
  unsigned char* dest = oti_bytes_data(result);
  for (size_t i = 0; i < count; i++) dest[i] = (unsigned char)ot_to_int(rt->stack[first + i]);
  return result;
}

static ot_value bytes_make(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "make-bytevector", count, 1, 2)) return 0;
  size_t n;
  if (!oti_natural(rt->stack[first], &n) || (count == 2 && !byte(rt->stack[first + 1])))
    return oti_fail(rt, "invalid bytevector length or fill");
  unsigned fill = count == 2 ? (unsigned)ot_to_int(rt->stack[first + 1]) : 0;
  ot_value result = oti_bytes_new(rt, n);
  if (!result) return 0;
  memset(oti_bytes_data(result), fill, n);
  return result;
}

static ot_value bytes_append(ot_state* rt, size_t first, size_t count) {
  size_t n = 0;
  for (size_t i = 0; i < count; i++) {
    ot_value v = rt->stack[first + i];
    if (!oti_kind(v, K_BYTEVECTOR)) return oti_fail(rt, "expected bytevector");
    size_t add = oti_bytes_length(v);
    if (add > SIZE_MAX - n) return oti_fail(rt, "bytevector too large");
    n += add;
  }
  ot_value result = oti_bytes_new(rt, n);
  if (!result) return 0;
  unsigned char* dest = oti_bytes_data(result);
  for (size_t i = 0, offset = 0; i < count; i++) {
    ot_value v = rt->stack[first + i];
    size_t len = oti_bytes_length(v);
    memcpy(dest + offset, oti_bytes_data(v), len);
    offset += len;
  }
  return result;
}

static ot_value bytes_string_utf8(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "string->utf8", count, 1, 3)) return 0;
  ot_value a = rt->stack[first];
  if (!oti_kind(a, K_STRING)) return oti_fail(rt, "expected string");
  size_t n = oti_bytes_length(a), chars = 0;
  const unsigned char* s = oti_bytes_data(a);
  for (size_t p = 0; p < n; chars++) {
    size_t next = oti_utf8_scalar(s, p, n);
    if (!next) return oti_decode_error(rt, p);
    p = next;
  }
  size_t start, end;
  if (!slice(rt, first + 1, count - 1, chars, &start, &end)) return 0;
  size_t lo = 0, hi;
  for (size_t i = 0; i < start; i++) lo = oti_utf8_scalar(s, lo, n);
  hi = lo;
  for (size_t i = start; i < end; i++) hi = oti_utf8_scalar(s, hi, n);
  ot_value result = oti_bytes_new(rt, hi - lo);
  if (result) memcpy(oti_bytes_data(result), oti_bytes_data(rt->stack[first]) + lo, hi - lo);
  return result;
}

static ot_value bytes_length(ot_state* rt, size_t first, size_t count) {
  if (!bytes_target(rt, "bytevector-length", first, count, 1, 1)) return 0;
  return ot_int((intptr_t)oti_bytes_length(rt->stack[first]));
}

/* The index argument of bytevector-u8-ref and bytevector-u8-set!. */
static bool bytes_index(ot_state* rt, size_t first, size_t* index) {
  if (oti_natural(rt->stack[first + 1], index) && *index < oti_bytes_length(rt->stack[first]))
    return true;
  oti_fail(rt, "bytevector index out of bounds");
  return false;
}

static ot_value bytes_ref(ot_state* rt, size_t first, size_t count) {
  size_t index;
  if (!bytes_target(rt, "bytevector-u8-ref", first, count, 2, 2) || !bytes_index(rt, first, &index))
    return 0;
  return ot_int(oti_bytes_data(rt->stack[first])[index]);
}

static ot_value bytes_set(ot_state* rt, size_t first, size_t count) {
  size_t index;
  if (!bytes_target(rt, "bytevector-u8-set!", first, count, 3, 3) ||
      !bytes_index(rt, first, &index))
    return 0;
  if (!byte(rt->stack[first + 2])) return oti_fail(rt, "expected byte");
  oti_bytes_data(rt->stack[first])[index] = (unsigned char)ot_to_int(rt->stack[first + 2]);
  return OT_NIL;
}

static ot_value bytes_copy_mut(ot_state* rt, size_t first, size_t count) {
  if (!bytes_target(rt, "bytevector-copy!", first, count, 3, 5)) return 0;
  ot_value a = rt->stack[first], from = rt->stack[first + 2];
  size_t n = oti_bytes_length(a), at, start, end;
  if (!oti_kind(from, K_BYTEVECTOR)) return oti_fail(rt, "expected source bytevector");
  if (!oti_natural(rt->stack[first + 1], &at) || at > n)
    return oti_fail(rt, "bytevector destination out of bounds");
  if (!slice(rt, first + 3, count - 3, oti_bytes_length(from), &start, &end)) return 0;
  if (end - start > n - at) return oti_fail(rt, "bytevector destination too short");
  memmove(oti_bytes_data(a) + at, oti_bytes_data(from) + start, end - start);
  return OT_NIL;
}

ot_value oti_bytes_copy_builtin(ot_state* rt, size_t first, size_t count) {
  if (!bytes_target(rt, "bytevector-copy", first, count, 1, 3)) return 0;
  size_t start, end;
  if (!slice(rt, first + 1, count - 1, oti_bytes_length(rt->stack[first]), &start, &end)) return 0;
  ot_value result = oti_bytes_new(rt, end - start);
  if (result) memcpy(oti_bytes_data(result), oti_bytes_data(rt->stack[first]) + start, end - start);
  return result;
}

static ot_value bytes_utf8_string(ot_state* rt, size_t first, size_t count) {
  if (!bytes_target(rt, "utf8->string", first, count, 1, 3)) return 0;
  ot_value a = rt->stack[first];
  size_t start, end;
  if (!slice(rt, first + 1, count - 1, oti_bytes_length(a), &start, &end)) return 0;
  for (size_t p = start; p < end;) {
    size_t next = oti_utf8_scalar(oti_bytes_data(a), p, end);
    if (!next) return oti_decode_error(rt, p);
    p = next;
  }
  return oti_text_from(rt, K_STRING, a, start, end - start);
}

const oti_builtin_def oti_bytes_builtins[] = {
    {B_BYTES, "bytevector", bytes_bytevector},
    {B_BYTES_P, "bytevector?", bytes_p},
    {B_MAKE_BYTES, "make-bytevector", bytes_make},
    {B_BYTES_LENGTH, "bytevector-length", bytes_length},
    {B_BYTES_REF, "bytevector-u8-ref", bytes_ref},
    {B_BYTES_SET, "bytevector-u8-set!", bytes_set},
    {B_BYTES_COPY, "bytevector-copy", oti_bytes_copy_builtin},
    {B_BYTES_COPY_MUT, "bytevector-copy!", bytes_copy_mut},
    {B_BYTES_APPEND, "bytevector-append", bytes_append},
    {B_UTF8_STRING, "utf8->string", bytes_utf8_string},
    {B_STRING_UTF8, "string->utf8", bytes_string_utf8},
    {0, NULL, NULL},
};
