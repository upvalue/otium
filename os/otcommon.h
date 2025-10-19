// otcommon.h - global type definitions and globally available functions
#ifndef OTCOMMON_H
#define OTCOMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

#if defined(OT_TEST) || defined(OT_ARCH_WASM)
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
size_t strlen(const char *s);
void oprintf(const char *fmt, ...);
void *memcpy(void *dst, const void *src, size_t n);
char *strcpy(char *dst, const char *src);
int strcmp(const char *s1, const char *s2);
int atoi(const char *s);
int memcmp(const void *s1, const void *s2, size_t n);
void ovsnprintf(char *str, size_t size, const char *format, va_list args);
void osnprintf(char *str, size_t size, const char *format, ...);

// syscalls
#define OU_YIELD 1
#define OU_PUTCHAR 2
#define OU_GETCHAR 3
#define OU_EXIT 4
#define OU_ALLOC_PAGE 5

void oputchar(char);

#define OT_PAGE_SIZE 4096

#ifdef __cplusplus
} // extern "C"
#endif

#endif