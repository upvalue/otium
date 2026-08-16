// Monotonic benchmark timing and wall-clock time.
#include "../builtins.h"
#include <time.h>

// ---------------------------------------------------------------------------
// Clock seam defaults (declared in common.h). POSIX clock_gettime-backed;
// bare-metal hosts install their own via ot_set_clock.

static u64 posix_monotonic_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
}

static u64 posix_wall_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
}

static const OtClock g_default_clock = {posix_monotonic_ns, posix_wall_ns};
static const OtClock* g_clock = &g_default_clock;

void ot_set_clock(const OtClock* c) { g_clock = c ? c : &g_default_clock; }
u64 ot_monotonic_ns(void) { return g_clock->monotonic_ns(); }
u64 ot_wall_ns(void) { return g_clock->wall_ns(); }

// ---------------------------------------------------------------------------
// Natives.

static Value nat_current_jiffy(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "current-jiffy", argc, 0, 0));
  return int_v((i64)ot_monotonic_ns());
}

static Value nat_jiffies_per_second(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "jiffies-per-second", argc, 0, 0));
  return int_v(1000000000);
}

static Value nat_current_second(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "current-second", argc, 0, 0));
  return float_v((f64)ot_wall_ns() / 1e9);
}

void register_time(State* vm) {
  ot_def_native(vm, "current-jiffy", nat_current_jiffy);
  ot_def_native(vm, "jiffies-per-second", nat_jiffies_per_second);
  ot_def_native(vm, "current-second", nat_current_second);
}
