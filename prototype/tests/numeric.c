#include "helpers.h"
#include "internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

TEST(float_formatting_round_trips) {
  uint64_t bits = 0x123456789abcdef0ULL;
  for (size_t i = 0; i < 10000; i++) {
    bits ^= bits << 13;
    bits ^= bits >> 7;
    bits ^= bits << 17;
    double value;
    memcpy(&value, &bits, sizeof(value));
    if (!isfinite(value)) continue;
    char buffer[64];
    size_t n = oti_format_float(value, buffer, sizeof(buffer));
    CHECK(n < sizeof(buffer));
    char* end;
    double parsed = strtod(buffer, &end);
    CHECK(!*end && !memcmp(&value, &parsed, sizeof(value)));
  }
}

TEST(integer_and_float_boundaries) {
  ot_runtime* rt = new_runtime(32768, 8192);
  ot_gc_stress(rt, true);
  const char* checks[] = {
      "(= (+ 4611686018427387903 1) -4611686018427387904)",
      "(= (- -4611686018427387904 1) 4611686018427387903)",
      "(= (* 4611686018427387903 4) -4)",
      "(= (abs -4611686018427387904) -4611686018427387904)",
      "(= (quotient -4611686018427387904 -1) -4611686018427387904)",
      "(= (/ -4611686018427387904 -1) -4611686018427387904)",
      "(= (expt 2 62) -4611686018427387904)",
      "(= (expt 2 63) 0)",
      "(= (expt 0 0) 1)",
      "(= (expt -1 4611686018427387903) -1)",
      "(= (min 9007199254740993 9007199254740992) 9007199254740992)",
      "(= (max 9007199254740992 9007199254740993) 9007199254740993)",
      "(and (float? (min 1 2.0)) (float? (max 2 1.0)))",
      "(and (integer? 2.0) (not (int? 2.0)) (not (integer? 2.5)) (not (integer? nil)))",
      "(and (nan? (sqrt -1)) (not (integer? (sqrt -1))))",
      "(and (infinite? (exp 1000)) (not (finite? (exp 1000))) (finite? 42))",
      "(and (nan? (min 1 (sqrt -1))) (nan? (max (sqrt -1) 1)))",
      "(= (exact 42.0) 42)",
      "(= (inexact 42) 42.0)",
      "(= (bitwise-arithmetic-shift -1 62) -4611686018427387904)",
      "(= (bitwise-arithmetic-shift -3 -1) -2)",
      "(= (bitwise-arithmetic-shift -4611686018427387904 -4611686018427387904) -1)",
      "(= (round -2.5) -3)",
      "(= (round 2.5) 3)",
      "(and (float? (/ 1)) (= (/ 1) 1.0))",
      "(= (atan 0 -1) (acos -1))"};
  for (size_t i = 0; i < sizeof(checks) / sizeof(*checks); i++) check_true(rt, checks[i]);
  const char* bad[] = {"(exact 2.5)",
                       "(exact (exp 1000))",
                       "(exact (sqrt -1))",
                       "(exact 4611686018427387904.0)",
                       "(floor 4611686018427387904.0)",
                       "(floor -4611686018427388928.0)",
                       "(quotient 1 0)",
                       "(quotient 1.0 1)",
                       "(min)",
                       "(max 1 #f)",
                       "(exact? #f)",
                       "(atan)",
                       "(atan 1 2 3)",
                       "(bitwise-and 1.0)",
                       "(bitwise-arithmetic-shift 1 62)",
                       "(bitwise-arithmetic-shift -1 63)",
                       "(bitwise-arithmetic-shift 1 4611686018427387903)"};
  /* Any message will do; check_error also checks that the runtime recovers. */
  for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) check_error(rt, bad[i], "");
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(float_formatting_round_trips),
    TEST_ENTRY(integer_and_float_boundaries),
};

TEST_MAIN(tests)
