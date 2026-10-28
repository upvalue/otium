#define _POSIX_C_SOURCE 200809L
#include "helpers.h"
#include "internal.h"
#include <string.h>
#include <time.h>

static intptr_t jiffy(void) {
  struct timespec t;
  CHECK(!clock_gettime(CLOCK_MONOTONIC, &t));
  return (intptr_t)t.tv_sec * 1000000000 + t.tv_nsec;
}

static double second(void) {
  struct timespec t;
  CHECK(!clock_gettime(CLOCK_REALTIME, &t));
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static ot_state* clock_runtime(void) {
  ot_state* rt = new_core_runtime();
  ot_gc_stress(rt, true);
  return rt;
}

TEST(clocks_match_host_clocks) {
  ot_state* rt = clock_runtime();
  ot_value value;
  CHECK(ot_call(rt, "jiffies-per-second", NULL, 0, &value));
  CHECK(value == ot_int(1000000000));
  for (size_t i = 0; i < 100; i++) {
    intptr_t before = jiffy();
    CHECK(ot_call(rt, "current-jiffy", NULL, 0, &value));
    intptr_t after = jiffy();
    CHECK(ot_is_int(value));
    CHECK(before <= ot_to_int(value) && ot_to_int(value) <= after);
    double start = second();
    CHECK(ot_call(rt, "current-second", NULL, 0, &value));
    double end = second(), actual;
    CHECK(oti_kind(value, K_FLOAT));
    actual = oti_as_float(value)->value;
    /* A civil clock adjustment during this check invalidates its bracket. */
    if (start <= end) CHECK(start <= actual && actual <= end);
  }
  ot_destroy(rt);
}

TEST(time_library_seconds) {
  ot_state* rt = clock_runtime();
  check_true(rt, "(require otium.time :as clock) (let ((before (clock/seconds)) (after "
                 "(clock/seconds))) (<= before after))");
  ot_destroy(rt);
}

TEST(clock_procedures_reject_arguments) {
  ot_state* rt = clock_runtime();
  const char* names[] = {"current-jiffy", "current-second", "jiffies-per-second"};
  ot_value arg = ot_int(0), value;
  for (size_t i = 0; i < 3; i++) CHECK(!ot_call(rt, names[i], &arg, 1, &value));
  CHECK(ot_call(rt, "current-jiffy", NULL, 0, &value));
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(clocks_match_host_clocks),
    TEST_ENTRY(time_library_seconds),
    TEST_ENTRY(clock_procedures_reject_arguments),
};

TEST_SUITE(time, tests);
