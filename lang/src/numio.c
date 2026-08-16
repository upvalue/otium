// numio.c — libc-backed float<->string seam (snprintf/strtod). Embedded hosts
// replace this file to drop the libc dependency; the formatting and parsing
// here are byte-for-byte the conventions the printer and reader always used.
#include "numio.h"
#include <math.h>

static u32 copy_out(const char* src, u32 n, char* out, u32 cap) {
  if (n > cap) n = cap;
  memcpy(out, src, n);
  return n;
}

// Shortest round-tripping float representation; integral values keep ".0".
u32 num_format_f64(f64 v, char* out, u32 cap) {
  char tmp[40];
  if (isnan(v)) return copy_out("nan", 3, out, cap);
  if (isinf(v)) return v < 0 ? copy_out("-inf", 4, out, cap) : copy_out("inf", 3, out, cap);
  int len = 0;
  // Integral values stay in decimal notation (spec: `1000.0`, not `1e+03`)
  // while the integer part is exactly representable.
  if (v == floor(v) && fabs(v) < 1e17) {
    len = snprintf(tmp, sizeof(tmp), "%.1f", v);
    if (strtod(tmp, nullptr) == v) return copy_out(tmp, (u32)len, out, cap);
  }
  for (int prec = 1; prec <= 17; prec++) {
    len = snprintf(tmp, sizeof(tmp), "%.*g", prec, v);
    if (strtod(tmp, nullptr) == v) break;
  }
  bool hasMark = false;
  for (int i = 0; i < len; i++)
    if (tmp[i] == '.' || tmp[i] == 'e' || tmp[i] == 'E') {
      hasMark = true;
      break;
    }
  if (!hasMark && len + 2 <= (int)sizeof(tmp)) {
    tmp[len++] = '.';
    tmp[len++] = '0';
  }
  return copy_out(tmp, (u32)len, out, cap);
}

u32 num_format_i64(i64 v, char* out, u32 cap) {
  char tmp[24];
  int len = snprintf(tmp, sizeof(tmp), "%lld", (long long)v);
  return copy_out(tmp, (u32)len, out, cap);
}

// float: NUL-terminate a copy, parse with strtod, require full consumption
bool num_parse_f64(const char* s, u32 len, f64* out) {
  char buf[64];
  if (len >= sizeof(buf)) return false;
  memcpy(buf, s, len);
  buf[len] = '\0';
  char* end = nullptr;
  f64 f = strtod(buf, &end);
  if (end != buf + len) return false;
  *out = f;
  return true;
}

// Decimal integer with optional sign; the overflow checks mirror the reader's
// hand-rolled accumulation (rejects out-of-range rather than wrapping).
bool num_parse_i64(const char* s, u32 len, i64* out) {
  u32 i = 0;
  bool neg = false;
  if (i < len && (s[i] == '+' || s[i] == '-')) {
    neg = s[i] == '-';
    i++;
  }
  if (i >= len) return false;
  u64 acc = 0;
  for (u32 j = i; j < len; j++) {
    if (s[j] < '0' || s[j] > '9') return false;
    u32 d = (u32)(s[j] - '0');
    if (acc > ((u64)0xFFFFFFFFFFFFFFFFull - d) / 10) return false;
    acc = acc * 10 + d;
  }
  u64 limit = neg ? (u64)1 << 63 : ((u64)1 << 63) - 1;
  if (acc > limit) return false;
  *out = neg ? (acc == ((u64)1 << 63) ? INT64_MIN : -(i64)acc) : (i64)acc;
  return true;
}
