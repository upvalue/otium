#include "internal.h"
#include <math.h>
#include <string.h>

static const char *names[] = {
    "+", "-", "*", "/", "<", "<=", ">", ">=", "=", "remainder", "modulo",
    "eq?", "not", "cons", "car", "cdr", "list", "null?", "nil?", "pair?",
    "integer?", "number?", "set-car!", "set-cdr!", "print", "println", "error",
    "ffi/open-library", "ffi/bind", "ffi/has-symbol?", "ffi/pointer?", "ffi/null?", "ffi/pointer=?",
    "ffi/alloc", "ffi/free!", "ffi/ref", "ffi/set!", "ffi/sizeof", "ffi/alignof", "ffi/view",
    "ffi/alloc-utf8", "ffi/read-utf8", "ffi/allocation-count", "ffi/allocation-bytes", "ffi/copy!", "ffi/read-bytes", "ffi/write-bytes!", "ffi/read-utf8-bytes", "ffi/%null", "ffi/%sizeof", "ffi/%alignof", "ffi/%bind", "ffi/%open", "ffi/%loader-error", "ffi/%alloc", "ffi/%ref", "ffi/%set!", "ffi/%fail", "ffi/%error-field", "ffi/%layout", "ffi/%layout-data",
    "array", "array?", "length", "get", "put!", "push!", "pop!", "copy", "equal?", "contains?", "table", "table?", "keys", "values", "apply",
    "symbol?", "keyword?", "string?", "boolean?", "list?", "procedure?", "symbol->string", "string->symbol", "keyword", "gensym", "string-length", "string-ref", "substring", "expander-application", "expander-optional-function", "expander-macro-var", "expander-invoke-macro", "expander-native-syntax?", "expander-locate", "expander-fail", "expander-qualify", "expander-local-name", "string-append", "number->string", "pos?", "neg?", "read-string", "eval", "int?", "float?", "macro?", "char?", "display", "write", "newline", "define-condition", "condition-type", "condition-message", "condition-of-type?", "error?", "class?", "dispatch-type", "class-subtype?", "generic?", "generic-methods", "%define-generic", "%define-method", "remove-method!", "%define-class", "make", "instance?", "slot-set!", "class-properties", "class-mount", "class-update-properties!", "%make-record-type", "record-new", "record?", "record-type?", "record-type", "record-ref", "record-set!", "quotient", "abs", "min", "max", "floor", "ceiling", "round", "truncate", "sqrt", "exp", "log", "sin", "cos", "tan", "asin", "acos", "atan", "expt", "exact", "inexact", "exact?", "inexact?", "nan?", "infinite?", "finite?", "bitwise-and", "bitwise-ior", "bitwise-xor", "bitwise-not", "bitwise-arithmetic-shift", "str", "string<?", "string->number", "buffer", "buffer-push!", "buffer->string", "char->integer", "integer->char", "char-upcase", "char-downcase", "char-whitespace?", "char-alphabetic?", "char-numeric?", "digit-value", "name", "symbol", "string->list", "type", "eof-object", "eof-object?", "buffer?", "current-ns", "describe", "signal", "bytevector", "bytevector?", "make-bytevector", "bytevector-length", "bytevector-u8-ref", "bytevector-u8-set!", "bytevector-copy", "bytevector-copy!", "bytevector-append", "utf8->string", "string->utf8", "find-restart", "compute-restarts", "restart-name", "restart-description", "invoke-restart", "exit", "quit", "current-jiffy", "jiffies-per-second", "current-second", "port?", "input-port?", "output-port?", "input-port-open?", "output-port-open?", "open-input-string", "open-output-string", "get-output-string", "open-input-bytevector", "open-output-bytevector", "get-output-bytevector", "close-port", "close-input-port", "close-output-port", "read-u8", "peek-u8", "read-char", "peek-char", "read-line", "read-all", "%read-characters", "read-bytevector", "read-bytevector!", "write-u8", "write-char", "write-string", "write-bytevector", "flush-output-port", "display-to", "write-to", "read-until", "read-while", "skip-while", "read", "make-input-port", "make-output-port", "open-ports", "%standard-input", "%standard-output", "%standard-error", "open-input-file", "open-output-file", "open-binary-input-file", "open-binary-output-file", "file-exists?", "delete-file", "rename-file", "file-info", "list-directory", "make-directory", "file-symbolic-link?", "current-directory", "otium.process/arguments", "otium.process/script-name", "otium.reader/tokens"
};
_Static_assert(sizeof(names) / sizeof(names[0]) == B_QQ_APPEND, "public builtin names must match their IDs");

