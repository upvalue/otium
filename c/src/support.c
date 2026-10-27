#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>

uint64_t oti_symbol_id(ot_value v) {
  size_t n;
  oti_text_bytes(v, &n);
  uint64_t id;
  memcpy(&id, object_bytes(v) + sizeof(size_t) + n + 1, sizeof(id));
  return id;
}

ot_value oti_intern(ot_state* rt, unsigned kind, const char* bytes, size_t n) {
  for (size_t i = 0; i < rt->constant_count; i++) {
    ot_value v = rt->constants[i];
    if (!oti_kind(v, kind) || (kind == K_SYMBOL && oti_symbol_id(v))) continue;
    size_t length;
    const char* s = oti_text_bytes(v, &length);
    if (length == n && !memcmp(s, bytes, n)) return v;
  }
  /* The source may be in the moving heap. */
  char* copy = malloc(n ? n : 1);
  if (!copy) return oti_fail(rt, "out of host memory");
  memcpy(copy, bytes, n);
  ot_value v = oti_text(rt, kind, copy, n);
  free(copy);
  if (v && oti_constant(rt, v) == UINT32_MAX) return 0;
  return v;
}

static bool proper(ot_value v) {
  ot_value slow = v, fast = v;
  while (oti_kind(fast, K_PAIR)) {
    fast = oti_cdr(fast);
    if (!oti_kind(fast, K_PAIR)) break;
    fast = oti_cdr(fast);
    slow = oti_cdr(slow);
    if (fast == slow) return false;
  }
  return fast == OT_EMPTY;
}

size_t oti_utf8_next(const char* s, size_t n, size_t p, uint32_t* cp) {
  unsigned char b = (unsigned char)s[p++];
  unsigned more = b < 128 ? 0 : b < 224 ? 1 : b < 240 ? 2 : 3;
  *cp = more ? b & ((1u << (6 - more)) - 1) : b;
  while (more-- && p < n) *cp = (*cp << 6) | ((unsigned char)s[p++] & 63);
  return p;
}

static bool support_arity(ot_state* rt, size_t count, size_t arity) {
  if (count == arity) return true;
  return oti_error(rt, "wrong number of arguments");
}

static ot_value support_string_append(ot_state* rt, size_t first, size_t count) {
  size_t total = 0;
  for (size_t i = 0; i < count; i++) {
    if (!oti_kind(rt->stack[first + i], K_STRING)) return oti_fail(rt, "expected string");
    size_t n;
    oti_text_bytes(rt->stack[first + i], &n);
    if (n > SIZE_MAX - total - 1) return oti_fail(rt, "string too large");
    total += n;
  }
  char* joined = malloc(total + 1);
  if (!joined) return oti_fail(rt, "out of host memory");
  size_t offset = 0;
  for (size_t i = 0; i < count; i++) {
    size_t n;
    const char* s = oti_text_bytes(rt->stack[first + i], &n);
    memcpy(joined + offset, s, n);
    offset += n;
  }
  ot_value result = oti_text(rt, K_STRING, joined, total);
  free(joined);
  return result;
}

static ot_value support_number_string(ot_state* rt, size_t first, size_t count) {
  if (!support_arity(rt, count, 1)) return 0;
  ot_value v = rt->stack[first];
  char buffer[128];
  int n;
  if (ot_is_int(v)) n = snprintf(buffer, sizeof(buffer), "%" PRIdPTR, ot_to_int(v));
  else if (oti_kind(v, K_FLOAT)) {
    double real;
    memcpy(&real, object_bytes(v), sizeof(real));
    n = (int)oti_format_float(real, buffer, sizeof(buffer));
  } else return oti_fail(rt, "expected number");
  return oti_text(rt, K_STRING, buffer, (size_t)n);
}

/* pos? and neg? */
static ot_value support_sign_p(ot_state* rt, bool positive, size_t first, size_t count) {
  if (!support_arity(rt, count, 1)) return 0;
  ot_value v = rt->stack[first];
  double number;
  if (ot_is_int(v)) number = (double)ot_to_int(v);
  else if (oti_kind(v, K_FLOAT)) memcpy(&number, object_bytes(v), sizeof(number));
  else return oti_fail(rt, "expected number");
  return ot_bool(positive ? number > 0 : number < 0);
}

