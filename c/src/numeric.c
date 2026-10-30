#include "internal.h"
#include <limits.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

static ot_value wrapped(uintptr_t bits) { return (bits << 1) | 1; }

static ot_value integral(ot_state* rt, double n) {
  if (!isfinite(n) || n < (double)(INTPTR_MIN / 2) || n >= -(double)(INTPTR_MIN / 2))
    return oti_fail(rt, "integer conversion out of range");
  return ot_int((intptr_t)n);
}

static bool numeric_integers(ot_state* rt, size_t first, size_t count) {
  for (size_t i = 0; i < count; i++)
    if (!ot_is_int(rt->stack[first + i])) return oti_error(rt, "expected integers");
  return true;
}

/* bitwise-and, bitwise-ior and bitwise-xor fold any number of integers. */
static ot_value numeric_bit_fold(ot_state* rt, unsigned id, size_t first, size_t count) {
  if (!numeric_integers(rt, first, count)) return 0;
  uintptr_t bits = id == B_BIT_AND ? UINTPTR_MAX : 0;
  for (size_t i = 0; i < count; i++) {
    uintptr_t n = (uintptr_t)ot_to_int(rt->stack[first + i]);
    if (id == B_BIT_AND) bits &= n;
    else if (id == B_BIT_IOR) bits |= n;
    else bits ^= n;
  }
  return wrapped(bits);
}

static ot_value numeric_bit_not(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "bitwise-not", count, 1, 1)) return 0;
  if (!numeric_integers(rt, first, count)) return 0;
  return wrapped(~(uintptr_t)ot_to_int(rt->stack[first]));
}

static ot_value numeric_bit_shift(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "bitwise-arithmetic-shift", count, 2, 2)) return 0;
  if (!numeric_integers(rt, first, count)) return 0;
  intptr_t n = ot_to_int(rt->stack[first]), shift = ot_to_int(rt->stack[first + 1]);
  const intptr_t width = sizeof(ot_value) * CHAR_BIT - 1;
  if (!n) return ot_int(0);
  if (shift <= -width) return ot_int(n < 0 ? -1 : 0);
  if (shift < 0) {
    unsigned right = (unsigned)-shift;
    uintptr_t magnitude = n < 0 ? ~(uintptr_t)n : (uintptr_t)n;
    magnitude >>= right;
    return wrapped(n < 0 ? ~magnitude : magnitude);
  }
  if (shift >= width) return oti_fail(rt, "integer shift overflow");
  uintptr_t magnitude = n < 0 ? 0 - (uintptr_t)n : (uintptr_t)n;
  uintptr_t limit = n < 0 ? (uintptr_t)INTPTR_MAX / 2 + 1 : (uintptr_t)INTPTR_MAX / 2;
  if (magnitude > (limit >> shift)) return oti_fail(rt, "integer shift overflow");
  return wrapped((uintptr_t)n << shift);
}

/* min and max. */
static ot_value numeric_extremum(ot_state* rt, unsigned id, size_t first, size_t count) {
  if (!oti_check_arity(rt, id == B_MIN ? "min" : "max", count, 1, SIZE_MAX)) return 0;
  ot_value selected = rt->stack[first];
  double best;
  if (!oti_number(rt, selected, &best)) return 0;
  bool inexact = !ot_is_int(selected);
  for (size_t i = 1; i < count; i++) {
    ot_value v = rt->stack[first + i];
    double n;
    if (!oti_number(rt, v, &n)) return 0;
    inexact |= !ot_is_int(v);
    int order;
    if (ot_is_int(selected) && ot_is_int(v)) {
      intptr_t a = ot_to_int(selected), b = ot_to_int(v);
      order = a < b ? -1 : a > b ? 1 : 0;
    } else if (ot_is_int(selected)) order = oti_integer_real(ot_to_int(selected), n);
    else if (ot_is_int(v)) {
      order = oti_integer_real(ot_to_int(v), best);
      if (order != 2) order = -order;
    } else order = best < n ? -1 : best > n ? 1 : 0;
    if (isnan(n) || (!isnan(best) && (id == B_MIN ? order == 1 : order == -1))) {
      selected = v;
      best = n;
    }
  }
  return inexact ? oti_float(rt, best) : selected;
}

