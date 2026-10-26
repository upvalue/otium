#include "internal.h"
#include <assert.h>
#include <math.h>
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
static void same(Value a, Value b) {
    if (rt_kind(a, K_FLOAT)) {
        assert(rt_kind(b, K_FLOAT));
        double x, y;
        memcpy(&x, object_bytes(a), sizeof(x));
        memcpy(&y, object_bytes(b), sizeof(y));
        assert((isnan(x) && isnan(y)) || !memcmp(&x, &y, sizeof(x)));
    } else assert(a == b);
}
static void compare_calls(Runtime *vm, Runtime *native, Value *a, Value *b) {
    Value x, y;
    bool ok = runtime_call(vm, "kernel", a, 3, &x);
    bool actual = runtime_call(native, "kernel", b, 3, &y);
    assert(ok == actual);
    if (ok) same(x, y);
    else assert(!strcmp(runtime_error(vm), runtime_error(native)));
}
int main(void) {
    const size_t thresholds[] = {1, 31};
    for (size_t mode = 0; mode < 2; mode++) {
        Runtime *vm = runtime_create(32768, 8192), *rt = runtime_create(32768, 8192);
        assert(vm && rt);
        runtime_native(rt, thresholds[mode]);
        runtime_gc_stress(vm, true); runtime_gc_stress(rt, true);
        eval(vm, "(def imported-add +) (def + imported-add)");
        eval(rt, "(def imported-add +) (def + imported-add)");
        const char *ops[] = {"+", "-", "*", "<", "<=", ">", ">=", "="};
        const double numbers[] = {0.0, -0.0, 0.5, -1.5, 1.1, -1.1,
            0x1p53, 0x1p62, -0x1p62, 0x1p-1074, -0x1p-1074,
            INFINITY, -INFINITY, NAN};
        const intptr_t integers[] = {0, 1, -1, INTPTR_MAX / 2, INTPTR_MIN / 2,
#if INTPTR_MAX > INT32_MAX
            9007199254740993LL, -9007199254740993LL,
#endif
        };
        Value args[3] = {V_NIL,V_NIL,V_NIL}, native_args[3] = {V_NIL,V_NIL,V_NIL};
        Roots ra, rb;
        roots_push(&vm->heap, &ra, args, 3); roots_push(&rt->heap, &rb, native_args, 3);
        size_t count = sizeof(numbers) / sizeof(*numbers), ints = sizeof(integers) / sizeof(*integers);
        for (size_t op = 0; op < sizeof(ops)/sizeof(*ops); op++) {
            char source[256];
            snprintf(source, sizeof(source), "(def (kernel n a b) (let ((x 0)) "
                "(while (> n 0) (set! x (%s a b)) (set! n (- n 1))) x))", ops[op]);
            eval(vm, source); eval(rt, source);
            args[0] = native_args[0] = value_int(64);
            args[1] = rt_float(vm, 0.5); native_args[1] = rt_float(rt, 0.5);
            args[2] = native_args[2] = value_int(2);
            compare_calls(vm, rt, args, native_args);
            size_t entries = rt->native_numeric_entries;
            compare_calls(vm, rt, args, native_args);
            assert(rt->native_numeric_entries > entries);
            args[0] = native_args[0] = value_int(2);
            for (size_t i = 0; i < count + ints; i++) {
                args[1] = i < count ? rt_float(vm, numbers[i]) : value_int(integers[i-count]);
                native_args[1] = i < count ? rt_float(rt, numbers[i]) : value_int(integers[i-count]);
                for (size_t j = 0; j < count + ints; j++) {
                    args[2] = j < count ? rt_float(vm, numbers[j]) : value_int(integers[j-count]);
                    native_args[2] = j < count ? rt_float(rt, numbers[j]) : value_int(integers[j-count]);
                    compare_calls(vm, rt, args, native_args);
                }
            }
        }
        roots_pop(&rt->heap, &rb); roots_pop(&vm->heap, &ra);
        const char *sources[] = {
            "(def (kernel n) (let ((x 0.0)) (while (> n 0) (set! x (+ x 0.5)) (set! n (- n 1))) x)) (kernel 100)",
            "(def (kernel n) (let loop ((n n) (x 0.0)) (if (= n 0) x (loop (- n 1) (+ x 0.5))))) (kernel 100)",
            "(def (kernel n) (let ((x 0.0)) (while (> n 0) (set! x (+ x 0.1 -0.2 0.3 4 5 6 7)) (set! n (- n 1))) x)) (kernel 100)",
            "(def (kernel n) (let ((x 0.0)) (while (> n 0) (set! x (+ (* 1.0000000000000002 0.9999999999999998) -1.0)) (set! n (- n 1))) x)) (kernel 100)",
            "(def object 1.25) (def (kernel n) (while (> n 0) (set! n (- n 1))) object) (kernel 100)",
            "(eq? (kernel 100) object)",
            "(def accessors (let ((x 0.5)) [(fn (n) (let ((total 0.0)) (while (> n 0) (set! total (+ total x)) (set! n (- n 1))) total)) (fn () (set! x 1.5))])) ((get accessors 0) 100)",
            "((get accessors 1)) ((get accessors 0) 100)",
            "(def hits 0) (def (other n x) (set! hits (+ hits 1)) x) (def (kernel n x) (while (> n 0) (set! x (+ x 0.5)) (set! n (- n 1))) (other n x)) (kernel 100 0.0)",
            "(kernel 100 0.0)", "hits",
            "(def old-add +) (def + (fn (a b) (set! hits (old-add hits 1)) (old-add a b))) (kernel 3 0.0)",
            "hits", "(def + old-add)",
            "(def (kernel n) (let ((x 0.0)) (while (> n 0) (set! x (if (= n 1) nil (+ x 0.5))) (set! n (- n 1))) x)) (kernel 100)"
        };
        for (size_t i = 0; i < sizeof(sources)/sizeof(*sources); i++) {
            Value a = eval(vm, sources[i]), b = eval(rt, sources[i]);
            if (rt_kind(a, K_FLOAT) || value_is_int(a) || !value_is_ptr(a)) same(a,b);
        }
        const char *invalid = "(def (invalid n) (let ((x 0.0)) (while (> n 0) "
            "(set! x (if (= n 1) nil (+ x 0.5))) (set! n (- n 1))) (+ x 1)))";
        eval(rt, invalid);
        Value bad_input = value_int(100), bad_result;
        assert(!runtime_call(rt, "invalid", &bad_input, 1, &bad_result));
        assert(!runtime_call(rt, "invalid", &bad_input, 1, &bad_result));
        assert(strstr(runtime_error(rt), "expected a number"));
        assert(rt->native_numeric_fallbacks > 0);
        eval(rt, "(def (allocation-test n) (let ((x 0.0)) (while (> n 0) "
            "(set! x (+ x 0.5)) (set! n (- n 1))) x)) (allocation-test 100)");
        Value input = value_int(100), answer;
        assert(runtime_call(rt, "allocation-test", &input, 1, &answer));
        size_t collections = runtime_collections(rt), entries = rt->native_numeric_entries;
        assert(runtime_call(rt, "allocation-test", &input, 1, &answer));
        assert(rt->native_numeric_entries == entries + 1);
        assert(runtime_collections(rt) == collections + 1);
        assert(rt_kind(answer, K_FLOAT));
        double real;
        memcpy(&real, object_bytes(answer), sizeof(real));
        assert(real == 50.0);
        const char *overflow = "(def (kernel n a b) (let ((x 0.0)) (while (> n 0) "
            "(set! x (+ a b 0.5)) (set! n (- n 1))) x))";
        eval(vm, overflow); eval(rt, overflow);
        args[0]=native_args[0]=value_int(64); args[1]=native_args[1]=value_int(1); args[2]=native_args[2]=value_int(1);
        compare_calls(vm, rt, args, native_args);
        args[1]=native_args[1]=value_int(INTPTR_MAX/2);
        compare_calls(vm, rt, args, native_args);
        assert(strstr(runtime_error(rt), "overflow"));
        args[1]=native_args[1]=value_int(1);
        compare_calls(vm, rt, args, native_args);
        printf("numeric loops match VM: threshold=%zu entries=%zu fallbacks=%zu\n",
            thresholds[mode], rt->native_numeric_entries, rt->native_numeric_fallbacks);
        runtime_destroy(vm); runtime_destroy(rt);
    }
}