bool oti_procedure(ot_value v) {
  return oti_kind(v, K_PARAM) || oti_kind(v, K_MACRO) || oti_kind(v, K_NEXT) ||
         oti_kind(v, K_GENERIC) || oti_kind(v, K_CLOSURE) || oti_kind(v, K_FOREIGN_FUNCTION) ||
         (v & 255) == TAG_BUILTIN;
}

/* One-argument type predicates. */
static ot_value support_type_p(ot_state* rt, unsigned id, size_t first, size_t count) {
  if (!support_arity(rt, count, 1)) return 0;
  ot_value v = rt->stack[first];
  bool result = false;
  switch (id) {
    case B_INT_P: result = ot_is_int(v); break;
    case B_FLOAT_P: result = oti_kind(v, K_FLOAT); break;
    case B_MACRO_P: result = oti_kind(v, K_MACRO); break;
    case B_CHAR_P: result = (v & 255) == TAG_CHAR; break;
    case B_SYMBOL_P: result = oti_kind(v, K_SYMBOL); break;
    case B_KEYWORD_P: result = oti_kind(v, K_KEYWORD); break;
    case B_STRING_P: result = oti_kind(v, K_STRING); break;
    case B_BOOLEAN_P: result = v == OT_TRUE || v == OT_FALSE; break;
    case B_LIST_P: result = proper(v); break;
    case B_PROCEDURE_P: result = oti_procedure(v); break;
  }
  return ot_bool(result);
}

static ot_value support_symbol_p(ot_state* rt, size_t first, size_t count) {
  return support_type_p(rt, B_SYMBOL_P, first, count);
}

static ot_value support_keyword_p(ot_state* rt, size_t first, size_t count) {
  return support_type_p(rt, B_KEYWORD_P, first, count);
}

static ot_value support_string_p(ot_state* rt, size_t first, size_t count) {
  return support_type_p(rt, B_STRING_P, first, count);
}

static ot_value support_boolean_p(ot_state* rt, size_t first, size_t count) {
  return support_type_p(rt, B_BOOLEAN_P, first, count);
}

static ot_value support_list_p(ot_state* rt, size_t first, size_t count) {
  return support_type_p(rt, B_LIST_P, first, count);
}

static ot_value support_procedure_p(ot_state* rt, size_t first, size_t count) {
  return support_type_p(rt, B_PROCEDURE_P, first, count);
}

static ot_value support_int_p(ot_state* rt, size_t first, size_t count) {
  return support_type_p(rt, B_INT_P, first, count);
}

static ot_value support_float_p(ot_state* rt, size_t first, size_t count) {
  return support_type_p(rt, B_FLOAT_P, first, count);
}

static ot_value support_macro_p(ot_state* rt, size_t first, size_t count) {
  return support_type_p(rt, B_MACRO_P, first, count);
}

static ot_value support_char_p(ot_state* rt, size_t first, size_t count) {
  return support_type_p(rt, B_CHAR_P, first, count);
}

static ot_value support_pos_p(ot_state* rt, size_t first, size_t count) {
  return support_sign_p(rt, true, first, count);
}

static ot_value support_neg_p(ot_state* rt, size_t first, size_t count) {
  return support_sign_p(rt, false, first, count);
}

static ot_value support_eval(ot_state* rt, size_t first, size_t count) {
  if (!support_arity(rt, count, 1)) return 0;
  return oti_eval_value(rt, rt->stack[first]);
}

static ot_value support_read_string(ot_state* rt, size_t first, size_t count) {
  if (!support_arity(rt, count, 1)) return 0;
  return oti_read_string(rt, rt->stack[first]);
}

static ot_value support_expander_application(ot_state* rt, size_t first, size_t count) {
  if (!support_arity(rt, count, 1)) return 0;
  return rt->stack[first];
}

static ot_value support_expander_optional_function(ot_state* rt, size_t first, size_t count) {
  if (!support_arity(rt, count, 2)) return 0;
  return rt->stack[first];
}

static ot_value support_expander_native_syntax_p(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!support_arity(rt, count, 1)) return 0;
  return OT_FALSE;
}

/* An unlocated output form becomes a located copy of its first pair, at the
 * input form's offset. The expander only passes the result on, so the copy
 * need not be the same pair. */
