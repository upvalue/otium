// builtins/arith.c — spec 10.1. Int math wraps two's-complement (done in
// u64), any float operand contaminates to float. Numbers are immediates, so
// locals holding computed Values are safe; argument reads go through slots.
#include "../builtins.h"
#include <math.h>

Value nat_add(State* vm, u32 base, u32 argc) {
  return ot_num_add_args(vm, base, argc);
}

Value nat_mul(State* vm, u32 base, u32 argc) {
  return ot_num_mul_args(vm, base, argc);
}

Value nat_sub(State* vm, u32 base, u32 argc) {
  return ot_num_sub_args(vm, base, argc);
}

// remainder with wrap safety (INT64_MIN % -1 is UB in C)
static i64 a_rem(i64 a, i64 b) { return b == -1 ? 0 : a % b; }

// Exact int division with wrap semantics for INT64_MIN / -1.
static i64 idiv_wrap(i64 a, i64 b) {
  if (b == -1) return (i64)(0 - (u64)a);  // avoids UB on INT64_MIN / -1
  return a / b;
}

static inline f64 imm_f(Value v) { return v.tag == Tag_Int ? (f64)v.i : v.f; }

Value nat_div(State* vm, u32 base, u32 argc) {
  OT_TRY(need_num_args(vm, "/", base, argc, 1, UINT32_MAX));
  // (/ n) = 1/n. The accumulator is an int or float immediate.
  u32 first = 0;
  Value acc;
  if (argc == 1) {
    acc = int_v(1);
  } else {
    acc = ot_tag(vm, ARG(0)) == Tag_Int ? int_v(ot_int(vm, ARG(0)))
                                        : float_v(ot_float(vm, ARG(0)));
    first = 1;
  }
  for (u32 i = first; i < argc; i++) {
    if (acc.tag == Tag_Int && ot_tag(vm, ARG(i)) == Tag_Int) {
      i64 b = ot_int(vm, ARG(i));
      if (b == 0) return raise_error(vm, "/: division by zero");
      if (a_rem(acc.i, b) == 0) acc = int_v(idiv_wrap(acc.i, b));
      else acc = float_v((f64)acc.i / (f64)b);
    } else {
      acc = float_v(imm_f(acc) / ot_num(vm, ARG(i)));
    }
  }
  return acc;
}

static Value nat_quotient(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "quotient", argc, 2, 2));
  OT_TRY(need_int(vm, "quotient", ARG(0)));
  OT_TRY(need_int(vm, "quotient", ARG(1)));
  if (ot_int(vm, ARG(1)) == 0) return raise_error(vm, "quotient: division by zero");
  return int_v(idiv_wrap(ot_int(vm, ARG(0)), ot_int(vm, ARG(1))));
}

static Value nat_remainder(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "remainder", argc, 2, 2));
  OT_TRY(need_int(vm, "remainder", ARG(0)));
  OT_TRY(need_int(vm, "remainder", ARG(1)));
  if (ot_int(vm, ARG(1)) == 0) return raise_error(vm, "remainder: division by zero");
  // sign of the dividend (C semantics)
  return int_v(a_rem(ot_int(vm, ARG(0)), ot_int(vm, ARG(1))));
}

static Value nat_modulo(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "modulo", argc, 2, 2));
  OT_TRY(need_int(vm, "modulo", ARG(0)));
  OT_TRY(need_int(vm, "modulo", ARG(1)));
  i64 b = ot_int(vm, ARG(1));
  if (b == 0) return raise_error(vm, "modulo: division by zero");
  i64 r = a_rem(ot_int(vm, ARG(0)), b);
  if (r != 0 && ((r < 0) != (b < 0))) r += b;  // sign of the divisor
  return int_v(r);
}

static Value nat_abs(State* vm, u32 base, u32 argc) {
  OT_TRY(need_num_args(vm, "abs", base, argc, 1, 1));
  if (ot_tag(vm, ARG(0)) == Tag_Float) return float_v(fabs(ot_float(vm, ARG(0))));
  i64 i = ot_int(vm, ARG(0));
  return i < 0 ? int_v((i64)(0 - (u64)i)) : int_v(i);  // INT64_MIN wraps to itself
}

// numeric compare: -1/0/1; both ints compared exactly, else as doubles
static int num_cmp(State* vm, Ref a, Ref b) {
  if (ot_tag(vm, a) == Tag_Int && ot_tag(vm, b) == Tag_Int) {
    i64 x = ot_int(vm, a), y = ot_int(vm, b);
    return x < y ? -1 : x > y ? 1 : 0;
  }
  f64 x = ot_num(vm, a), y = ot_num(vm, b);
  return x < y ? -1 : x > y ? 1 : 0;
}

