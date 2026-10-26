#include "internal.h"
#include <math.h>
#include <string.h>

static const char* const names[] = {
#define BUILTIN(id, name) [id] = name,
#include "builtins.def"
#undef BUILTIN
};
_Static_assert(sizeof(names) / sizeof(names[0]) == B_QQ_APPEND,
               "public builtin names must match their IDs");

int oti_builtin_index(const char* name) {
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    if (!strcmp(name, names[i])) return (int)i;
  return -1;
}

bool oti_builtins(ot_runtime* rt) {
  uint32_t ffi = oti_namespace(rt, "ffi", 3);
  uint32_t process = oti_namespace(rt, "otium.process", 13);
  uint32_t reader = oti_namespace(rt, "otium.reader", 12);
  if (ffi == UINT32_MAX || process == UINT32_MAX || reader == UINT32_MAX) return false;
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    bool foreign = !strncmp(names[i], "ffi/", 4);
    bool process_binding = !strncmp(names[i], "otium.process/", 14);
    bool reader_binding = !strncmp(names[i], "otium.reader/", 13);
    const char* name = names[i] + (foreign ? 4 : process_binding ? 14 : reader_binding ? 13 : 0);
    rt->current_namespace = foreign ? ffi : process_binding ? process : reader_binding ? reader : 0;
    uint32_t global = oti_global(rt, name, strlen(name));
    if (global == UINT32_MAX) return false;
    rt->globals[global] = ((ot_value)i << 8) | TAG_BUILTIN;
  }
  rt->current_namespace = 0;
  return true;
}

bool oti_number(ot_runtime* rt, ot_value v, double* out) {
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

static ot_value boolean(bool b) { return b ? OT_TRUE : OT_FALSE; }

static ot_value arithmetic(ot_runtime* rt, unsigned id, size_t first, size_t count) {
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

static ot_value compare(ot_runtime* rt, unsigned id, size_t first, size_t count) {
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
  return boolean(result);
}

/* Quasiquote copies only the splice prefix; its final tail can be any value. */
static ot_value quasi_append(ot_runtime* rt, size_t first, size_t count) {
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

ot_value oti_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count) {
  if (id == B_EVAL_IN_NAMESPACE) {
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
  if (id == B_QQ_APPEND) return quasi_append(rt, first, count);
  if (id == B_READER_TOKENS) return oti_tokens(rt, first, count);
  if (id == B_PROCESS_WRITE_ERROR) return oti_port_print(rt, id, first, count);
  if (id >= B_PROCESS_ARGUMENTS) return oti_process_builtin(rt, id, first, count);
  if (id >= B_FILE_EXISTS) return oti_filesystem_builtin(rt, id, first, count);
  if (id >= B_PORT_P) return oti_port_builtin(rt, id, first, count);
  if (id == B_PRINT || id == B_PRINTLN || id == B_DISPLAY || id == B_WRITE || id == B_NEWLINE)
    return oti_port_print(rt, id, first, count);
  if (id >= B_CURRENT_JIFFY) return oti_time_builtin(rt, id, count);
  if (id == B_EXIT || id == B_QUIT) {
    if (count > 1 ||
        (count && (!ot_is_int(rt->stack[first]) || ot_to_int(rt->stack[first]) < 0 ||
                   ot_to_int(rt->stack[first]) > 255))) {
      oti_error(rt, "exit status must be an integer from 0 to 255");
      return 0;
    }
    rt->exit_status = count ? (int)ot_to_int(rt->stack[first]) : 0;
    rt->exit_pending = true;
    oti_error(rt, "exit requested");
    return 0;
  }
  if (id >= B_FIND_RESTART) return oti_restart_builtin(rt, id, first, count);
  if (id >= B_BYTES) return oti_bytes_builtin(rt, id, first, count);
  if (id == B_SIGNAL) return oti_signal(rt, first, count);
  if (id >= B_TYPE) return oti_reflection_builtin(rt, id, first, count);
  if (id >= B_STR) return oti_string_builtin(rt, id, first, count);
  if (id >= B_QUOTIENT) return oti_numeric_builtin(rt, id, first, count);
  if (id >= B_CLASS_P) return oti_class_builtin(rt, id, first, count);
  if (id >= B_DEFINE_CONDITION) return oti_condition_builtin(rt, id, first, count);
  if (id >= B_SYMBOL_P) return oti_support_builtin(rt, id, first, count);
  if (id >= B_ARRAY) return oti_collection_builtin(rt, id, first, count);
  if (id >= B_FFI_OPEN) return oti_foreign_builtin(rt, id, first, count);
  if (id <= B_DIV) return arithmetic(rt, id, first, count);
  if (id <= B_NUMEQ) return compare(rt, id, first, count);
  if (id == B_LIST) {
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
  if (id == B_ERROR) return oti_raise(rt, first, count);
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
    case B_EQ: return boolean(a == b);
    case B_NOT: return boolean(!oti_truth(a));
    case B_NULL: return boolean(a == OT_EMPTY);
    case B_NIL: return boolean(a == OT_NIL);
    case B_PAIR: return boolean(oti_kind(a, K_PAIR));
    case B_INTEGER: {
      double real;
      if (ot_is_int(a)) return OT_TRUE;
      if (!oti_kind(a, K_FLOAT)) return OT_FALSE;
      memcpy(&real, object_bytes(a), sizeof(real));
      return boolean(isfinite(real) && trunc(real) == real);
    }
    case B_NUMBER: return boolean(ot_is_int(a) || oti_kind(a, K_FLOAT));
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
