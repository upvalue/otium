#include "internal.h"
#include <stdlib.h>
#include <string.h>

#include "runtime-reference.inc"

static const char* type_name(ot_value value) {
  if (value == OT_NIL) return ":nil";
  if (value == OT_EMPTY) return ":null";
  if (value == OT_TRUE || value == OT_FALSE) return ":boolean";
  if (value == OT_EOF) return ":eof";
  if (ot_is_int(value)) return ":int";
  if ((value & 255) == TAG_CHAR) return ":char";
  if ((value & 255) == TAG_BUILTIN) return ":function";
  if (value_is_ptr(value)) switch (object_kind(value)) {
      case K_FLOAT: return ":float";
      case K_STRING: return ":string";
      case K_SYMBOL: return ":symbol";
      case K_KEYWORD: return ":keyword";
      case K_PAIR: return ":pair";
      case K_ARRAY: return ":array";
      case K_TABLE: return ":table";
      case K_BYTEVECTOR: return ":bytevector";
      case K_BUFFER: return ":buffer";
      case K_MACRO: return ":macro";
      case K_RECORD: return ":record";
      case K_RECORD_TYPE: return ":record-type";
      case K_INSTANCE: return ":instance";
      case K_PORT: return ":port";
      case K_RESTART: return ":restart";
      case K_PARAM: return ":param";
      case K_NEXT:
      case K_GENERIC:
      case K_CLOSURE:
      case K_FOREIGN_FUNCTION:
      case K_NATIVE: return ":function";
      case K_FOREIGN_LAYOUT: return ":foreign-layout";
      case K_POINTER: return ":pointer";
      case K_THREAD: return ":thread";
      case K_WAITQ: return ":wait-queue";
      default: break;
    }
  return ":native";
}

static ot_value reflection_eof_object(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!oti_check_arity(rt, "eof-object", count, 0, 0)) return 0;
  return OT_EOF;
}

static ot_value reflection_eof_object_p(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "eof-object?", count, 1, 1)) return 0;
  return rt->stack[first] == OT_EOF ? OT_TRUE : OT_FALSE;
}

static ot_value reflection_buffer_p(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "buffer?", count, 1, 1)) return 0;
  return oti_kind(rt->stack[first], K_BUFFER) ? OT_TRUE : OT_FALSE;
}

static ot_value reflection_type(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "type", count, 1, 1)) return 0;
  const char* name = type_name(rt->stack[first]);
  return oti_intern(rt, K_KEYWORD, name, strlen(name));
}

static ot_value reflection_current_ns(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!oti_check_arity(rt, "current-ns", count, 0, 0)) return 0;
  const char* name = rt->namespaces[rt->current_namespace];
  return oti_intern(rt, K_SYMBOL, name, strlen(name));
}

static ot_value reflection_describe(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "describe", count, 1, 1)) return 0;
  ot_value value = rt->stack[first];
  if (!oti_kind(value, K_SYMBOL)) {
    oti_error(rt, "describe expects a symbol");
    return 0;
  }
  size_t n;
  const char* name = oti_text_bytes(value, &n);
  uint32_t index = oti_lookup_global(rt, name, n);
  /* Special forms and builtins are documented in lib/catalog/runtime.json. */
  char key[64];
  const char* reference = "";
  if (n < sizeof(key)) {
    memcpy(key, name, n);
    key[n] = 0;
    reference = oti_runtime_documentation(key);
  }
  if (index == UINT32_MAX || oti_unbound(oti_global_value(rt, index)))
    return *reference ? oti_text(rt, K_STRING, reference, strlen(reference)) : OT_NIL;
  const char* space = rt->namespaces[rt->global_info[index].space];
  const char* local = rt->global_info[index].name;
  const char* doc = rt->global_info[index].doc.text;
  size_t length = rt->global_info[index].doc.length;
  if (!doc) {
    doc = *reference ? reference : "no documentation";
    length = strlen(doc);
  }
  size_t prefix = strlen(space) + strlen(local) + 3;
  if (length > SIZE_MAX - prefix - 1) {
    oti_error(rt, "documentation too large");
    return 0;
  }
  char* text = malloc(prefix + length + 1);
  if (!text) {
    oti_error(rt, "out of host memory");
    return 0;
  }
  snprintf(text, prefix + 1, "%s/%s: ", space, local);
  memcpy(text + prefix, doc, length);
  ot_value result = oti_text(rt, K_STRING, text, prefix + length);
  free(text);
  return result;
}

const oti_builtin_def oti_reflection_builtins[] = {
    {B_TYPE, "type", reflection_type},
    {B_EOF, "eof-object", reflection_eof_object},
    {B_EOF_P, "eof-object?", reflection_eof_object_p},
    {B_BUFFER_P, "buffer?", reflection_buffer_p},
    {B_CURRENT_NS, "current-ns", reflection_current_ns},
    {B_DESCRIBE, "describe", reflection_describe},
    {0, NULL, NULL},
};
