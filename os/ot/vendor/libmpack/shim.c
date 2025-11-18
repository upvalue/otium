// shim.c - otium os shim for libmpack compilation
#ifdef OT_POSIX
// In POSIX mode, use standard library
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#else
// In kernel mode, use custom implementations
#include "ot/common.h"

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
