// builtins/arith.c — spec 10.1. Int math wraps two's-complement (done in
// u64), any float operand contaminates to float.
#include "../builtins.h"
#include <math.h>

static inline f64 as_f(Value v) { return v.tag == Tag_Int ? (f64)v.i : v.f; }

static bool any_float(State* vm, u32 base, u32 argc) {
  for (u32 i = 0; i < argc; i++)
    if (ARG(i).tag == Tag_Float) return true;
  return false;
}

Value nat_add(State* vm, u32 base, u32 argc) {
  OT_TRY(need_nums(vm, "+", base, argc));
  if (any_float(vm, base, argc)) {
    f64 acc = 0.0;
    for (u32 i = 0; i < argc; i++) acc += as_f(ARG(i));
    return float_v(acc);
  }
  u64 acc = 0;
  for (u32 i = 0; i < argc; i++) acc += (u64)ARG(i).i;
  return int_v((i64)acc);
}

Value nat_mul(State* vm, u32 base, u32 argc) {
  OT_TRY(need_nums(vm, "*", base, argc));
  if (any_float(vm, base, argc)) {
    f64 acc = 1.0;
    for (u32 i = 0; i < argc; i++) acc *= as_f(ARG(i));
    return float_v(acc);
  }
  u64 acc = 1;
  for (u32 i = 0; i < argc; i++) acc *= (u64)ARG(i).i;
  return int_v((i64)acc);
}

Value nat_sub(State* vm, u32 base, u32 argc) {
  OT_TRY(need_num_args(vm, "-", base, argc, 1, UINT32_MAX));
  if (any_float(vm, base, argc)) {
    f64 acc = as_f(ARG(0));
    if (argc == 1) return float_v(-acc);
    for (u32 i = 1; i < argc; i++) acc -= as_f(ARG(i));
    return float_v(acc);
  }
  u64 acc = (u64)ARG(0).i;
  if (argc == 1) return int_v((i64)(0 - acc));
  for (u32 i = 1; i < argc; i++) acc -= (u64)ARG(i).i;
  return int_v((i64)acc);
}

// remainder with wrap safety (INT64_MIN % -1 is UB in C)
static i64 a_rem(i64 a, i64 b) { return b == -1 ? 0 : a % b; }

// Exact int division with wrap semantics for INT64_MIN / -1.
static i64 idiv_wrap(i64 a, i64 b) {
  if (b == -1) return (i64)(0 - (u64)a);  // avoids UB on INT64_MIN / -1
  return a / b;
}

Value nat_div(State* vm, u32 base, u32 argc) {
  OT_TRY(need_num_args(vm, "/", base, argc, 1, UINT32_MAX));
  // (/ n) = 1/n
  u32 first = 0;
  Value acc;
  if (argc == 1) {
    acc = int_v(1);
  } else {
    acc = ARG(0);
    first = 1;
  }
  for (u32 i = first; i < argc; i++) {
    Value b = ARG(i);
    if (acc.tag == Tag_Int && b.tag == Tag_Int) {
      if (b.i == 0) return raise_error(vm, "/: division by zero");
      if (a_rem(acc.i, b.i) == 0) acc = int_v(idiv_wrap(acc.i, b.i));
      else acc = float_v((f64)acc.i / (f64)b.i);
    } else {
      acc = float_v(as_f(acc) / as_f(b));
    }
  }
  return acc;
}

static Value nat_quotient(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "quotient", argc, 2, 2));
  OT_TRY(need_int(vm, "quotient", ARG(0)));
  OT_TRY(need_int(vm, "quotient", ARG(1)));
  if (ARG(1).i == 0) return raise_error(vm, "quotient: division by zero");
  return int_v(idiv_wrap(ARG(0).i, ARG(1).i));
}

static Value nat_remainder(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "remainder", argc, 2, 2));
  OT_TRY(need_int(vm, "remainder", ARG(0)));
  OT_TRY(need_int(vm, "remainder", ARG(1)));
  if (ARG(1).i == 0) return raise_error(vm, "remainder: division by zero");
  return int_v(a_rem(ARG(0).i, ARG(1).i));  // sign of the dividend (C semantics)
}

