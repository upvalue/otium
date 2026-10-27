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

static char* display(ot_state* rt, size_t first, size_t count, size_t* length) {
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

static ot_value copied(ot_state* rt, const char* bytes, size_t n) {
  char* copy = malloc(n ? n : 1);
  if (!copy) return oti_fail(rt, "out of host memory");
  memcpy(copy, bytes, n);
  ot_value result = oti_text(rt, K_STRING, copy, n);
  free(copy);
  return result;
}

static bool append_buffer(ot_state* rt, ot_value* buffer, const char* bytes, size_t n) {
  oti_buffer_obj* info = oti_as_buffer(*buffer);
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
    info = oti_as_buffer(*buffer);
    if (info->length) memcpy(object_bytes(data), object_bytes(info->data), info->length);
    info->data = data;
    info->capacity = capacity;
  }
  if (n) memcpy((char*)object_bytes(info->data) + info->length, bytes, n);
  info->length = needed;
  for (size_t p = 0; p < n;) {
    uint32_t cp;
    p = oti_utf8_next(bytes, n, p, &cp);
    info->characters++;
  }
  return true;
}

static ot_value string_str(ot_state* rt, size_t first, size_t count) {
  size_t n;
  char* bytes = display(rt, first, count, &n);
  if (!bytes) return 0;
  ot_value result = oti_text(rt, K_STRING, bytes, n);
  free(bytes);
  return result;
}

static ot_value string_buffer(ot_state* rt, size_t first, size_t count) {
  if (count > 1) return oti_fail(rt, "buffer expects at most one argument");
  size_t n;
  char* bytes = display(rt, first, count, &n);
  if (!bytes) return 0;
  ot_value result = oti_alloc(rt, K_BUFFER, 1, OTI_BYTES(oti_buffer_obj, 1));
  if (result && !append_buffer(rt, &result, bytes, n)) result = 0;
  free(bytes);
  return result;
}

static ot_value string_buffer_push(ot_state* rt, size_t first, size_t count) {
  if (!count || !oti_kind(rt->stack[first], K_BUFFER)) return oti_fail(rt, "expected buffer");
  size_t n;
  char* bytes = display(rt, first + 1, count - 1, &n);
  if (!bytes) return 0;
  ot_value result = rt->stack[first];
  if (!append_buffer(rt, &result, bytes, n)) result = 0;
  free(bytes);
  return result;
}

static ot_value string_lt(ot_state* rt, size_t first, size_t count) {
  if (count < 2) return oti_fail(rt, "string<? needs at least two strings");
  bool result = true;
  for (size_t i = 0; i < count; i++) {
    if (!oti_kind(rt->stack[first + i], K_STRING)) return oti_fail(rt, "expected string");
    if (i) {
      size_t a, b;
      const char *x = oti_text_bytes(rt->stack[first + i - 1], &a),
                 *y = oti_text_bytes(rt->stack[first + i], &b);
      int order = memcmp(x, y, a < b ? a : b);
      result &= order < 0 || (!order && a < b);
    }
  }
  return result ? OT_TRUE : OT_FALSE;
}

/* The single argument of a one-argument builtin, or 0 after recording an error. */
static ot_value string_argument(ot_state* rt, size_t first, size_t count) {
  if (count != 1) return oti_fail(rt, "wrong number of arguments");
  return rt->stack[first];
}

static ot_value string_buffer_string(ot_state* rt, size_t first, size_t count) {
  ot_value value = string_argument(rt, first, count);
  if (!value) return 0;
  if (!oti_kind(value, K_BUFFER)) return oti_fail(rt, "expected buffer");
  size_t n = oti_as_buffer(value)->length;
  return copied(rt, n ? object_bytes(oti_as_buffer(value)->data) : "", n);
}

/* The text of a string, symbol or keyword without a keyword's leading colon. */
static const char* string_name_text(ot_state* rt, ot_value value, size_t* n) {
  if (!oti_kind(value, K_STRING) && !oti_kind(value, K_SYMBOL) && !oti_kind(value, K_KEYWORD)) {
    oti_fail(rt, "expected string, symbol or keyword");
    return NULL;
  }
  const char* bytes = oti_text_bytes(value, n);
  if (oti_kind(value, K_KEYWORD)) {
    bytes++;
    (*n)--;
  }
  return bytes;
}

static ot_value string_name(ot_state* rt, size_t first, size_t count) {
  ot_value value = string_argument(rt, first, count);
  if (!value) return 0;
  size_t n;
  const char* bytes = string_name_text(rt, value, &n);
  return bytes ? copied(rt, bytes, n) : 0;
}

static ot_value string_symbol(ot_state* rt, size_t first, size_t count) {
  ot_value value = string_argument(rt, first, count);
  if (!value) return 0;
  size_t n;
  const char* bytes = string_name_text(rt, value, &n);
  if (!bytes) return 0;
  return oti_kind(value, K_SYMBOL) ? value : oti_intern(rt, K_SYMBOL, bytes, n);
}