int rt_builtin_index(const char *name) {
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (!strcmp(name, names[i])) return (int)i;
    return -1;
}

bool rt_builtins(Runtime *rt) {
    uint32_t ffi = rt_namespace(rt, "ffi", 3);
    uint32_t process = rt_namespace(rt, "otium.process", 13);
    uint32_t reader=rt_namespace(rt,"otium.reader",12);
    if (ffi == UINT32_MAX || process == UINT32_MAX || reader==UINT32_MAX) return false;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        bool foreign = !strncmp(names[i], "ffi/", 4);
        bool process_binding = !strncmp(names[i], "otium.process/", 14);
        bool reader_binding=!strncmp(names[i],"otium.reader/",13);
        const char *name = names[i] + (foreign ? 4 : process_binding ? 14 : reader_binding ? 13 : 0);
        rt->current_namespace = foreign ? ffi : process_binding ? process : reader_binding ? reader : 0;
        uint32_t global = rt_global(rt, name, strlen(name));
        if (global == UINT32_MAX) return false;
        rt->globals[global] = ((Value)i << 8) | TAG_BUILTIN;
    }
    rt->current_namespace = 0;
    return true;
}

bool rt_number(Runtime *rt, Value v, double *out) {
    if (value_is_int(v)) { *out = (double)value_to_int(v); return true; }
    if (rt_kind(v, K_FLOAT)) { memcpy(out, object_bytes(v), sizeof(*out)); return true; }
    return rt_error(rt, "expected a number");
}

static Value boolean(bool b) { return b ? V_TRUE : V_FALSE; }

static Value arithmetic(Runtime *rt, unsigned id, size_t first, size_t count) {
    if (!count && (id == B_SUB || id == B_DIV)) { rt_error(rt, "arithmetic needs an argument"); return 0; }
    intptr_t integer = id == B_MUL || id == B_DIV ? 1 : 0;
    double real = 0;
    bool exact = !(id == B_DIV && count == 1);
    for (size_t j = 0; j < count; j++) {
        if (!value_is_int(rt->stack[first + j])) {
            double ignored;
            if (!rt_number(rt, rt->stack[first + j], &ignored)) return 0;
            exact = false;
        }
    }
    if (!exact) real = (double)integer;
    size_t i = 0;
    if ((id == B_SUB || id == B_DIV) && count > 1) {
        Value v = rt->stack[first];
        if (exact) integer = value_to_int(v);
        else if (!rt_number(rt, v, &real)) return 0;
        i = 1;
    }
    for (; i < count; i++) {
        Value v = rt->stack[first + i];
        if (exact && value_is_int(v)) {
            intptr_t operand = value_to_int(v), next = 0;
            uintptr_t bits = 0;
            switch (id) {
            case B_ADD: bits = (uintptr_t)integer + (uintptr_t)operand; break;
            case B_SUB: bits = (uintptr_t)integer - (uintptr_t)operand; break;
            case B_MUL: bits = (uintptr_t)integer * (uintptr_t)operand; break;
            case B_DIV:
                if (!operand) { rt_error(rt, "division by zero"); return 0; }
                if (integer % operand) { exact = false; real = (double)integer / (double)operand; continue; }
                bits = (uintptr_t)(integer / operand); break;
            }
            next = value_to_int((bits << 1) | 1);
            integer = next;
        } else {
            double operand;
            if (!rt_number(rt, v, &operand)) return 0;
            if (exact) { exact = false; real = (double)integer; }
            switch (id) {
            case B_ADD: real += operand; break;
            case B_SUB: real -= operand; break;
            case B_MUL: real *= operand; break;
            case B_DIV:
                if (operand == 0) { rt_error(rt, "division by zero"); return 0; }
                real /= operand; break;
            }
        }
    }
    return exact ? value_int(integer) : rt_float(rt, real);
}

/* Compare integers without rounding them through double, including on hosts
 * where long double has no more precision than double. Two means unordered. */
int rt_integer_real(intptr_t integer, double real) {
    if (isnan(real)) return 2;
    if (real < (double)(INTPTR_MIN / 2)) return 1;
    if (real >= -(double)(INTPTR_MIN / 2)) return -1;
    intptr_t truncated = (intptr_t)real;
    if (integer < truncated) return -1;
    if (integer > truncated) return 1;
    return (double)truncated < real ? -1 : (double)truncated > real ? 1 : 0;
}