static Value nat_modulo(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "modulo", argc, 2, 2));
  OT_TRY(need_int(vm, "modulo", ARG(0)));
  OT_TRY(need_int(vm, "modulo", ARG(1)));
  i64 b = ARG(1).i;
  if (b == 0) return raise_error(vm, "modulo: division by zero");
  i64 r = a_rem(ARG(0).i, b);
  if (r != 0 && ((r < 0) != (b < 0))) r += b;  // sign of the divisor
  return int_v(r);
}

static Value nat_abs(State* vm, u32 base, u32 argc) {
  OT_TRY(need_num_args(vm, "abs", base, argc, 1, 1));
  Value v = ARG(0);
  if (v.tag == Tag_Float) return float_v(fabs(v.f));
  return v.i < 0 ? int_v((i64)(0 - (u64)v.i)) : v;  // INT64_MIN wraps to itself
}

// numeric compare: -1/0/1; both ints compared exactly, else as doubles
static int num_cmp(Value a, Value b) {
  if (a.tag == Tag_Int && b.tag == Tag_Int) return a.i < b.i ? -1 : a.i > b.i ? 1 : 0;
  f64 x = as_f(a), y = as_f(b);
  return x < y ? -1 : x > y ? 1 : 0;
}

static Value nat_min(State* vm, u32 base, u32 argc) {
  OT_TRY(need_num_args(vm, "min", base, argc, 1, UINT32_MAX));
  Value best = ARG(0);
  for (u32 i = 1; i < argc; i++)
    if (num_cmp(ARG(i), best) < 0) best = ARG(i);
  return best;
}

static Value nat_max(State* vm, u32 base, u32 argc) {
  OT_TRY(need_num_args(vm, "max", base, argc, 1, UINT32_MAX));
  Value best = ARG(0);
  for (u32 i = 1; i < argc; i++)
    if (num_cmp(ARG(i), best) > 0) best = ARG(i);
  return best;
}

// floor/ceiling/round: identity on ints; float -> int, out-of-range errors.
static Value float_to_int(State* vm, const char* who, f64 f) {
  // exactly representable i64 bounds: [-2^63, 2^63)
  if (!(f >= -9223372036854775808.0 && f < 9223372036854775808.0))
    return raise_error(vm, "%s: result outside int range", who);
  return int_v((i64)f);
}

static Value round_like(State* vm, u32 base, u32 argc, const char* who, f64 (*op)(f64)) {
  OT_TRY(need_num_args(vm, who, base, argc, 1, 1));
  Value v = ARG(0);
  if (v.tag == Tag_Int) return v;
  return float_to_int(vm, who, op(v.f));
}

#define OT_ROUND_NATIVE(cname, lname, op)                                                          \
  static Value cname(State* vm, u32 base, u32 argc) {                                              \
    return round_like(vm, base, argc, lname, op);                                                  \
  }
OT_ROUND_NATIVE(nat_floor, "floor", floor)
OT_ROUND_NATIVE(nat_ceiling, "ceiling", ceil)
OT_ROUND_NATIVE(nat_round, "round", round)
OT_ROUND_NATIVE(nat_truncate, "truncate", trunc)
#undef OT_ROUND_NATIVE

static Value unary_float(State* vm, u32 base, u32 argc, const char* who, f64 (*op)(f64)) {
  OT_TRY(need_num_args(vm, who, base, argc, 1, 1));
  return float_v(op(as_f(ARG(0))));
}

#define OT_UNARY_FLOAT_NATIVE(cname, lname, op)                                                    \
  static Value cname(State* vm, u32 base, u32 argc) {                                              \
    return unary_float(vm, base, argc, lname, op);                                                 \
  }
OT_UNARY_FLOAT_NATIVE(nat_sqrt, "sqrt", sqrt)
OT_UNARY_FLOAT_NATIVE(nat_exp, "exp", exp)
OT_UNARY_FLOAT_NATIVE(nat_log, "log", log)
OT_UNARY_FLOAT_NATIVE(nat_sin, "sin", sin)
OT_UNARY_FLOAT_NATIVE(nat_cos, "cos", cos)
OT_UNARY_FLOAT_NATIVE(nat_tan, "tan", tan)
OT_UNARY_FLOAT_NATIVE(nat_asin, "asin", asin)
OT_UNARY_FLOAT_NATIVE(nat_acos, "acos", acos)
#undef OT_UNARY_FLOAT_NATIVE

