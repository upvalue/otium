#include "runtime.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static Value eval(Runtime *rt, const char *source) {
    Value value;
    if (!runtime_eval(rt, source, strlen(source), &value)) {
        fprintf(stderr, "%s\n%s\n", source, runtime_error(rt));
        abort();
    }
    return value;
}

int main(void) {
    const size_t thresholds[] = {1, 31};
    for (size_t mode = 0; mode < 2; mode++) {
        Runtime *rt = runtime_create(32768, 8192);
        assert(rt);
        runtime_native(rt, thresholds[mode]);
        runtime_gc_stress(rt, true);
        eval(rt, "(def imported-add +) (def + imported-add)");
        assert(eval(rt, "(def (sum n) (let ((total 0)) (while (> n 0) "
            "(set! total (+ total n)) (set! n (- n 1))) total)) (sum 10000)") == value_int(50005000));
        assert(eval(rt, "(sum 10000)") == value_int(50005000));
        assert(runtime_native_scalar_entries(rt) > 0);
        assert(runtime_native_scalar_fallbacks(rt) == 0);
        assert(eval(rt, "(def (named n) (let loop ((n n) (total 0)) "
            "(if (= n 0) total (loop (- n 1) (+ total n))))) (named 10000)") == value_int(50005000));
        size_t entries = runtime_native_scalar_entries(rt);
        assert(eval(rt, "(named 10000)") == value_int(50005000));
        assert(runtime_native_scalar_entries(rt) > entries);
        assert(eval(rt, "(= (sum 4.0) 10.0)") == V_TRUE);
        assert(eval(rt, "(def fraction 0.5) (def (later n) (let ((x 0)) "
            "(while (> n 0) (set! x (if (= n 1) fraction (+ x n))) (set! n (- n 1))) (+ x 1))) "
            "(= (later 100) 1.5)") == V_TRUE);
        size_t fallbacks = runtime_native_scalar_fallbacks(rt);
        assert(eval(rt, "(= (later 100) 1.5)") == V_TRUE);
        assert(runtime_native_scalar_fallbacks(rt) > fallbacks);
        assert(eval(rt, "(def (tail-types n total) (if (= n 0) total "
            "(tail-types (- n 1) (if (= n 1) fraction (+ total n))))) "
            "(= (tail-types 100 0) fraction)") == V_TRUE);
        fallbacks = runtime_native_scalar_fallbacks(rt);
        assert(eval(rt, "(= (tail-types 100 0) fraction)") == V_TRUE);
        assert(runtime_native_scalar_fallbacks(rt) > fallbacks);
        assert(eval(rt, "(def hits 0) (def (other n) (set! hits (+ hits 1)) n) "
            "(def (redirect n) (let ((x 0)) (while (> n 0) (set! x (+ x n)) "
            "(set! n (- n 1))) (other x))) (redirect 100)") == value_int(5050));
        assert(eval(rt, "hits") == value_int(1));
        fallbacks = runtime_native_scalar_fallbacks(rt);
        assert(eval(rt, "(redirect 100)") == value_int(5050));
        assert(runtime_native_scalar_fallbacks(rt) > fallbacks);
        assert(eval(rt, "hits") == value_int(2));
        assert(eval(rt, "(def original-add +) (def + (fn (a b) "
            "(set! hits (original-add hits 1)) (original-add a b))) (sum 4)") == value_int(10));
        assert(eval(rt, "hits") == value_int(6));
        eval(rt, "(def + original-add)");
        assert(eval(rt, "(sum 4)") == value_int(10));
        assert(eval(rt, "(def (tail n) (if (= n 0) 0 (tail (- n 1)))) (tail 100)") == value_int(0));
        assert(eval(rt, "(tail 100)") == value_int(0));
        assert(eval(rt, "(def saved-tail tail) (def (tail n) (set! hits (+ hits 1)) n) "
            "(saved-tail 3)") == value_int(2));
        assert(eval(rt, "hits") == value_int(7));
        assert(eval(rt, "(def accessors (let ((n 40)) [(fn () (let ((i 0) (value 0)) "
            "(while (< i 2) (set! value (+ n 2)) (set! i (+ i 1))) value)) "
            "(fn () (set! n (+ n 1)))])) ((get accessors 0))") == value_int(42));
        eval(rt, "((get accessors 1))");
        assert(eval(rt, "((get accessors 0))") == value_int(43));
        assert(eval(rt, "(def object (cons 42 nil)) (def (read-object) object) "
            "(eq? (read-object) object)") == V_TRUE);
        eval(rt, "(def (overflow n) (let ((left 2)) (while (> left 0) "
            "(set! n (+ n 1)) (set! left (- left 1))) n)) (overflow 1)");
        Value argument = value_int(INTPTR_MAX / 2), result;
        assert(!runtime_call(rt, "overflow", &argument, 1, &result));
        assert(strstr(runtime_error(rt), "integer overflow"));
        argument = value_int(40);
        assert(runtime_call(rt, "overflow", &argument, 1, &result) && result == value_int(42));
        eval(rt, "(def (uninitialized n) (while (> n 0) (def x x) (set! n 0)) n)");
        assert(!runtime_call(rt, "uninitialized", &argument, 1, &result));
        assert(strstr(runtime_error(rt), "uninitialized local"));
        assert(runtime_call(rt, "overflow", &argument, 1, &result) && result == value_int(42));
        printf("native scalar tests passed: threshold=%zu entries=%zu fallbacks=%zu\n",
            thresholds[mode], runtime_native_scalar_entries(rt), runtime_native_scalar_fallbacks(rt));
        runtime_destroy(rt);
    }
    Runtime *vm = runtime_create(32768, 8192), *native = runtime_create(32768, 8192);
    assert(vm && native);
    runtime_native(native, 1);
    uint32_t random = 0x9e3779b9u;
    for (size_t i = 0; i < 24; i++) {
        random = random * 1664525u + 1013904223u;
        int scale = (int)(random % 7) - 3;
        int offset = (int)((random >> 8) % 11) - 5;
        int threshold = (int)((random >> 16) % 41) - 20;
        char source[512];
        snprintf(source, sizeof(source),
            "(def (kernel n a) (let loop ((n n) (acc a)) (if (= n 0) acc "
            "(loop (- n 1) (if (< acc %d) (+ (* acc %d) %d) (- acc %d)))))) (kernel 10 %d)",
            threshold, scale, offset, offset, threshold - 1);
        Value expected = eval(vm, source);
        size_t entries = runtime_native_scalar_entries(native);
        assert(eval(native, source) == expected);
        assert(runtime_native_scalar_entries(native) > entries);
    }
    runtime_destroy(vm); runtime_destroy(native);
    puts("24 generated scalar loops match the VM");
}