static ot_value string_to_list(ot_state* rt, size_t first, size_t count) {
  ot_value value = string_argument(rt, first, count);
  if (!value) return 0;
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
    oti_as_pair(pair)->car = ((ot_value)cp << 8) | TAG_CHAR;
    oti_as_pair(pair)->cdr = OT_EMPTY;
    if (roots[1] == OT_EMPTY) roots[1] = pair;
    else oti_as_pair(roots[2])->cdr = pair;
    roots[2] = pair;
  }
  ot_value result = roots[1];
  oti_roots_pop(&rt->heap, &root);
  return result;
}

static ot_value string_to_number(ot_state* rt, size_t first, size_t count) {
  ot_value value = string_argument(rt, first, count);
  if (!value) return 0;
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

static ot_value string_integer_char(ot_state* rt, size_t first, size_t count) {
  ot_value value = string_argument(rt, first, count);
  if (!value) return 0;
  if (!ot_is_int(value)) return oti_fail(rt, "expected integer code point");
  intptr_t cp = ot_to_int(value);
  if (cp < 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
    return oti_fail(rt, "invalid character code point");
  return ((ot_value)cp << 8) | TAG_CHAR;
}

/* The code point of the single character argument. */
static bool string_char_argument(ot_state* rt, size_t first, size_t count, uint32_t* cp) {
  ot_value value = string_argument(rt, first, count);
  if (!value) return false;
  if ((value & 255) != TAG_CHAR) {
    oti_fail(rt, "expected character");
    return false;
  }
  *cp = (uint32_t)(value >> 8);
  return true;
}

static ot_value string_char_integer(ot_state* rt, size_t first, size_t count) {
  uint32_t cp;
  if (!string_char_argument(rt, first, count, &cp)) return 0;
  return ot_int(cp);
}

static ot_value string_char_upcase(ot_state* rt, size_t first, size_t count) {
  uint32_t cp;
  if (!string_char_argument(rt, first, count, &cp)) return 0;
  if (cp >= 'a' && cp <= 'z') cp -= 32;
  return ((ot_value)cp << 8) | TAG_CHAR;
}

static ot_value string_char_downcase(ot_state* rt, size_t first, size_t count) {
  uint32_t cp;
  if (!string_char_argument(rt, first, count, &cp)) return 0;
  if (cp >= 'A' && cp <= 'Z') cp += 32;
  return ((ot_value)cp << 8) | TAG_CHAR;
}

static ot_value string_char_whitespace(ot_state* rt, size_t first, size_t count) {
  uint32_t cp;
  if (!string_char_argument(rt, first, count, &cp)) return 0;
  return cp == ' ' || (cp >= 9 && cp <= 13) ? OT_TRUE : OT_FALSE;
}

static ot_value string_char_alphabetic(ot_state* rt, size_t first, size_t count) {
  uint32_t cp;
  if (!string_char_argument(rt, first, count, &cp)) return 0;
  return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') ? OT_TRUE : OT_FALSE;
}

static ot_value string_char_numeric(ot_state* rt, size_t first, size_t count) {
  uint32_t cp;
  if (!string_char_argument(rt, first, count, &cp)) return 0;
  return cp >= '0' && cp <= '9' ? OT_TRUE : OT_FALSE;
}

static ot_value string_digit_value(ot_state* rt, size_t first, size_t count) {
  uint32_t cp;
  if (!string_char_argument(rt, first, count, &cp)) return 0;
  return cp >= '0' && cp <= '9' ? ot_int(cp - '0') : OT_NIL;
}

const oti_builtin_def oti_string_builtins[] = {
    {B_STR, "str", string_str},
    {B_STRING_LT, "string<?", string_lt},
    {B_STRING_NUMBER, "string->number", string_to_number},
    {B_BUFFER, "buffer", string_buffer},
    {B_BUFFER_PUSH, "buffer-push!", string_buffer_push},
    {B_BUFFER_STRING, "buffer->string", string_buffer_string},
    {B_CHAR_INTEGER, "char->integer", string_char_integer},
    {B_INTEGER_CHAR, "integer->char", string_integer_char},
    {B_CHAR_UP, "char-upcase", string_char_upcase},
    {B_CHAR_DOWN, "char-downcase", string_char_downcase},
    {B_CHAR_SPACE, "char-whitespace?", string_char_whitespace},
    {B_CHAR_ALPHA, "char-alphabetic?", string_char_alphabetic},
    {B_CHAR_NUMERIC, "char-numeric?", string_char_numeric},
    {B_DIGIT_VALUE, "digit-value", string_digit_value},
    {B_NAME, "name", string_name},
    {B_SYMBOL, "symbol", string_symbol},
    {B_STRING_LIST, "string->list", string_to_list},
    {0, NULL, NULL},
};
