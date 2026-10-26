#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

static Value fail(Runtime *rt, const char *message) { rt_error(rt, message); return 0; }
static const char *text(Value value, size_t *n) {
    memcpy(n, object_bytes(value), sizeof(*n));
    return (char *)object_bytes(value) + sizeof(*n);
}
size_t rt_utf8_encode(uint32_t cp, char bytes[4]) {
    if (cp < 128) { bytes[0] = (char)cp; return 1; }
    unsigned n = cp < 2048 ? 2 : cp < 65536 ? 3 : 4;
    for (unsigned i = n - 1; i; i--) { bytes[i] = (char)(128 | (cp & 63)); cp >>= 6; }
    bytes[0] = (char)((n == 2 ? 192 : n == 3 ? 224 : 240) | cp);
    return n;
}
Value rt_string_get(Value string, Value key, Value fallback) {
    if (!value_is_int(key) || value_to_int(key) < 0) return fallback;
    size_t n, at = 0, index = (size_t)value_to_int(key); const char *s = text(string, &n);
    for (size_t i = 0; at < n; i++) {
        uint32_t cp; at = rt_utf8_next(s, n, at, &cp);
        if (i == index) return ((Value)cp << 8) | TAG_CHAR;
    }
    return fallback;
}
static char *display(Runtime *rt, size_t first, size_t count, size_t *length) {
    char *bytes = NULL;
    FILE *stream = open_memstream(&bytes, length);
    if (!stream) { fail(rt, "cannot allocate display buffer"); return NULL; }
    for (size_t i = 0; i < count; i++) runtime_print(rt, stream, rt->stack[first + i]);
    bool ok = !ferror(stream);
    if (fclose(stream)) ok = false;
    if (!ok) { free(bytes); fail(rt, "cannot format value"); return NULL; }
    return bytes;
}
static Value copied(Runtime *rt, const char *bytes, size_t n) {
    char *copy = malloc(n ? n : 1);
    if (!copy) return fail(rt, "out of host memory");
    memcpy(copy, bytes, n);
    Value result = rt_text(rt, K_STRING, copy, n); free(copy); return result;
}
static bool append_buffer(Runtime *rt, Value *buffer, const char *bytes, size_t n) {
    BufferInfo *info = object_bytes(*buffer);
    if (n > SIZE_MAX - info->length) return rt_error(rt, "buffer too large");
    size_t needed = info->length + n;
    if (needed > info->capacity) {
        size_t capacity = info->capacity < 32 ? 32 : info->capacity;
        while (capacity < needed) { if (capacity > SIZE_MAX / 2) { capacity = needed; break; } capacity *= 2; }
        Roots roots; roots_push(&rt->heap, &roots, buffer, 1);
        Value data = rt_alloc(rt, K_BUFFER_DATA, 0, capacity);
        roots_pop(&rt->heap, &roots);
        if (!data) return false;
        info = object_bytes(*buffer);
        if (info->length) memcpy(object_bytes(data), object_bytes(object_fields(*buffer)[0]), info->length);
        object_fields(*buffer)[0] = data; info->capacity = capacity;
    }
    if (n) memcpy((char *)object_bytes(object_fields(*buffer)[0]) + info->length, bytes, n);
    info->length = needed;
    for (size_t p = 0; p < n;) { uint32_t cp; p = rt_utf8_next(bytes, n, p, &cp); info->characters++; }
    return true;
}
Value rt_string_builtin(Runtime *rt, unsigned id, size_t first, size_t count) {
    if (id == B_STR || id == B_BUFFER || id == B_BUFFER_PUSH) {
        if (id == B_BUFFER && count > 1) return fail(rt, "buffer expects at most one argument");
        if (id == B_BUFFER_PUSH && (!count || !rt_kind(rt->stack[first], K_BUFFER))) return fail(rt, "expected buffer");
        size_t n; char *bytes = display(rt, first + (id == B_BUFFER_PUSH), count - (id == B_BUFFER_PUSH), &n);
        if (!bytes) return 0;
        Value result;
        if (id == B_STR) result = rt_text(rt, K_STRING, bytes, n);
        else {
            result = id == B_BUFFER ? rt_alloc(rt, K_BUFFER, 1, sizeof(BufferInfo)) : rt->stack[first];
            if (result && !append_buffer(rt, &result, bytes, n)) result = 0;
        }
        free(bytes); return result;
    }
    if (id == B_STRING_LT) {
        if (count < 2) return fail(rt, "string<? needs at least two strings");
        bool result = true;
        for (size_t i = 0; i < count; i++) {
            if (!rt_kind(rt->stack[first + i], K_STRING)) return fail(rt, "expected string");
            if (i) {
                size_t a, b; const char *x = text(rt->stack[first + i - 1], &a), *y = text(rt->stack[first + i], &b);
                int order = memcmp(x, y, a < b ? a : b);
                result &= order < 0 || (!order && a < b);
            }
        }
        return result ? V_TRUE : V_FALSE;
    }
    if (count != 1) return fail(rt, "wrong number of arguments");
    Value value = rt->stack[first];
    if (id == B_BUFFER_STRING) {
        if (!rt_kind(value, K_BUFFER)) return fail(rt, "expected buffer");
        size_t n = ((BufferInfo *)object_bytes(value))->length;
        return copied(rt, n ? object_bytes(object_fields(value)[0]) : "", n);
    }
    if (id == B_NAME || id == B_SYMBOL) {
        if (!rt_kind(value, K_STRING) && !rt_kind(value, K_SYMBOL) && !rt_kind(value, K_KEYWORD)) return fail(rt, "expected string, symbol or keyword");
        size_t n; const char *bytes = text(value, &n);
        if (rt_kind(value, K_KEYWORD)) { bytes++; n--; }
        return id == B_NAME ? copied(rt, bytes, n) : rt_kind(value, K_SYMBOL) ? value : rt_intern(rt, K_SYMBOL, bytes, n);
    }
    if (id == B_STRING_LIST) {
        if (!rt_kind(value, K_STRING)) return fail(rt, "expected string");
        Value roots[3] = {value, V_EMPTY, V_EMPTY}; Roots root;
        roots_push(&rt->heap, &root, roots, 3);
        size_t n; text(value, &n);
        for (size_t at = 0; at < n;) {
            size_t ignored; uint32_t cp;
            at = rt_utf8_next(text(roots[0], &ignored), n, at, &cp);
            Value pair = rt_alloc(rt, K_PAIR, 2, 0);
            if (!pair) { roots[1] = 0; break; }
            object_fields(pair)[0] = ((Value)cp << 8) | TAG_CHAR;
            object_fields(pair)[1] = V_EMPTY;
            if (roots[1] == V_EMPTY) roots[1] = pair;
            else object_fields(roots[2])[1] = pair;
            roots[2] = pair;
        }
        Value result = roots[1]; roots_pop(&rt->heap, &root); return result;
    }
    if (id == B_STRING_NUMBER) {
        if (!rt_kind(value, K_STRING)) return fail(rt, "expected string");
        size_t n; const char *bytes = text(value, &n);
        while (n && isspace((unsigned char)*bytes)) { bytes++; n--; }
        while (n && isspace((unsigned char)bytes[n - 1])) n--;
        if (!n) return V_NIL;
        for (size_t i = 0; i < n; i++)
            if (isspace((unsigned char)bytes[i]) || strchr(";()[]{}\"'`,", bytes[i]) || !bytes[i]) return V_NIL;
        Reader reader; reader_init(&reader, bytes, n, 1024 * 1024);
        Ast *form = reader_one(&reader);
        Value result = V_NIL;
        if (form && !reader.error) {
            if (form->kind == AST_INT && form->as.integer >= INTPTR_MIN / 2 && form->as.integer <= INTPTR_MAX / 2) result = value_int((intptr_t)form->as.integer);
            else if (form->kind == AST_FLOAT) result = rt_float(rt, form->as.real);
        }
        reader_destroy(&reader); return result;
    }
    if (id == B_INTEGER_CHAR) {
        if (!value_is_int(value)) return fail(rt, "expected integer code point");
        intptr_t cp = value_to_int(value);
        if (cp < 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return fail(rt, "invalid character code point");
        return ((Value)cp << 8) | TAG_CHAR;
    }
    if ((value & 255) != TAG_CHAR) return fail(rt, "expected character");
    uint32_t cp = (uint32_t)(value >> 8);
    switch (id) {
    case B_CHAR_INTEGER: return value_int(cp);
    case B_CHAR_UP: if (cp >= 'a' && cp <= 'z') cp -= 32; return ((Value)cp << 8) | TAG_CHAR;
    case B_CHAR_DOWN: if (cp >= 'A' && cp <= 'Z') cp += 32; return ((Value)cp << 8) | TAG_CHAR;
    case B_CHAR_SPACE: return cp == ' ' || (cp >= 9 && cp <= 13) ? V_TRUE : V_FALSE;
    case B_CHAR_ALPHA: return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') ? V_TRUE : V_FALSE;
    case B_CHAR_NUMERIC: return cp >= '0' && cp <= '9' ? V_TRUE : V_FALSE;
    case B_DIGIT_VALUE: return cp >= '0' && cp <= '9' ? value_int(cp - '0') : V_NIL;
    default: return fail(rt, "unknown string builtin");
    }
}
