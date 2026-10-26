#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include <stdint.h>
#include <time.h>

ot_value oti_time_builtin(ot_runtime* rt, unsigned id, size_t count) {
  if (count) {
    oti_error(rt, "time procedures take no arguments");
    return 0;
  }
  if (id == B_JIFFIES_SECOND) return ot_int(1000000000);
  struct timespec now;
  if (clock_gettime(id == B_CURRENT_JIFFY ? CLOCK_MONOTONIC : CLOCK_REALTIME, &now)) {
    oti_error(rt, "cannot read clock");
    return 0;
  }
  if (id == B_CURRENT_SECOND) return oti_float(rt, (double)now.tv_sec + (double)now.tv_nsec / 1e9);
  uintmax_t limit = (uintmax_t)INTPTR_MAX / 2;
  if (now.tv_sec < 0 || now.tv_nsec < 0 ||
      (uintmax_t)now.tv_sec > (limit - (uintmax_t)now.tv_nsec) / 1000000000) {
    oti_error(rt, "monotonic clock exceeds integer range");
    return 0;
  }
  return ot_int((intptr_t)((uintmax_t)now.tv_sec * 1000000000 + (uintmax_t)now.tv_nsec));
}
