// builtins/arith.cpp — spec 10.1. Int math wraps two's-complement (done in
// u64), any float operand contaminates to float.
#include "../builtins.hpp"
#include "../state.hpp"
#include "../ns.hpp"
#include <cmath>

namespace ot {

static inline f64 as_f(Value v) { return v.tag == Tag::Int ? (f64)v.i : v.f; }

static bool any_float(State& vm, u32 base, u32 argc) {
  for (u32 i = 0; i < argc; i++)
    if (ARG(i).tag == Tag::Float) return true;
  return false;
}

Value nat_add(State& vm, u32 base, u32 argc) {
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

Value nat_mul(State& vm, u32 base, u32 argc) {
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

Value nat_sub(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "-", argc, 1, UINT32_MAX));
  OT_TRY(need_nums(vm, "-", base, argc));
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

Value nat_div(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "/", argc, 1, UINT32_MAX));
  OT_TRY(need_nums(vm, "/", base, argc));
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
    if (acc.tag == Tag::Int && b.tag == Tag::Int) {
      if (b.i == 0) return raise_error(vm, "/: division by zero");
      if (a_rem(acc.i, b.i) == 0) acc = int_v(idiv_wrap(acc.i, b.i));
      else acc = float_v((f64)acc.i / (f64)b.i);
    } else {
      acc = float_v(as_f(acc) / as_f(b));
    }
  }
  return acc;
}

static Value nat_quotient(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "quotient", argc, 2, 2));
  OT_TRY(need_int(vm, "quotient", ARG(0)));
  OT_TRY(need_int(vm, "quotient", ARG(1)));
  if (ARG(1).i == 0) return raise_error(vm, "quotient: division by zero");
  return int_v(idiv_wrap(ARG(0).i, ARG(1).i));
}

static Value nat_remainder(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "remainder", argc, 2, 2));
  OT_TRY(need_int(vm, "remainder", ARG(0)));
  OT_TRY(need_int(vm, "remainder", ARG(1)));
  if (ARG(1).i == 0) return raise_error(vm, "remainder: division by zero");
  return int_v(a_rem(ARG(0).i, ARG(1).i));  // sign of the dividend (C semantics)
}

static Value nat_modulo(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "modulo", argc, 2, 2));
  OT_TRY(need_int(vm, "modulo", ARG(0)));
  OT_TRY(need_int(vm, "modulo", ARG(1)));
  i64 b = ARG(1).i;
  if (b == 0) return raise_error(vm, "modulo: division by zero");
  i64 r = a_rem(ARG(0).i, b);
  if (r != 0 && ((r < 0) != (b < 0))) r += b;  // sign of the divisor
  return int_v(r);
}

static Value nat_abs(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "abs", argc, 1, 1));
  OT_TRY(need_nums(vm, "abs", base, argc));
  Value v = ARG(0);
  if (v.tag == Tag::Float) return float_v(std::fabs(v.f));
  return v.i < 0 ? int_v((i64)(0 - (u64)v.i)) : v;  // INT64_MIN wraps to itself
}

// numeric compare: -1/0/1; both ints compared exactly, else as doubles
static int num_cmp(Value a, Value b) {
  if (a.tag == Tag::Int && b.tag == Tag::Int) return a.i < b.i ? -1 : a.i > b.i ? 1 : 0;
  f64 x = as_f(a), y = as_f(b);
  return x < y ? -1 : x > y ? 1 : 0;
}

static Value nat_min(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "min", argc, 1, UINT32_MAX));
  OT_TRY(need_nums(vm, "min", base, argc));
  Value best = ARG(0);
  for (u32 i = 1; i < argc; i++)
    if (num_cmp(ARG(i), best) < 0) best = ARG(i);
  return best;
}

static Value nat_max(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "max", argc, 1, UINT32_MAX));
  OT_TRY(need_nums(vm, "max", base, argc));
  Value best = ARG(0);
  for (u32 i = 1; i < argc; i++)
    if (num_cmp(ARG(i), best) > 0) best = ARG(i);
  return best;
}

// floor/ceiling/round: identity on ints; float -> int, out-of-range errors.
static Value float_to_int(State& vm, const char* who, f64 f) {
  // exactly representable i64 bounds: [-2^63, 2^63)
  if (!(f >= -9223372036854775808.0 && f < 9223372036854775808.0))
    return raise_error(vm, "%s: result outside int range", who);
  return int_v((i64)f);
}