static Value pick(State* vm, u32 base, u32 argc, const char* who, int wantSign) {
  OT_TRY(need_num_args(vm, who, base, argc, 1, UINT32_MAX));
  Ref best = ARG(0);
  for (u32 i = 1; i < argc; i++)
    if (num_cmp(vm, ARG(i), best) * wantSign > 0) best = ARG(i);
  return ot_ret(vm, best);
}

static Value nat_min(State* vm, u32 base, u32 argc) { return pick(vm, base, argc, "min", -1); }
static Value nat_max(State* vm, u32 base, u32 argc) { return pick(vm, base, argc, "max", 1); }

// floor/ceiling/round: identity on ints; float -> int, out-of-range errors.
static Value float_to_int(State* vm, const char* who, f64 f) {
  // exactly representable i64 bounds: [-2^63, 2^63)
  if (!(f >= -9223372036854775808.0 && f < 9223372036854775808.0))
    return raise_error(vm, "%s: result outside int range", who);
  return int_v((i64)f);
}

static Value round_like(State* vm, u32 base, u32 argc, const char* who, f64 (*op)(f64)) {
  OT_TRY(need_num_args(vm, who, base, argc, 1, 1));
  if (ot_tag(vm, ARG(0)) == Tag_Int) return int_v(ot_int(vm, ARG(0)));
  return float_to_int(vm, who, op(ot_float(vm, ARG(0))));
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
  return float_v(op(ot_num(vm, ARG(0))));
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
  return float_v(argc == 1 ? atan(ot_num(vm, ARG(0)))
                           : atan2(ot_num(vm, ARG(0)), ot_num(vm, ARG(1))));
}

static Value nat_expt(State* vm, u32 base, u32 argc) {
  OT_TRY(need_num_args(vm, "expt", base, argc, 2, 2));
  if (ot_tag(vm, ARG(0)) == Tag_Int && ot_tag(vm, ARG(1)) == Tag_Int &&
      ot_int(vm, ARG(1)) >= 0) {
    u64 factor = (u64)ot_int(vm, ARG(0));
    u64 result = 1;
    u64 exponent = (u64)ot_int(vm, ARG(1));
    while (exponent) {
      if (exponent & 1) result *= factor;
      exponent >>= 1;
      if (exponent) factor *= factor;
    }
    return int_v((i64)result);
  }
  return float_v(pow(ot_num(vm, ARG(0)), ot_num(vm, ARG(1))));
}

static Value nat_exact(State* vm, u32 base, u32 argc) {
  OT_TRY(need_num_args(vm, "exact", base, argc, 1, 1));
  if (ot_tag(vm, ARG(0)) == Tag_Int) return int_v(ot_int(vm, ARG(0)));
  f64 f = ot_float(vm, ARG(0));
  if (!isfinite(f) || trunc(f) != f)
    return raise_error(vm, "exact: expected an integer-valued finite number");
  return float_to_int(vm, "exact", f);
}

static Value nat_inexact(State* vm, u32 base, u32 argc) {
  OT_TRY(need_num_args(vm, "inexact", base, argc, 1, 1));
  return float_v(ot_num(vm, ARG(0)));
}

#define OT_NUM_PRED(cname, lname, ...)                                                             \
  static Value cname(State* vm, u32 base, u32 argc) {                                              \
    OT_TRY(need_num_args(vm, lname, base, argc, 1, 1));                                            \
    Tag t = ot_tag(vm, ARG(0));                                                                    \
    (void)t;                                                                                       \
    return bool_v((__VA_ARGS__));                                                                  \
  }
OT_NUM_PRED(nat_exactp, "exact?", t == Tag_Int)
OT_NUM_PRED(nat_inexactp, "inexact?", t == Tag_Float)
OT_NUM_PRED(nat_integerp, "integer?",
            t == Tag_Int || (isfinite(ot_float(vm, ARG(0))) &&
                             trunc(ot_float(vm, ARG(0))) == ot_float(vm, ARG(0))))
OT_NUM_PRED(nat_nanp, "nan?", t == Tag_Float && isnan(ot_float(vm, ARG(0))))
OT_NUM_PRED(nat_infinitep, "infinite?", t == Tag_Float && isinf(ot_float(vm, ARG(0))))
OT_NUM_PRED(nat_finitep, "finite?", t == Tag_Int || isfinite(ot_float(vm, ARG(0))))
#undef OT_NUM_PRED

