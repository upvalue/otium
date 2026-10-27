#include "internal.h"
#include <math.h>
#include <string.h>

/* Every builtin gets the id, the first argument's stack slot and the count. */
typedef ot_value builtin_handler(ot_state* rt, unsigned id, size_t first, size_t count);

typedef struct {
  const char* name;
  builtin_handler* run;
} oti_builtin_entry;

static const oti_builtin_entry builtin_table[B_COUNT];

int oti_builtin_index(const char* name) {
  for (size_t i = 0; i < B_QQ_APPEND; i++)
    if (!strcmp(name, builtin_table[i].name)) return (int)i;
  return -1;
}

bool oti_builtins(ot_state* rt) {
  uint32_t ffi = oti_namespace(rt, "ffi", 3);
  uint32_t process = oti_namespace(rt, "otium.process", 13);
  uint32_t reader = oti_namespace(rt, "otium.reader", 12);
  if (ffi == UINT32_MAX || process == UINT32_MAX || reader == UINT32_MAX) return false;
  for (size_t i = 0; i < B_QQ_APPEND; i++) {
    const char* full = builtin_table[i].name;
    bool foreign = !strncmp(full, "ffi/", 4);
    bool process_binding = !strncmp(full, "otium.process/", 14);
    bool reader_binding = !strncmp(full, "otium.reader/", 13);
    const char* name = full + (foreign ? 4 : process_binding ? 14 : reader_binding ? 13 : 0);
    rt->current_namespace = foreign ? ffi : process_binding ? process : reader_binding ? reader : 0;
    uint32_t global = oti_global(rt, name, strlen(name));
    if (global == UINT32_MAX) return false;
    rt->globals[global] = ((ot_value)i << 8) | TAG_BUILTIN;
    rt->builtin_cells[i] = global;
  }
  rt->current_namespace = 0;
  return true;
}

bool oti_number(ot_state* rt, ot_value v, double* out) {
  if (ot_is_int(v)) {
    *out = (double)ot_to_int(v);
    return true;
  }
  if (oti_kind(v, K_FLOAT)) {
    memcpy(out, object_bytes(v), sizeof(*out));
    return true;
  }
  return oti_error(rt, "expected a number");
}

static ot_value arithmetic(ot_state* rt, unsigned id, size_t first, size_t count) {
  if (!count && (id == B_SUB || id == B_DIV)) {
    oti_error(rt, "arithmetic needs an argument");
    return 0;
  }
  intptr_t integer = id == B_MUL || id == B_DIV ? 1 : 0;
  double real = 0;
  bool exact = !(id == B_DIV && count == 1);
  for (size_t j = 0; j < count; j++) {
    if (!ot_is_int(rt->stack[first + j])) {
      double ignored;
      if (!oti_number(rt, rt->stack[first + j], &ignored)) return 0;
      exact = false;
    }
  }
  if (!exact) real = (double)integer;
  size_t i = 0;
  if ((id == B_SUB || id == B_DIV) && count > 1) {
    ot_value v = rt->stack[first];
    if (exact) integer = ot_to_int(v);
    else if (!oti_number(rt, v, &real)) return 0;
    i = 1;
  }
  for (; i < count; i++) {
    ot_value v = rt->stack[first + i];
    if (exact && ot_is_int(v)) {
      intptr_t operand = ot_to_int(v), next = 0;
      uintptr_t bits = 0;
      switch (id) {
        case B_ADD: bits = (uintptr_t)integer + (uintptr_t)operand; break;
        case B_SUB: bits = (uintptr_t)integer - (uintptr_t)operand; break;
        case B_MUL: bits = (uintptr_t)integer * (uintptr_t)operand; break;
        case B_DIV:
          if (!operand) {
            oti_error(rt, "division by zero");
            return 0;
          }
          if (integer % operand) {
            exact = false;
            real = (double)integer / (double)operand;
            continue;
          }
          bits = (uintptr_t)(integer / operand);
          break;
      }
      next = ot_to_int((bits << 1) | 1);
      integer = next;
    } else {
      double operand;
      if (!oti_number(rt, v, &operand)) return 0;
      if (exact) {
        exact = false;
        real = (double)integer;
      }
      switch (id) {
        case B_ADD: real += operand; break;
        case B_SUB: real -= operand; break;
        case B_MUL: real *= operand; break;
        case B_DIV:
          if (operand == 0) {
            oti_error(rt, "division by zero");
            return 0;
          }
          real /= operand;
          break;
      }
    }
  }
  return exact ? ot_int(integer) : oti_float(rt, real);
}