static ot_value support_expander_locate(ot_state* rt, size_t first, size_t count) {
  if (!support_arity(rt, count, 2)) return 0;
  size_t offset, ignored;
  if (!oti_pair_offset(rt->stack[first], &offset) || !oti_kind(rt->stack[first + 1], K_PAIR) ||
      oti_pair_offset(rt->stack[first + 1], &ignored))
    return rt->stack[first + 1];
  ot_value pair = oti_alloc(rt, K_PAIR, 2, OTI_LOCATED_PAIR_BYTES);
  if (!pair) return 0;
  ot_value to = rt->stack[first + 1];
  object_fields(pair)[0] = object_fields(to)[0];
  object_fields(pair)[1] = object_fields(to)[1];
  memcpy(object_bytes(pair), &offset, sizeof(offset));
  return pair;
}

static ot_value support_expander_invoke_macro(ot_state* rt, size_t first, size_t count) {
  if (!support_arity(rt, count, 2)) return 0;
  if (rt->expanding && ++rt->expansion_calls > 1024)
    return oti_fail(rt, "macro expansion limit exceeded");
  return oti_call_list(rt, rt->stack[first], rt->stack[first + 1]);
}

static ot_value support_expander_fail(ot_state* rt, size_t first, size_t count) {
  if (!support_arity(rt, count, 3)) return 0;
  ot_value message = rt->stack[first + 1];
  if (!oti_kind(message, K_STRING)) return oti_fail(rt, "expansion error");
  size_t n;
  const char* s = oti_text_bytes(message, &n);
  (void)n;
  ot_value code = rt->stack[first];
  bool fresh = !oti_failed(rt);
  oti_error(rt, s);
  size_t offset;
  if (fresh && oti_pair_offset(rt->stack[first + 2], &offset)) oti_error_at(rt, offset);
  if (fresh && oti_kind(code, K_STRING)) {
    size_t length;
    const char* bytes = oti_text_bytes(code, &length);
    if (length < sizeof(rt->error.code)) {
      memcpy(rt->error.code, bytes, length);
      rt->error.code[length] = 0;
    }
  }
  return 0;
}

static ot_value support_expander_macro_var(ot_state* rt, size_t first, size_t count) {
  if (!support_arity(rt, count, 1)) return 0;
  ot_value v = rt->stack[first];
  if (!oti_kind(v, K_SYMBOL)) return oti_fail(rt, "expected symbol");
  size_t n;
  const char* s = oti_text_bytes(v, &n);
  uint32_t index = oti_lookup_global(rt, s, n);
  return index != UINT32_MAX && rt->global_info[index].macro ? rt->globals[index] : OT_FALSE;
}

static ot_value support_expander_local_name(ot_state* rt, size_t first, size_t count) {
  if (!support_arity(rt, count, 1)) return 0;
  ot_value v = rt->stack[first];
  if (!oti_kind(v, K_SYMBOL)) return oti_fail(rt, "expected symbol");
  size_t n;
  const char* s = oti_text_bytes(v, &n);
  uint32_t index = oti_lookup_global(rt, s, n);
  if (oti_symbol_id(v)) return OT_FALSE;
  const char* slash = memchr(s, '/', n);
  if (!slash) return OT_FALSE;
  const char* bare = slash + 1;
  size_t length = n - (size_t)(bare - s);
  uint32_t local = oti_lookup_global(rt, bare, length);
  return index != UINT32_MAX && index == local ? oti_intern(rt, K_SYMBOL, bare, length) : OT_FALSE;
}

static ot_value support_expander_qualify(ot_state* rt, size_t first, size_t count) {
  if (!support_arity(rt, count, 1)) return 0;
  ot_value v = rt->stack[first];
  if (!oti_kind(v, K_SYMBOL)) return oti_fail(rt, "expected symbol");
  size_t n;
  const char* s = oti_text_bytes(v, &n);
  uint32_t index = oti_lookup_global(rt, s, n);
  if (oti_symbol_id(v)) return OT_FALSE;
  uint32_t ns = index == UINT32_MAX ? rt->current_namespace : rt->global_info[index].space;
  if (index != UINT32_MAX) {
    s = rt->global_info[index].name;
    n = strlen(s);
  } else if (memchr(s, '/', n)) return v;
  const char* space = rt->namespaces[ns];
  size_t prefix = strlen(space);
  char* joined = malloc(prefix + n + 2);
  if (!joined) return oti_fail(rt, "out of host memory");
  memcpy(joined, space, prefix);
  joined[prefix] = '/';
  memcpy(joined + prefix + 1, s, n);
  ot_value result = oti_intern(rt, K_SYMBOL, joined, prefix + n + 1);
  free(joined);
  return result;
}