// Comparison chains stay behind the slot boundary because these tiny natives
// otherwise pay several out-of-line accessor calls per argument.
static Value nat_num_eq(State* vm, u32 base, u32 argc) {
  return ot_num_compare_args(vm, base, argc, "=", OtNumCompare_Eq);
}
static Value nat_lt(State* vm, u32 base, u32 argc) {
  return ot_num_compare_args(vm, base, argc, "<", OtNumCompare_Lt);
}
static Value nat_gt(State* vm, u32 base, u32 argc) {
  return ot_num_compare_args(vm, base, argc, ">", OtNumCompare_Gt);
}
static Value nat_le(State* vm, u32 base, u32 argc) {
  return ot_num_compare_args(vm, base, argc, "<=", OtNumCompare_Le);
}
static Value nat_ge(State* vm, u32 base, u32 argc) {
  return ot_num_compare_args(vm, base, argc, ">=", OtNumCompare_Ge);
}

static Value nat_inc(State* vm, u32 base, u32 argc) {
  OT_TRY(need_num_args(vm, "inc", base, argc, 1, 1));
  if (ot_tag(vm, ARG(0)) == Tag_Int) return int_v((i64)((u64)ot_int(vm, ARG(0)) + 1));
  return float_v(ot_float(vm, ARG(0)) + 1.0);
}
static Value nat_dec(State* vm, u32 base, u32 argc) {
  OT_TRY(need_num_args(vm, "dec", base, argc, 1, 1));
  if (ot_tag(vm, ARG(0)) == Tag_Int) return int_v((i64)((u64)ot_int(vm, ARG(0)) - 1));
  return float_v(ot_float(vm, ARG(0)) - 1.0);
}

static Value sign_test(State* vm, u32 base, u32 argc, const char* who, int want) {
  OT_TRY(need_num_args(vm, who, base, argc, 1, 1));
  int s;
  if (ot_tag(vm, ARG(0)) == Tag_Int) {
    i64 i = ot_int(vm, ARG(0));
    s = i < 0 ? -1 : i > 0 ? 1 : 0;
  } else {
    f64 f = ot_float(vm, ARG(0));
    s = f < 0.0 ? -1 : f > 0.0 ? 1 : (f == 0.0 ? 0 : 2);  // NaN -> 2
  }
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
  return bool_v((ot_int(vm, ARG(0)) & 1) == 0);
}
static Value nat_oddp(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "odd?", argc, 1, 1));
  OT_TRY(need_int(vm, "odd?", ARG(0)));
  return bool_v((ot_int(vm, ARG(0)) & 1) != 0);
}

void register_arith(State* vm) {
  ot_def_native(vm, "+", nat_add);
  ot_def_native(vm, "*", nat_mul);
  ot_def_native(vm, "-", nat_sub);
  ot_def_native(vm, "/", nat_div);
  ot_def_native(vm, "quotient", nat_quotient);
  ot_def_native(vm, "remainder", nat_remainder);
  ot_def_native(vm, "modulo", nat_modulo);
  ot_def_native(vm, "abs", nat_abs);
  ot_def_native(vm, "min", nat_min);
  ot_def_native(vm, "max", nat_max);
  ot_def_native(vm, "floor", nat_floor);
  ot_def_native(vm, "ceiling", nat_ceiling);
  ot_def_native(vm, "round", nat_round);
  ot_def_native(vm, "truncate", nat_truncate);
  ot_def_native(vm, "sqrt", nat_sqrt);
  ot_def_native(vm, "exp", nat_exp);
  ot_def_native(vm, "log", nat_log);
  ot_def_native(vm, "sin", nat_sin);
  ot_def_native(vm, "cos", nat_cos);
  ot_def_native(vm, "tan", nat_tan);
  ot_def_native(vm, "asin", nat_asin);
  ot_def_native(vm, "acos", nat_acos);
  ot_def_native(vm, "atan", nat_atan);
  ot_def_native(vm, "expt", nat_expt);
  ot_def_native(vm, "exact", nat_exact);
  ot_def_native(vm, "inexact", nat_inexact);
  ot_def_native(vm, "exact?", nat_exactp);
  ot_def_native(vm, "inexact?", nat_inexactp);
  ot_def_native(vm, "integer?", nat_integerp);
  ot_def_native(vm, "nan?", nat_nanp);
  ot_def_native(vm, "infinite?", nat_infinitep);
  ot_def_native(vm, "finite?", nat_finitep);
  ot_def_native(vm, "=", nat_num_eq);
  ot_def_native(vm, "<", nat_lt);
  ot_def_native(vm, ">", nat_gt);
  ot_def_native(vm, "<=", nat_le);
  ot_def_native(vm, ">=", nat_ge);
  ot_def_native(vm, "inc", nat_inc);
  ot_def_native(vm, "dec", nat_dec);
  ot_def_native(vm, "zero?", nat_zerop);
  ot_def_native(vm, "pos?", nat_posp);
  ot_def_native(vm, "neg?", nat_negp);
  ot_def_native(vm, "even?", nat_evenp);
  ot_def_native(vm, "odd?", nat_oddp);
}
