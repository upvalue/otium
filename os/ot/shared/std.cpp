// std.cpp - standard library like functions

#include "ot/common.h"

char scratch_buffer[1024];

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
  while (*s)
    result = result * 10 + (*s++ - '0');
  return result;
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

void oprintf(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  ovsnprintf(scratch_buffer, sizeof(scratch_buffer), fmt, args);
  va_end(args);
  oputsn(scratch_buffer, (int)strlen(scratch_buffer));
}
