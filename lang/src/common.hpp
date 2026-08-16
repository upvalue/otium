// common.hpp — shared fundamental types and fatal/assert plumbing.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cstdio>

namespace ot {

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using f64 = double;

inline bool ascii_whitespace(u8 c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

// UTF-8 code points are counted as non-continuation bytes. Invalid input
// therefore degrades to a byte-ish count rather than raising an error.
inline u32 utf8_count(const char* bytes, u32 len) {
  u32 count = 0;
  for (u32 i = 0; i < len; i++)
    if (((u8)bytes[i] & 0xC0) != 0x80) count++;
  return count;
}

// Abort with a message. Host hook may replace this later.
[[noreturn]] inline void ot_fatal(const char* msg) {
  fprintf(stderr, "otium fatal: %s\n", msg ? msg : "(null)");
  abort();
}

}  // namespace ot

#ifndef NDEBUG
#define OT_ASSERT(cond)                                                                            \
  do {                                                                                             \
    if (!(cond)) ::ot::ot_fatal("assertion failed: " #cond);                                       \
  } while (0)
#else
#define OT_ASSERT(cond)                                                                            \
  do {                                                                                             \
    (void)sizeof(cond);                                                                            \
  } while (0)
#endif
