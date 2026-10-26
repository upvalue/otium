#include "internal.h"
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

size_t oti_utf8_encode(uint32_t cp, char bytes[4]) {
  if (cp < 128) {
    bytes[0] = (char)cp;
    return 1;
  }
  unsigned n = cp < 2048 ? 2 : cp < 65536 ? 3 : 4;
  for (unsigned i = n - 1; i; i--) {
    bytes[i] = (char)(128 | (cp & 63));
    cp >>= 6;
  }
  bytes[0] = (char)((n == 2 ? 192 : n == 3 ? 224 : 240) | cp);
  return n;
}

ot_value oti_string_get(ot_value string, ot_value key, ot_value fallback) {
  if (!ot_is_int(key) || ot_to_int(key) < 0) return fallback;
  size_t n, at = 0, index = (size_t)ot_to_int(key);
  const char* s = oti_text_bytes(string, &n);
  for (size_t i = 0; at < n; i++) {
    uint32_t cp;
    at = oti_utf8_next(s, n, at, &cp);
    if (i == index) return ((ot_value)cp << 8) | TAG_CHAR;
  }
  return fallback;
}

static char* display(ot_runtime* rt, size_t first, size_t count, size_t* length) {
  char* bytes = NULL;
  FILE* stream = open_memstream(&bytes, length);
  if (!stream) {
    oti_fail(rt, "cannot allocate display buffer");
    return NULL;
  }
  for (size_t i = 0; i < count; i++) ot_print(rt, stream, rt->stack[first + i]);
  bool ok = !ferror(stream);
  if (fclose(stream)) ok = false;
  if (!ok) {
    free(bytes);
    oti_fail(rt, "cannot format value");
    return NULL;
  }
  return bytes;
}

static ot_value copied(ot_runtime* rt, const char* bytes, size_t n) {
  char* copy = malloc(n ? n : 1);
  if (!copy) return oti_fail(rt, "out of host memory");
  memcpy(copy, bytes, n);
  ot_value result = oti_text(rt, K_STRING, copy, n);
  free(copy);
  return result;
}

static bool append_buffer(ot_runtime* rt, ot_value* buffer, const char* bytes, size_t n) {
  oti_buffer_info* info = object_bytes(*buffer);
  if (n > SIZE_MAX - info->length) return oti_error(rt, "buffer too large");
  size_t needed = info->length + n;
  if (needed > info->capacity) {
    size_t capacity = info->capacity < 32 ? 32 : info->capacity;
    while (capacity < needed) {
      if (capacity > SIZE_MAX / 2) {
        capacity = needed;
        break;
      }
      capacity *= 2;
    }
    oti_roots roots;
    oti_roots_push(&rt->heap, &roots, buffer, 1);
    ot_value data = oti_alloc(rt, K_BUFFER_DATA, 0, capacity);
    oti_roots_pop(&rt->heap, &roots);
    if (!data) return false;
    info = object_bytes(*buffer);
    if (info->length)
      memcpy(object_bytes(data), object_bytes(object_fields(*buffer)[0]), info->length);
    object_fields(*buffer)[0] = data;
    info->capacity = capacity;
  }
  if (n) memcpy((char*)object_bytes(object_fields(*buffer)[0]) + info->length, bytes, n);
  info->length = needed;
  for (size_t p = 0; p < n;) {
    uint32_t cp;
    p = oti_utf8_next(bytes, n, p, &cp);
    info->characters++;
  }
  return true;
}