/* Compare integers without rounding them through double, including on hosts
 * where long double has no more precision than double. Two means unordered. */
int oti_integer_real(intptr_t integer, double real) {
  if (isnan(real)) return 2;
  if (real < (double)(INTPTR_MIN / 2)) return 1;
  if (real >= -(double)(INTPTR_MIN / 2)) return -1;
  intptr_t truncated = (intptr_t)real;
  if (integer < truncated) return -1;
  if (integer > truncated) return 1;
  return (double)truncated < real ? -1 : (double)truncated > real ? 1 : 0;
}

static ot_value compare(ot_state* rt, unsigned id, size_t first, size_t count) {
  if (count < 2) {
    oti_error(rt, "comparison needs at least two arguments");
    return 0;
  }
  bool result = true;
  ot_value previous = OT_NIL;
  for (size_t i = 0; i < count; i++) {
    ot_value v = rt->stack[first + i];
    double current = 0;
    if (!ot_is_int(v) && !oti_number(rt, v, &current)) return 0;
    if (i) {
      int order;
      if (ot_is_int(previous) && ot_is_int(v)) {
        intptr_t left = ot_to_int(previous), right = ot_to_int(v);
        order = left < right ? -1 : left > right ? 1 : 0;
      } else if (ot_is_int(previous)) order = oti_integer_real(ot_to_int(previous), current);
      else {
        double left = 0;
        if (!oti_number(rt, previous, &left)) return 0;
        if (ot_is_int(v)) {
          order = oti_integer_real(ot_to_int(v), left);
          if (order != 2) order = -order;
        } else
          order = isnan(left) || isnan(current) ? 2 : left < current ? -1 : left > current ? 1 : 0;
      }
      switch (id) {
        case B_LT: result &= order == -1; break;
        case B_LE: result &= order == -1 || order == 0; break;
        case B_GT: result &= order == 1; break;
        case B_GE: result &= order == 1 || order == 0; break;
        case B_NUMEQ: result &= order == 0; break;
      }
    }
    previous = v;
  }
  return ot_bool(result);
}

/* Quasiquote copies only the splice prefix; its final tail can be any value. */
static ot_value quasi_append(ot_state* rt, size_t first, size_t count) {
  if (count != 2) {
    oti_error(rt, "invalid quasiquote splice arity");
    return 0;
  }
  ot_value slow = rt->stack[first], fast = slow;
  while (oti_kind(fast, K_PAIR)) {
    fast = object_fields(fast)[1];
    if (!oti_kind(fast, K_PAIR)) break;
    fast = object_fields(fast)[1];
    slow = object_fields(slow)[1];
    if (fast == slow) {
      oti_error(rt, "cyclic quasiquote splice");
      return 0;
    }
  }
  if (fast != OT_EMPTY) {
    oti_error(rt, "quasiquote splice needs a proper list");
    return 0;
  }
  ot_value slots[] = {rt->stack[first], rt->stack[first + 1], OT_EMPTY, OT_EMPTY};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, slots, 4);
  while (slots[0] != OT_EMPTY) {
    ot_value pair = oti_alloc(rt, K_PAIR, 2, 0);
    if (!pair) {
      slots[2] = 0;
      break;
    }
    object_fields(pair)[0] = object_fields(slots[0])[0];
    object_fields(pair)[1] = slots[1];
    if (slots[2] == OT_EMPTY) slots[2] = pair;
    else object_fields(slots[3])[1] = pair;
    slots[3] = pair;
    slots[0] = object_fields(slots[0])[1];
  }
  ot_value result = slots[2] == OT_EMPTY ? slots[1] : slots[2];
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

static ot_value eval_in_namespace(ot_state* rt, unsigned id, size_t first, size_t count) {
  (void)id;
  if (count != 2) {
    oti_error(rt, "invalid evaluator call");
    return 0;
  }
  uint32_t saved = rt->current_namespace;
  rt->current_namespace = (uint32_t)ot_to_int(rt->stack[first]);
  ot_value result = oti_eval_value(rt, rt->stack[first + 1]);
  rt->current_namespace = saved;
  return result;
}

