#ifndef OTIUM_NATIVE_NUMERIC_H
#define OTIUM_NATIVE_NUMERIC_H

#include "internal.h"
#include <math.h>
#include <float.h>
#include <string.h>

/* Zero is not a language value. It marks a computed, unboxed double here.
 * Loaded values retain their identity until arithmetic produces a new value.
 * No allocation is allowed while these private slots are in use. */
typedef struct { Value value; double real; unsigned kind; } NativeNumber;
static inline __attribute__((always_inline)) NativeNumber native_value(Value value) {
    NativeNumber n = {value, 0, value_is_int(value) ? 1u : 0u};
    if (rt_kind(value, K_FLOAT)) {
        n.kind = 2;
        memcpy(&n.real, object_bytes(value), sizeof(n.real));
    }
    return n;
}
static inline __attribute__((always_inline)) NativeNumber native_float_value(Value value) {
    NativeNumber n = {value, 0, 2};
    memcpy(&n.real, object_bytes(value), sizeof(n.real));
    return n;
}
static inline __attribute__((always_inline)) bool native_number(NativeNumber n) {
    return n.kind != 0;
}
static inline __attribute__((always_inline)) double native_real(NativeNumber n) {
    return n.kind == 1 ? (double)value_to_int(n.value) : n.real;
}
static inline __attribute__((always_inline)) bool native_arithmetic(unsigned id, NativeNumber a, NativeNumber b,
                                     NativeNumber *result) {
    if (a.kind == 1 && b.kind == 1) {
        Value value;
        bool ok = id == B_ADD ? value_int_add(a.value, b.value, &value) :
            id == B_SUB ? value_int_sub(a.value, b.value, &value) :
                          value_int_mul(a.value, b.value, &value);
        if (ok) *result = native_value(value);
        return ok;
    }
    double left = native_real(a), right = native_real(b);
    *result = (NativeNumber){0, id == B_ADD ? left + right :
        id == B_SUB ? left - right : left * right, 2};
    return true;
}
static inline __attribute__((always_inline)) int native_integer_real(intptr_t integer, double real) {
    if (isnan(real)) return 2;
    if (real < (double)(INTPTR_MIN / 2)) return 1;
    if (real >= -(double)(INTPTR_MIN / 2)) return -1;
    intptr_t truncated = (intptr_t)real;
    if (integer < truncated) return -1;
    if (integer > truncated) return 1;
    return (double)truncated < real ? -1 : (double)truncated > real ? 1 : 0;
}
static inline __attribute__((always_inline)) bool native_compare(unsigned id, NativeNumber a, NativeNumber b) {
    int order;
    if (a.kind == 1 && b.kind == 1) {
        intptr_t left = value_to_int(a.value), right = value_to_int(b.value);
        order = left < right ? -1 : left > right ? 1 : 0;
    } else if (a.kind == 1) {
        order = native_integer_real(value_to_int(a.value), native_real(b));
    } else if (b.kind == 1) {
        order = native_integer_real(value_to_int(b.value), native_real(a));
        if (order != 2) order = -order;
    } else {
        double left = native_real(a), right = native_real(b);
        order = isnan(left) || isnan(right) ? 2 : left < right ? -1 : left > right ? 1 : 0;
    }
    return id == B_LT ? order == -1 : id == B_LE ? order == -1 || order == 0 :
        id == B_GT ? order == 1 : id == B_GE ? order == 1 || order == 0 : order == 0;
}
static inline __attribute__((always_inline)) bool native_return(Runtime *rt, NativeNumber n) {
    /* Allocation can move every private pointer. Only the resulting value is
     * used afterward, and rt_leave cannot allocate. */
    Value value = n.value ? n.value : rt_float(rt, n.real);
    return value && rt_leave(rt, value);
}
#endif
