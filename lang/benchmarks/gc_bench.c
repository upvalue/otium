#define _POSIX_C_SOURCE 200809L

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

static char* read_source(const char* path, size_t* length) {
  FILE* input = fopen(path, "rb");
  if (input == NULL) return NULL;
  if (fseek(input, 0, SEEK_END) != 0) {
    fclose(input);
    return NULL;
  }
  long end = ftell(input);
  if (end < 0 || (unsigned long)end > SIZE_MAX - 1) {
    fclose(input);
    return NULL;
  }
  rewind(input);
  char* source = malloc((size_t)end + 1);
  if (source == NULL) {
    fclose(input);
    return NULL;
  }
  size_t read = fread(source, 1, (size_t)end, input);
  bool failed = read != (size_t)end || ferror(input);
  fclose(input);
  if (failed) {
    free(source);
    return NULL;
  }
  source[read] = '\0';
  *length = read;
  return source;
}

static bool language_workload(ots* state, const char* path, const char* source, size_t length) {
  otv result = ot_nil;
  return ot_eval_src(state, source, length, path, &result);
}

static void print_result(const char* workload, uint64_t elapsed_ns, ot_gc_stats stats) {
  printf("{\"workload\":\"%s\",\"elapsed_ns\":%" PRIu64, workload, elapsed_ns);
#define OT_GC_JSON(NAME, VALUE) printf(",\"%s\":%" PRIu64, NAME, (uint64_t)(VALUE))
#define OT_GC_STAT(TYPE, MEMBER, JSON, HUMAN, UNIT) OT_GC_JSON(JSON, stats.MEMBER);
#define OT_GC_PHASE(MEMBER, HUMAN, COLLECTIONS_JSON, TOTAL_JSON, MAX_JSON) \
  OT_GC_JSON(COLLECTIONS_JSON, stats.MEMBER.collections);                   \
  OT_GC_JSON(TOTAL_JSON, stats.MEMBER.total_pause_ns);                      \
  OT_GC_JSON(MAX_JSON, stats.MEMBER.max_pause_ns);
  OT_GC_STATS_FIELDS(OT_GC_STAT, OT_GC_PHASE)
#undef OT_GC_PHASE
#undef OT_GC_STAT
#undef OT_GC_JSON
  puts("}");
}

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    fprintf(stderr, "usage: gc-bench churn|mixed|fragmentation [iterations]\n"
                    "       gc-bench BENCHMARK.scm\n");
    return 2;
  }
  bool direct = strcmp(argv[1], "churn") == 0 || strcmp(argv[1], "mixed") == 0 ||
                strcmp(argv[1], "fragmentation") == 0;
  if (!direct && argc != 2) {
    fputs("gc-bench: a language benchmark does not take an iteration count\n", stderr);
    return 2;
  }
  size_t iterations = 0;
  if (direct) {
    iterations = argc == 3 ? (size_t)strtoull(argv[2], NULL, 10) : 0;
    if (iterations == 0) {
      if (strcmp(argv[1], "churn") == 0) iterations = 1000000;
      else if (strcmp(argv[1], "mixed") == 0) iterations = 30000;
      else iterations = 8000;
    }
  }
  size_t source_length = 0;
  char* source = NULL;
  if (!direct) {
    source = read_source(argv[1], &source_length);
    if (source == NULL) {
      fprintf(stderr, "gc-bench: could not read %s\n", argv[1]);
      return 2;
    }
  }

  ot_config config = ot_config_default();
  config.gc_force_compact = strcmp(argv[1], "fragmentation") == 0;
  ots* state = ot_create(&config);
  if (state == NULL) {
    fputs("gc-bench: could not create runtime\n", stderr);
    free(source);
    return 1;
  }

  ot_reset_gc_stats(state);
  uint64_t started = monotonic_ns();
  bool ok;
  if (strcmp(argv[1], "churn") == 0) ok = churn(state, iterations);
  else if (strcmp(argv[1], "mixed") == 0) ok = mixed(state, iterations);
  else if (strcmp(argv[1], "fragmentation") == 0) ok = fragmentation(state, iterations);
  else ok = language_workload(state, argv[1], source, source_length);
  uint64_t elapsed = monotonic_ns() - started;
  free(source);
  if (!ok) {
    fprintf(stderr, "gc-bench: %s result check failed\n", argv[1]);
    ot_destroy(state);
    return 3;
  }
  const char* workload = strrchr(argv[1], '/');
  workload = workload == NULL ? argv[1] : workload + 1;
  print_result(workload, elapsed, ot_get_gc_stats(state));
  ot_destroy(state);
  return 0;
}
