#include "internal.h"
#include <stdlib.h>
#include <string.h>

static Value fail(Runtime *rt, const char *message) { rt_error(rt, message); return 0; }
static ArrayInfo *info(Value a) { return object_bytes(a); }
static Value *items(Value a) { return info(a)->capacity ? object_fields(object_fields(a)[0]) : NULL; }

static bool reserve(Runtime *rt, Value *array, size_t needed) {
    size_t capacity = info(*array)->capacity;
    if (needed <= capacity) return true;
    if (needed > UINT32_MAX) return rt_error(rt, "array too large");
    size_t grown = capacity > UINT32_MAX / 2 ? UINT32_MAX : capacity * 2;
    if (grown < 4) grown = 4;
    if (grown < needed) grown = needed;
    Roots root;
    roots_push(&rt->heap, &root, array, 1);
    Value storage = rt_alloc(rt, K_ARRAY_DATA, (uint32_t)grown, 0);
    if (storage) {
        if (info(*array)->length)
            memcpy(object_fields(storage), items(*array), info(*array)->length * sizeof(Value));
        object_fields(*array)[0] = storage;
        info(*array)->capacity = grown;
    }
    roots_pop(&rt->heap, &root);
    return storage != 0;
}

static Value array_new(Runtime *rt, size_t length) {
    Value a = rt_alloc(rt, K_ARRAY, 1, sizeof(ArrayInfo));
    if (!a) return 0;
    *info(a) = (ArrayInfo){0, 0};
    if (!reserve(rt, &a, length)) return 0;
    return a;
}

static bool index_of(Value key, size_t length, size_t *index) {
    if (!value_is_int(key) || value_to_int(key) < 0) return false;
    *index = (size_t)value_to_int(key);
    return *index < length;
}

static Value get(Runtime *rt, Value a, Value key, Value fallback) {
    if (a == V_NIL) return fallback;
    if (rt_kind(a, K_BYTEVECTOR)) {
        size_t index;
        return index_of(key, rt_bytes_length(a), &index) ? value_int(rt_bytes_data(a)[index]) : fallback;
    }
    if (rt_kind(a, K_STRING)) return rt_string_get(a, key, fallback);
    if (rt_kind(a, K_TABLE)) return rt_table_get(rt, a, key, fallback);
    if (!rt_kind(a, K_ARRAY)) return fail(rt, "expected a collection or nil");
    size_t index;
    return index_of(key, info(a)->length, &index) ? items(a)[index] : fallback;
}

Value rt_collection_call(Runtime *rt, size_t callable, size_t first, size_t count) {
    if (count < 1 || count > 2) return fail(rt, "array call needs an index and optional default");
    Value a = rt->stack[callable], b = rt->stack[first];
    Value fallback = count == 2 ? rt->stack[first + 1] : V_NIL;
    if (rt_kind(a, K_KEYWORD)) return rt_kind(b, K_TABLE) ? rt_table_get(rt, b, a, fallback) : fallback;
    if (rt_kind(a, K_ARRAY) && !value_is_int(b)) return fail(rt, "array index must be an integer");
    return get(rt, a, b, fallback);
}

typedef struct { Value a, b; } Comparison;
static size_t pair_hash(Comparison p) {
    uintptr_t x = (p.a >> 3) ^ ((p.b >> 3) * (uintptr_t)0x9e3779b1u);
    x ^= x >> 16; x *= (uintptr_t)0x85ebca6bu; x ^= x >> 13;
    return x;
}

/* No managed allocation occurs during equality. The work list and seen set can
 * safely hold heap addresses, including cycles, until this call returns. */