static Value nat_atan(State* vm, u32 base, u32 argc) {
  OT_TRY(need_num_args(vm, "atan", base, argc, 1, 2));
  return float_v(argc == 1 ? atan(as_f(ARG(0))) : atan2(as_f(ARG(0)), as_f(ARG(1))));
}

static Value nat_expt(State* vm, u32 base, u32 argc) {
  OT_TRY(need_num_args(vm, "expt", base, argc, 2, 2));
  if (ARG(0).tag == Tag_Int && ARG(1).tag == Tag_Int && ARG(1).i >= 0) {
    u64 factor = (u64)ARG(0).i;
    u64 result = 1;
    u64 exponent = (u64)ARG(1).i;
    while (exponent) {
      if (exponent & 1) result *= factor;
      exponent >>= 1;
      if (exponent) factor *= factor;
    }
    return int_v((i64)result);
  }
  return float_v(pow(as_f(ARG(0)), as_f(ARG(1))));
}

static Value nat_exact(State* vm, u32 base, u32 argc) {
  OT_TRY(need_num_args(vm, "exact", base, argc, 1, 1));
  if (ARG(0).tag == Tag_Int) return ARG(0);
  if (!isfinite(ARG(0).f) || trunc(ARG(0).f) != ARG(0).f)
    return raise_error(vm, "exact: expected an integer-valued finite number");
  return float_to_int(vm, "exact", ARG(0).f);
}

static Value nat_inexact(State* vm, u32 base, u32 argc) {
  OT_TRY(need_num_args(vm, "inexact", base, argc, 1, 1));
  return float_v(as_f(ARG(0)));
}

#define OT_NUM_PRED(cname, lname, ...)                                                             \
  static Value cname(State* vm, u32 base, u32 argc) {                                              \
    OT_TRY(need_num_args(vm, lname, base, argc, 1, 1));                                            \
    return bool_v((__VA_ARGS__));                                                                  \
  }
OT_NUM_PRED(nat_exactp, "exact?", ARG(0).tag == Tag_Int)
OT_NUM_PRED(nat_inexactp, "inexact?", ARG(0).tag == Tag_Float)
OT_NUM_PRED(nat_integerp, "integer?",
            ARG(0).tag == Tag_Int ||
                (ARG(0).tag == Tag_Float && isfinite(ARG(0).f) && trunc(ARG(0).f) == ARG(0).f))
OT_NUM_PRED(nat_nanp, "nan?", ARG(0).tag == Tag_Float && isnan(ARG(0).f))
OT_NUM_PRED(nat_infinitep, "infinite?", ARG(0).tag == Tag_Float && isinf(ARG(0).f))
OT_NUM_PRED(nat_finitep, "finite?", ARG(0).tag == Tag_Int || isfinite(ARG(0).f))
#undef OT_NUM_PRED

// comparison chains
static Value chain(State* vm, u32 base, u32 argc, const char* who, bool (*ok)(int cmp)) {
  OT_TRY(need_num_args(vm, who, base, argc, 2, UINT32_MAX));
  for (u32 i = 0; i + 1 < argc; i++) {
    // NaN: all comparisons (and =) are false
    if (ARG(i).tag == Tag_Float && isnan(ARG(i).f)) return bool_v(false);
    if (ARG(i + 1).tag == Tag_Float && isnan(ARG(i + 1).f)) return bool_v(false);
    if (!ok(num_cmp(ARG(i), ARG(i + 1)))) return bool_v(false);
  }
  return bool_v(true);
}

static bool ok_eq(int c) { return c == 0; }
static bool ok_lt(int c) { return c < 0; }
static bool ok_gt(int c) { return c > 0; }
static bool ok_le(int c) { return c <= 0; }
static bool ok_ge(int c) { return c >= 0; }

static Value nat_num_eq(State* vm, u32 base, u32 argc) { return chain(vm, base, argc, "=", ok_eq); }
static Value nat_lt(State* vm, u32 base, u32 argc) { return chain(vm, base, argc, "<", ok_lt); }
static Value nat_gt(State* vm, u32 base, u32 argc) { return chain(vm, base, argc, ">", ok_gt); }
static Value nat_le(State* vm, u32 base, u32 argc) { return chain(vm, base, argc, "<=", ok_le); }
static Value nat_ge(State* vm, u32 base, u32 argc) { return chain(vm, base, argc, ">=", ok_ge); }