static ot_value support_gensym(ot_state* rt, size_t first, size_t count) {
  if (count > 1) return oti_fail(rt, "gensym expects at most one argument");
  ot_value v = count ? rt->stack[first] : OT_NIL;
  if (count && !oti_kind(v, K_STRING)) return oti_fail(rt, "gensym expects a string prefix");
  size_t n = 1;
  const char* prefix = count ? oti_text_bytes(v, &n) : "g";
  uint64_t serial = ++rt->gensym_counter;
  if (!serial) return oti_fail(rt, "gensym counter exhausted");
  char* name = malloc(n + 32);
  if (!name) return oti_fail(rt, "out of host memory");
  memcpy(name, prefix, n);
  int written = snprintf(name + n, 32, "#%" PRIu64, serial);
  ot_value result = oti_text(rt, K_SYMBOL, name, n + (size_t)written);
  free(name);
  if (result)
    memcpy(object_bytes(result) + sizeof(size_t) + n + (size_t)written + 1, &serial,
           sizeof(serial));
  return result;
}

static ot_value support_string_symbol(ot_state* rt, size_t first, size_t count) {
  if (!support_arity(rt, count, 1)) return 0;
  ot_value v = rt->stack[first];
  if (!oti_kind(v, K_STRING)) return oti_fail(rt, "expected string or symbol");
  size_t n;
  const char* s = oti_text_bytes(v, &n);
  return oti_intern(rt, K_SYMBOL, s, n);
}

static ot_value support_keyword(ot_state* rt, size_t first, size_t count) {
  if (!support_arity(rt, count, 1)) return 0;
  ot_value v = rt->stack[first];
  if (oti_kind(v, K_KEYWORD)) return v;
  if (!oti_kind(v, K_STRING) && !oti_kind(v, K_SYMBOL))
    return oti_fail(rt, "expected string or symbol");
  size_t n;
  const char* s = oti_text_bytes(v, &n);
  char* name = malloc(n + 1);
  if (!name) return oti_fail(rt, "out of host memory");
  name[0] = ':';
  memcpy(name + 1, s, n);
  ot_value result = oti_intern(rt, K_KEYWORD, name, n + 1);
  free(name);
  return result;
}

static ot_value support_symbol_string(ot_state* rt, size_t first, size_t count) {
  if (!support_arity(rt, count, 1)) return 0;
  ot_value v = rt->stack[first];
  if (!oti_kind(v, K_SYMBOL)) return oti_fail(rt, "expected symbol");
  size_t n;
  const char* s = oti_text_bytes(v, &n);
  char* copy = malloc(n ? n : 1);
  if (!copy) return oti_fail(rt, "out of host memory");
  memcpy(copy, s, n);
  ot_value result = oti_text(rt, K_STRING, copy, n);
  free(copy);
  return result;
}

ot_value oti_string_length_builtin(ot_state* rt, size_t first, size_t count) {
  if (!support_arity(rt, count, 1)) return 0;
  ot_value v = rt->stack[first];
  if (!oti_kind(v, K_STRING)) return oti_fail(rt, "expected string");
  size_t n, length = 0;
  const char* s = oti_text_bytes(v, &n);
  uint32_t cp;
  for (size_t pos = 0; pos < n; length++) pos = oti_utf8_next(s, n, pos, &cp);
  return ot_int((intptr_t)length);
}

