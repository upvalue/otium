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
int vsnprintf_(char *buffer, size_t count, const char *format, va_list va);

// Dummy _putchar for mpaland printf (not used, but required for linking)
void _putchar(char character) {
  (void)character; // Unused
}
}

#endif // USE_PICOLIBC

char _ot_scratch_buffer[OT_PAGE_SIZE];
extern "C" char *ot_scratch_buffer = _ot_scratch_buffer;

// ============================================================================
// Printf functions
// ============================================================================

#if defined(USE_PICOLIBC)
// Use picolibc's tinystdio vsnprintf

int ovsnprintf(char *str, size_t size, const char *format, va_list args) { return vsnprintf(str, size, format, args); }

#else
// Use mpaland/printf implementation

int ovsnprintf(char *str, size_t size, const char *format, va_list args) { return vsnprintf_(str, size, format, args); }

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
      return BoolResult<int>::err(false); // Would overflow INT_MAX
    }
    if (sign == -1 && result > (2147483648LL - digit) / 10) {
      return BoolResult<int>::err(false); // Would overflow INT_MIN
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