static ot_value exit_builtin(ot_state* rt, unsigned id, size_t first, size_t count) {
  (void)id;
  if (count > 1 || (count && (!ot_is_int(rt->stack[first]) || ot_to_int(rt->stack[first]) < 0 ||
                              ot_to_int(rt->stack[first]) > 255))) {
    oti_error(rt, "exit status must be an integer from 0 to 255");
    return 0;
  }
  rt->exit_status = count ? (int)ot_to_int(rt->stack[first]) : 0;
  rt->exit_pending = true;
  oti_error(rt, "exit requested");
  return 0;
}

static ot_value list_builtin(ot_state* rt, unsigned id, size_t first, size_t count) {
  (void)id;
  ot_value result = OT_EMPTY;
  oti_roots root;
  oti_roots_push(&rt->heap, &root, &result, 1);
  for (size_t i = count; i > 0; i--) {
    ot_value pair = oti_alloc(rt, K_PAIR, 2, 0);
    if (!pair) {
      result = 0;
      break;
    }
    object_fields(pair)[0] = rt->stack[first + i - 1];
    object_fields(pair)[1] = result;
    result = pair;
  }
  oti_roots_pop(&rt->heap, &root);
  return result;
}

static ot_value error_builtin(ot_state* rt, unsigned id, size_t first, size_t count) {
  (void)id;
  return oti_raise(rt, first, count);
}

static ot_value signal_builtin(ot_state* rt, unsigned id, size_t first, size_t count) {
  (void)id;
  return oti_signal(rt, first, count);
}

static ot_value tokens(ot_state* rt, unsigned id, size_t first, size_t count) {
  (void)id;
  return oti_tokens(rt, first, count);
}

static ot_value time_builtin(ot_state* rt, unsigned id, size_t first, size_t count) {
  (void)first;
  return oti_time_builtin(rt, id, count);
}

static ot_value qq_append_builtin(ot_state* rt, unsigned id, size_t first, size_t count) {
  (void)id;
  return quasi_append(rt, first, count);
}

/* Fixed-arity list, equality and predicate primitives. */
static ot_value core_builtin(ot_state* rt, unsigned id, size_t first, size_t count) {
  size_t arity = id == B_REMAINDER || id == B_MODULO || id == B_EQ || id == B_CONS ||
                         id == B_SET_CAR || id == B_SET_CDR
                     ? 2
                     : 1;
  if (count != arity) {
    oti_error(rt, "wrong number of builtin arguments");
    return 0;
  }
  ot_value a = rt->stack[first], b = arity == 2 ? rt->stack[first + 1] : OT_NIL;
  switch (id) {
    case B_REMAINDER:
    case B_MODULO: {
      if (!ot_is_int(a) || !ot_is_int(b)) {
        oti_error(rt, "expected integers");
        return 0;
      }
      intptr_t divisor = ot_to_int(b);
      if (!divisor) {
        oti_error(rt, "division by zero");
        return 0;
      }
      intptr_t r = ot_to_int(a) % divisor;
      if (id == B_MODULO && r && ((r < 0) != (divisor < 0))) r += divisor;
      return ot_int(r);
    }
    case B_EQ: return ot_bool(a == b);
    case B_NOT: return ot_bool(!oti_truth(a));
    case B_NULL: return ot_bool(a == OT_EMPTY);
    case B_NIL: return ot_bool(a == OT_NIL);
    case B_PAIR: return ot_bool(oti_kind(a, K_PAIR));
    case B_INTEGER: {
      double real;
      if (ot_is_int(a)) return OT_TRUE;
      if (!oti_kind(a, K_FLOAT)) return OT_FALSE;
      memcpy(&real, object_bytes(a), sizeof(real));
      return ot_bool(isfinite(real) && trunc(real) == real);
    }
    case B_NUMBER: return ot_bool(ot_is_int(a) || oti_kind(a, K_FLOAT));
    case B_CONS: {
      ot_value pair = oti_alloc(rt, K_PAIR, 2, 0);
      if (pair) {
        object_fields(pair)[0] = rt->stack[first];
        object_fields(pair)[1] = rt->stack[first + 1];
      }
      return pair;
    }
    case B_CAR:
    case B_CDR:
    case B_SET_CAR:
    case B_SET_CDR:
      if (!oti_kind(a, K_PAIR)) {
        oti_coded_error(rt, "E0401", "expected a pair");
        return 0;
      }
      if (id == B_CAR || id == B_CDR) return object_fields(a)[id == B_CDR];
      if (object_frozen(a)) {
        oti_error(rt, "frozen table key");
        return 0;
      }
      object_fields(a)[id == B_SET_CDR] = b;
      return a;
  }
  oti_error(rt, "unknown builtin");
  return 0;
}