static Value compare(Runtime *rt, unsigned id, size_t first, size_t count) {
    if (count < 2) { rt_error(rt, "comparison needs at least two arguments"); return 0; }
    bool result = true;
    Value previous = V_NIL;
    for (size_t i = 0; i < count; i++) {
        Value v = rt->stack[first + i];
        double current = 0;
        if (!value_is_int(v) && !rt_number(rt, v, &current)) return 0;
        if (i) {
            int order;
            if (value_is_int(previous) && value_is_int(v)) {
                intptr_t left = value_to_int(previous), right = value_to_int(v);
                order = left < right ? -1 : left > right ? 1 : 0;
            } else if (value_is_int(previous)) order = rt_integer_real(value_to_int(previous), current);
            else {
                double left = 0;
                if (!rt_number(rt, previous, &left)) return 0;
                if (value_is_int(v)) {
                    order = rt_integer_real(value_to_int(v), left);
                    if (order != 2) order = -order;
                } else order = isnan(left) || isnan(current) ? 2 : left < current ? -1 : left > current ? 1 : 0;
            }
            switch (id) {
            case B_LT: result &= order == -1; break;
            case B_LE: result &= order == -1 || order == 0; break;
            case B_GT: result &= order == 1; break;
            case B_GE: result &= order == 1 || order == 0; break;
            case B_NUMEQ: result &= order == 0; break;
            }
        }
        previous = v;
    }
    return boolean(result);
}

/* Quasiquote copies only the splice prefix; its final tail can be any value. */
static Value quasi_append(Runtime *rt, size_t first, size_t count) {
    if (count != 2) { rt_error(rt, "invalid quasiquote splice arity"); return 0; }
    Value slow = rt->stack[first], fast = slow;
    while (rt_kind(fast, K_PAIR)) {
        fast = object_fields(fast)[1];
        if (!rt_kind(fast, K_PAIR)) break;
        fast = object_fields(fast)[1];
        slow = object_fields(slow)[1];
        if (fast == slow) { rt_error(rt, "cyclic quasiquote splice"); return 0; }
    }
    if (fast != V_EMPTY) { rt_error(rt, "quasiquote splice needs a proper list"); return 0; }
    Value slots[] = {rt->stack[first], rt->stack[first + 1], V_EMPTY, V_EMPTY};
    Roots roots;
    roots_push(&rt->heap, &roots, slots, 4);
    while (slots[0] != V_EMPTY) {
        Value pair = rt_alloc(rt, K_PAIR, 2, 0);
        if (!pair) { slots[2] = 0; break; }
        object_fields(pair)[0] = object_fields(slots[0])[0];
        object_fields(pair)[1] = slots[1];
        if (slots[2] == V_EMPTY) slots[2] = pair;
        else object_fields(slots[3])[1] = pair;
        slots[3] = pair;
        slots[0] = object_fields(slots[0])[1];
    }
    Value result = slots[2] == V_EMPTY ? slots[1] : slots[2];
    roots_pop(&rt->heap, &roots);
    return result;
}

