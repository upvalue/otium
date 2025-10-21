// ot/common.h - global type definitions and globally available functions
#ifndef OT_COMMON_H
#define OT_COMMON_H

#include "ot/config.h"

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

// These are common C stdlib-like functions, callable from anyhwere

// In a few cases, they're prefixed with "o" because these are sometimes
// macros in normal stdlib headers and that causes compilation conflicts
// when this is compiled on a normal system instea dof for a freestanding target
void *omemset(void *buf, char c, size_t n);
size_t strlen(const char *s);
void oprintf(const char *fmt, ...);

#ifndef memcpy
void *memcpy(void *dst, const void *src, size_t n);
#endif

#ifndef strcpy
char *strcpy(char *dst, const char *src);
#endif

int strcmp(const char *s1, const char *s2);
int atoi(const char *s);
int memcmp(const void *s1, const void *s2, size_t n);
void ovsnprintf(char *str, size_t size, const char *format, va_list args);
void osnprintf(char *str, size_t size, const char *format, ...);

// System calls
#define OU_YIELD 1
#define OU_PUTCHAR 2
#define OU_GETCHAR 3
#define OU_EXIT 4
#define OU_ALLOC_PAGE 5
#define OU_GET_ARG_PAGE 6

// oputchar -- returns 0 in case of failure, 1 otherwise
int oputchar(char);

#define OT_PAGE_SIZE 4096

#ifdef __cplusplus
} // extern "C"
#endif

#endif