// ot/common.h - global type definitions and globally available functions
#ifndef OT_COMMON_H
#define OT_COMMON_H

#include "ot/config.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define is_aligned(value, align) __builtin_is_aligned(value, align)

#define OT_SOFT_ASSERT(msg, condition)                                                                                 \
  do {                                                                                                                 \
    if (!(condition)) {                                                                                                \
      oprintf("SOFT-ASSERT: %s\n", msg);                                                                               \
    }                                                                                                                  \
  } while (0)

// These are common C stdlib-like functions, callable from anywhere

// Otium-specific wrappers
void oprintf(const char *fmt, ...);
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
#define OU_IO_PUTS 7            // Writes a string in the comm page to the console
#define OU_PROC_LOOKUP 8        // Look up a process by name
#define OU_IPC_SEND 9           // Send IPC message to a process
#define OU_IPC_RECV 10          // Receive IPC message (blocks if none available)
#define OU_IPC_REPLY 11         // Reply to IPC sender
#define OU_SHUTDOWN 12          // Shutdown all processes and exit the kernel
#define OU_LOCK_KNOWN_MEMORY 13 // Lock a known memory region

// Known memory region identifiers
typedef enum { KNOWN_MEMORY_NONE = 0, KNOWN_MEMORY_FRAMEBUFFER = 1, KNOWN_MEMORY_COUNT } KnownMemory;

// Arguments to the get sys page
#define OU_SYS_PAGE_ARG 0
#define OU_SYS_PAGE_COMM 1
// Get the local storage page for the current process
#define OU_SYS_PAGE_STORAGE 2

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

#ifdef __cplusplus
} // extern "C"

// Include C++ headers outside of extern "C"
#include "ot/lib/result.hpp"

// C++ functions
BoolResult<int> parse_int(const char *s);

// Placement new/delete operators for freestanding environment
// In POSIX builds, use standard library implementation
#ifdef OT_POSIX
#include <new>
#else
// Placement new for constructing objects in pre-allocated memory
inline void *operator new(size_t, void *ptr) noexcept { return ptr; }
inline void operator delete(void *, void *) noexcept {}
// Regular delete operators (we don't use dynamic allocation in freestanding)
// Note: Cannot be inline per C++ standard for replacement operators
void operator delete(void *) noexcept;
void operator delete(void *, unsigned int) noexcept;
void operator delete(void *, unsigned long) noexcept;
#endif

#endif

#endif