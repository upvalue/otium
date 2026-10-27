#include "internal.h"
#include <stdint.h>
#include <time.h>

static bool time_arity(ot_state* rt, size_t count) {
  if (!count) return true;
  return oti_error(rt, "time procedures take no arguments");
}

static ot_value time_current_jiffy(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!time_arity(rt, count)) return 0;
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now)) {
    oti_error(rt, "cannot read clock");
    return 0;
  }
  uintmax_t limit = (uintmax_t)INTPTR_MAX / 2;
  if (now.tv_sec < 0 || now.tv_nsec < 0 ||
      (uintmax_t)now.tv_sec > (limit - (uintmax_t)now.tv_nsec) / 1000000000) {
    oti_error(rt, "monotonic clock exceeds integer range");
    return 0;
  }
  return ot_int((intptr_t)((uintmax_t)now.tv_sec * 1000000000 + (uintmax_t)now.tv_nsec));
}

static ot_value time_jiffies_second(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!time_arity(rt, count)) return 0;
  return ot_int(1000000000);
}

static ot_value time_current_second(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!time_arity(rt, count)) return 0;
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