Value rt_equal(Runtime *rt, Value a, Value b, bool keys) {
    if (a == b) return V_TRUE;
    if (!value_is_ptr(a) || !value_is_ptr(b) || object_kind(a) != object_kind(b)) return V_FALSE;
    Comparison *work = NULL, *seen = NULL;
    size_t capacity = 0, count = 0, seen_capacity = 0, seen_count = 0;
    Value result = V_TRUE;
    if (!rt_grow(rt, (void **)&work, &capacity, 1, sizeof(*work))) return 0;
    work[count++] = (Comparison){a, b};
    while (count) {
        Comparison p = work[--count];
        if (p.a == p.b) continue;
        if (!value_is_ptr(p.a) || !value_is_ptr(p.b) || object_kind(p.a) != object_kind(p.b)) {
            result = V_FALSE; break;
        }
        unsigned kind = object_kind(p.a);
        if (kind == K_FLOAT) {
            double x, y;
            memcpy(&x, object_bytes(p.a), sizeof(x)); memcpy(&y, object_bytes(p.b), sizeof(y));
            if (x != y) { result = V_FALSE; break; }
            continue;
        }
        if (kind == K_STRING || kind == K_SYMBOL || kind == K_KEYWORD) {
            size_t x, y;
            memcpy(&x, object_bytes(p.a), sizeof(x)); memcpy(&y, object_bytes(p.b), sizeof(y));
            if (x != y || memcmp((char *)object_bytes(p.a) + sizeof(x),
                                (char *)object_bytes(p.b) + sizeof(y), x)) { result = V_FALSE; break; }
            continue;
        }
        if (kind != K_PAIR && (kind != K_ARRAY || keys)) { result = V_FALSE; break; }
        if (seen_count >= seen_capacity / 2) {
            size_t next = seen_capacity ? seen_capacity * 2 : 64;
            if (next < seen_capacity || next > SIZE_MAX / sizeof(*seen)) { result = fail(rt, "equality too large"); break; }
            Comparison *grown = calloc(next, sizeof(*grown));
            if (!grown) { result = fail(rt, "out of host memory"); break; }
            for (size_t i = 0; i < seen_capacity; i++) if (seen[i].a) {
                size_t slot = pair_hash(seen[i]) & (next - 1);
                while (grown[slot].a) slot = (slot + 1) & (next - 1);
                grown[slot] = seen[i];
            }
            free(seen); seen = grown; seen_capacity = next;
        }
        size_t slot = pair_hash(p) & (seen_capacity - 1);
        while (seen[slot].a && (seen[slot].a != p.a || seen[slot].b != p.b))
            slot = (slot + 1) & (seen_capacity - 1);
        if (seen[slot].a) continue;
        seen[slot] = p; seen_count++;
        size_t length = kind == K_PAIR ? 2 : info(p.a)->length;
        if (kind == K_ARRAY && length != info(p.b)->length) { result = V_FALSE; break; }
        if (length > SIZE_MAX - count || !rt_grow(rt, (void **)&work, &capacity, count + length, sizeof(*work))) {
            result = fail(rt, "equality too large"); break;
        }
        Value *left = kind == K_PAIR ? object_fields(p.a) : items(p.a);
        Value *right = kind == K_PAIR ? object_fields(p.b) : items(p.b);
        for (size_t i = 0; i < length; i++) work[count++] = (Comparison){left[i], right[i]};
    }
    free(work); free(seen);
    return result;
}

