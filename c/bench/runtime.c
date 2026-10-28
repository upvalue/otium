#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

static const struct {
  const char *name, *source, *function;
  intptr_t argument, expected;
  bool real_result;
} workloads[] = {
    {"float1000000",
     "(def (float-sum n) (let ((total 0.0) (step 0.5)) "
     "(while (> n 0) (set! total (+ total step)) (set! n (- n 1))) total))",
     "float-sum", 1000000, 500000, true},
    {"fib30", "(def (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))", "fib", 30, 832040,
     false},
    {"sum1000000",
     "(def (sum n) (let loop ((n n) (total 0)) (if (= n 0) total (loop (- n 1) (+ total n)))))",
     "sum", 1000000, 500000500000, false},
    {"pairs50000",
     "(def (pairs n) (let ((total 0)) (while (> n 0) "
     "(let ((p (cons n (cons (- n 1) nil)))) (set! total (+ total (car p) (car (cdr p))))) "
     "(set! n (- n 1))) total))",
     "pairs", 50000, 2500000000, false},
    {"optional100000",
     "(def (sum-optional a &optional (b 1) (c 2)) (+ a b c)) "
     "(def (optional-loop n) (let ((total 0)) (while (> n 0) "
     "(set! total (+ total (sum-optional n))) (set! n (- n 1))) total))",
     "optional-loop", 100000, 5000350000, false},
    {"apply100000",
     "(def (sum-three a b c) (+ a b c)) "
     "(def (apply-loop n) (let ((total 0) (tail [1 2])) (while (> n 0) "
     "(set! total (+ total (apply sum-three n tail))) (set! n (- n 1))) total))",
     "apply-loop", 100000, 5000350000, false},
    {"rest100000",
     "(def (sum-rest a & rest) (+ a (car rest) (car (cdr rest)))) "
     "(def (rest-loop n) (let ((total 0)) (while (> n 0) "
     "(set! total (+ total (sum-rest n 1 2))) (set! n (- n 1))) total))",
     "rest-loop", 100000, 5000350000, false},
    {"objectkeys5000",
     "(def (object-keys n) (let ((keys []) (t {}) (i 0) (total 0)) "
     "(while (< i n) (let ((key [])) (push! keys key) (put! t key (* i 2))) (set! i (+ i 1))) "
     "(set! i 0) (while (< i n) (set! total (+ total (get t (get keys i)))) "
     "(set! i (+ i 1))) total))",
     "object-keys", 5000, 24995000, false},
    {"tables10000",
     "(def (tables n) (let ((t {}) (i 0) (total 0)) "
     "(while (< i n) (put! t i (* i 2)) (set! i (+ i 1))) (set! i 0) "
     "(while (< i n) (set! total (+ total (get t i))) (set! i (+ i 1))) total))",
     "tables", 10000, 99990000, false},
    {"arrays50000",
     "(def (arrays n) (let ((a []) (i 0) (total 0)) "
     "(while (< i n) (push! a i) (set! i (+ i 1))) (set! i 0) "
     "(while (< i n) (set! total (+ total (get a i))) (set! i (+ i 1))) total))",
     "arrays", 50000, 1249975000, false},
    {"ffi1000000",
     "(def lib (ffi/open-library '(\"" OTIUM_TEST_FIXTURE_DIR "/ffi-fixture-17.so\"))) "
     "(def echo (ffi/bind lib \"echo_int\" '(int) 'int #t)) "
     "(def (foreign-loop n) (let loop ((n n) (total 0)) "
     "(if (= n 0) total (loop (- n 1) (+ total (echo n))))))",
     "foreign-loop", 1000000, 500000500000, false}};

static double now(void) {
  struct timespec t;
  if (clock_gettime(CLOCK_MONOTONIC, &t)) abort();
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* Print one JSON line of five timed calls, after two warmup calls. */
static int run(size_t index) {
  ot_state* rt = ot_create(1024 * 1024, 128 * 1024);
  if (!rt) return 1;
  ot_value answer, argument = ot_int(workloads[index].argument);
  if (!ot_eval(rt, workloads[index].source, strlen(workloads[index].source), &answer)) goto error;
  double samples[5];
  for (size_t i = 0; i < 7; i++) {
    double start = now();
    if (!ot_call(rt, workloads[index].function, &argument, 1, &answer)) goto error;
    double elapsed = now() - start;
    if (workloads[index].real_result) {
      if (!oti_kind(answer, K_FLOAT)) goto error;
      double real;
      memcpy(&real, object_bytes(answer), sizeof(real));
      if (real != (double)workloads[index].expected) goto error;
    } else if (answer != ot_int(workloads[index].expected)) goto error;
    if (i >= 2) samples[i - 2] = elapsed;
  }
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage)) goto error;
  printf("{\"workload\":\"%s\",\"runtime\":\"c-%s\",\"samples\":[", workloads[index].name, "vm");
  for (size_t i = 0; i < 5; i++) printf("%s%.9f", i ? "," : "", samples[i]);
  printf("],\"ffi_signatures\":%zu,\"collections\":%zu,\"max_frames\":%zu,\"peak_rss_kib\":%ld}\n",
         ot_get_stats(rt).signatures, ot_get_stats(rt).collections, ot_get_stats(rt).max_frames,
         usage.ru_maxrss);
  ot_destroy(rt);
  return 0;
error:
  fprintf(stderr, "%s: %s\n", workloads[index].name, ot_error(rt));
  ot_destroy(rt);
  return 1;
}

/* With no arguments, run every workload; otherwise run the named ones. */
int main(int argc, char** argv) {
  size_t count = sizeof(workloads) / sizeof(workloads[0]);
  int status = 0;
  for (size_t index = 0; index < count; index++) {
    bool selected = argc == 1;
    for (int i = 1; i < argc; i++) selected |= !strcmp(workloads[index].name, argv[i]);
    if (selected && run(index)) status = 1;
  }
  for (int i = 1; i < argc; i++) {
    size_t index = 0;
    while (index < count && strcmp(workloads[index].name, argv[i])) index++;
    if (index == count) {
      fprintf(stderr, "unknown workload: %s\n", argv[i]);
      status = 2;
    }
  }
  return status;
}
