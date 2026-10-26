#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include <assert.h>
#include <string.h>
#include <time.h>

static intptr_t jiffy(void) {
  struct timespec t;
  assert(!clock_gettime(CLOCK_MONOTONIC, &t));
  return (intptr_t)t.tv_sec * 1000000000 + t.tv_nsec;
}

static double second(void) {
  struct timespec t;
  assert(!clock_gettime(CLOCK_REALTIME, &t));
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

int main(void) {
  ot_runtime* rt = ot_create(1024 * 1024, 128 * 1024);
  assert(rt);
  assert(ot_load_core(rt, "../lib"));
  ot_gc_stress(rt, true);
  ot_value value;
  assert(ot_call(rt, "jiffies-per-second", NULL, 0, &value));
  assert(value == ot_int(1000000000));
  for (size_t i = 0; i < 100; i++) {
    intptr_t before = jiffy();
    assert(ot_call(rt, "current-jiffy", NULL, 0, &value));
    intptr_t after = jiffy();
    assert(ot_is_int(value));
    assert(before <= ot_to_int(value) && ot_to_int(value) <= after);
    double start = second();
    assert(ot_call(rt, "current-second", NULL, 0, &value));
    double end = second(), actual;
    assert(oti_kind(value, K_FLOAT));
    memcpy(&actual, object_bytes(value), sizeof(actual));
    /* A civil clock adjustment during this check invalidates its bracket. */
    if (start <= end) assert(start <= actual && actual <= end);
  }
  const char* source = "(require otium.time :as clock) (let ((before (clock/seconds)) (after "
                       "(clock/seconds))) (<= before after))";
  assert(ot_eval(rt, source, strlen(source), &value) && value == OT_TRUE);
  const char* names[] = {"current-jiffy", "current-second", "jiffies-per-second"};
  ot_value arg = ot_int(0);
  for (size_t i = 0; i < 3; i++) assert(!ot_call(rt, names[i], &arg, 1, &value));
  assert(ot_call(rt, "current-jiffy", NULL, 0, &value));
  ot_destroy(rt);
  puts("monotonic and civil clock checks passed");
}