Value rt_collection_builtin(Runtime *rt, unsigned id, size_t first, size_t count) {
    if (id >= B_TABLE || (count && rt_kind(rt->stack[first], K_TABLE) && id != B_ARRAY && id != B_EQUAL && id != B_ARRAY_P))
        return rt_table_builtin(rt, id, first, count);
    if (id == B_ARRAY) {
        Value a = array_new(rt, count);
        if (!a) return 0;
        if (count) memcpy(items(a), rt->stack + first, count * sizeof(Value));
        info(a)->length = count;
        return a;
    }
    size_t min = id == B_GET || id == B_EQUAL || id == B_CONTAINS ? 2 : id == B_PUT_MUT ? 3 : 1;
    size_t max = id == B_GET ? 3 : id == B_PUSH || id == B_PUT_MUT ? SIZE_MAX : min;
    if (count < min || count > max || (id == B_PUT_MUT && !(count % 2))) return fail(rt, "wrong number of collection arguments");
    Value a = rt->stack[first], b = count > 1 ? rt->stack[first + 1] : V_NIL;
    if (id == B_COPY && rt_kind(a,K_INSTANCE)) return rt_instance_copy(rt,a);
    if (id == B_COPY && a == V_NIL) return V_NIL;
    if (id == B_CONTAINS && a == V_NIL) return V_FALSE;
    if (id == B_CONTAINS && rt_kind(a, K_STRING)) {
        if (!rt_kind(b, K_STRING)) return fail(rt, "expected substring");
        size_t n, m; memcpy(&n, object_bytes(a), sizeof(n)); memcpy(&m, object_bytes(b), sizeof(m));
        const char *s = (char *)object_bytes(a) + sizeof(n), *part = (char *)object_bytes(b) + sizeof(m);
        if (m > n) return V_FALSE;
        for (size_t i = 0; i <= n - m; i++) if (!memcmp(s + i, part, m)) return V_TRUE;
        return V_FALSE;
    }
    if (id == B_CONTAINS && (a == V_EMPTY || rt_kind(a, K_PAIR))) {
        Value slow = a, fast = a;
        while (rt_kind(a, K_PAIR)) {
            Value equal = rt_equal(rt, object_fields(a)[0], b, false);
            if (equal != V_FALSE) return equal;
            a = object_fields(a)[1];
            if (rt_kind(slow, K_PAIR)) slow = object_fields(slow)[1];
            for (unsigned i = 0; i < 2 && rt_kind(fast, K_PAIR); i++) fast = object_fields(fast)[1];
            if (rt_kind(fast, K_PAIR) && fast == slow) return fail(rt, "cyclic list");
        }
        return a == V_EMPTY ? V_FALSE : fail(rt, "expected proper list");
    }
    if (id == B_ARRAY_P) return rt_kind(a, K_ARRAY) ? V_TRUE : V_FALSE;
    if (id == B_EQUAL) return rt_equal(rt, a, b, false);
    if (id == B_GET) return get(rt, a, b, count == 3 ? rt->stack[first + 2] : V_NIL);
    if (id == B_LENGTH && (a == V_NIL || a == V_EMPTY)) return value_int(0);
    if (id == B_LENGTH && rt_kind(a, K_BUFFER)) return value_int((intptr_t)((BufferInfo *)object_bytes(a))->characters);
    if (id == B_LENGTH && rt_kind(a, K_STRING)) return rt_support_builtin(rt, B_STRING_LENGTH, first, count);
    if (id == B_LENGTH && rt_kind(a, K_PAIR)) {
        size_t length = 0; Value slow = a, fast = a;
        while (rt_kind(a, K_PAIR)) {
            length++; a = object_fields(a)[1];
            if (rt_kind(slow, K_PAIR)) slow = object_fields(slow)[1];
            for (unsigned i = 0; i < 2 && rt_kind(fast, K_PAIR); i++) fast = object_fields(fast)[1];
            if (rt_kind(fast, K_PAIR) && fast == slow) return fail(rt, "length needs an acyclic list");
        }
        if (a != V_EMPTY) return fail(rt, "length needs a proper list");
        return value_int((intptr_t)length);
    }
    if (rt_kind(a, K_BYTEVECTOR)) {
        if (id == B_LENGTH) return value_int((intptr_t)rt_bytes_length(a));
        if (id == B_COPY) return rt_bytes_builtin(rt, B_BYTES_COPY, first, count);
        if (id == B_PUT_MUT) {
            for (size_t i = 1; i < count; i += 2) {
                size_t index; Value byte = rt->stack[first + i + 1];
                if (!index_of(rt->stack[first + i], rt_bytes_length(a), &index)) return fail(rt, "bytevector index out of bounds");
                if (!value_is_int(byte) || value_to_int(byte) < 0 || value_to_int(byte) > 255) return fail(rt, "expected byte");
                rt_bytes_data(a)[index] = (unsigned char)value_to_int(byte);
            }
            return a;
        }
    }
    if (!rt_kind(a, K_ARRAY)) return fail(rt, "expected an array");
    switch (id) {
    case B_LENGTH: return value_int((intptr_t)info(a)->length);
    case B_PUT_MUT: {
        for (size_t i = 1; i < count; i += 2) {
            size_t index;
            if (!index_of(rt->stack[first + i], info(a)->length, &index)) return fail(rt, "array index out of bounds");
            items(a)[index] = rt->stack[first + i + 1];
        }
        return a;
    }
    case B_PUSH: {
        size_t length = info(a)->length;
        if (count - 1 > UINT32_MAX - length) return fail(rt, "array too large");
        if (!reserve(rt, &a, length + count - 1)) return 0;
        if (count > 1) memcpy(items(a) + length, rt->stack + first + 1, (count - 1) * sizeof(Value));
        info(a)->length = length + count - 1;
        return a;
    }
    case B_POP: {
        if (!info(a)->length) return V_NIL;
        size_t index = --info(a)->length;
        Value result = items(a)[index];
        items(a)[index] = V_NIL;
        return result;
    }
    case B_COPY: {
        size_t length = info(a)->length;
        Value copy = array_new(rt, length);
        if (!copy) return 0;
        a = rt->stack[first];
        if (length) memcpy(items(copy), items(a), length * sizeof(Value));
        info(copy)->length = length;
        return copy;
    }
    case B_CONTAINS:
        for (size_t i = 0; i < info(a)->length; i++) {
            Value result = rt_equal(rt, items(a)[i], b, false);
            if (result != V_FALSE) return result;
        }
        return V_FALSE;
    }
    return fail(rt, "unknown collection operation");
}
