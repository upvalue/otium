#include "common.h"

#include <stdarg.h>

void memset(void *s, int c, size_t n) {
  for (size_t i = 0; i < n; i++) {
    ((char *)s)[i] = (char)c;
  }
}

void *memcpy(void *dest, const void *src, size_t n) {
  for (size_t i = 0; i < n; i++) {
    ((char *)dest)[i] = ((char *)src)[i];
  }
  return dest;
}

size_t strlen(const char *s) {
  size_t len = 0;
  while (*s++)
    len++;
  return len;
}

void printf(const char *fmt, ...) {
  va_list vargs;
  va_start(vargs, fmt);

  while (*fmt) {
    if (*fmt == '%') {
      fmt++;          // Skip '%'
      switch (*fmt) { // Read the next character
      case '\0':      // '%' at the end of the format string
        putchar('%');
        goto end;
      case '%': // Print '%'
        putchar('%');
        break;
      case 'c': { // Print a character.
        char c = va_arg(vargs, int);
        putchar(c);
        break;
      }
      case 's': { // Print a NULL-terminated string.
        const char *s = va_arg(vargs, const char *);
        while (*s) {
          putchar(*s);
          s++;
        }
        break;
      }
      case 'd': { // Print an integer in decimal.
        int value = va_arg(vargs, int);
        unsigned magnitude = value;
        if (value < 0) {
          putchar('-');
          magnitude = -magnitude;
        }

        unsigned divisor = 1;
        while (magnitude / divisor > 9)
          divisor *= 10;

        while (divisor > 0) {
          putchar('0' + magnitude / divisor);
          magnitude %= divisor;
          divisor /= 10;
        }

        break;
      }
      case 'x': { // Print an integer in hexadecimal.
        unsigned value = va_arg(vargs, unsigned);
        for (int i = 7; i >= 0; i--) {
          unsigned nibble = (value >> (i * 4)) & 0xf;
          putchar("0123456789abcdef"[nibble]);
        }
      }
      }
    } else {
      putchar(*fmt);
    }

    fmt++;
  }

end:
  va_end(vargs);
}