static Value nat_inc(State* vm, u32 base, u32 argc) {
  OT_TRY(need_num_args(vm, "inc", base, argc, 1, 1));
  Value v = ARG(0);
  return v.tag == Tag_Int ? int_v((i64)((u64)v.i + 1)) : float_v(v.f + 1.0);
}
static Value nat_dec(State* vm, u32 base, u32 argc) {
  OT_TRY(need_num_args(vm, "dec", base, argc, 1, 1));
  Value v = ARG(0);
  return v.tag == Tag_Int ? int_v((i64)((u64)v.i - 1)) : float_v(v.f - 1.0);
}

static Value sign_test(State* vm, u32 base, u32 argc, const char* who, int want) {
  OT_TRY(need_num_args(vm, who, base, argc, 1, 1));
  Value v = ARG(0);
  int s;
  if (v.tag == Tag_Int) s = v.i < 0 ? -1 : v.i > 0 ? 1 : 0;
  else s = v.f < 0.0 ? -1 : v.f > 0.0 ? 1 : (v.f == 0.0 ? 0 : 2);  // NaN -> 2
  return bool_v(s == want);
}

static Value nat_zerop(State* vm, u32 base, u32 argc) {
  return sign_test(vm, base, argc, "zero?", 0);
}
static Value nat_posp(State* vm, u32 base, u32 argc) {
  return sign_test(vm, base, argc, "pos?", 1);
}
static Value nat_negp(State* vm, u32 base, u32 argc) {
  return sign_test(vm, base, argc, "neg?", -1);
}

static Value nat_evenp(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "even?", argc, 1, 1));
  OT_TRY(need_int(vm, "even?", ARG(0)));
  return bool_v((ARG(0).i & 1) == 0);
}
static Value nat_oddp(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "odd?", argc, 1, 1));
  OT_TRY(need_int(vm, "odd?", ARG(0)));
  return bool_v((ARG(0).i & 1) != 0);
}

void register_arith(State* vm) {
  def_native(vm, "+", nat_add);
  def_native(vm, "*", nat_mul);
  def_native(vm, "-", nat_sub);
  def_native(vm, "/", nat_div);
  def_native(vm, "quotient", nat_quotient);
  def_native(vm, "remainder", nat_remainder);
  def_native(vm, "modulo", nat_modulo);
  def_native(vm, "abs", nat_abs);
  def_native(vm, "min", nat_min);
  def_native(vm, "max", nat_max);
  def_native(vm, "floor", nat_floor);
  def_native(vm, "ceiling", nat_ceiling);
  def_native(vm, "round", nat_round);
  def_native(vm, "truncate", nat_truncate);
  def_native(vm, "sqrt", nat_sqrt);
  def_native(vm, "exp", nat_exp);
  def_native(vm, "log", nat_log);
  def_native(vm, "sin", nat_sin);
  def_native(vm, "cos", nat_cos);
  def_native(vm, "tan", nat_tan);
  def_native(vm, "asin", nat_asin);
  def_native(vm, "acos", nat_acos);
  def_native(vm, "atan", nat_atan);
  def_native(vm, "expt", nat_expt);
  def_native(vm, "exact", nat_exact);
  def_native(vm, "inexact", nat_inexact);
  def_native(vm, "exact?", nat_exactp);
  def_native(vm, "inexact?", nat_inexactp);
  def_native(vm, "integer?", nat_integerp);
  def_native(vm, "nan?", nat_nanp);
  def_native(vm, "infinite?", nat_infinitep);
  def_native(vm, "finite?", nat_finitep);
  def_native(vm, "=", nat_num_eq);
  def_native(vm, "<", nat_lt);
  def_native(vm, ">", nat_gt);
  def_native(vm, "<=", nat_le);
  def_native(vm, ">=", nat_ge);
  def_native(vm, "inc", nat_inc);
  def_native(vm, "dec", nat_dec);
  def_native(vm, "zero?", nat_zerop);
  def_native(vm, "pos?", nat_posp);
  def_native(vm, "neg?", nat_negp);
  def_native(vm, "even?", nat_evenp);
  def_native(vm, "odd?", nat_oddp);
}
