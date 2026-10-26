#ifndef OTIUM_PROTOTYPE_INTERNAL_H
#define OTIUM_PROTOTYPE_INTERNAL_H

#include "runtime.h"
#include "reader.h"

enum { K_PAIR = 1, K_BOX, K_CLOSURE, K_FLOAT, K_STRING, K_SYMBOL, K_KEYWORD, K_ARRAY,
       K_FOREIGN_LIBRARY, K_FOREIGN_FUNCTION, K_POINTER, K_ARRAY_DATA, K_TABLE, K_TABLE_DATA };
enum { V_UNBOUND = 18, TAG_CHAR = 22, TAG_BUILTIN = 26 };
enum {
    B_ADD, B_SUB, B_MUL, B_DIV, B_LT, B_LE, B_GT, B_GE, B_NUMEQ,
    B_REMAINDER, B_MODULO, B_EQ, B_NOT, B_CONS, B_CAR, B_CDR, B_LIST,
    B_NULL, B_NIL, B_PAIR, B_INTEGER, B_NUMBER, B_SET_CAR, B_SET_CDR,
    B_PRINT, B_PRINTLN, B_ERROR,
    B_FFI_OPEN, B_FFI_BIND, B_FFI_HAS_SYMBOL, B_FFI_POINTER, B_FFI_NULL, B_FFI_POINTER_EQ,
    B_FFI_ALLOC, B_FFI_FREE, B_FFI_REF, B_FFI_SET, B_FFI_SIZEOF, B_FFI_ALIGNOF,
    B_FFI_VIEW, B_FFI_ALLOC_UTF8, B_FFI_READ_UTF8, B_FFI_COUNT, B_FFI_BYTES,
    B_ARRAY, B_ARRAY_P, B_LENGTH, B_GET, B_PUT_MUT, B_PUSH, B_POP, B_COPY, B_EQUAL, B_CONTAINS, B_TABLE, B_TABLE_P, B_KEYS, B_VALUES, B_APPLY,
    /* Compiler-only splice helper, absent from the global environment. */
    B_QQ_APPEND
};
typedef enum {
    OP_CONST, OP_GLOBAL, OP_SET_GLOBAL, OP_DEFINE_GLOBAL, OP_LOCAL, OP_BIND, OP_SET_LOCAL,
    OP_UPVALUE, OP_SET_UPVALUE, OP_CLOSURE, OP_CALL, OP_TAIL_CALL,
    OP_RETURN, OP_POP, OP_DUP, OP_JUMP, OP_JFALSE, OP_PRESENT, OP_CLEAR_LOCAL
} Opcode;
typedef struct { uint32_t op, arg; } Instruction;
typedef struct { uint32_t index; bool local; } Capture;
typedef struct {
    Instruction *code;
    size_t count, capacity;
    bool *boxed;
    size_t locals, locals_capacity, arity, optional;
    Capture *captures;
    size_t capture_count, capture_capacity;
    bool (*native)(Runtime *);
    void *native_handle;
    size_t heat;
    bool compiling, rest;
} Function;
typedef struct { uint32_t function; size_t ip, base; } Frame;
typedef struct ForeignState ForeignState;
typedef struct ModuleState ModuleState;

struct Runtime {
    Heap heap;
    Value *constants, *globals, *stack;
    size_t constant_count, constant_capacity, global_count, global_capacity;
    /* Constant states: 0 free, 1 retained, 2 allocated by the current compile. */
    unsigned char *constant_state;
    size_t constant_state_capacity, constant_free, function_free, permanent_constants;
    bool compiling;
    char **names;
    unsigned char *global_defined, *global_private;
    size_t global_private_capacity;
    uint32_t *global_namespaces;
    size_t global_namespace_capacity;
    char **namespaces;
    size_t namespace_count, namespace_capacity;
    uint32_t current_namespace, required_namespace;
    ModuleState *modules;
    size_t name_capacity, global_defined_capacity;
    size_t sp, stack_capacity, stack_limit;
    Function **functions;
    size_t function_count, function_capacity;
    Frame *frames;
    size_t frame_count, frame_capacity, max_frames;
    Roots constant_roots, global_roots, stack_roots;
    char error[192];
    size_t error_offset;
    bool stress_gc;
    size_t native_threshold, native_functions, native_entries;
    size_t native_depth, max_native_depth;
    size_t native_scalar_entries, native_scalar_fallbacks;
    size_t native_numeric_entries, native_numeric_fallbacks;
    double native_compile_seconds;
    ForeignState *foreign;
};