static Value round_like(State& vm, u32 base, u32 argc, const char* who, f64 (*op)(f64)) {
  OT_TRY(need_argc(vm, who, argc, 1, 1));
  OT_TRY(need_nums(vm, who, base, argc));
  Value v = ARG(0);
  if (v.tag == Tag::Int) return v;
  return float_to_int(vm, who, op(v.f));
}

static f64 op_floor(f64 f) { return std::floor(f); }
static f64 op_ceil(f64 f) { return std::ceil(f); }
static f64 op_round(f64 f) { return std::round(f); }  // half away from zero

static Value nat_floor(State& vm, u32 base, u32 argc) {
  return round_like(vm, base, argc, "floor", op_floor);
}
static Value nat_ceiling(State& vm, u32 base, u32 argc) {
  return round_like(vm, base, argc, "ceiling", op_ceil);
}
static Value nat_round(State& vm, u32 base, u32 argc) {
  return round_like(vm, base, argc, "round", op_round);
}

// comparison chains
static Value chain(State& vm, u32 base, u32 argc, const char* who, bool (*ok)(int cmp)) {
  OT_TRY(need_argc(vm, who, argc, 2, UINT32_MAX));
  OT_TRY(need_nums(vm, who, base, argc));
  for (u32 i = 0; i + 1 < argc; i++) {
    // NaN: all comparisons (and =) are false
    if (ARG(i).tag == Tag::Float && std::isnan(ARG(i).f)) return bool_v(false);
    if (ARG(i + 1).tag == Tag::Float && std::isnan(ARG(i + 1).f)) return bool_v(false);
    if (!ok(num_cmp(ARG(i), ARG(i + 1)))) return bool_v(false);
  }
  return bool_v(true);
}

static bool ok_eq(int c) { return c == 0; }
static bool ok_lt(int c) { return c < 0; }
static bool ok_gt(int c) { return c > 0; }
static bool ok_le(int c) { return c <= 0; }
static bool ok_ge(int c) { return c >= 0; }

static Value nat_num_eq(State& vm, u32 base, u32 argc) { return chain(vm, base, argc, "=", ok_eq); }
static Value nat_lt(State& vm, u32 base, u32 argc) { return chain(vm, base, argc, "<", ok_lt); }
static Value nat_gt(State& vm, u32 base, u32 argc) { return chain(vm, base, argc, ">", ok_gt); }
static Value nat_le(State& vm, u32 base, u32 argc) { return chain(vm, base, argc, "<=", ok_le); }
static Value nat_ge(State& vm, u32 base, u32 argc) { return chain(vm, base, argc, ">=", ok_ge); }

static Value nat_inc(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "inc", argc, 1, 1));
  OT_TRY(need_nums(vm, "inc", base, argc));
  Value v = ARG(0);
  return v.tag == Tag::Int ? int_v((i64)((u64)v.i + 1)) : float_v(v.f + 1.0);
}
static Value nat_dec(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "dec", argc, 1, 1));
  OT_TRY(need_nums(vm, "dec", base, argc));
  Value v = ARG(0);
  return v.tag == Tag::Int ? int_v((i64)((u64)v.i - 1)) : float_v(v.f - 1.0);
}

static Value sign_test(State& vm, u32 base, u32 argc, const char* who, int want) {
  OT_TRY(need_argc(vm, who, argc, 1, 1));
  OT_TRY(need_nums(vm, who, base, argc));
  Value v = ARG(0);
  int s;
  if (v.tag == Tag::Int) s = v.i < 0 ? -1 : v.i > 0 ? 1 : 0;
  else s = v.f < 0.0 ? -1 : v.f > 0.0 ? 1 : (v.f == 0.0 ? 0 : 2);  // NaN -> 2
  return bool_v(s == want);
}

static Value nat_zerop(State& vm, u32 base, u32 argc) { return sign_test(vm, base, argc, "zero?", 0); }
static Value nat_posp(State& vm, u32 base, u32 argc) { return sign_test(vm, base, argc, "pos?", 1); }
static Value nat_negp(State& vm, u32 base, u32 argc) { return sign_test(vm, base, argc, "neg?", -1); }

static Value nat_evenp(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "even?", argc, 1, 1));
  OT_TRY(need_int(vm, "even?", ARG(0)));
  return bool_v((ARG(0).i & 1) == 0);
}
static Value nat_oddp(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "odd?", argc, 1, 1));
  OT_TRY(need_int(vm, "odd?", ARG(0)));
  return bool_v((ARG(0).i & 1) != 0);
}

void register_arith(State& vm) {
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

}  // namespace ot
