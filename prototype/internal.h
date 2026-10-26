#ifndef OTIUM_PROTOTYPE_INTERNAL_H
#define OTIUM_PROTOTYPE_INTERNAL_H

#include "runtime.h"
#include "reader.h"

enum { K_PAIR = 1, K_BOX, K_CLOSURE, K_FLOAT, K_STRING, K_SYMBOL, K_KEYWORD, K_ARRAY,
       K_FOREIGN_LIBRARY, K_FOREIGN_FUNCTION, K_POINTER, K_ARRAY_DATA, K_TABLE, K_TABLE_DATA, K_CLASS, K_GENERIC, K_METHOD, K_MACRO, K_BUFFER, K_BUFFER_DATA, K_PARAM, K_BYTEVECTOR, K_RESTART, K_PORT, K_LAYOUT, K_INSTANCE, K_NEXT, K_RECORD_TYPE, K_RECORD, K_FOREIGN_LAYOUT };
enum { V_EOF = 30, V_UNBOUND = 18, TAG_CHAR = 22, TAG_BUILTIN = 26 };
enum {
    B_ADD, B_SUB, B_MUL, B_DIV, B_LT, B_LE, B_GT, B_GE, B_NUMEQ,
    B_REMAINDER, B_MODULO, B_EQ, B_NOT, B_CONS, B_CAR, B_CDR, B_LIST,
    B_NULL, B_NIL, B_PAIR, B_INTEGER, B_NUMBER, B_SET_CAR, B_SET_CDR,
    B_PRINT, B_PRINTLN, B_ERROR,
    B_FFI_OPEN, B_FFI_BIND, B_FFI_HAS_SYMBOL, B_FFI_POINTER, B_FFI_NULL, B_FFI_POINTER_EQ,
    B_FFI_ALLOC, B_FFI_FREE, B_FFI_REF, B_FFI_SET, B_FFI_SIZEOF, B_FFI_ALIGNOF,
    B_FFI_VIEW, B_FFI_ALLOC_UTF8, B_FFI_READ_UTF8, B_FFI_COUNT, B_FFI_BYTES, B_FFI_COPY, B_FFI_READ_BYTES, B_FFI_WRITE_BYTES, B_FFI_READ_UTF8_BYTES, B_FFI_RAW_NULL, B_FFI_RAW_SIZE, B_FFI_RAW_ALIGN, B_FFI_RAW_BIND, B_FFI_RAW_OPEN, B_FFI_LOADER_ERROR, B_FFI_RAW_ALLOC, B_FFI_RAW_REF, B_FFI_RAW_SET, B_FFI_FAIL, B_FFI_ERROR_FIELD, B_FFI_LAYOUT, B_FFI_LAYOUT_DATA,
    B_ARRAY, B_ARRAY_P, B_LENGTH, B_GET, B_PUT_MUT, B_PUSH, B_POP, B_COPY, B_EQUAL, B_CONTAINS, B_TABLE, B_TABLE_P, B_KEYS, B_VALUES, B_APPLY,
    B_SYMBOL_P, B_KEYWORD_P, B_STRING_P, B_BOOLEAN_P, B_LIST_P, B_PROCEDURE_P, B_SYMBOL_STRING, B_STRING_SYMBOL, B_KEYWORD, B_GENSYM, B_STRING_LENGTH, B_STRING_REF, B_SUBSTRING, B_EXP_APPLICATION, B_EXP_OPTIONAL, B_EXP_MACRO, B_EXP_INVOKE, B_EXP_NATIVE, B_EXP_LOCATE, B_EXP_FAIL, B_EXP_QUALIFY, B_EXP_LOCAL,
    B_STRING_APPEND, B_NUMBER_STRING, B_POS, B_NEG, B_READ_STRING, B_EVAL, B_INT_P, B_FLOAT_P, B_MACRO_P, B_CHAR_P, B_DISPLAY, B_WRITE, B_NEWLINE, B_DEFINE_CONDITION, B_CONDITION_TYPE, B_CONDITION_MESSAGE, B_CONDITION_IS, B_ERROR_P,
    B_CLASS_P, B_DISPATCH_TYPE, B_CLASS_SUBTYPE, B_GENERIC_P, B_GENERIC_METHODS, B_GENERIC_DEFINE, B_GENERIC_METHOD, B_REMOVE_METHOD, B_CLASS_DEFINE, B_MAKE, B_INSTANCE_P, B_SLOT_SET, B_CLASS_PROPERTIES, B_CLASS_MOUNT, B_CLASS_UPDATE, B_MAKE_RECORD_TYPE, B_RECORD_NEW, B_RECORD_P, B_RECORD_TYPE_P, B_RECORD_TYPE, B_RECORD_REF, B_RECORD_SET,
    B_QUOTIENT, B_ABS, B_MIN, B_MAX, B_FLOOR, B_CEILING, B_ROUND, B_TRUNCATE,
    B_SQRT, B_EXP, B_LOG, B_SIN, B_COS, B_TAN, B_ASIN, B_ACOS, B_ATAN, B_EXPT,
    B_EXACT, B_INEXACT, B_EXACT_P, B_INEXACT_P, B_NAN_P, B_INFINITE_P, B_FINITE_P,
    B_BIT_AND, B_BIT_IOR, B_BIT_XOR, B_BIT_NOT, B_BIT_SHIFT,
    B_STR, B_STRING_LT, B_STRING_NUMBER, B_BUFFER, B_BUFFER_PUSH, B_BUFFER_STRING,
    B_CHAR_INTEGER, B_INTEGER_CHAR, B_CHAR_UP, B_CHAR_DOWN, B_CHAR_SPACE, B_CHAR_ALPHA, B_CHAR_NUMERIC, B_DIGIT_VALUE, B_NAME, B_SYMBOL, B_STRING_LIST,
    B_TYPE, B_EOF, B_EOF_P, B_BUFFER_P, B_CURRENT_NS, B_DESCRIBE, B_SIGNAL,
    B_BYTES, B_BYTES_P, B_MAKE_BYTES, B_BYTES_LENGTH, B_BYTES_REF, B_BYTES_SET, B_BYTES_COPY, B_BYTES_COPY_MUT, B_BYTES_APPEND, B_UTF8_STRING, B_STRING_UTF8,
    B_FIND_RESTART, B_COMPUTE_RESTARTS, B_RESTART_NAME, B_RESTART_DESCRIPTION, B_INVOKE_RESTART,
    B_EXIT, B_QUIT, B_CURRENT_JIFFY, B_JIFFIES_SECOND, B_CURRENT_SECOND,
    B_PORT_P, B_INPUT_P, B_OUTPUT_P, B_INPUT_OPEN, B_OUTPUT_OPEN, B_OPEN_INPUT_STRING, B_OPEN_OUTPUT_STRING, B_GET_OUTPUT_STRING, B_OPEN_INPUT_BYTES, B_OPEN_OUTPUT_BYTES, B_GET_OUTPUT_BYTES, B_CLOSE_PORT, B_CLOSE_INPUT, B_CLOSE_OUTPUT, B_READ_U8, B_PEEK_U8, B_READ_CHAR, B_PEEK_CHAR, B_READ_LINE, B_READ_ALL, B_READ_CHARACTERS, B_READ_BYTES, B_READ_BYTES_MUT, B_WRITE_U8, B_WRITE_CHAR, B_WRITE_STRING, B_WRITE_BYTES, B_FLUSH_PORT, B_DISPLAY_TO, B_WRITE_TO, B_READ_UNTIL, B_READ_WHILE, B_SKIP_WHILE, B_READ_DATUM, B_MAKE_INPUT_PORT, B_MAKE_OUTPUT_PORT, B_OPEN_PORTS, B_STDIN, B_STDOUT, B_STDERR,
    B_OPEN_INPUT_FILE, B_OPEN_OUTPUT_FILE, B_OPEN_BINARY_INPUT, B_OPEN_BINARY_OUTPUT,
    B_FILE_EXISTS, B_DELETE_FILE, B_RENAME_FILE, B_FILE_INFO, B_LIST_DIRECTORY, B_MAKE_DIRECTORY, B_FILE_LINK, B_CURRENT_DIRECTORY,
    B_PROCESS_ARGUMENTS, B_SCRIPT_NAME, B_READER_TOKENS,
    /* Compiler-only helpers, absent from the global environment. */
    B_QQ_APPEND, B_EVAL_IN_NAMESPACE
};
typedef enum {
    OP_CONST, OP_GLOBAL, OP_SET_GLOBAL, OP_DEFINE_GLOBAL, OP_LOCAL, OP_BIND, OP_SET_LOCAL,
    OP_UPVALUE, OP_SET_UPVALUE, OP_CLOSURE, OP_CALL, OP_TAIL_CALL,
    OP_CLEANUP, OP_RUN_CLEANUP, OP_RESTARTS, OP_END_RESTARTS, OP_TRY, OP_END_TRY, OP_RAISE, OP_MAKE_PARAM, OP_BIND_PARAM, OP_UNBIND_PARAMS, OP_BIND_HANDLERS, OP_UNBIND_HANDLERS, OP_RETURN, OP_POP, OP_DUP, OP_JUMP, OP_JFALSE, OP_PRESENT, OP_CLEAR_LOCAL
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
    bool rest, evaluator;
    uint32_t namespace;
} Function;
typedef struct { uint32_t function; size_t ip, base; } Frame;
typedef struct { size_t frames, sp, target, params, handlers, handler_top, restarts, cleanups; } TryFrame;
typedef struct { TryFrame state; size_t first, count, tries; } RestartScope;
typedef struct { TryFrame state; size_t tries; bool outcome; } Cleanup;
enum { C_OBJECT, C_NUMBER, C_BOOLEAN, C_CHARACTER, C_STRING, C_SYMBOL, C_NULL, C_PAIR, C_VECTOR, C_BYTEVECTOR, C_TABLE, C_PROCEDURE, C_TYPE, C_NIL, C_KEYWORD, C_POINTER, C_PORT, C_COUNT };
typedef struct ForeignState ForeignState;
typedef struct ModuleState ModuleState;