static const oti_builtin_entry builtin_table[B_COUNT] = {
    [B_ADD] = {"+", arithmetic},
    [B_SUB] = {"-", arithmetic},
    [B_MUL] = {"*", arithmetic},
    [B_DIV] = {"/", arithmetic},
    [B_LT] = {"<", compare},
    [B_LE] = {"<=", compare},
    [B_GT] = {">", compare},
    [B_GE] = {">=", compare},
    [B_NUMEQ] = {"=", compare},
    [B_REMAINDER] = {"remainder", core_builtin},
    [B_MODULO] = {"modulo", core_builtin},
    [B_EQ] = {"eq?", core_builtin},
    [B_NOT] = {"not", core_builtin},
    [B_CONS] = {"cons", core_builtin},
    [B_CAR] = {"car", core_builtin},
    [B_CDR] = {"cdr", core_builtin},
    [B_LIST] = {"list", list_builtin},
    [B_NULL] = {"null?", core_builtin},
    [B_NIL] = {"nil?", core_builtin},
    [B_PAIR] = {"pair?", core_builtin},
    [B_INTEGER] = {"integer?", core_builtin},
    [B_NUMBER] = {"number?", core_builtin},
    [B_SET_CAR] = {"set-car!", core_builtin},
    [B_SET_CDR] = {"set-cdr!", core_builtin},
    [B_PRINT] = {"print", oti_port_print},
    [B_PRINTLN] = {"println", oti_port_print},
    [B_ERROR] = {"error", error_builtin},
    [B_FFI_OPEN] = {"ffi/open-library", oti_foreign_builtin},
    [B_FFI_BIND] = {"ffi/bind", oti_foreign_builtin},
    [B_FFI_HAS_SYMBOL] = {"ffi/has-symbol?", oti_foreign_builtin},
    [B_FFI_POINTER] = {"ffi/pointer?", oti_foreign_builtin},
    [B_FFI_NULL] = {"ffi/null?", oti_foreign_builtin},
    [B_FFI_POINTER_EQ] = {"ffi/pointer=?", oti_foreign_builtin},
    [B_FFI_ALLOC] = {"ffi/alloc", oti_foreign_builtin},
    [B_FFI_FREE] = {"ffi/free!", oti_foreign_builtin},
    [B_FFI_REF] = {"ffi/ref", oti_foreign_builtin},
    [B_FFI_SET] = {"ffi/set!", oti_foreign_builtin},
    [B_FFI_SIZEOF] = {"ffi/sizeof", oti_foreign_builtin},
    [B_FFI_ALIGNOF] = {"ffi/alignof", oti_foreign_builtin},
    [B_FFI_VIEW] = {"ffi/view", oti_foreign_builtin},
    [B_FFI_ALLOC_UTF8] = {"ffi/alloc-utf8", oti_foreign_builtin},
    [B_FFI_READ_UTF8] = {"ffi/read-utf8", oti_foreign_builtin},
    [B_FFI_COUNT] = {"ffi/allocation-count", oti_foreign_builtin},
    [B_FFI_BYTES] = {"ffi/allocation-bytes", oti_foreign_builtin},
    [B_FFI_COPY] = {"ffi/copy!", oti_foreign_builtin},
    [B_FFI_READ_BYTES] = {"ffi/read-bytes", oti_foreign_builtin},
    [B_FFI_WRITE_BYTES] = {"ffi/write-bytes!", oti_foreign_builtin},
    [B_FFI_READ_UTF8_BYTES] = {"ffi/read-utf8-bytes", oti_foreign_builtin},
    [B_FFI_OWN] = {"ffi/own", oti_foreign_builtin},
    [B_FFI_CALLBACK] = {"ffi/%callback", oti_foreign_builtin},
    [B_FFI_FREE_CALLBACK] = {"ffi/free-callback!", oti_foreign_builtin},
    [B_FFI_CALLBACK_COUNT] = {"ffi/callback-count", oti_foreign_builtin},
    [B_FFI_RAW_NULL] = {"ffi/%null", oti_foreign_builtin},
    [B_FFI_RAW_SIZE] = {"ffi/%sizeof", oti_foreign_builtin},
    [B_FFI_RAW_ALIGN] = {"ffi/%alignof", oti_foreign_builtin},
    [B_FFI_RAW_BIND] = {"ffi/%bind", oti_foreign_builtin},
    [B_FFI_RAW_OPEN] = {"ffi/%open", oti_foreign_builtin},
    [B_FFI_LOADER_ERROR] = {"ffi/%loader-error", oti_foreign_builtin},
    [B_FFI_RAW_ALLOC] = {"ffi/%alloc", oti_foreign_builtin},
    [B_FFI_RAW_REF] = {"ffi/%ref", oti_foreign_builtin},
    [B_FFI_RAW_SET] = {"ffi/%set!", oti_foreign_builtin},
    [B_FFI_FAIL] = {"ffi/%fail", oti_foreign_builtin},
    [B_FFI_ERROR_FIELD] = {"ffi/%error-field", oti_foreign_builtin},
    [B_FFI_LAYOUT] = {"ffi/%layout", oti_foreign_builtin},
    [B_FFI_LAYOUT_DATA] = {"ffi/%layout-data", oti_foreign_builtin},
    [B_ARRAY] = {"array", oti_collection_builtin},
    [B_ARRAY_P] = {"array?", oti_collection_builtin},
    [B_LENGTH] = {"length", oti_collection_builtin},
    [B_GET] = {"get", oti_collection_builtin},
    [B_PUT_MUT] = {"put!", oti_collection_builtin},
    [B_PUSH] = {"push!", oti_collection_builtin},
    [B_POP] = {"pop!", oti_collection_builtin},
    [B_COPY] = {"copy", oti_collection_builtin},
    [B_EQUAL] = {"equal?", oti_collection_builtin},
    [B_CONTAINS] = {"contains?", oti_collection_builtin},
    [B_TABLE] = {"table", oti_collection_builtin},
    [B_TABLE_P] = {"table?", oti_collection_builtin},
    [B_KEYS] = {"keys", oti_collection_builtin},
    [B_VALUES] = {"values", oti_collection_builtin},
    [B_APPLY] = {"apply", oti_collection_builtin},
    [B_SYMBOL_P] = {"symbol?", oti_support_builtin},
    [B_KEYWORD_P] = {"keyword?", oti_support_builtin},
    [B_STRING_P] = {"string?", oti_support_builtin},
    [B_BOOLEAN_P] = {"boolean?", oti_support_builtin},
    [B_LIST_P] = {"list?", oti_support_builtin},
    [B_PROCEDURE_P] = {"procedure?", oti_support_builtin},
    [B_SYMBOL_STRING] = {"symbol->string", oti_support_builtin},
    [B_STRING_SYMBOL] = {"string->symbol", oti_support_builtin},
    [B_KEYWORD] = {"keyword", oti_support_builtin},
    [B_GENSYM] = {"gensym", oti_support_builtin},
    [B_STRING_LENGTH] = {"string-length", oti_support_builtin},
    [B_STRING_REF] = {"string-ref", oti_support_builtin},
    [B_SUBSTRING] = {"substring", oti_support_builtin},
    [B_EXP_APPLICATION] = {"expander-application", oti_support_builtin},
    [B_EXP_OPTIONAL] = {"expander-optional-function", oti_support_builtin},
    [B_EXP_MACRO] = {"expander-macro-var", oti_support_builtin},
    [B_EXP_INVOKE] = {"expander-invoke-macro", oti_support_builtin},
    [B_EXP_NATIVE] = {"expander-native-syntax?", oti_support_builtin},
    [B_EXP_LOCATE] = {"expander-locate", oti_support_builtin},
    [B_EXP_FAIL] = {"expander-fail", oti_support_builtin},
    [B_EXP_QUALIFY] = {"expander-qualify", oti_support_builtin},
    [B_EXP_LOCAL] = {"expander-local-name", oti_support_builtin},
    [B_STRING_APPEND] = {"string-append", oti_support_builtin},
    [B_NUMBER_STRING] = {"number->string", oti_support_builtin},
    [B_POS] = {"pos?", oti_support_builtin},
    [B_NEG] = {"neg?", oti_support_builtin},
    [B_READ_STRING] = {"read-string", oti_support_builtin},
    [B_EVAL] = {"eval", oti_support_builtin},
    [B_INT_P] = {"int?", oti_support_builtin},
    [B_FLOAT_P] = {"float?", oti_support_builtin},
    [B_MACRO_P] = {"macro?", oti_support_builtin},
    [B_CHAR_P] = {"char?", oti_support_builtin},
    [B_DISPLAY] = {"display", oti_port_print},
    [B_WRITE] = {"write", oti_port_print},
    [B_NEWLINE] = {"newline", oti_port_print},
    [B_DEFINE_CONDITION] = {"define-condition", oti_condition_builtin},
    [B_CONDITION_TYPE] = {"condition-type", oti_condition_builtin},
    [B_CONDITION_MESSAGE] = {"condition-message", oti_condition_builtin},
    [B_CONDITION_IS] = {"condition-of-type?", oti_condition_builtin},
    [B_ERROR_P] = {"error?", oti_condition_builtin},
    [B_CLASS_P] = {"class?", oti_class_builtin},
    [B_DISPATCH_TYPE] = {"dispatch-type", oti_class_builtin},
    [B_CLASS_SUBTYPE] = {"class-subtype?", oti_class_builtin},
    [B_GENERIC_P] = {"generic?", oti_class_builtin},
    [B_GENERIC_METHODS] = {"generic-methods", oti_class_builtin},
    [B_GENERIC_DEFINE] = {"%define-generic", oti_class_builtin},
    [B_GENERIC_METHOD] = {"%define-method", oti_class_builtin},
    [B_REMOVE_METHOD] = {"remove-method!", oti_class_builtin},
    [B_CLASS_DEFINE] = {"%define-class", oti_class_builtin},
    [B_MAKE] = {"make", oti_class_builtin},
    [B_INSTANCE_P] = {"instance?", oti_class_builtin},
    [B_SLOT_SET] = {"slot-set!", oti_class_builtin},
    [B_CLASS_PROPERTIES] = {"class-properties", oti_class_builtin},
    [B_CLASS_MOUNT] = {"class-mount", oti_class_builtin},
    [B_CLASS_UPDATE] = {"class-update-properties!", oti_class_builtin},
    [B_MAKE_RECORD_TYPE] = {"%make-record-type", oti_class_builtin},
    [B_RECORD_NEW] = {"record-new", oti_class_builtin},
    [B_RECORD_P] = {"record?", oti_class_builtin},
    [B_RECORD_TYPE_P] = {"record-type?", oti_class_builtin},
    [B_RECORD_TYPE] = {"record-type", oti_class_builtin},
    [B_RECORD_REF] = {"record-ref", oti_class_builtin},
    [B_RECORD_SET] = {"record-set!", oti_class_builtin},
    [B_QUOTIENT] = {"quotient", oti_numeric_builtin},
    [B_ABS] = {"abs", oti_numeric_builtin},
    [B_MIN] = {"min", oti_numeric_builtin},
    [B_MAX] = {"max", oti_numeric_builtin},
    [B_FLOOR] = {"floor", oti_numeric_builtin},
    [B_CEILING] = {"ceiling", oti_numeric_builtin},
    [B_ROUND] = {"round", oti_numeric_builtin},
    [B_TRUNCATE] = {"truncate", oti_numeric_builtin},
    [B_SQRT] = {"sqrt", oti_numeric_builtin},
    [B_EXP] = {"exp", oti_numeric_builtin},
    [B_LOG] = {"log", oti_numeric_builtin},
    [B_SIN] = {"sin", oti_numeric_builtin},
    [B_COS] = {"cos", oti_numeric_builtin},
    [B_TAN] = {"tan", oti_numeric_builtin},
    [B_ASIN] = {"asin", oti_numeric_builtin},
    [B_ACOS] = {"acos", oti_numeric_builtin},
    [B_ATAN] = {"atan", oti_numeric_builtin},
    [B_EXPT] = {"expt", oti_numeric_builtin},
    [B_EXACT] = {"exact", oti_numeric_builtin},
    [B_INEXACT] = {"inexact", oti_numeric_builtin},
    [B_EXACT_P] = {"exact?", oti_numeric_builtin},
    [B_INEXACT_P] = {"inexact?", oti_numeric_builtin},
    [B_NAN_P] = {"nan?", oti_numeric_builtin},
    [B_INFINITE_P] = {"infinite?", oti_numeric_builtin},
    [B_FINITE_P] = {"finite?", oti_numeric_builtin},
    [B_BIT_AND] = {"bitwise-and", oti_numeric_builtin},
    [B_BIT_IOR] = {"bitwise-ior", oti_numeric_builtin},
    [B_BIT_XOR] = {"bitwise-xor", oti_numeric_builtin},
    [B_BIT_NOT] = {"bitwise-not", oti_numeric_builtin},
    [B_BIT_SHIFT] = {"bitwise-arithmetic-shift", oti_numeric_builtin},
    [B_STR] = {"str", oti_string_builtin},
    [B_STRING_LT] = {"string<?", oti_string_builtin},
    [B_STRING_NUMBER] = {"string->number", oti_string_builtin},
    [B_BUFFER] = {"buffer", oti_string_builtin},
    [B_BUFFER_PUSH] = {"buffer-push!", oti_string_builtin},
    [B_BUFFER_STRING] = {"buffer->string", oti_string_builtin},
    [B_CHAR_INTEGER] = {"char->integer", oti_string_builtin},
    [B_INTEGER_CHAR] = {"integer->char", oti_string_builtin},
    [B_CHAR_UP] = {"char-upcase", oti_string_builtin},
    [B_CHAR_DOWN] = {"char-downcase", oti_string_builtin},
    [B_CHAR_SPACE] = {"char-whitespace?", oti_string_builtin},
    [B_CHAR_ALPHA] = {"char-alphabetic?", oti_string_builtin},
    [B_CHAR_NUMERIC] = {"char-numeric?", oti_string_builtin},
    [B_DIGIT_VALUE] = {"digit-value", oti_string_builtin},
    [B_NAME] = {"name", oti_string_builtin},
    [B_SYMBOL] = {"symbol", oti_string_builtin},
    [B_STRING_LIST] = {"string->list", oti_string_builtin},
    [B_TYPE] = {"type", oti_reflection_builtin},
    [B_EOF] = {"eof-object", oti_reflection_builtin},
    [B_EOF_P] = {"eof-object?", oti_reflection_builtin},
    [B_BUFFER_P] = {"buffer?", oti_reflection_builtin},
    [B_CURRENT_NS] = {"current-ns", oti_reflection_builtin},
    [B_DESCRIBE] = {"describe", oti_reflection_builtin},
    [B_SIGNAL] = {"signal", signal_builtin},
    [B_BYTES] = {"bytevector", oti_bytes_builtin},
    [B_BYTES_P] = {"bytevector?", oti_bytes_builtin},
    [B_MAKE_BYTES] = {"make-bytevector", oti_bytes_builtin},
    [B_BYTES_LENGTH] = {"bytevector-length", oti_bytes_builtin},
    [B_BYTES_REF] = {"bytevector-u8-ref", oti_bytes_builtin},
    [B_BYTES_SET] = {"bytevector-u8-set!", oti_bytes_builtin},
    [B_BYTES_COPY] = {"bytevector-copy", oti_bytes_builtin},
    [B_BYTES_COPY_MUT] = {"bytevector-copy!", oti_bytes_builtin},
    [B_BYTES_APPEND] = {"bytevector-append", oti_bytes_builtin},
    [B_UTF8_STRING] = {"utf8->string", oti_bytes_builtin},
    [B_STRING_UTF8] = {"string->utf8", oti_bytes_builtin},
    [B_FIND_RESTART] = {"find-restart", oti_restart_builtin},
    [B_COMPUTE_RESTARTS] = {"compute-restarts", oti_restart_builtin},
    [B_RESTART_NAME] = {"restart-name", oti_restart_builtin},
    [B_RESTART_DESCRIPTION] = {"restart-description", oti_restart_builtin},
    [B_INVOKE_RESTART] = {"invoke-restart", oti_restart_builtin},
    [B_EXIT] = {"exit", exit_builtin},
    [B_QUIT] = {"quit", exit_builtin},
    [B_CURRENT_JIFFY] = {"current-jiffy", time_builtin},
    [B_JIFFIES_SECOND] = {"jiffies-per-second", time_builtin},
    [B_CURRENT_SECOND] = {"current-second", time_builtin},
    [B_PORT_P] = {"port?", oti_port_builtin},
    [B_INPUT_P] = {"input-port?", oti_port_builtin},
    [B_OUTPUT_P] = {"output-port?", oti_port_builtin},
    [B_INPUT_OPEN] = {"input-port-open?", oti_port_builtin},
    [B_OUTPUT_OPEN] = {"output-port-open?", oti_port_builtin},
    [B_OPEN_INPUT_STRING] = {"open-input-string", oti_port_builtin},
    [B_OPEN_OUTPUT_STRING] = {"open-output-string", oti_port_builtin},
    [B_GET_OUTPUT_STRING] = {"get-output-string", oti_port_builtin},
    [B_OPEN_INPUT_BYTES] = {"open-input-bytevector", oti_port_builtin},
    [B_OPEN_OUTPUT_BYTES] = {"open-output-bytevector", oti_port_builtin},
    [B_GET_OUTPUT_BYTES] = {"get-output-bytevector", oti_port_builtin},
    [B_CLOSE_PORT] = {"close-port", oti_port_builtin},
    [B_CLOSE_INPUT] = {"close-input-port", oti_port_builtin},
    [B_CLOSE_OUTPUT] = {"close-output-port", oti_port_builtin},
    [B_READ_U8] = {"read-u8", oti_port_builtin},
    [B_PEEK_U8] = {"peek-u8", oti_port_builtin},
    [B_READ_CHAR] = {"read-char", oti_port_builtin},
    [B_PEEK_CHAR] = {"peek-char", oti_port_builtin},
    [B_READ_LINE] = {"read-line", oti_port_builtin},
    [B_READ_ALL] = {"read-all", oti_port_builtin},
    [B_READ_CHARACTERS] = {"%read-characters", oti_port_builtin},
    [B_READ_BYTES] = {"read-bytevector", oti_port_builtin},
    [B_READ_BYTES_MUT] = {"read-bytevector!", oti_port_builtin},
    [B_WRITE_U8] = {"write-u8", oti_port_builtin},
    [B_WRITE_CHAR] = {"write-char", oti_port_builtin},
    [B_WRITE_STRING] = {"write-string", oti_port_builtin},
    [B_WRITE_BYTES] = {"write-bytevector", oti_port_builtin},
    [B_FLUSH_PORT] = {"flush-output-port", oti_port_builtin},
    [B_DISPLAY_TO] = {"display-to", oti_port_builtin},
    [B_WRITE_TO] = {"write-to", oti_port_builtin},
    [B_READ_UNTIL] = {"read-until", oti_port_builtin},
    [B_READ_WHILE] = {"read-while", oti_port_builtin},
    [B_SKIP_WHILE] = {"skip-while", oti_port_builtin},
    [B_READ_DATUM] = {"read", oti_port_builtin},
    [B_MAKE_INPUT_PORT] = {"make-input-port", oti_port_builtin},
    [B_MAKE_OUTPUT_PORT] = {"make-output-port", oti_port_builtin},
    [B_OPEN_PORTS] = {"open-ports", oti_port_builtin},
    [B_STDIN] = {"%standard-input", oti_port_builtin},
    [B_STDOUT] = {"%standard-output", oti_port_builtin},
    [B_STDERR] = {"%standard-error", oti_port_builtin},
    [B_OPEN_INPUT_FILE] = {"open-input-file", oti_port_builtin},
    [B_OPEN_OUTPUT_FILE] = {"open-output-file", oti_port_builtin},
    [B_OPEN_BINARY_INPUT] = {"open-binary-input-file", oti_port_builtin},
    [B_OPEN_BINARY_OUTPUT] = {"open-binary-output-file", oti_port_builtin},
    [B_FILE_EXISTS] = {"file-exists?", oti_filesystem_builtin},
    [B_DELETE_FILE] = {"delete-file", oti_filesystem_builtin},
    [B_RENAME_FILE] = {"rename-file", oti_filesystem_builtin},
    [B_FILE_INFO] = {"file-info", oti_filesystem_builtin},
    [B_LIST_DIRECTORY] = {"list-directory", oti_filesystem_builtin},
    [B_MAKE_DIRECTORY] = {"make-directory", oti_filesystem_builtin},
    [B_FILE_LINK] = {"file-symbolic-link?", oti_filesystem_builtin},
    [B_CURRENT_DIRECTORY] = {"current-directory", oti_filesystem_builtin},
    [B_PROCESS_ARGUMENTS] = {"otium.process/arguments", oti_process_builtin},
    [B_SCRIPT_NAME] = {"otium.process/script-name", oti_process_builtin},
    [B_PROCESS_GETENV] = {"otium.process/getenv", oti_process_builtin},
    [B_PROCESS_SETENV] = {"otium.process/setenv!", oti_process_builtin},
    [B_PROCESS_WRITE_ERROR] = {"otium.process/write-error", oti_port_print},
    [B_READER_TOKENS] = {"otium.reader/tokens", tokens},
    [B_QQ_APPEND] = {NULL, qq_append_builtin},
    [B_EVAL_IN_NAMESPACE] = {NULL, eval_in_namespace},
};

ot_value oti_builtin(ot_state* rt, unsigned id, size_t first, size_t count) {
  if (id >= B_COUNT || !builtin_table[id].run) {
    oti_error(rt, "unknown builtin");
    return 0;
  }
  return builtin_table[id].run(rt, id, first, count);
}
