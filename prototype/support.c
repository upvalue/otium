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

ot_value oti_intern(ot_runtime* rt, unsigned kind, const char* bytes, size_t n) {
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
    fast = object_fields(fast)[1];
    if (!oti_kind(fast, K_PAIR)) break;
    fast = object_fields(fast)[1];
    slow = object_fields(slow)[1];
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

ot_value oti_support_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count) {
  size_t arity =
      id == B_NEWLINE                                                                          ? 0
      : id == B_SUBSTRING || id == B_EXP_FAIL                                                  ? 3
      : id == B_STRING_REF || id == B_EXP_OPTIONAL || id == B_EXP_INVOKE || id == B_EXP_LOCATE ? 2
                                                                                               : 1;
  if (id == B_GENSYM) {
    if (count > 1) return oti_fail(rt, "gensym expects at most one argument");
  } else if (id == B_SUBSTRING) {
    if (count < 2 || count > 3) return oti_fail(rt, "substring expects two or three arguments");
  } else if (id != B_STRING_APPEND && id != B_DISPLAY && id != B_WRITE && count != arity)
    return oti_fail(rt, "wrong number of arguments");
  ot_value v = count ? rt->stack[first] : OT_NIL;
  bool predicate;
  switch (id) {
    case B_STRING_APPEND: {
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
    case B_NUMBER_STRING: {
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
    case B_POS:
    case B_NEG: {
      double number;
      if (ot_is_int(v)) number = (double)ot_to_int(v);
      else if (oti_kind(v, K_FLOAT)) memcpy(&number, object_bytes(v), sizeof(number));
      else return oti_fail(rt, "expected number");
      predicate = id == B_POS ? number > 0 : number < 0;
      break;
    }
    case B_EVAL: return oti_eval_value(rt, v);
    case B_READ_STRING: return oti_read_string(rt, v);
    case B_INT_P: predicate = ot_is_int(v); break;
    case B_FLOAT_P: predicate = oti_kind(v, K_FLOAT); break;
    case B_MACRO_P: predicate = oti_kind(v, K_MACRO); break;
    case B_CHAR_P: predicate = (v & 255) == TAG_CHAR; break;
    case B_SYMBOL_P: predicate = oti_kind(v, K_SYMBOL); break;
    case B_KEYWORD_P: predicate = oti_kind(v, K_KEYWORD); break;
    case B_STRING_P: predicate = oti_kind(v, K_STRING); break;
    case B_BOOLEAN_P: predicate = v == OT_TRUE || v == OT_FALSE; break;
    case B_LIST_P: predicate = proper(v); break;
    case B_PROCEDURE_P:
      predicate = oti_kind(v, K_PARAM) || oti_kind(v, K_MACRO) || oti_kind(v, K_NEXT) ||
                  oti_kind(v, K_GENERIC) || oti_kind(v, K_CLOSURE) ||
                  oti_kind(v, K_FOREIGN_FUNCTION) || (v & 255) == TAG_BUILTIN;
      break;
    case B_EXP_APPLICATION:
    case B_EXP_OPTIONAL: return v;
    case B_EXP_NATIVE: return OT_FALSE;
    case B_EXP_LOCATE: return rt->stack[first + 1];
    case B_EXP_INVOKE:
      if (rt->expanding && ++rt->expansion_calls > 1024)
        return oti_fail(rt, "macro expansion limit exceeded");
      return oti_call_list(rt, v, rt->stack[first + 1]);
    case B_EXP_FAIL: {
      ot_value message = rt->stack[first + 1];
      if (!oti_kind(message, K_STRING)) return oti_fail(rt, "expansion error");
      size_t n;
      const char* s = oti_text_bytes(message, &n);
      (void)n;
      ot_value code = rt->stack[first];
      oti_error(rt, s);
      if (oti_kind(code, K_STRING)) {
        size_t length;
        const char* bytes = oti_text_bytes(code, &length);
        if (length < sizeof(rt->expansion_error_code)) {
          memcpy(rt->expansion_error_code, bytes, length);
          rt->expansion_error_code[length] = 0;
          rt->error_code = rt->expansion_error_code;
        }
      }
      return 0;
    }
    case B_EXP_MACRO:
    case B_EXP_QUALIFY:
    case B_EXP_LOCAL: {
      if (!oti_kind(v, K_SYMBOL)) return oti_fail(rt, "expected symbol");
      size_t n;
      const char* s = oti_text_bytes(v, &n);
      uint32_t index = oti_lookup_global(rt, s, n);
      if (id == B_EXP_MACRO)
        return index != UINT32_MAX && rt->global_macro[index] ? rt->globals[index] : OT_FALSE;
      if (oti_symbol_id(v)) return OT_FALSE;
      if (id == B_EXP_LOCAL) {
        const char* slash = memchr(s, '/', n);
        if (!slash) return OT_FALSE;
        const char* bare = slash + 1;
        size_t length = n - (size_t)(bare - s);
        uint32_t local = oti_lookup_global(rt, bare, length);
        return index != UINT32_MAX && index == local ? oti_intern(rt, K_SYMBOL, bare, length)
                                                     : OT_FALSE;
      }
      uint32_t ns = index == UINT32_MAX ? rt->current_namespace : rt->global_namespaces[index];
      if (index != UINT32_MAX) {
        s = rt->names[index];
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
    case B_GENSYM: {
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
    case B_STRING_SYMBOL:
    case B_KEYWORD: {
      if (id == B_KEYWORD && oti_kind(v, K_KEYWORD)) return v;
      if (!oti_kind(v, K_STRING) && !(id == B_KEYWORD && oti_kind(v, K_SYMBOL)))
        return oti_fail(rt, "expected string or symbol");
      size_t n;
      const char* s = oti_text_bytes(v, &n);
      if (id == B_STRING_SYMBOL) return oti_intern(rt, K_SYMBOL, s, n);
      char* name = malloc(n + 1);
      if (!name) return oti_fail(rt, "out of host memory");
      name[0] = ':';
      memcpy(name + 1, s, n);
      ot_value result = oti_intern(rt, K_KEYWORD, name, n + 1);
      free(name);
      return result;
    }
    case B_SYMBOL_STRING: {
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
    case B_STRING_LENGTH:
    case B_STRING_REF:
    case B_SUBSTRING: {
      if (!oti_kind(v, K_STRING)) return oti_fail(rt, "expected string");
      size_t n;
      const char* s = oti_text_bytes(v, &n);
      intptr_t start = 0, end = 0;
      if (id != B_STRING_LENGTH) {
        if (!ot_is_int(rt->stack[first + 1])) return oti_fail(rt, "expected string index");
        start = ot_to_int(rt->stack[first + 1]);
        end = start;
        if (id == B_SUBSTRING) {
          if (count == 3 && !ot_is_int(rt->stack[first + 2]))
            return oti_fail(rt, "expected string index");
          end = count == 3 ? ot_to_int(rt->stack[first + 2]) : INTPTR_MAX;
          if (start < 0) start = 0;
          if (end < start) end = start;
        }
        if (start < 0 || end < start) return oti_fail(rt, "string index out of bounds");
      }
      size_t length = 0, a = n, b = n;
      uint32_t cp;
      for (size_t pos = 0; pos < n; length++) {
        size_t before = pos;
        pos = oti_utf8_next(s, n, pos, &cp);
        if (length == (size_t)start) {
          a = before;
          if (id == B_STRING_REF) return ((ot_value)cp << 8) | TAG_CHAR;
        }
        if (length == (size_t)end) b = before;
      }
      if (id == B_STRING_LENGTH) return ot_int((intptr_t)length);
      if (id == B_STRING_REF) return oti_fail(rt, "string index out of bounds");
      if ((size_t)start == length) a = n;
      if ((size_t)end == length) b = n;
      char* copy = malloc(b - a + 1);
      if (!copy) return oti_fail(rt, "out of host memory");
      memcpy(copy, s + a, b - a);
      ot_value result = oti_text(rt, K_STRING, copy, b - a);
      free(copy);
      return result;
    }
    default: return oti_fail(rt, "unknown support builtin");
  }
  return predicate ? OT_TRUE : OT_FALSE;
}