void rt_free_function(Function *fn);
bool rt_reclaim(Runtime *rt);
bool rt_error(Runtime *rt, const char *message);
bool rt_grow(Runtime *rt, void **data, size_t *capacity, size_t needed, size_t width);
bool rt_stack(Runtime *rt, size_t needed);
Value rt_alloc(Runtime *rt, uint32_t kind, uint32_t fields, size_t bytes);
uint32_t rt_global(Runtime *rt, const char *name, size_t length);
uint32_t rt_resolve_global(Runtime *rt, const char *name, size_t length);
uint32_t rt_namespace(Runtime *rt, const char *name, size_t length);
bool rt_require(Runtime *rt, const Ast *specs);
void rt_modules_destroy(Runtime *rt);
uint32_t rt_binding_global(Runtime *rt, uint32_t space, const char *name, size_t length, bool create);
uint32_t rt_import_global_in(Runtime *rt, uint32_t space, const char *name, size_t length);
uint32_t rt_import_global(Runtime *rt, const char *name, size_t length);
uint32_t rt_alias_namespace(Runtime *rt, const char *name, size_t length);
uint32_t rt_constant(Runtime *rt, Value value);
Value rt_text(Runtime *rt, uint32_t kind, const char *text, size_t length);
Value rt_float(Runtime *rt, double number);
bool rt_compile(Runtime *rt, const Ast *form, uint32_t *function);
bool rt_execute(Runtime *rt, uint32_t function, Value *result);
enum { NATIVE_DIRECT_LIMIT = 32 };
bool rt_drive(Runtime *rt, size_t stop_frames);
bool rt_invoke(Runtime *rt, size_t argc, Value *result);
bool rt_apply(Runtime *rt, size_t call, size_t argc, bool tail);
bool rt_builtins(Runtime *rt);
Value rt_builtin(Runtime *rt, unsigned id, size_t first, size_t count);
int rt_builtin_index(const char *name);
bool rt_enter(Runtime *rt, size_t call, size_t argc, bool tail);
bool rt_leave(Runtime *rt, Value result);
bool rt_native_compile(Runtime *rt, uint32_t function);
void rt_native_close(Function *fn);
bool rt_module_compile(Runtime *rt, bool (*emit)(Runtime *, FILE *, void *), void *context,
                       const char *entry_name, void **handle, void **entry, double *seconds);
typedef struct { size_t length, capacity; } ArrayInfo;
typedef struct { size_t count, used, capacity; } TableInfo;
Value rt_equal(Runtime *rt, Value a, Value b, bool keys);
Value rt_table_set(Runtime *rt, Value table, Value key, Value value);
Value rt_table_get(Runtime *rt, Value table, Value key, Value fallback);
Value rt_table_builtin(Runtime *rt, unsigned id, size_t first, size_t count);
static inline bool rt_collection(Value v) {
    return value_is_ptr(v) && (object_kind(v) == K_ARRAY || object_kind(v) == K_TABLE || object_kind(v) == K_KEYWORD);
}
Value rt_collection_builtin(Runtime *rt, unsigned id, size_t first, size_t count);
Value rt_collection_call(Runtime *rt, size_t callable, size_t first, size_t count);
void rt_foreign_collect(Runtime *rt);
bool rt_foreign_init(Runtime *rt);
void rt_foreign_destroy(Runtime *rt);
Value rt_foreign_builtin(Runtime *rt, unsigned id, size_t first, size_t count);
Value rt_foreign_call(Runtime *rt, size_t callable, size_t first, size_t count);

static inline bool rt_kind(Value v, unsigned kind) {
    return value_is_ptr(v) && object_kind(v) == kind;
}
static inline bool rt_truth(Value v) { return v != V_NIL && v != V_FALSE; }

#endif
