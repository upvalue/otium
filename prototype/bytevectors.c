#include "internal.h"
#include <stdlib.h>
#include <string.h>

size_t oti_bytes_length(ot_value v) {
  size_t n;
  memcpy(&n, object_bytes(v), sizeof(n));
  return n;
}

unsigned char* oti_bytes_data(ot_value v) { return (unsigned char*)object_bytes(v) + sizeof(size_t); }

static ot_value fail(ot_runtime* rt, const char* s) {
  oti_error(rt, s);
  return 0;
}

static bool natural(ot_value v, size_t* n) {
  if (!ot_is_int(v) || ot_to_int(v) < 0) return false;
  *n = (size_t)ot_to_int(v);
  return true;
}

static bool byte(ot_value v) {
  return ot_is_int(v) && ot_to_int(v) >= 0 && ot_to_int(v) <= 255;
}

ot_value oti_bytes_new(ot_runtime* rt, size_t n) {
  if (n > UINT32_MAX - sizeof(size_t) - 32) return fail(rt, "bytevector too large");
  ot_value v = oti_alloc(rt, K_BYTEVECTOR, 0, sizeof(size_t) + n);
  if (v) memcpy(object_bytes(v), &n, sizeof(n));
  return v;
}

static bool slice(ot_runtime* rt, size_t first, size_t count, size_t n, size_t* start, size_t* end) {
  *start = 0;
  *end = n;
  if ((count && !natural(rt->stack[first], start)) ||
      (count > 1 && !natural(rt->stack[first + 1], end)) || *start > *end || *end > n)
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

ot_value oti_bytes_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count) {
  size_t min = 1, max = 1;
  switch (id) {
    case B_BYTES:
    case B_BYTES_APPEND:
      min = 0;
      max = SIZE_MAX;
      break;
    case B_MAKE_BYTES: max = 2; break;
    case B_BYTES_REF: min = max = 2; break;
    case B_BYTES_SET: min = max = 3; break;
    case B_BYTES_COPY:
    case B_STRING_UTF8:
    case B_UTF8_STRING: max = 3; break;
    case B_BYTES_COPY_MUT:
      min = 3;
      max = 5;
      break;
  }
  if (count < min || count > max) return fail(rt, "wrong number of bytevector arguments");
  ot_value a = count ? rt->stack[first] : OT_NIL;
  if (id == B_BYTES_P) return oti_kind(a, K_BYTEVECTOR) ? OT_TRUE : OT_FALSE;
  if (id == B_BYTES || id == B_MAKE_BYTES || id == B_BYTES_APPEND) {
    size_t n = 0;
    unsigned fill = 0;
    if (id == B_MAKE_BYTES) {
      if (!natural(a, &n) || (count == 2 && !byte(rt->stack[first + 1])))
        return fail(rt, "invalid bytevector length or fill");
      if (count == 2) fill = (unsigned)ot_to_int(rt->stack[first + 1]);
    } else
      for (size_t i = 0; i < count; i++) {
        ot_value v = rt->stack[first + i];
        size_t add = 1;
        if (id == B_BYTES) {
          if (!byte(v)) return fail(rt, "expected byte");
        } else {
          if (!oti_kind(v, K_BYTEVECTOR)) return fail(rt, "expected bytevector");
          add = oti_bytes_length(v);
        }
        if (add > SIZE_MAX - n) return fail(rt, "bytevector too large");
        n += add;
      }
    ot_value result = oti_bytes_new(rt, n);
    if (!result) return 0;
    unsigned char* dest = oti_bytes_data(result);
    if (id == B_MAKE_BYTES) memset(dest, fill, n);
    else
      for (size_t i = 0, offset = 0; i < count; i++) {
        ot_value v = rt->stack[first + i];
        if (id == B_BYTES) dest[i] = (unsigned char)ot_to_int(v);
        else {
          size_t len = oti_bytes_length(v);
          memcpy(dest + offset, oti_bytes_data(v), len);
          offset += len;
        }
      }
    return result;
  }
  if (id == B_STRING_UTF8) {
    if (!oti_kind(a, K_STRING)) return fail(rt, "expected string");
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
  if (!oti_kind(a, K_BYTEVECTOR)) return fail(rt, "expected bytevector");
  size_t n = oti_bytes_length(a);
  if (id == B_BYTES_LENGTH) return ot_int((intptr_t)n);
  if (id == B_BYTES_REF || id == B_BYTES_SET) {
    size_t index;
    if (!natural(rt->stack[first + 1], &index) || index >= n)
      return fail(rt, "bytevector index out of bounds");
    if (id == B_BYTES_REF) return ot_int(oti_bytes_data(a)[index]);
    if (!byte(rt->stack[first + 2])) return fail(rt, "expected byte");
    oti_bytes_data(a)[index] = (unsigned char)ot_to_int(rt->stack[first + 2]);
    return OT_NIL;
  }
  if (id == B_BYTES_COPY_MUT) {
    ot_value from = rt->stack[first + 2];
    size_t at, start, end;
    if (!oti_kind(from, K_BYTEVECTOR)) return fail(rt, "expected source bytevector");
    if (!natural(rt->stack[first + 1], &at) || at > n)
      return fail(rt, "bytevector destination out of bounds");
    if (!slice(rt, first + 3, count - 3, oti_bytes_length(from), &start, &end)) return 0;
    if (end - start > n - at) return fail(rt, "bytevector destination too short");
    memmove(oti_bytes_data(a) + at, oti_bytes_data(from) + start, end - start);
    return OT_NIL;
  }
  size_t start, end;
  if (!slice(rt, first + 1, count - 1, n, &start, &end)) return 0;
  if (id == B_UTF8_STRING) {
    for (size_t p = start; p < end;) {
      size_t next = oti_utf8_scalar(oti_bytes_data(a), p, end);
      if (!next) return oti_decode_error(rt, p);
      p = next;
    }
    char* copy = malloc(end - start + 1);
    if (!copy) return fail(rt, "out of host memory");
    memcpy(copy, oti_bytes_data(a) + start, end - start);
    ot_value result = oti_text(rt, K_STRING, copy, end - start);
    free(copy);
    return result;
  }
  ot_value result = oti_bytes_new(rt, end - start);
  if (result) memcpy(oti_bytes_data(result), oti_bytes_data(rt->stack[first]) + start, end - start);
  return result;
}
