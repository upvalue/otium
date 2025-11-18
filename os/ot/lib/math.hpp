// ot/lib/math.hpp - Freestanding math functions
#pragma once

#include <stdint.h>

// Freestanding math functions for environments without libm
// Simple implementations optimized for typical use cases

namespace ot {
namespace math {

// Constants
constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 2.0f * PI;
constexpr float HALF_PI = 0.5f * PI;

// Helper: reduce angle to [-PI, PI] range
inline float reduce_angle(float x) {
  // Quick range reduction for typical cases
  while (x > PI)
    x -= TWO_PI;
  while (x < -PI)
    x += TWO_PI;
  return x;
}

// Sine using Taylor series approximation
// Good accuracy for angles in [-PI, PI]
inline float ou_sinf(float x) {
  x = reduce_angle(x);

  // Taylor series: sin(x) = x - x^3/3! + x^5/5! - x^7/7! + x^9/9!
  float x2 = x * x;
  float x3 = x2 * x;
  float x5 = x3 * x2;
  float x7 = x5 * x2;
  float x9 = x7 * x2;

  return x - (x3 / 6.0f) + (x5 / 120.0f) - (x7 / 5040.0f) + (x9 / 362880.0f);
}

// Cosine using identity: cos(x) = sin(x + PI/2)
inline float ou_cosf(float x) { return ou_sinf(x + HALF_PI); }

// Could add more as needed:
// inline float ou_sqrtf(float x) { return __builtin_sqrtf(x); }
// inline float ou_fabsf(float x) { return __builtin_fabsf(x); }

} // namespace math
} // namespace ot

// Convenience: bring functions into global namespace for easier use
using ot::math::ou_cosf;
using ot::math::ou_sinf;
