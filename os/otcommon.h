// otcommon.h - global type definitions and globally available functions
#ifndef OTCOMMON_H
#define OTCOMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

#ifdef OT_TEST
#include <stddef.h>
#include <stdint.h>
#else

typedef unsigned char uint8_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef uint32_t size_t;
typedef int32_t intptr_t;
typedef uint32_t uintptr_t;

#endif

#define is_aligned(value, align) __builtin_is_aligned(value, align)

// basic functions
void *omemset(void *buf, char c, size_t n);
void oprintf(const char *fmt, ...);
void *memcpy(void *dst, const void *src, size_t n);
char *strcpy(char *dst, const char *src);
int strcmp(const char *s1, const char *s2);

// syscalls
void oputchar(char);

#ifdef __cplusplus
} // extern "C"
#endif

#endif