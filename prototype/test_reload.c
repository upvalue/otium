#include "internal.h"
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

int main(int argc, char **argv) {
    bool native = argc == 2 && !strcmp(argv[1], "--native");
    assert(argc == 1 || native);
    Runtime *rt = runtime_create(8192, 4096);
    assert(rt);
    runtime_native(rt, native ? 1 : 0);
    runtime_gc_stress(rt, true);
    eval(rt, "(def stable 'stable) (def tag :tag) (def held {}) (put! held :self held) "
             "(def current (let ((x 42)) (fn () (fn () (list x 'stable \"kept\"))))) "
             "(put! held :old current)");
    size_t peak_functions = 0, peak_constants = 0, peak_heap = 0;
    for (unsigned i = 0; i < (native ? 40u : 2000u); i++) {
        char source[512];
        snprintf(source, sizeof(source),
            "(def current (let ((x %u)) (fn () (fn () (list x 'stable \"new-%u\"))))) "
            "(car ((current)))", i, i);
        assert(eval(rt, source) == value_int(i));
        assert(eval(rt, "(eq? stable 'stable)") == V_TRUE);
        assert(eval(rt, "(eq? tag :tag)") == V_TRUE);
        assert(eval(rt, "(car (((held :old))))") == value_int(42));
        Value result;
        const char *bad = "(fn () (list \"discarded\" (fn (duplicate duplicate) duplicate)))";
        assert(!runtime_eval(rt, bad, strlen(bad), &result));
        assert(strstr(runtime_error(rt), "duplicate"));
        assert(eval(rt, "(car (((held :old))))") == value_int(42));
        if (rt->function_count > peak_functions) peak_functions = rt->function_count;
        if (rt->constant_count > peak_constants) peak_constants = rt->constant_count;
        if (rt->heap.used > peak_heap) peak_heap = rt->heap.used;
        assert(rt->function_count < 32 && rt->constant_count < 64);
    }
    eval(rt, "(put! held :old nil) (set! current nil) (set! held nil)");
    eval(rt, "nil");
    assert(rt->function_count < 4);
    printf("reload tests passed: peak slots functions=%zu constants=%zu heap=%zu\n",
        peak_functions, peak_constants, peak_heap);
    runtime_destroy(rt);
}
