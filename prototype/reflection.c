#include "internal.h"
#include <stdlib.h>
#include <string.h>

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
      case K_FOREIGN_FUNCTION: return ":function";
      case K_FOREIGN_LAYOUT: return ":foreign-layout";
      case K_POINTER: return ":pointer";
      default: break;
    }
  return ":native";
}

ot_value oti_reflection_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count) {
  size_t arity = id == B_EOF || id == B_CURRENT_NS ? 0 : 1;
  if (count != arity) {
    oti_error(rt, "wrong number of arguments");
    return 0;
  }
  ot_value value = count ? rt->stack[first] : OT_NIL;
  switch (id) {
    case B_EOF: return OT_EOF;
    case B_EOF_P: return value == OT_EOF ? OT_TRUE : OT_FALSE;
    case B_BUFFER_P: return oti_kind(value, K_BUFFER) ? OT_TRUE : OT_FALSE;
    case B_TYPE: {
      const char* name = type_name(value);
      return oti_intern(rt, K_KEYWORD, name, strlen(name));
    }
    case B_CURRENT_NS: {
      const char* name = rt->namespaces[rt->current_namespace];
      return oti_intern(rt, K_SYMBOL, name, strlen(name));
    }
    case B_DESCRIBE: {
      if (!oti_kind(value, K_SYMBOL)) {
        oti_error(rt, "describe expects a symbol");
        return 0;
      }
      size_t n;
      memcpy(&n, object_bytes(value), sizeof(n));
      const char* name = (char*)object_bytes(value) + sizeof(n);
      uint32_t index = oti_lookup_global(rt, name, n);
      if (index == UINT32_MAX || rt->globals[index] == V_UNBOUND) {
        if (n == 2 && !memcmp(name, "ns", 2)) {
          const char* doc = "(ns name (:require ...)) Set the current namespace.";
          return oti_text(rt, K_STRING, doc, strlen(doc));
        }
        return OT_NIL;
      }
      const char* space = rt->namespaces[rt->global_namespaces[index]];
      const char* local = rt->names[index];
      oti_documentation doc = rt->docs[index];
      if (!doc.text) {
        doc.text = "no documentation";
        doc.length = strlen(doc.text);
      }
      size_t prefix = strlen(space) + strlen(local) + 3;
      if (doc.length > SIZE_MAX - prefix - 1) {
        oti_error(rt, "documentation too large");
        return 0;
      }
      char* text = malloc(prefix + doc.length + 1);
      if (!text) {
        oti_error(rt, "out of host memory");
        return 0;
      }
      snprintf(text, prefix + 1, "%s/%s: ", space, local);
      memcpy(text + prefix, doc.text, doc.length);
      ot_value result = oti_text(rt, K_STRING, text, prefix + doc.length);
      free(text);
      return result;
    }
    default: oti_error(rt, "unknown reflection builtin"); return 0;
  }
}