typedef struct { char *text; size_t length; } Documentation;

struct Runtime {
    Heap heap;
    Value process_values[2];
    Roots process_roots;
    Value *open_ports;
    size_t open_port_count, open_port_capacity;
    Roots open_port_roots;
    Value standard_ports[3];
    Roots standard_port_roots;
    Value *constants, *globals, *stack;
    size_t constant_count, constant_capacity, global_count, global_capacity;
    /* Constant states: 0 free, 1 retained until code reclamation. */
    unsigned char *constant_state;
    size_t constant_state_capacity, constant_free, function_free, permanent_constants;
    bool compiling, evaluating;
    Documentation *docs;
    size_t docs_capacity;
    char **names;
    unsigned char *global_defined, *global_private, *global_macro;
    size_t global_macro_capacity;
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
    Value *handlers;
    size_t *handler_previous;
    size_t handler_count, handler_capacity, handler_previous_capacity, handler_top, callback_depth;
    Roots handler_roots;
    Value *restarts, restart_transfer[2];
    size_t restart_count, restart_capacity, restart_scope_count, restart_scope_capacity;
    RestartScope *restart_scopes;
    Roots restart_roots, restart_transfer_roots;
    bool restart_pending;
    Value *cleanups;
    Cleanup *cleanup_states;
    size_t cleanup_count, cleanup_capacity, cleanup_state_capacity;
    Roots cleanup_roots;
    volatile sig_atomic_t *interrupt_flag;
    bool interrupted;
    bool exit_pending;
    int exit_status;
    bool signaled;
    Value *params;
    size_t param_count, param_capacity;
    Roots param_roots;
    Value classes[C_COUNT];
    Roots class_roots;
    bool classes_ready, core_loaded;
    Roots constant_roots, global_roots, stack_roots, condition_roots;
    Value condition_values[2];
    bool raised;
    TryFrame *tries;
    size_t try_count, try_capacity;
    char error[192];
    char expansion_error_code[32];
    const char *error_code;
    size_t error_offset;
    bool stress_gc, shared_expander, expanding;
    size_t expansion_calls, eval_depth;
    uint64_t gensym_counter;
    ForeignState *foreign;
};