static ot_value support_string_ref(ot_state* rt, size_t first, size_t count) {
  if (!support_arity(rt, count, 2)) return 0;
  ot_value v = rt->stack[first];
  if (!oti_kind(v, K_STRING)) return oti_fail(rt, "expected string");
  size_t n;
  const char* s = oti_text_bytes(v, &n);
  if (!ot_is_int(rt->stack[first + 1])) return oti_fail(rt, "expected string index");
  intptr_t index = ot_to_int(rt->stack[first + 1]);
  if (index < 0) return oti_fail(rt, "string index out of bounds");
  size_t length = 0;
  uint32_t cp;
  for (size_t pos = 0; pos < n; length++) {
    pos = oti_utf8_next(s, n, pos, &cp);
    if (length == (size_t)index) return ((ot_value)cp << 8) | TAG_CHAR;
  }
  return oti_fail(rt, "string index out of bounds");
}

static ot_value support_substring(ot_state* rt, size_t first, size_t count) {
  if (count < 2 || count > 3) return oti_fail(rt, "substring expects two or three arguments");
  ot_value v = rt->stack[first];
  if (!oti_kind(v, K_STRING)) return oti_fail(rt, "expected string");
  size_t n;
  const char* s = oti_text_bytes(v, &n);
  if (!ot_is_int(rt->stack[first + 1])) return oti_fail(rt, "expected string index");
  intptr_t start = ot_to_int(rt->stack[first + 1]);
  if (count == 3 && !ot_is_int(rt->stack[first + 2])) return oti_fail(rt, "expected string index");
  intptr_t end = count == 3 ? ot_to_int(rt->stack[first + 2]) : INTPTR_MAX;
  if (start < 0) start = 0;
  if (end < start) end = start;
  size_t length = 0, a = n, b = n;
  uint32_t cp;
  for (size_t pos = 0; pos < n; length++) {
    size_t before = pos;
    pos = oti_utf8_next(s, n, pos, &cp);
    if (length == (size_t)start) a = before;
    if (length == (size_t)end) b = before;
  }
  if ((size_t)start == length) a = n;
  if ((size_t)end == length) b = n;
  char* copy = malloc(b - a + 1);
  if (!copy) return oti_fail(rt, "out of host memory");
  memcpy(copy, s + a, b - a);
  ot_value result = oti_text(rt, K_STRING, copy, b - a);
  free(copy);
  return result;
}

const oti_builtin_def oti_support_builtins[] = {
    {B_SYMBOL_P, "symbol?", support_symbol_p},
    {B_KEYWORD_P, "keyword?", support_keyword_p},
    {B_STRING_P, "string?", support_string_p},
    {B_BOOLEAN_P, "boolean?", support_boolean_p},
    {B_LIST_P, "list?", support_list_p},
    {B_PROCEDURE_P, "procedure?", support_procedure_p},
    {B_INT_P, "int?", support_int_p},
    {B_FLOAT_P, "float?", support_float_p},
    {B_MACRO_P, "macro?", support_macro_p},
    {B_CHAR_P, "char?", support_char_p},
    {B_SYMBOL_STRING, "symbol->string", support_symbol_string},
    {B_STRING_SYMBOL, "string->symbol", support_string_symbol},
    {B_KEYWORD, "keyword", support_keyword},
    {B_GENSYM, "gensym", support_gensym},
    {B_STRING_LENGTH, "string-length", oti_string_length_builtin},
    {B_STRING_REF, "string-ref", support_string_ref},
    {B_SUBSTRING, "substring", support_substring},
    {B_EXP_APPLICATION, "expander-application", support_expander_application},
    {B_EXP_OPTIONAL, "expander-optional-function", support_expander_optional_function},
    {B_EXP_MACRO, "expander-macro-var", support_expander_macro_var},
    {B_EXP_INVOKE, "expander-invoke-macro", support_expander_invoke_macro},
    {B_EXP_NATIVE, "expander-native-syntax?", support_expander_native_syntax_p},
    {B_EXP_LOCATE, "expander-locate", support_expander_locate},
    {B_EXP_FAIL, "expander-fail", support_expander_fail},
    {B_EXP_QUALIFY, "expander-qualify", support_expander_qualify},
    {B_EXP_LOCAL, "expander-local-name", support_expander_local_name},
    {B_STRING_APPEND, "string-append", support_string_append},
    {B_NUMBER_STRING, "number->string", support_number_string},
    {B_POS, "pos?", support_pos_p},
    {B_NEG, "neg?", support_neg_p},
    {B_READ_STRING, "read-string", support_read_string},
    {B_EVAL, "eval", support_eval},
    {0, NULL, NULL},
};