/* Check the argument count and convert every argument to a double. */
static bool numeric_args(ot_state* rt, const char* who, size_t first, size_t count, size_t arity,
                         double* a, double* b) {
  if (!oti_check_arity(rt, who, count, arity, arity)) return false;
  if (!oti_number(rt, rt->stack[first], a)) return false;
  return count != 2 || oti_number(rt, rt->stack[first + 1], b);
}

static ot_value numeric_quotient(ot_state* rt, size_t first, size_t count) {
  double a, b;
  if (!numeric_args(rt, "quotient", first, count, 2, &a, &b)) return 0;
  ot_value n = rt->stack[first], d = rt->stack[first + 1];
  if (!ot_is_int(n) || !ot_is_int(d)) return oti_fail(rt, "expected integers");
  if (!ot_to_int(d)) return oti_fail(rt, "division by zero");
  return wrapped((uintptr_t)(ot_to_int(n) / ot_to_int(d)));
}

static ot_value numeric_abs(ot_state* rt, size_t first, size_t count) {
  double a;
  if (!numeric_args(rt, "abs", first, count, 1, &a, NULL)) return 0;
  ot_value value = rt->stack[first];
  if (ot_is_int(value)) {
    intptr_t n = ot_to_int(value);
    return wrapped(n < 0 ? 0 - (uintptr_t)n : (uintptr_t)n);
  }
  return oti_float(rt, fabs(a));
}

/* exact?, inexact?, finite?, infinite? and nan?. */
static ot_value numeric_classify(ot_state* rt, const char* who, unsigned id, size_t first,
                                 size_t count) {
  double a;
  if (!numeric_args(rt, who, first, count, 1, &a, NULL)) return 0;
  ot_value value = rt->stack[first];
  switch (id) {
    case B_EXACT_P: return ot_bool(ot_is_int(value));
    case B_INEXACT_P: return ot_bool(!ot_is_int(value));
    case B_FINITE_P: return ot_bool(isfinite(a));
    case B_INFINITE_P: return ot_bool(isinf(a));
    default: return ot_bool(isnan(a));
  }
}

static ot_value numeric_exact(ot_state* rt, size_t first, size_t count) {
  double a;
  if (!numeric_args(rt, "exact", first, count, 1, &a, NULL)) return 0;
  ot_value value = rt->stack[first];
  if (ot_is_int(value)) return value;
  if (trunc(a) != a) return oti_fail(rt, "exact expects an integer-valued number");
  return integral(rt, a);
}

static ot_value numeric_inexact(ot_state* rt, size_t first, size_t count) {
  double a;
  if (!numeric_args(rt, "inexact", first, count, 1, &a, NULL)) return 0;
  ot_value value = rt->stack[first];
  return ot_is_int(value) ? oti_float(rt, a) : value;
}

/* floor, ceiling, round and truncate. */
static ot_value numeric_rounding(ot_state* rt, const char* who, unsigned id, size_t first,
                                 size_t count) {
  double a;
  if (!numeric_args(rt, who, first, count, 1, &a, NULL)) return 0;
  ot_value value = rt->stack[first];
  if (ot_is_int(value)) return value;
  double (*round_fn)(double) = id == B_FLOOR     ? floor
                               : id == B_CEILING ? ceil
                               : id == B_ROUND   ? round
                                                 : trunc;
  return integral(rt, round_fn(a));
}

/* One-argument libm wrappers. */
static ot_value numeric_libm(ot_state* rt, const char* who, double (*fn)(double), size_t first,
                             size_t count) {
  double a;
  if (!numeric_args(rt, who, first, count, 1, &a, NULL)) return 0;
  return oti_float(rt, fn(a));
}

static ot_value numeric_atan(ot_state* rt, size_t first, size_t count) {
  double a, b = 0;
  if (!oti_check_arity(rt, "atan", count, 1, 2)) return 0;
  if (!numeric_args(rt, "atan", first, count, count, &a, &b)) return 0;
  return oti_float(rt, count == 2 ? atan2(a, b) : atan(a));
}