void rt_free_function(Function *fn);
bool rt_reclaim(Runtime *rt);
bool rt_coded_error(Runtime *rt, const char *code, const char *message);
bool rt_error(Runtime *rt, const char *message);
bool rt_grow(Runtime *rt, void **data, size_t *capacity, size_t needed, size_t width);
bool rt_stack(Runtime *rt, size_t needed);
Value rt_alloc(Runtime *rt, uint32_t kind, uint32_t fields, size_t bytes);
uint32_t rt_global(Runtime *rt, const char *name, size_t length);
uint32_t rt_lookup_global(Runtime *rt, const char *name, size_t length);
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
Value rt_evaluator(Runtime *rt, uint32_t space);
Value rt_eval_value(Runtime *rt, Value value);
Ast *rt_syntax_value(Runtime *rt, Value value, Reader *arena);
Value rt_read_string(Runtime *rt, Value string);
Ast *rt_expand_form(Runtime *rt, const Ast *form, Reader *arena);
bool rt_compile(Runtime *rt, const Ast *form, uint32_t *function);
bool rt_execute(Runtime *rt, uint32_t function, Value *result);
bool rt_drive(Runtime *rt, size_t stop_frames);
bool rt_invoke(Runtime *rt, size_t argc, Value *result);
bool rt_apply(Runtime *rt, size_t call, size_t argc, bool tail);
void rt_restore_params(Runtime *rt, size_t count);
bool rt_bind_param(Runtime *rt, Value param, Value value);
Value rt_param_call(Runtime *rt, Value param, size_t count);
bool rt_builtins(Runtime *rt);
Value rt_reflection_builtin(Runtime *rt, unsigned id, size_t first, size_t count);
bool rt_classes_init(Runtime *rt);
bool rt_generic_prepare(Runtime *rt, size_t call, size_t *count);
size_t rt_format_float(double value, char *buffer, size_t size);
bool rt_number(Runtime *rt, Value value, double *result);
int rt_integer_real(intptr_t integer, double real);
size_t rt_utf8_next(const char *s, size_t n, size_t p, uint32_t *cp);
size_t rt_utf8_encode(uint32_t cp, char bytes[4]);
Value rt_string_get(Value string, Value key, Value fallback);
Value rt_string_builtin(Runtime *rt, unsigned id, size_t first, size_t count);
typedef struct { size_t length, capacity, characters; } BufferInfo;
Value rt_numeric_builtin(Runtime *rt, unsigned id, size_t first, size_t count);
Value rt_class_builtin(Runtime *rt, unsigned id, size_t first, size_t count);
Value rt_condition_builtin(Runtime *rt, unsigned id, size_t first, size_t count);
Value rt_error_condition(Runtime *rt);
Value rt_raise(Runtime *rt, size_t first, size_t count);
Value rt_signal(Runtime *rt, size_t first, size_t count);
void rt_signal_error(Runtime *rt);
void rt_restore_handlers(Runtime *rt, size_t count, size_t top);
bool rt_install_handlers(Runtime *rt, size_t first, size_t count);
void rt_remove_handlers(Runtime *rt, size_t count);
Value rt_support_builtin(Runtime *rt, unsigned id, size_t first, size_t count);
Value rt_intern(Runtime *rt, unsigned kind, const char *text, size_t length);
Value rt_call_list(Runtime *rt, Value function, Value args);
uint64_t rt_symbol_id(Value symbol);
Value rt_builtin(Runtime *rt, unsigned id, size_t first, size_t count);
int rt_builtin_index(const char *name);
bool rt_enter(Runtime *rt, size_t call, size_t argc, bool tail);
bool rt_leave(Runtime *rt, Value result);
typedef struct { bool abstract, builtin; } ClassInfo;
bool rt_subtype(Value child, Value parent);
Value rt_dispatch_type(Runtime *rt, Value value);
Value rt_instance_builtin(Runtime *rt, unsigned id, size_t first, size_t count);
Value rt_instance_call(Runtime *rt, Value instance, size_t first, size_t count);
Value rt_instance_copy(Runtime *rt, Value instance);
Value rt_record_builtin(Runtime *rt, unsigned id, size_t first, size_t count);
Value rt_record_call(Runtime *rt, Value record, size_t first, size_t count);
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

