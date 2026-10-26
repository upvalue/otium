#include "internal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

static Value eval(Runtime *rt, const char *source) {
    Value result;
    if (!runtime_eval(rt, source, strlen(source), &result)) {
        fprintf(stderr, "%s\n%s\n", source, runtime_error(rt));
        abort();
    }
    assert(rt->native_depth == 0);
    return result;
}

int main(void) {
    Runtime *rt = runtime_create(128 * 1024, 131072);
    assert(rt);
    runtime_native(rt, 1);
    eval(rt, "(def (deep n) (if (= n 0) 0 (+ 1 (deep (- n 1)))))");
    Value argument = value_int(2), result;
    assert(runtime_call(rt, "deep", &argument, 1, &result) && result == value_int(2));
    struct rlimit original, limited;
    assert(getrlimit(RLIMIT_STACK, &original) == 0);
    limited = original;
    if (limited.rlim_cur == RLIM_INFINITY || limited.rlim_cur > 256 * 1024)
        limited.rlim_cur = 256 * 1024;
    assert(setrlimit(RLIMIT_STACK, &limited) == 0);
    argument = value_int(10000);
    assert(runtime_call(rt, "deep", &argument, 1, &result) && result == value_int(10000));
    assert(setrlimit(RLIMIT_STACK, &original) == 0);
    assert(runtime_max_frames(rt) >= 10000);
    assert(runtime_max_native_depth(rt) == NATIVE_DIRECT_LIMIT);
    assert(rt->native_depth == 0);
    argument = value_int(100000);
    assert(!runtime_call(rt, "deep", &argument, 1, &result));
    assert(strstr(runtime_error(rt), "stack limit"));
    assert(rt->native_depth == 0);
    argument = value_int(42);
    assert(runtime_call(rt, "deep", &argument, 1, &result) && result == value_int(42));
    runtime_gc_stress(rt, true);
    assert(eval(rt, "(def (kept n) (let ((p (cons n nil)) (f (fn () (car p)))) (if (= n 0) (f) (+ (kept (- n 1)) (f))))) (kept 120)") == value_int(7260));
    assert(eval(rt, "(def (other n) (+ n 1)) (def (mixed n) (if (= n 0) (other 0) (+ (mixed (- n 1)) (other n)))) (mixed 300)") == value_int(45451));
    runtime_destroy(rt);
    rt = runtime_create(128 * 1024, 32768);
    assert(rt);
    runtime_native(rt, 31);
    runtime_gc_stress(rt, true);
    assert(eval(rt, "(def (other n) (+ n 1)) (def (mixed n) (if (= n 0) (other 0) (+ (mixed (- n 1)) (other n)))) (mixed 300)") == value_int(45451));
    assert(runtime_max_native_depth(rt) == NATIVE_DIRECT_LIMIT);
    runtime_destroy(rt);
    puts("bounded native call tests passed");
}
