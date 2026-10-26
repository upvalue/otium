#include "heap.h"
#include <assert.h>
#include <stdio.h>

static void check(intptr_t n) {
  volatile ot_value encoded = ot_int(n);
  ot_value v = encoded;
  assert(ot_is_int(v));
  assert(!value_is_ptr(v));
  assert(ot_to_int(v) == n);
}

static void arithmetic(intptr_t a, intptr_t b) {
  const intptr_t low = INTPTR_MIN / 2, high = INTPTR_MAX / 2;
  volatile ot_value left = ot_int(a), right = ot_int(b);
  ot_value out;
  bool add_ok = !(b > 0 ? a > high - b : b < 0 && a < low - b);
  assert(value_int_add(left, right, &out) == add_ok);
  if (add_ok) assert(ot_to_int(out) == a + b);
  bool sub_ok = !(b < 0 ? a > high + b : b > 0 && a < low + b);
  assert(value_int_sub(left, right, &out) == sub_ok);
  if (sub_ok) assert(ot_to_int(out) == a - b);
  bool mul_ok = true;
  if (a > 0) mul_ok = b > 0 ? a <= high / b : b >= low / a;
  else if (a < 0) mul_ok = b > 0 ? a >= low / b : b == 0 || a >= high / b;
  assert(value_int_mul(left, right, &out) == mul_ok);
  if (mul_ok) assert(ot_to_int(out) == a * b);
}

int main(void) {
  check(INTPTR_MIN / 2);
  check(INTPTR_MIN / 2 + 1);
  check(INTPTR_MAX / 2);
  check(INTPTR_MAX / 2 - 1);
  for (intptr_t n = -65536; n <= 65536; n++) check(n);
  for (uintptr_t bit = 1; bit <= (uintptr_t)INTPTR_MAX / 2; bit <<= 1) {
    check((intptr_t)bit);
    check(-(intptr_t)bit);
    check((intptr_t)bit - 1);
    check(-(intptr_t)bit - 1);
  }
  const intptr_t edges[] = {INTPTR_MIN / 2,     INTPTR_MIN / 2 + 1, -2, -1, 0, 1, 2,
                            INTPTR_MAX / 2 - 1, INTPTR_MAX / 2};
  for (size_t i = 0; i < sizeof(edges) / sizeof(edges[0]); i++)
    for (size_t j = 0; j < sizeof(edges) / sizeof(edges[0]); j++) arithmetic(edges[i], edges[j]);
  uintptr_t random = 0x9e3779b9u;
  intptr_t previous = 0;
  for (size_t i = 0; i < 1000000; i++) {
    random ^= random << 13;
    random ^= random >> 7;
    random ^= random << 17;
    intptr_t n = (intptr_t)(random & ((uintptr_t)INTPTR_MAX / 2));
    check(n);
    check(-n);
    intptr_t operand = (random & 1) ? n : -n;
    arithmetic(operand, previous);
    previous = operand;
  }
  puts("integer representation tests passed");
}