static ot_value numeric_expt(ot_state* rt, size_t first, size_t count) {
  double a, b;
  if (!numeric_args(rt, "expt", first, count, 2, &a, &b)) return 0;
  ot_value value = rt->stack[first], power = rt->stack[first + 1];
  if (ot_is_int(value) && ot_is_int(power) && ot_to_int(power) >= 0) {
    uintptr_t base = (uintptr_t)ot_to_int(value), exponent = (uintptr_t)ot_to_int(power),
              result = 1;
    while (exponent) {
      if (exponent & 1) result *= base;
      exponent >>= 1;
      base *= base;
    }
    return wrapped(result);
  }
  return oti_float(rt, pow(a, b));
}

static ot_value numeric_min(ot_state* rt, size_t first, size_t count) {
  return numeric_extremum(rt, B_MIN, first, count);
}

static ot_value numeric_max(ot_state* rt, size_t first, size_t count) {
  return numeric_extremum(rt, B_MAX, first, count);
}

static ot_value numeric_exact_p(ot_state* rt, size_t first, size_t count) {
  return numeric_classify(rt, "exact?", B_EXACT_P, first, count);
}

static ot_value numeric_inexact_p(ot_state* rt, size_t first, size_t count) {
  return numeric_classify(rt, "inexact?", B_INEXACT_P, first, count);
}

static ot_value numeric_finite_p(ot_state* rt, size_t first, size_t count) {
  return numeric_classify(rt, "finite?", B_FINITE_P, first, count);
}

static ot_value numeric_infinite_p(ot_state* rt, size_t first, size_t count) {
  return numeric_classify(rt, "infinite?", B_INFINITE_P, first, count);
}

static ot_value numeric_nan_p(ot_state* rt, size_t first, size_t count) {
  return numeric_classify(rt, "nan?", B_NAN_P, first, count);
}

static ot_value numeric_floor(ot_state* rt, size_t first, size_t count) {
  return numeric_rounding(rt, "floor", B_FLOOR, first, count);
}

static ot_value numeric_ceiling(ot_state* rt, size_t first, size_t count) {
  return numeric_rounding(rt, "ceiling", B_CEILING, first, count);
}

static ot_value numeric_round(ot_state* rt, size_t first, size_t count) {
  return numeric_rounding(rt, "round", B_ROUND, first, count);
}

static ot_value numeric_truncate(ot_state* rt, size_t first, size_t count) {
  return numeric_rounding(rt, "truncate", B_TRUNCATE, first, count);
}

static ot_value numeric_sqrt(ot_state* rt, size_t first, size_t count) {
  return numeric_libm(rt, "sqrt", sqrt, first, count);
}

static ot_value numeric_exp(ot_state* rt, size_t first, size_t count) {
  return numeric_libm(rt, "exp", exp, first, count);
}

static ot_value numeric_log(ot_state* rt, size_t first, size_t count) {
  return numeric_libm(rt, "log", log, first, count);
}

static ot_value numeric_sin(ot_state* rt, size_t first, size_t count) {
  return numeric_libm(rt, "sin", sin, first, count);
}

static ot_value numeric_cos(ot_state* rt, size_t first, size_t count) {
  return numeric_libm(rt, "cos", cos, first, count);
}

static ot_value numeric_tan(ot_state* rt, size_t first, size_t count) {
  return numeric_libm(rt, "tan", tan, first, count);
}

static ot_value numeric_asin(ot_state* rt, size_t first, size_t count) {
  return numeric_libm(rt, "asin", asin, first, count);
}

static ot_value numeric_acos(ot_state* rt, size_t first, size_t count) {
  return numeric_libm(rt, "acos", acos, first, count);
}

static ot_value numeric_bit_and(ot_state* rt, size_t first, size_t count) {
  return numeric_bit_fold(rt, B_BIT_AND, first, count);
}

static ot_value numeric_bit_ior(ot_state* rt, size_t first, size_t count) {
  return numeric_bit_fold(rt, B_BIT_IOR, first, count);
}

