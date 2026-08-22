#include "otium.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t monotonic_ns(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

static bool churn(ots* state, size_t allocations) {
  otv list = ot_null;
  otv car = ot_nil;
  OT_FRAME(state, &list, &car);
  size_t made = 0;
  while (made < allocations) {
    size_t count = allocations - made < 5000 ? allocations - made : 5000;
    list = ot_null;
    for (size_t i = 0; i < count; i++) list = ot_cons(state, ot_make_int((intptr_t)i), list);
    if (!ot_pair_values(list, &car, NULL) || ot_get_int(car) != (intptr_t)count - 1) {
      OT_FRAME_POP(state);
      return false;
    }
    made += count;
  }
  OT_FRAME_POP(state);
  return true;
}

static bool mixed(ots* state, size_t count) {
  otv keep = ot_nil;
  otv table = ot_nil;
  otv value = ot_nil;
  otv garbage = ot_nil;
  OT_FRAME(state, &keep, &table, &value, &garbage);
  keep = ot_array_new(state, count);
  table = ot_table_new(state, count / 2 + 1);
  for (size_t i = 0; i < count; i++) {
    value = ot_cons(state, ot_make_int((intptr_t)i), ot_make_int((intptr_t)(i + 1)));
    keep = ot_array_append(state, keep, value);
    if ((i & 1u) == 0) table = ot_table_put(state, table, ot_make_int((intptr_t)i), value);
    garbage = ot_null;
    for (int j = 0; j < 6; j++) garbage = ot_cons(state, value, garbage);
  }
  ot_collect(state);
  bool ok = ot_array_length(keep) == count && ot_table_length(table) == (count + 1) / 2;
  OT_FRAME_POP(state);
  return ok;
}

static bool fragmentation(ots* state, size_t count) {
  otv keep = ot_nil;
  otv discard = ot_nil;
  otv value = ot_nil;
  OT_FRAME(state, &keep, &discard, &value);
  keep = ot_array_new(state, count);
  discard = ot_array_new(state, count);
  for (size_t i = 0; i < count; i++) {
    value = ot_make_string(state, "live", 4);
    keep = ot_array_append(state, keep, value);
    value = ot_make_string(state, "dead", 4);
    discard = ot_array_append(state, discard, value);
  }
  ot_collect(state);
  ot_collect(state);
  discard = ot_nil;
  ot_collect(state);
  value = ot_array_get(keep, count / 2, ot_nil);
  const char* bytes = NULL;
  size_t length = 0;
  bool ok = ot_string_bytes(value, &bytes, &length) && length == 4 && memcmp(bytes, "live", 4) == 0;
  OT_FRAME_POP(state);
  return ok;
}

static void print_result(const char* workload, uint64_t elapsed_ns, ot_gc_stats stats) {
  printf("{\"workload\":\"%s\",\"elapsed_ns\":%" PRIu64 ",\"allocations\":%" PRIu64
         ",\"allocated_bytes\":%" PRIu64 ",\"copied_bytes\":%" PRIu64 ",\"promoted_bytes\":%" PRIu64
         ",\"moved_bytes\":%" PRIu64 ",\"reclaimed_bytes\":%" PRIu64
         ",\"used_bytes\":%zu,\"peak_used_bytes\":%zu,\"capacity_bytes\":%zu"
         ",\"reserved_bytes\":%zu,\"metadata_bytes\":%zu,\"fragmentation_bytes\":%zu"
         ",\"minor_collections\":%" PRIu64 ",\"minor_total_pause_ns\":%" PRIu64
         ",\"minor_max_pause_ns\":%" PRIu64 ",\"major_sweep_collections\":%" PRIu64
         ",\"major_sweep_total_pause_ns\":%" PRIu64 ",\"major_sweep_max_pause_ns\":%" PRIu64
         ",\"major_compact_collections\":%" PRIu64 ",\"major_compact_total_pause_ns\":%" PRIu64
         ",\"major_compact_max_pause_ns\":%" PRIu64 ",\"full_copy_collections\":%" PRIu64
         ",\"full_copy_total_pause_ns\":%" PRIu64 ",\"full_copy_max_pause_ns\":%" PRIu64
         ",\"mutator_pause_collections\":%" PRIu64 ",\"mutator_pause_total_ns\":%" PRIu64
         ",\"mutator_pause_max_ns\":%" PRIu64 "}\n",
         workload, elapsed_ns, stats.allocations, stats.allocated_bytes, stats.copied_bytes,
         stats.promoted_bytes, stats.moved_bytes, stats.reclaimed_bytes, stats.used_bytes,
         stats.peak_used_bytes, stats.capacity_bytes, stats.reserved_bytes, stats.metadata_bytes,
         stats.fragmentation_bytes, stats.minor.collections, stats.minor.total_pause_ns,
         stats.minor.max_pause_ns, stats.major_sweep.collections, stats.major_sweep.total_pause_ns,
         stats.major_sweep.max_pause_ns, stats.major_compact.collections,
         stats.major_compact.total_pause_ns, stats.major_compact.max_pause_ns,
         stats.full_copy.collections, stats.full_copy.total_pause_ns, stats.full_copy.max_pause_ns,
         stats.mutator_pause.collections, stats.mutator_pause.total_pause_ns,
         stats.mutator_pause.max_pause_ns);
}

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    fprintf(stderr, "usage: gc-bench churn|mixed|fragmentation [iterations]\n");
    return 2;
  }
  size_t iterations = argc == 3 ? (size_t)strtoull(argv[2], NULL, 10) : 0;
  if (iterations == 0) {
    if (strcmp(argv[1], "churn") == 0) iterations = 1000000;
    else if (strcmp(argv[1], "mixed") == 0) iterations = 30000;
    else iterations = 8000;
  }

  ot_config config = ot_config_default();
  config.gc_force_compact = strcmp(argv[1], "fragmentation") == 0;
  ots* state = ot_create(&config);
  if (state == NULL) {
    fputs("gc-bench: could not create runtime\n", stderr);
    return 1;
  }

  ot_reset_gc_stats(state);
  uint64_t started = monotonic_ns();
  bool ok;
  if (strcmp(argv[1], "churn") == 0) ok = churn(state, iterations);
  else if (strcmp(argv[1], "mixed") == 0) ok = mixed(state, iterations);
  else if (strcmp(argv[1], "fragmentation") == 0) ok = fragmentation(state, iterations);
  else {
    fprintf(stderr, "gc-bench: unknown workload: %s\n", argv[1]);
    ot_destroy(state);
    return 2;
  }
  uint64_t elapsed = monotonic_ns() - started;
  if (!ok) {
    fprintf(stderr, "gc-bench: %s result check failed\n", argv[1]);
    ot_destroy(state);
    return 1;
  }
  print_result(argv[1], elapsed, ot_get_gc_stats(state));
  ot_destroy(state);
  return 0;
}
