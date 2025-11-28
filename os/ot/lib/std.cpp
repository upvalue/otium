// std.cpp - standard library like functions

#include "ot/common.h"

#if defined(USE_PICOLIBC)
// Picolibc provides all standard libc functions via tinystdio
#include <stdio.h>
#include <string.h>

// Define stdout for picolibc tinystdio
// The put function signature: int (*put)(char c, FILE *stream)
static int _ot_stdout_put(char c, FILE *stream) {
  (void)stream;
  return oputchar(c);
}

// Create the stdout FILE structure
static FILE _ot_stdout_file = FDEV_SETUP_STREAM(_ot_stdout_put, NULL, NULL, __SWR);

// Export as stdout (picolibc expects this)
FILE *const stdout = &_ot_stdout_file;

#else
// Non-picolibc builds: use mpaland/printf

// Forward declarations from mpaland/printf (ot/lib/vendor/printf.c)
extern "C" {
int vsnprintf_(char* buffer, size_t count, const char* format, va_list va);

// Dummy _putchar for mpaland printf (not used, but required for linking)
void _putchar(char character) {
  (void)character; // Unused
}
}

#endif // USE_PICOLIBC

char _ot_scratch_buffer[OT_PAGE_SIZE];
extern "C" char *ot_scratch_buffer = _ot_scratch_buffer;

// ============================================================================
// Standard libc implementations (only when NOT using picolibc or system libc)
// ============================================================================

#if !defined(USE_PICOLIBC) && !defined(OT_POSIX) && !defined(OT_ARCH_WASM)

void *memset(void *buf, int c, size_t n) {
  uint8_t *p = (uint8_t *)buf;
  while (n--)
    *p++ = (uint8_t)c;
  return buf;
}

void *memcpy(void *dst, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  while (n--)
    *d++ = *s++;
  return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;

  // If dst and src don't overlap, or dst is before src, copy forward
  if (d <= s || d >= s + n) {
    while (n--)
      *d++ = *s++;
  } else {
    // dst is after src and they overlap, copy backward
    d += n;
    s += n;
    while (n--)
      *--d = *--s;
  }

  return dst;
}

char *strcpy(char *dst, const char *src) {
  char *d = dst;
  while (*src)
    *d++ = *src++;
  *d = '\0';
  return dst;
}

size_t strlen(const char *s) {
  size_t len = 0;
  while (*s++)
    len++;
  return len;
}

int strcmp(const char *s1, const char *s2) {
  while (*s1 && *s1 == *s2) {
    s1++;
    s2++;
  }
  return *(const uint8_t *)s1 - *(const uint8_t *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
  if (n == 0)
    return 0;

  while (n > 0 && *s1 && *s2) {
    if (*s1 != *s2)
      return (unsigned char)*s1 - (unsigned char)*s2;
    s1++;
    s2++;
    n--;
  }

  if (n == 0)
    return 0;

  return (unsigned char)*s1 - (unsigned char)*s2;
}

int atoi(const char *s) {
  int result = 0;
  int sign = 1;

  // Handle sign
  if (*s == '-') {
    sign = -1;
    s++;
  } else if (*s == '+') {
    s++;
  }

  // Parse digits
  while (*s >= '0' && *s <= '9') {
    result = result * 10 + (*s++ - '0');
  }

  return sign * result;
}

int memcmp(const void *s1, const void *s2, size_t n) {
  const uint8_t *p1 = (const uint8_t *)s1;
  const uint8_t *p2 = (const uint8_t *)s2;
  while (n--) {
    if (*p1 != *p2)
      return *p1 - *p2;
    p1++;
    p2++;
  }
  return 0;
}

#endif // !USE_PICOLIBC && !OT_POSIX && !OT_ARCH_WASM

// ============================================================================
// Otium-specific wrappers (always provided)
// ============================================================================

void *omemset(void *buf, char c, size_t n) {
  return memset(buf, (int)(unsigned char)c, n);
}

void *omemmove(void *dst, const void *src, size_t n) {
  return memmove(dst, src, n);
}

// ============================================================================
// Printf functions
// ============================================================================

#if defined(USE_PICOLIBC)
// Use picolibc's tinystdio vsnprintf

int ovsnprintf(char *str, size_t size, const char *format, va_list args) {
  return vsnprintf(str, size, format, args);
}

#else
// Use mpaland/printf implementation

int ovsnprintf(char *str, size_t size, const char *format, va_list args) {
  return vsnprintf_(str, size, format, args);
}

#endif // USE_PICOLIBC

int osnprintf(char *str, size_t size, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int r = ovsnprintf(str, size, format, args);
  va_end(args);
  return r;
}

void oprintf(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int len = ovsnprintf(ot_scratch_buffer, OT_PAGE_SIZE, fmt, args);
  va_end(args);
  oputsn(ot_scratch_buffer, len);
}

// Parse integer with error handling
BoolResult<int> parse_int(const char *s) {
  if (!s || *s == '\0') {
    return BoolResult<int>::err(false);
  }

  int result = 0;
  int sign = 1;
  const char *orig = s;

  // Handle sign
  if (*s == '-') {
    sign = -1;
    s++;
  } else if (*s == '+') {
    s++;
  }

  // Must have at least one digit after optional sign
  if (*s < '0' || *s > '9') {
    return BoolResult<int>::err(false);
  }

  // Parse digits
  while (*s >= '0' && *s <= '9') {
    int digit = *s - '0';

    // Check for overflow before multiplying
    if (sign == 1 && result > (2147483647 - digit) / 10) {
      return BoolResult<int>::err(false);  // Would overflow INT_MAX
    }
    if (sign == -1 && result > (2147483648LL - digit) / 10) {
      return BoolResult<int>::err(false);  // Would overflow INT_MIN
    }

    result = result * 10 + digit;
    s++;
  }

  // Should have consumed entire string
  if (*s != '\0') {
    return BoolResult<int>::err(false);
  }

  return BoolResult<int>::ok(sign * result);
}
