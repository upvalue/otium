// std.cpp - standard library like functions

#include "ot/common.h"

// Forward declarations from mpaland/printf (ot/shared/vendor/printf.c)
extern "C" {
int vsnprintf_(char* buffer, size_t count, const char* format, va_list va);

// Dummy _putchar for mpaland printf (not used, but required for linking)
void _putchar(char character) {
  (void)character; // Unused
}
}

char _ot_scratch_buffer[OT_PAGE_SIZE];
extern "C" char *ot_scratch_buffer = _ot_scratch_buffer;

void *omemset(void *buf, char c, size_t n) {
  uint8_t *p = (uint8_t *)buf;
  while (n--)
    *p++ = c;
  return buf;
}

void *memcpy(void *dst, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  while (n--)
    *d++ = *s++;
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

int ovsnprintf(char *str, size_t size, const char *format, va_list args) {
  // Use mpaland/printf implementation
  return vsnprintf_(str, size, format, args);
}

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
