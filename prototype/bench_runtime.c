#define _POSIX_C_SOURCE 200809L
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
    {"float1000000", "(def (float-sum n) (let ((total 0.0) (step 0.5)) "
        "(while (> n 0) (set! total (+ total step)) (set! n (- n 1))) total))",
        "float-sum", 1000000, 500000, true},
    {"fib30", "(def (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))", "fib", 30, 832040, false},
    {"sum1000000", "(def (sum n) (let loop ((n n) (total 0)) (if (= n 0) total (loop (- n 1) (+ total n)))))",
        "sum", 1000000, 500000500000, false},
    {"pairs50000", "(def (pairs n) (let ((total 0)) (while (> n 0) "
        "(let ((p (cons n (cons (- n 1) nil)))) (set! total (+ total (car p) (car (cdr p))))) "
        "(set! n (- n 1))) total))", "pairs", 50000, 2500000000, false},
    {"optional100000", "(def (sum-optional a &optional (b 1) (c 2)) (+ a b c)) "
        "(def (optional-loop n) (let ((total 0)) (while (> n 0) "
        "(set! total (+ total (sum-optional n))) (set! n (- n 1))) total))",
        "optional-loop", 100000, 5000350000, false},
    {"apply100000", "(def (sum-three a b c) (+ a b c)) "
        "(def (apply-loop n) (let ((total 0) (tail [1 2])) (while (> n 0) "
        "(set! total (+ total (apply sum-three n tail))) (set! n (- n 1))) total))",
        "apply-loop", 100000, 5000350000, false},
    {"rest100000", "(def (sum-rest a & rest) (+ a (car rest) (car (cdr rest)))) "
        "(def (rest-loop n) (let ((total 0)) (while (> n 0) "
        "(set! total (+ total (sum-rest n 1 2))) (set! n (- n 1))) total))",
        "rest-loop", 100000, 5000350000, false},
    {"objectkeys5000", "(def (object-keys n) (let ((keys []) (t {}) (i 0) (total 0)) "
        "(while (< i n) (let ((key [])) (push! keys key) (put! t key (* i 2))) (set! i (+ i 1))) "
        "(set! i 0) (while (< i n) (set! total (+ total (get t (get keys i)))) "
        "(set! i (+ i 1))) total))", "object-keys", 5000, 24995000, false},
    {"tables10000", "(def (tables n) (let ((t {}) (i 0) (total 0)) "
        "(while (< i n) (put! t i (* i 2)) (set! i (+ i 1))) (set! i 0) "
        "(while (< i n) (set! total (+ total (get t i))) (set! i (+ i 1))) total))",
        "tables", 10000, 99990000, false},
    {"arrays50000", "(def (arrays n) (let ((a []) (i 0) (total 0)) "
        "(while (< i n) (push! a i) (set! i (+ i 1))) (set! i 0) "
        "(while (< i n) (set! total (+ total (get a i))) (set! i (+ i 1))) total))",
        "arrays", 50000, 1249975000, false},
    {"ffi1000000", "(def lib (ffi/open-library '(\"prototype/ffi_fixture.so\" \"ffi_fixture.so\"))) "
        "(def echo (ffi/bind lib \"echo_int\" '(int) 'int #t)) "
        "(def (foreign-loop n) (let loop ((n n) (total 0)) "
        "(if (= n 0) total (loop (- n 1) (+ total (echo n))))))",
        "foreign-loop", 1000000, 500000500000, false}
};

static double now(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) abort();
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) return 2;
    bool native = argc == 3 && !strcmp(argv[2], "--native");
    size_t index;
    for (index = 0; index < sizeof(workloads) / sizeof(workloads[0]); index++)
        if (!strcmp(workloads[index].name, argv[1])) break;
    if (index == sizeof(workloads) / sizeof(workloads[0])) return 2;
    Runtime *rt = runtime_create(1024 * 1024, 128 * 1024);
    if (!rt) return 1;
    runtime_native(rt, native ? 1024 : 0);
    Value answer, argument = value_int(workloads[index].argument);
    if (!runtime_eval(rt, workloads[index].source, strlen(workloads[index].source), &answer)) goto error;
    double samples[5];
    for (size_t i = 0; i < 7; i++) {
        double compilation_before = runtime_native_compile_seconds(rt);
        double foreign_before = runtime_foreign_compile_seconds(rt);
        double start = now();
        if (!runtime_call(rt, workloads[index].function, &argument, 1, &answer)) goto error;
        double elapsed = now() - start;
        if (workloads[index].real_result) {
            if (!rt_kind(answer, K_FLOAT)) goto error;
            double real;
            memcpy(&real, object_bytes(answer), sizeof(real));
            if (real != (double)workloads[index].expected) goto error;
        } else if (answer != value_int(workloads[index].expected)) goto error;
        if (i >= 2 && (runtime_native_compile_seconds(rt) != compilation_before ||
                      runtime_foreign_compile_seconds(rt) != foreign_before)) {
            fputs("unexpected compilation during measurement\n", stderr);
            goto error;
        }
        if (i >= 2) samples[i - 2] = elapsed;
    }
    if (native && !runtime_native_functions(rt)) goto error;
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage)) goto error;
    printf("{\"workload\":\"%s\",\"runtime\":\"prototype-%s\",\"samples\":[",
        workloads[index].name, native ? "native" : "vm");
    for (size_t i = 0; i < 5; i++) printf("%s%.9f", i ? "," : "", samples[i]);
    printf("],\"compile_seconds\":%.9f,\"ffi_compile_seconds\":%.9f,\"ffi_signatures\":%zu,"
        "\"compiled_functions\":%zu,\"native_entries\":%zu,"
        "\"collections\":%zu,\"max_frames\":%zu,\"max_native_depth\":%zu,\"scalar_entries\":%zu,\"scalar_fallbacks\":%zu,\"numeric_entries\":%zu,\"numeric_fallbacks\":%zu,\"peak_rss_kib\":%ld}\n",
        runtime_native_compile_seconds(rt), runtime_foreign_compile_seconds(rt), runtime_foreign_signatures(rt),
        runtime_native_functions(rt), runtime_native_entries(rt),
        runtime_collections(rt), runtime_max_frames(rt), runtime_max_native_depth(rt), runtime_native_scalar_entries(rt), runtime_native_scalar_fallbacks(rt), rt->native_numeric_entries, rt->native_numeric_fallbacks, usage.ru_maxrss);
    runtime_destroy(rt);
    return 0;
error:
    fprintf(stderr, "%s: %s\n", workloads[index].name, runtime_error(rt));
    runtime_destroy(rt);
    return 1;
}
