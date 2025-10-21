// shim.c - otium os shim for libmpack compilation
#ifdef OT_TEST
// In test mode, use standard library
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#else
// In kernel mode, use custom implementations
#include "ot/common.h"

#define NULL 0
typedef int bool;
#define memset omemset

#define assert(condition)                                                      \
  do {                                                                         \
    if (!(condition)) {                                                        \
      oprintf("Assertion failed: %s, file %s, line %d\n", #condition,          \
              __FILE__, __LINE__);                                             \
    }                                                                          \
  } while (0)
#endif

// Disable floating point support to avoid soft-float dependencies
#define MPACK_DISABLE_FLOAT 1
