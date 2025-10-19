// std.cpp - standard library like functions

#include "ot/common.h"

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

int atoi(const char *s) {
  int result = 0;
  while (*s)
    result = result * 10 + (*s++ - '0');
  return result;
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
      case 'c': {
        char c = va_arg(vargs, int);
        oputchar(c);
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

void ovsnprintf(char *str, size_t size, const char *format, va_list args) {
  size_t pos = 0;

  auto putchar_buf = [&](char c) {
    if (pos + 1 < size) {
      str[pos++] = c;
    }
  };

  while (*format && pos + 1 < size) {
    if (*format == '%') {
      format++;
      switch (*format) {
      case '\0':
        putchar_buf('%');
        goto end;
      case '%':
        putchar_buf('%');
        break;
      case 's': {
        const char *s = va_arg(args, const char *);
        while (*s && pos + 1 < size) {
          putchar_buf(*s);
          s++;
        }
        break;
      }
      case 'd': {
        int value = va_arg(args, int);
        unsigned magnitude = value;
        if (value < 0) {
          putchar_buf('-');
          magnitude = -magnitude;
        }

        unsigned divisor = 1;
        while (magnitude / divisor > 9)
          divisor *= 10;

        while (divisor > 0 && pos + 1 < size) {
          putchar_buf('0' + magnitude / divisor);
          magnitude %= divisor;
          divisor /= 10;
        }
        break;
      }
      case 'c': {
        char c = va_arg(args, int);
        putchar_buf(c);
        break;
      }
      case 'x': {
        unsigned value = va_arg(args, unsigned);
        for (int i = 7; i >= 0 && pos + 1 < size; i--) {
          unsigned nibble = (value >> (i * 4)) & 0xf;
          putchar_buf("0123456789abcdef"[nibble]);
        }
        break;
      }
      }
    } else {
      putchar_buf(*format);
    }
    format++;
  }

end:
  if (size > 0)
    str[pos] = '\0';
}

void osnprintf(char *str, size_t size, const char *format, ...) {
  va_list args;
  va_start(args, format);
  ovsnprintf(str, size, format, args);
  va_end(args);
}