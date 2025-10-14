// std.cpp - standard library like functions

#include "otcommon.h"

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

void oprintf(const char *fmt, ...) {
  va_list vargs;
  va_start(vargs, fmt);

  while (*fmt) {
    if (*fmt == '%') {
      fmt++;          // Skip '%'
      switch (*fmt) { // Read the next character
      case '\0':      // '%' at the end of the format string
        oputchar('%');
        goto end;
      case '%': // Print '%'
        oputchar('%');
        break;
      case 's': { // Print a NULL-terminated string.
        const char *s = va_arg(vargs, const char *);
        while (*s) {
          oputchar(*s);
          s++;
        }
        break;
      }
      case 'd': { // Print an integer in decimal.
        int value = va_arg(vargs, int);
        unsigned magnitude =
            value; // https://github.com/nuta/operating-system-in-1000-lines/issues/64
        if (value < 0) {
          oputchar('-');
          magnitude = -magnitude;
        }

        unsigned divisor = 1;
        while (magnitude / divisor > 9)
          divisor *= 10;

        while (divisor > 0) {
          oputchar('0' + magnitude / divisor);
          magnitude %= divisor;
          divisor /= 10;
        }

        break;
      }
      case 'x': { // Print an integer in hexadecimal.
        unsigned value = va_arg(vargs, unsigned);
        for (int i = 7; i >= 0; i--) {
          unsigned nibble = (value >> (i * 4)) & 0xf;
          oputchar("0123456789abcdef"[nibble]);
        }
      }
      }
    } else {
      oputchar(*fmt);
    }

    fmt++;
  }

end:
  va_end(vargs);
}