Value rt_builtin(Runtime *rt, unsigned id, size_t first, size_t count) {
    if (id == B_EVAL_IN_NAMESPACE) {
        if (count != 2) { rt_error(rt, "invalid evaluator call"); return 0; }
        uint32_t saved = rt->current_namespace;
        rt->current_namespace = (uint32_t)value_to_int(rt->stack[first]);
        Value result = rt_eval_value(rt, rt->stack[first + 1]);
        rt->current_namespace = saved;
        return result;
    }
    if (id == B_QQ_APPEND) return quasi_append(rt, first, count);
    if (id == B_READER_TOKENS) return rt_tokens(rt,first,count);
    if (id >= B_PROCESS_ARGUMENTS) return rt_process_builtin(rt, id, count);
    if (id >= B_FILE_EXISTS) return rt_filesystem_builtin(rt,id,first,count);
    if (id >= B_PORT_P) return rt_port_builtin(rt, id, first, count);
    if (id == B_PRINT || id == B_PRINTLN || id == B_DISPLAY || id == B_WRITE || id == B_NEWLINE) return rt_port_print(rt, id, first, count);
    if (id >= B_CURRENT_JIFFY) return rt_time_builtin(rt, id, count);
    if (id == B_EXIT || id == B_QUIT) {
        if (count > 1 || (count && (!value_is_int(rt->stack[first]) || value_to_int(rt->stack[first]) < 0 || value_to_int(rt->stack[first]) > 255))) {
            rt_error(rt, "exit status must be an integer from 0 to 255"); return 0;
        }
        rt->exit_status = count ? (int)value_to_int(rt->stack[first]) : 0;
        rt->exit_pending = true; rt_error(rt, "exit requested"); return 0;
    }
    if (id >= B_FIND_RESTART) return rt_restart_builtin(rt, id, first, count);
    if (id >= B_BYTES) return rt_bytes_builtin(rt, id, first, count);
    if (id == B_SIGNAL) return rt_signal(rt, first, count);
    if (id >= B_TYPE) return rt_reflection_builtin(rt, id, first, count);
    if (id >= B_STR) return rt_string_builtin(rt, id, first, count);
    if (id >= B_QUOTIENT) return rt_numeric_builtin(rt, id, first, count);
    if (id >= B_CLASS_P) return rt_class_builtin(rt, id, first, count);
    if (id >= B_DEFINE_CONDITION) return rt_condition_builtin(rt, id, first, count);
    if (id >= B_SYMBOL_P) return rt_support_builtin(rt, id, first, count);
    if (id >= B_ARRAY) return rt_collection_builtin(rt, id, first, count);
    if (id >= B_FFI_OPEN) return rt_foreign_builtin(rt, id, first, count);
    if (id <= B_DIV) return arithmetic(rt, id, first, count);
    if (id <= B_NUMEQ) return compare(rt, id, first, count);
    if (id == B_LIST) {
        Value result = V_EMPTY;
        Roots root;
        roots_push(&rt->heap, &root, &result, 1);
        for (size_t i = count; i > 0; i--) {
            Value pair = rt_alloc(rt, K_PAIR, 2, 0);
            if (!pair) { result = 0; break; }
            object_fields(pair)[0] = rt->stack[first + i - 1];
            object_fields(pair)[1] = result;
            result = pair;
        }
        roots_pop(&rt->heap, &root);
        return result;
    }
    if (id == B_ERROR) return rt_raise(rt, first, count);
    size_t arity = id == B_REMAINDER || id == B_MODULO || id == B_EQ || id == B_CONS ||
        id == B_SET_CAR || id == B_SET_CDR ? 2 : 1;
    if (count != arity) { rt_error(rt, "wrong number of builtin arguments"); return 0; }
    Value a = rt->stack[first], b = arity == 2 ? rt->stack[first + 1] : V_NIL;
    switch (id) {
    case B_REMAINDER: case B_MODULO: {
        if (!value_is_int(a) || !value_is_int(b)) { rt_error(rt, "expected integers"); return 0; }
        intptr_t divisor = value_to_int(b);
        if (!divisor) { rt_error(rt, "division by zero"); return 0; }
        intptr_t r = value_to_int(a) % divisor;
        if (id == B_MODULO && r && ((r < 0) != (divisor < 0))) r += divisor;
        return value_int(r);
    }
    case B_EQ: return boolean(a == b);
    case B_NOT: return boolean(!rt_truth(a));
    case B_NULL: return boolean(a == V_EMPTY);
    case B_NIL: return boolean(a == V_NIL);
    case B_PAIR: return boolean(rt_kind(a, K_PAIR));
    case B_INTEGER: {
        double real;
        if (value_is_int(a)) return V_TRUE;
        if (!rt_kind(a, K_FLOAT)) return V_FALSE;
        memcpy(&real, object_bytes(a), sizeof(real));
        return boolean(isfinite(real) && trunc(real) == real);
    }
    case B_NUMBER: return boolean(value_is_int(a) || rt_kind(a, K_FLOAT));
    case B_CONS: {
        Value pair = rt_alloc(rt, K_PAIR, 2, 0);
        if (pair) {
            object_fields(pair)[0] = rt->stack[first];
            object_fields(pair)[1] = rt->stack[first + 1];
        }
        return pair;
    }
    case B_CAR: case B_CDR: case B_SET_CAR: case B_SET_CDR:
        if (!rt_kind(a, K_PAIR)) { rt_error(rt, "expected a pair"); return 0; }
        if (id == B_CAR || id == B_CDR) return object_fields(a)[id == B_CDR];
        if (object_frozen(a)) { rt_error(rt, "frozen table key"); return 0; }
        object_fields(a)[id == B_SET_CDR] = b;
        return a;
    }
    rt_error(rt, "unknown builtin");
    return 0;
}
