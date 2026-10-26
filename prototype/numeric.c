#include "internal.h"
#include <limits.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

static ot_value wrapped(uintptr_t bits) { return (bits << 1) | 1; }

static ot_value integral(ot_runtime* rt, double n) {
  if (!isfinite(n) || n < (double)(INTPTR_MIN / 2) || n >= -(double)(INTPTR_MIN / 2))
    return oti_fail(rt, "integer conversion out of range");
  return ot_int((intptr_t)n);
}

static ot_value bitwise(ot_runtime* rt, unsigned id, size_t first, size_t count) {
  if ((id == B_BIT_NOT && count != 1) || (id == B_BIT_SHIFT && count != 2))
    return oti_fail(rt, "wrong number of arguments");
  uintptr_t bits = id == B_BIT_AND ? UINTPTR_MAX : 0;
  for (size_t i = 0; i < count; i++)
    if (!ot_is_int(rt->stack[first + i])) return oti_fail(rt, "expected integers");
  if (id == B_BIT_NOT) return wrapped(~(uintptr_t)ot_to_int(rt->stack[first]));
  if (id == B_BIT_SHIFT) {
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
  for (size_t i = 0; i < count; i++) {
    uintptr_t n = (uintptr_t)ot_to_int(rt->stack[first + i]);
    if (id == B_BIT_AND) bits &= n;
    else if (id == B_BIT_IOR) bits |= n;
    else bits ^= n;
  }
  return wrapped(bits);
}

ot_value oti_numeric_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count) {
  if (id >= B_BIT_AND) return bitwise(rt, id, first, count);
  if (id == B_MIN || id == B_MAX) {
    if (!count) return oti_fail(rt, "extremum needs an argument");
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
  size_t arity = id == B_QUOTIENT || id == B_EXPT ? 2 : 1;
  if (id == B_ATAN ? count < 1 || count > 2 : count != arity)
    return oti_fail(rt, "wrong number of arguments");
  ot_value value = rt->stack[first];
  double a, b = 0;
  if (!oti_number(rt, value, &a)) return 0;
  if (count == 2 && !oti_number(rt, rt->stack[first + 1], &b)) return 0;
  switch (id) {
    case B_QUOTIENT:
      if (!ot_is_int(value) || !ot_is_int(rt->stack[first + 1]))
        return oti_fail(rt, "expected integers");
      if (!ot_to_int(rt->stack[first + 1])) return oti_fail(rt, "division by zero");
      return wrapped((uintptr_t)(ot_to_int(value) / ot_to_int(rt->stack[first + 1])));
    case B_ABS:
      if (ot_is_int(value)) {
        intptr_t n = ot_to_int(value);
        return wrapped(n < 0 ? 0 - (uintptr_t)n : (uintptr_t)n);
      }
      return oti_float(rt, fabs(a));
    case B_EXACT_P: return ot_bool(ot_is_int(value));
    case B_INEXACT_P: return ot_bool(!ot_is_int(value));
    case B_FINITE_P: return ot_bool(isfinite(a));
    case B_INFINITE_P: return ot_bool(isinf(a));
    case B_NAN_P: return ot_bool(isnan(a));
    case B_EXACT:
      if (ot_is_int(value)) return value;
      if (trunc(a) != a) return oti_fail(rt, "exact expects an integer-valued number");
      return integral(rt, a);
    case B_INEXACT: return ot_is_int(value) ? oti_float(rt, a) : value;
    case B_FLOOR:
    case B_CEILING:
    case B_ROUND:
    case B_TRUNCATE:
      if (ot_is_int(value)) return value;
      return integral(rt, id == B_FLOOR     ? floor(a)
                          : id == B_CEILING ? ceil(a)
                          : id == B_ROUND   ? round(a)
                                            : trunc(a));
    case B_SQRT: return oti_float(rt, sqrt(a));
    case B_EXP: return oti_float(rt, exp(a));
    case B_LOG: return oti_float(rt, log(a));
    case B_SIN: return oti_float(rt, sin(a));
    case B_COS: return oti_float(rt, cos(a));
    case B_TAN: return oti_float(rt, tan(a));
    case B_ASIN: return oti_float(rt, asin(a));
    case B_ACOS: return oti_float(rt, acos(a));
    case B_ATAN: return oti_float(rt, count == 2 ? atan2(a, b) : atan(a));
    case B_EXPT:
      if (ot_is_int(value) && ot_is_int(rt->stack[first + 1]) &&
          ot_to_int(rt->stack[first + 1]) >= 0) {
        uintptr_t base = (uintptr_t)ot_to_int(value),
                  exponent = (uintptr_t)ot_to_int(rt->stack[first + 1]), result = 1;
        while (exponent) {
          if (exponent & 1) result *= base;
          exponent >>= 1;
          base *= base;
        }
        return wrapped(result);
      }
      return oti_float(rt, pow(a, b));
    default: return oti_fail(rt, "unknown numeric builtin");
  }
}

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
