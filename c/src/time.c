#include "internal.h"
#include <stdint.h>
#include <time.h>

/* Nanosecond jiffies would pass the 30-bit integer range of a 32-bit target
 * about a second after its clock starts, so those count milliseconds. */
#if INTPTR_MAX > INT32_MAX
#define JIFFIES_PER_SECOND 1000000000
#else
#define JIFFIES_PER_SECOND 1000
#endif

static ot_value time_current_jiffy(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!oti_check_arity(rt, "current-jiffy", count, 0, 0)) return 0;
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now)) {
    oti_error(rt, "cannot read clock");
    return 0;
  }
  uintmax_t limit = (uintmax_t)INTPTR_MAX / 2;
  uintmax_t part = (uintmax_t)now.tv_nsec / (1000000000 / JIFFIES_PER_SECOND);
  if (now.tv_sec < 0 || now.tv_nsec < 0 ||
      (uintmax_t)now.tv_sec > (limit - part) / JIFFIES_PER_SECOND) {
    oti_error(rt, "monotonic clock exceeds integer range");
    return 0;
  }
  return ot_int((intptr_t)((uintmax_t)now.tv_sec * JIFFIES_PER_SECOND + part));
}

static ot_value time_jiffies_second(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!oti_check_arity(rt, "jiffies-per-second", count, 0, 0)) return 0;
  return ot_int(JIFFIES_PER_SECOND);
}

static ot_value time_current_second(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!oti_check_arity(rt, "current-second", count, 0, 0)) return 0;
  struct timespec now;
  if (clock_gettime(CLOCK_REALTIME, &now)) {
    oti_error(rt, "cannot read clock");
    return 0;
  }
  return oti_float(rt, (double)now.tv_sec + (double)now.tv_nsec / 1e9);
}

const oti_builtin_def oti_time_builtins[] = {
    {B_CURRENT_JIFFY, "current-jiffy", time_current_jiffy},
    {B_JIFFIES_SECOND, "jiffies-per-second", time_jiffies_second},
    {B_CURRENT_SECOND, "current-second", time_current_second},
    {0, NULL, NULL},
};