size_t rt_bytes_length(Value v);
unsigned char *rt_bytes_data(Value v);
Value rt_bytes_builtin(Runtime *rt, unsigned id, size_t first, size_t count);
Value rt_decode_error(Runtime *rt, size_t position);

Value rt_restart_builtin(Runtime *rt, unsigned id, size_t first, size_t count);
bool rt_install_restarts(Runtime *rt, size_t first, size_t count, size_t target);
void rt_restore_restarts(Runtime *rt, size_t scopes);
bool rt_resume_restart(Runtime *rt, size_t stop_frames);
bool rt_install_cleanup(Runtime *rt, bool outcome);
bool rt_run_cleanup(Runtime *rt, bool unwinding);
void rt_restore_cleanups(Runtime *rt, size_t count);
Value rt_time_builtin(Runtime *rt, unsigned id, size_t count);
Value rt_port_builtin(Runtime *rt, unsigned id, size_t first, size_t count);
Value rt_port_print(Runtime *rt, unsigned id, size_t first, size_t count);
Value rt_io_error(Runtime *rt, const char *reason);
Value rt_bytes_new(Runtime *rt, size_t n);
size_t rt_utf8_scalar(const unsigned char *s, size_t p, size_t end);
void rt_port_repr(FILE *out, Value port);
Value rt_read_source(Runtime *rt, const char *source, size_t length);
Value rt_process_builtin(Runtime *rt, unsigned id, size_t count);
Value rt_tokens(Runtime *rt, size_t first, size_t count);
Value rt_filesystem_builtin(Runtime *rt, unsigned id, size_t first, size_t count);
void rt_ports_destroy(Runtime *rt);
Value rt_file_error(Runtime *rt, const char *op, Value path, int error);
Value rt_ffi_error(Runtime *rt, const char *operation, const char *reason, const char *message);
#endif