ot_value oti_string_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count) {
  if (id == B_STR || id == B_BUFFER || id == B_BUFFER_PUSH) {
    if (id == B_BUFFER && count > 1) return oti_fail(rt, "buffer expects at most one argument");
    if (id == B_BUFFER_PUSH && (!count || !oti_kind(rt->stack[first], K_BUFFER)))
      return oti_fail(rt, "expected buffer");
    size_t n;
    char* bytes = display(rt, first + (id == B_BUFFER_PUSH), count - (id == B_BUFFER_PUSH), &n);
    if (!bytes) return 0;
    ot_value result;
    if (id == B_STR) result = oti_text(rt, K_STRING, bytes, n);
    else {
      result = id == B_BUFFER ? oti_alloc(rt, K_BUFFER, 1, sizeof(oti_buffer_info)) : rt->stack[first];
      if (result && !append_buffer(rt, &result, bytes, n)) result = 0;
    }
    free(bytes);
    return result;
  }
  if (id == B_STRING_LT) {
    if (count < 2) return oti_fail(rt, "string<? needs at least two strings");
    bool result = true;
    for (size_t i = 0; i < count; i++) {
      if (!oti_kind(rt->stack[first + i], K_STRING)) return oti_fail(rt, "expected string");
      if (i) {
        size_t a, b;
        const char *x = oti_text_bytes(rt->stack[first + i - 1], &a), *y = oti_text_bytes(rt->stack[first + i], &b);
        int order = memcmp(x, y, a < b ? a : b);
        result &= order < 0 || (!order && a < b);
      }
    }
    return result ? OT_TRUE : OT_FALSE;
  }
  if (count != 1) return oti_fail(rt, "wrong number of arguments");
  ot_value value = rt->stack[first];
  if (id == B_BUFFER_STRING) {
    if (!oti_kind(value, K_BUFFER)) return oti_fail(rt, "expected buffer");
    size_t n = ((oti_buffer_info*)object_bytes(value))->length;
    return copied(rt, n ? object_bytes(object_fields(value)[0]) : "", n);
  }
  if (id == B_NAME || id == B_SYMBOL) {
    if (!oti_kind(value, K_STRING) && !oti_kind(value, K_SYMBOL) && !oti_kind(value, K_KEYWORD))
      return oti_fail(rt, "expected string, symbol or keyword");
    size_t n;
    const char* bytes = oti_text_bytes(value, &n);
    if (oti_kind(value, K_KEYWORD)) {
      bytes++;
      n--;
    }
    return id == B_NAME               ? copied(rt, bytes, n)
           : oti_kind(value, K_SYMBOL) ? value
                                      : oti_intern(rt, K_SYMBOL, bytes, n);
  }
  if (id == B_STRING_LIST) {
    if (!oti_kind(value, K_STRING)) return oti_fail(rt, "expected string");
    ot_value roots[3] = {value, OT_EMPTY, OT_EMPTY};
    oti_roots root;
    oti_roots_push(&rt->heap, &root, roots, 3);
    size_t n;
    oti_text_bytes(value, &n);
    for (size_t at = 0; at < n;) {
      size_t ignored;
      uint32_t cp;
      at = oti_utf8_next(oti_text_bytes(roots[0], &ignored), n, at, &cp);
      ot_value pair = oti_alloc(rt, K_PAIR, 2, 0);
      if (!pair) {
        roots[1] = 0;
        break;
      }
      object_fields(pair)[0] = ((ot_value)cp << 8) | TAG_CHAR;
      object_fields(pair)[1] = OT_EMPTY;
      if (roots[1] == OT_EMPTY) roots[1] = pair;
      else object_fields(roots[2])[1] = pair;
      roots[2] = pair;
    }
    ot_value result = roots[1];
    oti_roots_pop(&rt->heap, &root);
    return result;
  }
  if (id == B_STRING_NUMBER) {
    if (!oti_kind(value, K_STRING)) return oti_fail(rt, "expected string");
    size_t n;
    const char* bytes = oti_text_bytes(value, &n);
    while (n && isspace((unsigned char)*bytes)) {
      bytes++;
      n--;
    }
    while (n && isspace((unsigned char)bytes[n - 1])) n--;
    if (!n) return OT_NIL;
    for (size_t i = 0; i < n; i++)
      if (isspace((unsigned char)bytes[i]) || strchr(";()[]{}\"'`,", bytes[i]) || !bytes[i])
        return OT_NIL;
    oti_reader reader;
    oti_reader_init(&reader, bytes, n, 1024 * 1024);
    oti_ast* form = oti_reader_one(&reader);
    ot_value result = OT_NIL;
    if (form && !reader.error) {
      if (form->kind == AST_INT && form->as.integer >= INTPTR_MIN / 2 &&
          form->as.integer <= INTPTR_MAX / 2)
        result = ot_int((intptr_t)form->as.integer);
      else if (form->kind == AST_FLOAT) result = oti_float(rt, form->as.real);
    }
    oti_reader_destroy(&reader);
    return result;
  }
  if (id == B_INTEGER_CHAR) {
    if (!ot_is_int(value)) return oti_fail(rt, "expected integer code point");
    intptr_t cp = ot_to_int(value);
    if (cp < 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
      return oti_fail(rt, "invalid character code point");
    return ((ot_value)cp << 8) | TAG_CHAR;
  }
  if ((value & 255) != TAG_CHAR) return oti_fail(rt, "expected character");
  uint32_t cp = (uint32_t)(value >> 8);
  switch (id) {
    case B_CHAR_INTEGER: return ot_int(cp);
    case B_CHAR_UP:
      if (cp >= 'a' && cp <= 'z') cp -= 32;
      return ((ot_value)cp << 8) | TAG_CHAR;
    case B_CHAR_DOWN:
      if (cp >= 'A' && cp <= 'Z') cp += 32;
      return ((ot_value)cp << 8) | TAG_CHAR;
    case B_CHAR_SPACE: return cp == ' ' || (cp >= 9 && cp <= 13) ? OT_TRUE : OT_FALSE;
    case B_CHAR_ALPHA:
      return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') ? OT_TRUE : OT_FALSE;
    case B_CHAR_NUMERIC: return cp >= '0' && cp <= '9' ? OT_TRUE : OT_FALSE;
    case B_DIGIT_VALUE: return cp >= '0' && cp <= '9' ? ot_int(cp - '0') : OT_NIL;
    default: return oti_fail(rt, "unknown string builtin");
  }
}
