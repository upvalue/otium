// common.h — shared fundamental types, allocator/clock seams, fatal/assert plumbing.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef double f64;

static inline bool ascii_whitespace(u8 c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

// UTF-8 code points are counted as non-continuation bytes. Invalid input
// therefore degrades to a byte-ish count rather than raising an error.
static inline u32 utf8_count(const char* bytes, u32 len) {
  u32 count = 0;
  for (u32 i = 0; i < len; i++)
    if (((u8)bytes[i] & 0xC0) != 0x80) count++;
  return count;
}

// Abort with a message. Host hook may replace this later.
[[noreturn]] static inline void ot_fatal(const char* msg) {
  fprintf(stderr, "otium fatal: %s\n", msg ? msg : "(null)");
  abort();
}

static inline u32 grow_capacity(u32 cap, u32 need, const char* overflowMessage) {
  if (need <= cap) return cap;
  u32 grown = cap ? cap : 8;
  while (grown < need) {
    if (grown > UINT32_MAX / 2) ot_fatal(overflowMessage);
    grown *= 2;
  }
  return grown;
}

// Host allocator seam. All C-heap storage in the runtime (Vec, heap payloads,
// interned names) goes through these; the default is calloc-backed. Embedded
// hosts install their own before creating a State. Process-global by design:
// low-memory targets run one VM.
//
// Nothing may call malloc/calloc/realloc/free directly outside vec.c, which
// implements the default backend (enforced by tests/check_hygiene.py). Callers
// must not assume returned memory is zeroed: the default backend zeroes, but a
// host backend need not, and ot_realloc cannot zero the grown tail either way.
// Zero explicitly where it matters.
typedef struct OtAllocator {
  void* (*alloc)(void* ud, size_t n);
  void* (*realloc)(void* ud, void* p, size_t n);
  void (*free)(void* ud, void* p);
  void* ud;
} OtAllocator;

void ot_set_allocator(const OtAllocator* a);  // NULL restores malloc-backed default
void* ot_alloc(size_t n);
void* ot_realloc(void* p, size_t n);
void ot_free(void* p);

// Host clock seam (used by builtins/time.c). Defaults are POSIX
// clock_gettime-backed; bare-metal hosts install their own.
typedef struct OtClock {
  u64 (*monotonic_ns)(void);
  u64 (*wall_ns)(void);
} OtClock;

void ot_set_clock(const OtClock* c);  // NULL restores the POSIX default
u64 ot_monotonic_ns(void);
u64 ot_wall_ns(void);

#ifndef NDEBUG
#define OT_ASSERT(cond)                                                                            \
  do {                                                                                             \
    if (!(cond)) ot_fatal("assertion failed: " #cond);                                             \
  } while (0)
#else
#define OT_ASSERT(cond)                                                                            \
  do {                                                                                             \
    (void)sizeof(cond);                                                                            \
  } while (0)
#endif
