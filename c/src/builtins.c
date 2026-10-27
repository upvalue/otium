#include "internal.h"
#include <math.h>
#include <string.h>

static const oti_builtin_def oti_core_builtins[];

static const oti_builtin_def* const builtin_modules[] = {
    oti_core_builtins,       oti_bytes_builtins,      oti_class_builtins,
    oti_collection_builtins, oti_condition_builtins,  oti_dev_builtins,
    oti_filesystem_builtins, oti_foreign_builtins,    oti_instance_builtins,
    oti_numeric_builtins,    oti_port_builtins,       oti_process_builtins,
    oti_record_builtins,     oti_reflection_builtins, oti_restart_builtins,
    oti_string_builtins,     oti_support_builtins,    oti_table_builtins,
    oti_time_builtins,
};

#define BUILTIN_MODULES (sizeof(builtin_modules) / sizeof(*builtin_modules))

int oti_builtin_index(const char* name) {
  for (size_t m = 0; m < BUILTIN_MODULES; m++)
    for (const oti_builtin_def* d = builtin_modules[m]; d->run; d++)
      if (d->name && !strcmp(name, d->name)) return (int)d->id;
  return -1;
}

/* Fill the dispatch array and bind each named builtin. Every id must be
 * defined exactly once. */
bool oti_builtins(ot_state* rt) {
  uint32_t ffi = oti_namespace(rt, "ffi", 3);
  uint32_t process = oti_namespace(rt, "otium.process", 13);
  uint32_t reader = oti_namespace(rt, "otium.reader", 12);
  uint32_t dev = oti_namespace(rt, "otium.dev", 9);
  /* otium.generic has no bindings of its own: its names resolve to core's. */
  uint32_t generic = oti_namespace(rt, "otium.generic", 13);
  if (ffi == UINT32_MAX || process == UINT32_MAX || reader == UINT32_MAX || generic == UINT32_MAX ||
      dev == UINT32_MAX)
    return false;
  memset(rt->builtin_run, 0, sizeof(rt->builtin_run));
  for (size_t m = 0; m < BUILTIN_MODULES; m++) {
    for (const oti_builtin_def* d = builtin_modules[m]; d->run; d++) {
      if (d->id >= B_COUNT || rt->builtin_run[d->id]) return oti_error(rt, "duplicate builtin");
      rt->builtin_run[d->id] = d->run;
      const char* full = d->name;
      if (!full) continue;
      bool foreign = !strncmp(full, "ffi/", 4);
      bool process_binding = !strncmp(full, "otium.process/", 14);
      bool reader_binding = !strncmp(full, "otium.reader/", 13);
      bool dev_binding = !strncmp(full, "otium.dev/", 10);
      const char* name = full + (foreign           ? 4
                                 : process_binding ? 14
                                 : reader_binding  ? 13
                                 : dev_binding     ? 10
                                                   : 0);
      rt->current_namespace = foreign           ? ffi
                              : process_binding ? process
                              : reader_binding  ? reader
                              : dev_binding     ? dev
                                                : 0;
      uint32_t global = oti_global(rt, name, strlen(name));
      if (global == UINT32_MAX) return false;
      rt->globals[global] = ((ot_value)d->id << 8) | TAG_BUILTIN;
      rt->builtin_cells[d->id] = global;
    }
  }
  rt->current_namespace = 0;
  for (size_t i = 0; i < B_COUNT; i++)
    if (!rt->builtin_run[i]) return oti_error(rt, "missing builtin");
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

/* Record E0401 unless v is a number, naming the operator as Chez does. */
static bool number_operand(ot_state* rt, unsigned id, ot_value v) {
  if (ot_is_int(v) || oti_kind(v, K_FLOAT)) return true;
  const char* who = id == B_ADD   ? "+"
                    : id == B_SUB ? "-"
                    : id == B_MUL ? "*"
                    : id == B_DIV ? "/"
                    : id == B_LT  ? "<"
                    : id == B_LE  ? "<="
                    : id == B_GT  ? ">"
                    : id == B_GE  ? ">="
                                  : "=";
  const char* what = id == B_DIV                                            ? "int or float"
                     : id == B_LT || id == B_LE || id == B_GT || id == B_GE ? "a real number"
                                                                            : "a number";
  return oti_type_error(rt, who, what, v);
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
      if (!number_operand(rt, id, rt->stack[first + j]) ||
          !oti_number(rt, rt->stack[first + j], &ignored))
        return 0;
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
    if (!number_operand(rt, id, v) || (!ot_is_int(v) && !oti_number(rt, v, &current))) return 0;
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
    fast = oti_cdr(fast);
    if (!oti_kind(fast, K_PAIR)) break;
    fast = oti_cdr(fast);
    slow = oti_cdr(slow);
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
    oti_as_pair(pair)->car = oti_car(slots[0]);
    oti_as_pair(pair)->cdr = slots[1];
    if (slots[2] == OT_EMPTY) slots[2] = pair;
    else oti_as_pair(slots[3])->cdr = pair;
    slots[3] = pair;
    slots[0] = oti_cdr(slots[0]);
  }
  ot_value result = slots[2] == OT_EMPTY ? slots[1] : slots[2];
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

static ot_value eval_in_namespace(ot_state* rt, size_t first, size_t count) {
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

static ot_value exit_builtin(ot_state* rt, size_t first, size_t count) {
  if (count > 1 || (count && (!ot_is_int(rt->stack[first]) || ot_to_int(rt->stack[first]) < 0 ||
                              ot_to_int(rt->stack[first]) > 255))) {
    oti_error(rt, "exit: expected integer status from 0 to 255");
    return 0;
  }
  rt->exit_status = count ? (int)ot_to_int(rt->stack[first]) : 0;
  rt->exit_pending = true;
  oti_error(rt, "exit requested");
  return 0;
}

ot_value oti_list_from(ot_state* rt, size_t first, size_t count) {
  ot_value result = OT_EMPTY;
  oti_roots root;
  oti_roots_push(&rt->heap, &root, &result, 1);
  for (size_t i = count; i > 0; i--) {
    ot_value pair = oti_alloc(rt, K_PAIR, 2, 0);
    if (!pair) {
      result = 0;
      break;
    }
    oti_as_pair(pair)->car = rt->stack[first + i - 1];
    oti_as_pair(pair)->cdr = result;
    result = pair;
  }
  oti_roots_pop(&rt->heap, &root);
  return result;
}

static bool core_arity(ot_state* rt, size_t count, size_t arity) {
  if (count == arity) return true;
  return oti_error(rt, "wrong number of builtin arguments");
}

/* remainder and modulo differ only in the sign of a nonzero result. */
static ot_value remainder_modulo(ot_state* rt, bool modulo, size_t first, size_t count) {
  if (!core_arity(rt, count, 2)) return 0;
  ot_value a = rt->stack[first], b = rt->stack[first + 1];
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
  if (modulo && r && ((r < 0) != (divisor < 0))) r += divisor;
  return ot_int(r);
}

static ot_value eq_builtin(ot_state* rt, size_t first, size_t count) {
  if (!core_arity(rt, count, 2)) return 0;
  return ot_bool(rt->stack[first] == rt->stack[first + 1]);
}

static ot_value not_builtin(ot_state* rt, size_t first, size_t count) {
  if (!core_arity(rt, count, 1)) return 0;
  return ot_bool(!oti_truth(rt->stack[first]));
}

static ot_value null_builtin(ot_state* rt, size_t first, size_t count) {
  if (!core_arity(rt, count, 1)) return 0;
  return ot_bool(rt->stack[first] == OT_EMPTY);
}

static ot_value nil_builtin(ot_state* rt, size_t first, size_t count) {
  if (!core_arity(rt, count, 1)) return 0;
  return ot_bool(rt->stack[first] == OT_NIL);
}

static ot_value pair_builtin(ot_state* rt, size_t first, size_t count) {
  if (!core_arity(rt, count, 1)) return 0;
  return ot_bool(oti_kind(rt->stack[first], K_PAIR));
}

static ot_value integer_builtin(ot_state* rt, size_t first, size_t count) {
  if (!core_arity(rt, count, 1)) return 0;
  ot_value a = rt->stack[first];
  double real;
  if (ot_is_int(a)) return OT_TRUE;
  if (!oti_kind(a, K_FLOAT)) return OT_FALSE;
  memcpy(&real, object_bytes(a), sizeof(real));
  return ot_bool(isfinite(real) && trunc(real) == real);
}

static ot_value number_builtin(ot_state* rt, size_t first, size_t count) {
  if (!core_arity(rt, count, 1)) return 0;
  ot_value a = rt->stack[first];
  return ot_bool(ot_is_int(a) || oti_kind(a, K_FLOAT));
}

static ot_value cons_builtin(ot_state* rt, size_t first, size_t count) {
  if (!core_arity(rt, count, 2)) return 0;
  ot_value pair = oti_alloc(rt, K_PAIR, 2, 0);
  if (pair) {
    oti_as_pair(pair)->car = rt->stack[first];
    oti_as_pair(pair)->cdr = rt->stack[first + 1];
  }
  return pair;
}

static bool pair_argument(ot_state* rt, const char* who, ot_value a) {
  return oti_kind(a, K_PAIR) || oti_type_error(rt, who, "a pair", a);
}

static ot_value car_builtin(ot_state* rt, size_t first, size_t count) {
  if (!core_arity(rt, count, 1) || !pair_argument(rt, "car", rt->stack[first])) return 0;
  return oti_car(rt->stack[first]);
}

static ot_value cdr_builtin(ot_state* rt, size_t first, size_t count) {
  if (!core_arity(rt, count, 1) || !pair_argument(rt, "cdr", rt->stack[first])) return 0;
  return oti_cdr(rt->stack[first]);
}

/* Pairs inside table keys are frozen so the key's hash cannot change. */
/* Chez names both mutators set-pair! in its errors. */
static bool mutable_pair(ot_state* rt, ot_value a) {
  if (!oti_kind(a, K_PAIR)) return oti_type_error(rt, "set-pair!", "pair", a);
  if (!object_frozen(a)) return true;
  return oti_error(rt, "frozen table key");
}

/* False, with an error, if pair is reachable from value: storing value in
 * pair would make a cycle. */
static bool acyclic(ot_state* rt, ot_value pair, ot_value value) {
  int found = oti_reaches(value, pair);
  if (found < 0) return oti_error(rt, "out of host memory");
  return !found || oti_tagged_error(rt, "set-pair!", NULL, "cycle", 0, NULL);
}

static ot_value set_car_builtin(ot_state* rt, size_t first, size_t count) {
  if (!core_arity(rt, count, 2) || !mutable_pair(rt, rt->stack[first]) ||
      !acyclic(rt, rt->stack[first], rt->stack[first + 1]))
    return 0;
  ot_value a = rt->stack[first];
  oti_as_pair(a)->car = rt->stack[first + 1];
  return a;
}

static ot_value set_cdr_builtin(ot_state* rt, size_t first, size_t count) {
  if (!core_arity(rt, count, 2) || !mutable_pair(rt, rt->stack[first]) ||
      !acyclic(rt, rt->stack[first], rt->stack[first + 1]))
    return 0;
  ot_value a = rt->stack[first];
  oti_as_pair(a)->cdr = rt->stack[first + 1];
  return a;
}

static ot_value add_builtin(ot_state* rt, size_t first, size_t count) {
  return arithmetic(rt, B_ADD, first, count);
}

static ot_value sub_builtin(ot_state* rt, size_t first, size_t count) {
  return arithmetic(rt, B_SUB, first, count);
}

static ot_value mul_builtin(ot_state* rt, size_t first, size_t count) {
  return arithmetic(rt, B_MUL, first, count);
}

static ot_value div_builtin(ot_state* rt, size_t first, size_t count) {
  return arithmetic(rt, B_DIV, first, count);
}

static ot_value lt_builtin(ot_state* rt, size_t first, size_t count) {
  return compare(rt, B_LT, first, count);
}

static ot_value le_builtin(ot_state* rt, size_t first, size_t count) {
  return compare(rt, B_LE, first, count);
}

static ot_value gt_builtin(ot_state* rt, size_t first, size_t count) {
  return compare(rt, B_GT, first, count);
}

static ot_value ge_builtin(ot_state* rt, size_t first, size_t count) {
  return compare(rt, B_GE, first, count);
}

static ot_value numeq_builtin(ot_state* rt, size_t first, size_t count) {
  return compare(rt, B_NUMEQ, first, count);
}

static ot_value remainder_builtin(ot_state* rt, size_t first, size_t count) {
  return remainder_modulo(rt, false, first, count);
}

static ot_value modulo_builtin(ot_state* rt, size_t first, size_t count) {
  return remainder_modulo(rt, true, first, count);
}

static const oti_builtin_def oti_core_builtins[] = {
    {B_ADD, "+", add_builtin},
    {B_SUB, "-", sub_builtin},
    {B_MUL, "*", mul_builtin},
    {B_DIV, "/", div_builtin},
    {B_LT, "<", lt_builtin},
    {B_LE, "<=", le_builtin},
    {B_GT, ">", gt_builtin},
    {B_GE, ">=", ge_builtin},
    {B_NUMEQ, "=", numeq_builtin},
    {B_REMAINDER, "remainder", remainder_builtin},
    {B_MODULO, "modulo", modulo_builtin},
    {B_EQ, "eq?", eq_builtin},
    {B_NOT, "not", not_builtin},
    {B_CONS, "cons", cons_builtin},
    {B_CAR, "car", car_builtin},
    {B_CDR, "cdr", cdr_builtin},
    {B_LIST, "list", oti_list_from},
    {B_NULL, "null?", null_builtin},
    {B_NIL, "nil?", nil_builtin},
    {B_PAIR, "pair?", pair_builtin},
    {B_INTEGER, "integer?", integer_builtin},
    {B_NUMBER, "number?", number_builtin},
    {B_SET_CAR, "set-car!", set_car_builtin},
    {B_SET_CDR, "set-cdr!", set_cdr_builtin},
    {B_ERROR, "error", oti_raise},
    {B_SIGNAL, "signal", oti_signal},
    {B_EXIT, "exit", exit_builtin},
    {B_QUIT, "quit", exit_builtin},
    {B_READER_TOKENS, "otium.reader/tokens", oti_tokens},
    {B_QQ_APPEND, NULL, quasi_append},
    {B_EVAL_IN_NAMESPACE, NULL, eval_in_namespace},
    {0, NULL, NULL},
};

ot_value oti_builtin(ot_state* rt, unsigned id, size_t first, size_t count) {
  if (id >= B_COUNT || !rt->builtin_run[id]) {
    oti_error(rt, "unknown builtin");
    return 0;
  }
  return rt->builtin_run[id](rt, first, count);
}
