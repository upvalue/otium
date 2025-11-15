// ot/common.h - global type definitions and globally available functions
#ifndef OT_COMMON_H
#define OT_COMMON_H

#include "ot/config.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

#if defined(OT_POSIX) || defined(OT_ARCH_WASM)
#include <stddef.h>
#include <stdint.h>
#else

#ifndef NULL
#define NULL 0
#endif

typedef unsigned char uint8_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;
typedef uint32_t size_t;
typedef int32_t intptr_t;
typedef uint32_t uintptr_t;

#endif

#define is_aligned(value, align) __builtin_is_aligned(value, align)

#define OT_SOFT_ASSERT(msg, condition)                                         \
  do {                                                                         \
    if (!(condition)) {                                                        \
      oprintf("SOFT-ASSERT: %s\n", msg);                                       \
    }                                                                          \
  } while (0)

// These are common C stdlib-like functions, callable from anyhwere

// In a few cases, they're prefixed with "o" because these are sometimes
// macros in normal stdlib headers and that causes compilation conflicts
// when this is compiled on a normal system instea dof for a freestanding target
void *omemset(void *buf, char c, size_t n);
void *omemmove(void *dst, const void *src, size_t n);
size_t strlen(const char *s);
void oprintf(const char *fmt, ...);

#ifndef memcpy
void *memcpy(void *dst, const void *src, size_t n);
#endif

#ifndef strcpy
char *strcpy(char *dst, const char *src);
#endif

int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
int atoi(const char *s);
int memcmp(const void *s1, const void *s2, size_t n);
int ovsnprintf(char *str, size_t size, const char *format, va_list args);
int osnprintf(char *str, size_t size, const char *format, ...);
int oputsn(const char *str, int n);

// System calls
#define OU_YIELD 1
#define OU_PUTCHAR 2
#define OU_GETCHAR 3
#define OU_EXIT 4
#define OU_ALLOC_PAGE 5
#define OU_GET_SYS_PAGE 6
#define OU_IO_PUTS 7          // Writes a string in the comm page to the console
#define OU_PROC_LOOKUP 8      // Look up a process by name
#define OU_IPC_SEND_MESSAGE 9 // Send a message to a process
#define OU_IPC_CHECK_MESSAGE 10 // Check if one or more messages are waiting
#define OU_IPC_POP_MESSAGE 11 // Pop a message from the end of the message queue

// Arguments to the get sys page
#define OU_SYS_PAGE_ARG 0
#define OU_SYS_PAGE_COMM 1
// Get a message page -- next argument must be a valid message number
#define OU_SYS_PAGE_MSG 2
// Get the local storage page for the current process
#define OU_SYS_PAGE_STORAGE 3

// oputchar -- returns 0 in case of failure, 1 otherwise
int oputchar(char);

#ifdef OT_ARCH_WASM
#define O_TIME_UNITS_PER_SECOND 1000
#else
#define O_TIME_UNITS_PER_SECOND 10000000
#endif

uint64_t o_time_get(void);

#define OT_PAGE_SIZE 4096

/** a page sized scratch buffer for general use -- generally not safe to call
 * around any other function, esp i/o ones */
extern char *ot_scratch_buffer;

/**
 * Maximum number of messages a process can receive.
 * Currently there is one page allocated per message.
 */
#define OT_MSG_LIMIT 16

#ifdef __cplusplus
} // extern "C"

// Include C++ headers outside of extern "C"
#include "ot/lib/result.hpp"

// C++ functions
BoolResult<int> parse_int(const char *s);
#endif

#endif