// otcommon.h - global type definitions and globally available functions
#ifndef OTCOMMON_H
#define OTCOMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef uint32_t size_t;

// basic functions
void *memset(void *buf, char c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
char *strcpy(char *dst, const char *src);
int strcmp(const char *s1, const char *s2);
void printf(const char *fmt, ...);

// syscalls
void putchar(char);

#ifdef __cplusplus
} // extern "C"
#endif

#endif