static ot_value numeric_bit_xor(ot_state* rt, size_t first, size_t count) {
  return numeric_bit_fold(rt, B_BIT_XOR, first, count);
}

const oti_builtin_def oti_numeric_builtins[] = {
    {B_QUOTIENT, "quotient", numeric_quotient},
    {B_ABS, "abs", numeric_abs},
    {B_MIN, "min", numeric_min},
    {B_MAX, "max", numeric_max},
    {B_FLOOR, "floor", numeric_floor},
    {B_CEILING, "ceiling", numeric_ceiling},
    {B_ROUND, "round", numeric_round},
    {B_TRUNCATE, "truncate", numeric_truncate},
    {B_SQRT, "sqrt", numeric_sqrt},
    {B_EXP, "exp", numeric_exp},
    {B_LOG, "log", numeric_log},
    {B_SIN, "sin", numeric_sin},
    {B_COS, "cos", numeric_cos},
    {B_TAN, "tan", numeric_tan},
    {B_ASIN, "asin", numeric_asin},
    {B_ACOS, "acos", numeric_acos},
    {B_ATAN, "atan", numeric_atan},
    {B_EXPT, "expt", numeric_expt},
    {B_EXACT, "exact", numeric_exact},
    {B_INEXACT, "inexact", numeric_inexact},
    {B_EXACT_P, "exact?", numeric_exact_p},
    {B_INEXACT_P, "inexact?", numeric_inexact_p},
    {B_NAN_P, "nan?", numeric_nan_p},
    {B_INFINITE_P, "infinite?", numeric_infinite_p},
    {B_FINITE_P, "finite?", numeric_finite_p},
    {B_BIT_AND, "bitwise-and", numeric_bit_and},
    {B_BIT_IOR, "bitwise-ior", numeric_bit_ior},
    {B_BIT_XOR, "bitwise-xor", numeric_bit_xor},
    {B_BIT_NOT, "bitwise-not", numeric_bit_not},
    {B_BIT_SHIFT, "bitwise-arithmetic-shift", numeric_bit_shift},
    {0, NULL, NULL},
};

/* Find the least significant-digit precision that round-trips, then use the
 * same fixed/exponent thresholds as the primary runtime. */
size_t oti_format_float(double value, char* buffer, size_t size) {
  if (isnan(value)) return (size_t)snprintf(buffer, size, "+nan.0");
  if (isinf(value)) return (size_t)snprintf(buffer, size, "%cinf.0", signbit(value) ? '-' : '+');
  if (value == 0) return (size_t)snprintf(buffer, size, "%s0.0", signbit(value) ? "-" : "");
  char scientific[40];
  for (int precision = 0; precision <= 16; precision++) {
    snprintf(scientific, sizeof(scientific), "%.*e", precision, value);
    double parsed = strtod(scientific, NULL);
    if (!memcmp(&value, &parsed, sizeof(value))) break;
  }
  char* exponent_text = strchr(scientific, 'e');
  int exponent = (int)strtol(exponent_text + 1, NULL, 10);
  *exponent_text = 0;
  if (exponent < -3 || exponent >= 10)
    return (size_t)snprintf(buffer, size, "%se%d", scientific, exponent);
  char digits[18];
  size_t count = 0;
  for (char* p = scientific + (scientific[0] == '-'); *p; p++)
    if (*p != '.') digits[count++] = *p;
  char fixed[64], *out = fixed;
  if (scientific[0] == '-') *out++ = '-';
  int point = exponent + 1;
  if (point <= 0) {
    *out++ = '0';
    *out++ = '.';
    for (int i = 0; i < -point; i++) *out++ = '0';
    memcpy(out, digits, count);
    out += count;
  } else {
    for (int i = 0; i < point; i++) *out++ = (size_t)i < count ? digits[i] : '0';
    *out++ = '.';
    if ((size_t)point >= count) *out++ = '0';
    else {
      memcpy(out, digits + point, count - (size_t)point);
      out += count - (size_t)point;
    }
  }
  *out = 0;
  return (size_t)snprintf(buffer, size, "%s", fixed);
}
