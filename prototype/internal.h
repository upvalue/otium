#ifndef OTIUM_PROTOTYPE_INTERNAL_H
#define OTIUM_PROTOTYPE_INTERNAL_H

/* Every runtime source includes this header first, so POSIX declarations are
 * visible everywhere, including in the single-file amalgamation. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "heap.h"
#include "reader.h"

enum {
  K_PAIR = 1,
  K_BOX,
  K_CLOSURE,
  K_FLOAT,
  K_STRING,
  K_SYMBOL,
  K_KEYWORD,
  K_ARRAY,
  K_FOREIGN_LIBRARY,
  K_FOREIGN_FUNCTION,
  K_POINTER,
  K_ARRAY_DATA,
  K_TABLE,
  K_TABLE_DATA,
  K_CLASS,
  K_GENERIC,
  K_METHOD,
  K_MACRO,
  K_BUFFER,
  K_BUFFER_DATA,
  K_PARAM,
  K_BYTEVECTOR,
  K_RESTART,
  K_PORT,
  K_LAYOUT,
  K_INSTANCE,
  K_NEXT,
  K_RECORD_TYPE,
  K_RECORD,
  K_FOREIGN_LAYOUT
};

enum { V_UNBOUND = 18, TAG_CHAR = 22, TAG_BUILTIN = 26 };

/* Builtin procedures. Names and handlers are in the table in builtins.c.
 * Several modules pick a sub-handler by comparing ids against the first id of
 * a group, so keep each module's builtins together and in this order. */
enum {
  B_ADD,
  B_SUB,
  B_MUL,
  B_DIV,
  B_LT,
  B_LE,
  B_GT,
  B_GE,
  B_NUMEQ,
  B_REMAINDER,
  B_MODULO,
  B_EQ,
  B_NOT,
  B_CONS,
  B_CAR,
  B_CDR,
  B_LIST,
  B_NULL,
  B_NIL,
  B_PAIR,
  B_INTEGER,
  B_NUMBER,
  B_SET_CAR,
  B_SET_CDR,
  B_PRINT,
  B_PRINTLN,
  B_ERROR,
  B_FFI_OPEN,
  B_FFI_BIND,
  B_FFI_HAS_SYMBOL,
  B_FFI_POINTER,
  B_FFI_NULL,
  B_FFI_POINTER_EQ,
  B_FFI_ALLOC,
  B_FFI_FREE,
  B_FFI_REF,
  B_FFI_SET,
  B_FFI_SIZEOF,
  B_FFI_ALIGNOF,
  B_FFI_VIEW,
  B_FFI_ALLOC_UTF8,
  B_FFI_READ_UTF8,
  B_FFI_COUNT,
  B_FFI_BYTES,
  B_FFI_COPY,
  B_FFI_READ_BYTES,
  B_FFI_WRITE_BYTES,
  B_FFI_READ_UTF8_BYTES,
  B_FFI_OWN,
  B_FFI_CALLBACK,
  B_FFI_FREE_CALLBACK,
  B_FFI_CALLBACK_COUNT,
  B_FFI_RAW_NULL,
  B_FFI_RAW_SIZE,
  B_FFI_RAW_ALIGN,
  B_FFI_RAW_BIND,
  B_FFI_RAW_OPEN,
  B_FFI_LOADER_ERROR,
  B_FFI_RAW_ALLOC,
  B_FFI_RAW_REF,
  B_FFI_RAW_SET,
  B_FFI_FAIL,
  B_FFI_ERROR_FIELD,
  B_FFI_LAYOUT,
  B_FFI_LAYOUT_DATA,
  B_ARRAY,
  B_ARRAY_P,
  B_LENGTH,
  B_GET,
  B_PUT_MUT,
  B_PUSH,
  B_POP,
  B_COPY,
  B_EQUAL,
  B_CONTAINS,
  B_TABLE,
  B_TABLE_P,
  B_KEYS,
  B_VALUES,
  B_APPLY,
  B_SYMBOL_P,
  B_KEYWORD_P,
  B_STRING_P,
  B_BOOLEAN_P,
  B_LIST_P,
  B_PROCEDURE_P,
  B_SYMBOL_STRING,
  B_STRING_SYMBOL,
  B_KEYWORD,
  B_GENSYM,
  B_STRING_LENGTH,
  B_STRING_REF,
  B_SUBSTRING,
  B_EXP_APPLICATION,
  B_EXP_OPTIONAL,
  B_EXP_MACRO,
  B_EXP_INVOKE,
  B_EXP_NATIVE,
  B_EXP_LOCATE,
  B_EXP_FAIL,
  B_EXP_QUALIFY,
  B_EXP_LOCAL,
  B_STRING_APPEND,
  B_NUMBER_STRING,
  B_POS,
  B_NEG,
  B_READ_STRING,
  B_EVAL,
  B_INT_P,
  B_FLOAT_P,
  B_MACRO_P,
  B_CHAR_P,
  B_DISPLAY,
  B_WRITE,
  B_NEWLINE,
  B_DEFINE_CONDITION,
  B_CONDITION_TYPE,
  B_CONDITION_MESSAGE,
  B_CONDITION_IS,
  B_ERROR_P,
  B_CLASS_P,
  B_DISPATCH_TYPE,
  B_CLASS_SUBTYPE,
  B_GENERIC_P,
  B_GENERIC_METHODS,
  B_GENERIC_DEFINE,
  B_GENERIC_METHOD,
  B_REMOVE_METHOD,
  B_CLASS_DEFINE,
  B_MAKE,
  B_INSTANCE_P,
  B_SLOT_SET,
  B_CLASS_PROPERTIES,
  B_CLASS_MOUNT,
  B_CLASS_UPDATE,
  B_MAKE_RECORD_TYPE,
  B_RECORD_NEW,
  B_RECORD_P,
  B_RECORD_TYPE_P,
  B_RECORD_TYPE,
  B_RECORD_REF,
  B_RECORD_SET,
  B_QUOTIENT,
  B_ABS,
  B_MIN,
  B_MAX,
  B_FLOOR,
  B_CEILING,
  B_ROUND,
  B_TRUNCATE,
  B_SQRT,
  B_EXP,
  B_LOG,
  B_SIN,
  B_COS,
  B_TAN,
  B_ASIN,
  B_ACOS,
  B_ATAN,
  B_EXPT,
  B_EXACT,
  B_INEXACT,
  B_EXACT_P,
  B_INEXACT_P,
  B_NAN_P,
  B_INFINITE_P,
  B_FINITE_P,
  B_BIT_AND,
  B_BIT_IOR,
  B_BIT_XOR,
  B_BIT_NOT,
  B_BIT_SHIFT,
  B_STR,
  B_STRING_LT,
  B_STRING_NUMBER,
  B_BUFFER,
  B_BUFFER_PUSH,
  B_BUFFER_STRING,
  B_CHAR_INTEGER,
  B_INTEGER_CHAR,
  B_CHAR_UP,
  B_CHAR_DOWN,
  B_CHAR_SPACE,
  B_CHAR_ALPHA,
  B_CHAR_NUMERIC,
  B_DIGIT_VALUE,
  B_NAME,
  B_SYMBOL,
  B_STRING_LIST,
  B_TYPE,
  B_EOF,
  B_EOF_P,
  B_BUFFER_P,
  B_CURRENT_NS,
  B_DESCRIBE,
  B_SIGNAL,
  B_BYTES,
  B_BYTES_P,
  B_MAKE_BYTES,
  B_BYTES_LENGTH,
  B_BYTES_REF,
  B_BYTES_SET,
  B_BYTES_COPY,
  B_BYTES_COPY_MUT,
  B_BYTES_APPEND,
  B_UTF8_STRING,
  B_STRING_UTF8,
  B_FIND_RESTART,
  B_COMPUTE_RESTARTS,
  B_RESTART_NAME,
  B_RESTART_DESCRIPTION,
  B_INVOKE_RESTART,
  B_EXIT,
  B_QUIT,
  B_CURRENT_JIFFY,
  B_JIFFIES_SECOND,
  B_CURRENT_SECOND,
  B_PORT_P,
  B_INPUT_P,
  B_OUTPUT_P,
  B_INPUT_OPEN,
  B_OUTPUT_OPEN,
  B_OPEN_INPUT_STRING,
  B_OPEN_OUTPUT_STRING,
  B_GET_OUTPUT_STRING,
  B_OPEN_INPUT_BYTES,
  B_OPEN_OUTPUT_BYTES,
  B_GET_OUTPUT_BYTES,
  B_CLOSE_PORT,
  B_CLOSE_INPUT,
  B_CLOSE_OUTPUT,
  B_READ_U8,
  B_PEEK_U8,
  B_READ_CHAR,
  B_PEEK_CHAR,
  B_READ_LINE,
  B_READ_ALL,
  B_READ_CHARACTERS,
  B_READ_BYTES,
  B_READ_BYTES_MUT,
  B_WRITE_U8,
  B_WRITE_CHAR,
  B_WRITE_STRING,
  B_WRITE_BYTES,
  B_FLUSH_PORT,
  B_DISPLAY_TO,
  B_WRITE_TO,
  B_READ_UNTIL,
  B_READ_WHILE,
  B_SKIP_WHILE,
  B_READ_DATUM,
  B_MAKE_INPUT_PORT,
  B_MAKE_OUTPUT_PORT,
  B_OPEN_PORTS,
  B_STDIN,
  B_STDOUT,
  B_STDERR,
  B_OPEN_INPUT_FILE,
  B_OPEN_OUTPUT_FILE,
  B_OPEN_BINARY_INPUT,
  B_OPEN_BINARY_OUTPUT,
  B_FILE_EXISTS,
  B_DELETE_FILE,
  B_RENAME_FILE,
  B_FILE_INFO,
  B_LIST_DIRECTORY,
  B_MAKE_DIRECTORY,
  B_FILE_LINK,
  B_CURRENT_DIRECTORY,
  B_PROCESS_ARGUMENTS,
  B_SCRIPT_NAME,
  B_PROCESS_GETENV,
  B_PROCESS_SETENV,
  B_PROCESS_WRITE_ERROR,
  B_READER_TOKENS,
  /* Compiler-only helpers, absent from the global environment. */
  B_QQ_APPEND,
  B_EVAL_IN_NAMESPACE,
  B_COUNT
};

typedef enum {
  OP_CONST,
  OP_GLOBAL,
  OP_SET_GLOBAL,
  OP_DEFINE_GLOBAL,
  OP_LOCAL,
  OP_BIND,
  OP_SET_LOCAL,
  OP_UPVALUE,
  OP_SET_UPVALUE,
  OP_CLOSURE,
  OP_CALL,
  OP_TAIL_CALL,
  OP_CLEANUP,
  OP_RUN_CLEANUP,
  OP_RESTARTS,
  OP_END_RESTARTS,
  OP_TRY,
  OP_END_TRY,
  OP_RAISE,
  OP_MAKE_PARAM,
  OP_BIND_PARAM,
  OP_UNBIND_PARAMS,
  OP_BIND_HANDLERS,
  OP_UNBIND_HANDLERS,
  OP_RETURN,
  OP_POP,
  OP_DUP,
  OP_JUMP,
  OP_JFALSE,
  OP_PRESENT,
  OP_CLEAR_LOCAL
} oti_opcode;

typedef struct {
  uint32_t op, arg;
} oti_instruction;

typedef struct {
  uint32_t index;
  bool local;
} oti_capture;

typedef struct {
  oti_instruction* code;
  size_t count, capacity;
  bool* boxed;
  size_t locals, locals_capacity, arity, optional;
  oti_capture* captures;
  size_t capture_count, capture_capacity;
  bool rest, evaluator;
  uint32_t namespace;
} oti_function;

typedef struct {
  uint32_t function;
  size_t ip, base;
} oti_frame;

typedef struct {
  size_t frames, sp, target, params, handlers, handler_top, restarts, cleanups;
} oti_try_frame;

typedef struct {
  oti_try_frame state;
  size_t first, count, tries;
} oti_restart_scope;

typedef struct {
  oti_try_frame state;
  size_t tries;
  bool outcome;
} oti_cleanup;

enum {
  C_OBJECT,
  C_NUMBER,
  C_BOOLEAN,
  C_CHARACTER,
  C_STRING,
  C_SYMBOL,
  C_NULL,
  C_PAIR,
  C_VECTOR,
  C_BYTEVECTOR,
  C_TABLE,
  C_PROCEDURE,
  C_TYPE,
  C_NIL,
  C_KEYWORD,
  C_POINTER,
  C_PORT,
  C_CPOINTER,
  C_COUNT
};
typedef struct oti_foreign_state oti_foreign_state;
typedef struct oti_module_state oti_module_state;

typedef struct {
  char* text;
  size_t length;
} oti_documentation;

struct ot_runtime {
  oti_heap heap;
  ot_value process_values[2];
  oti_roots process_roots;
  ot_value* open_ports;
  size_t open_port_count, open_port_capacity;
  oti_roots open_port_roots;
  ot_value standard_ports[3];
  oti_roots standard_port_roots;
  ot_value *constants, *globals, *stack;
  size_t constant_count, constant_capacity, global_count, global_capacity;
  /* Constant states: 0 free, 1 retained until code reclamation. */
  unsigned char* constant_state;
  size_t constant_state_capacity, constant_free, function_free, permanent_constants;
  bool compiling, evaluating;
  oti_documentation* docs;
  size_t docs_capacity;
  char** names;
  unsigned char *global_defined, *global_private, *global_macro;
  size_t global_macro_capacity;
  size_t global_private_capacity;
  uint32_t* global_namespaces;
  size_t global_namespace_capacity;
  char** namespaces;
  size_t namespace_count, namespace_capacity;
  uint32_t current_namespace, required_namespace;
  oti_module_state* modules;
  size_t name_capacity, global_defined_capacity;
  size_t sp, stack_capacity, stack_limit;
  oti_function** functions;
  size_t function_count, function_capacity;
  oti_frame* frames;
  size_t frame_count, frame_capacity, max_frames;
  ot_value* handlers;
  size_t* handler_previous;
  size_t handler_count, handler_capacity, handler_previous_capacity, handler_top, callback_depth;
  oti_roots handler_roots;
  ot_value *restarts, restart_transfer[2];
  size_t restart_count, restart_capacity, restart_scope_count, restart_scope_capacity;
  oti_restart_scope* restart_scopes;
  oti_roots restart_roots, restart_transfer_roots;
  bool restart_pending;
  ot_value* cleanups;
  oti_cleanup* cleanup_states;
  size_t cleanup_count, cleanup_capacity, cleanup_state_capacity;
  oti_roots cleanup_roots;
  volatile sig_atomic_t* interrupt_flag;
  bool interrupted;
  bool exit_pending;
  int exit_status;
  bool signaled;
  ot_value* params;
  size_t param_count, param_capacity;
  oti_roots param_roots;
  ot_value classes[C_COUNT];
  oti_roots class_roots;
  bool classes_ready, core_loaded;
  oti_roots constant_roots, global_roots, stack_roots, condition_roots;
  ot_value condition_values[2];
  bool raised;
  oti_try_frame* tries;
  size_t try_count, try_capacity;
  char error[192];
  char expansion_error_code[32];
  const char* error_code;
  size_t error_offset;
  bool stress_gc, shared_expander, expanding;
  size_t expansion_calls, eval_depth;
  uint64_t gensym_counter;
  ot_io_host io_host;
  void* io_userdata;
  bool io_host_set, host_io_active, host_io_reentry;
  oti_foreign_state* foreign;
};

/* Upper bound on a single source file or a nest of module loads. */
#define OTI_SOURCE_LIMIT ((size_t)16 * 1024 * 1024)

void oti_free_function(oti_function* fn);
/* Read an open file to the end and close it. The result is NUL-terminated and
 * owned by the caller. what names the file in error messages. */
char* oti_read_file(ot_runtime* rt, FILE* file, size_t limit, const char* what, size_t* length);
bool oti_reclaim(ot_runtime* rt);
bool oti_coded_error(ot_runtime* rt, const char* code, const char* message);
/* Record an error and return 0, the failure result of a builtin. */
ot_value oti_fail(ot_runtime* rt, const char* message);
/* A new pair. car and cdr are rooted across the allocation. */
ot_value oti_cons(ot_runtime* rt, ot_value car, ot_value cdr);
bool oti_error(ot_runtime* rt, const char* message);
bool oti_host_entry(ot_runtime* rt);
bool oti_grow(ot_runtime* rt, void** data, size_t* capacity, size_t needed, size_t width);
bool oti_stack(ot_runtime* rt, size_t needed);
ot_value oti_alloc(ot_runtime* rt, uint32_t kind, uint32_t fields, size_t bytes);
uint32_t oti_global(ot_runtime* rt, const char* name, size_t length);
uint32_t oti_lookup_global(ot_runtime* rt, const char* name, size_t length);
uint32_t oti_resolve_global(ot_runtime* rt, const char* name, size_t length);
uint32_t oti_namespace(ot_runtime* rt, const char* name, size_t length);
bool oti_require(ot_runtime* rt, const oti_ast* specs);
void oti_modules_destroy(ot_runtime* rt);
uint32_t oti_binding_global(ot_runtime* rt, uint32_t space, const char* name, size_t length,
                           bool create);
uint32_t oti_import_global_in(ot_runtime* rt, uint32_t space, const char* name, size_t length);
uint32_t oti_import_global(ot_runtime* rt, const char* name, size_t length);
uint32_t oti_alias_namespace(ot_runtime* rt, const char* name, size_t length);
uint32_t oti_constant(ot_runtime* rt, ot_value value);
ot_value oti_text(ot_runtime* rt, uint32_t kind, const char* text, size_t length);
ot_value oti_float(ot_runtime* rt, double number);
ot_value oti_evaluator(ot_runtime* rt, uint32_t space);
ot_value oti_eval_value(ot_runtime* rt, ot_value value);
oti_ast* oti_syntax_value(ot_runtime* rt, ot_value value, oti_arena* arena);
ot_value oti_read_string(ot_runtime* rt, ot_value string);
oti_ast* oti_expand_form(ot_runtime* rt, const oti_ast* form, oti_arena* arena);
bool oti_compile(ot_runtime* rt, const oti_ast* form, uint32_t* function);
bool oti_execute(ot_runtime* rt, uint32_t function, ot_value* result);
bool oti_drive(ot_runtime* rt, size_t stop_frames);
bool oti_invoke(ot_runtime* rt, size_t argc, ot_value* result);
bool oti_apply(ot_runtime* rt, size_t call, size_t argc, bool tail);
void oti_restore_params(ot_runtime* rt, size_t count);
bool oti_bind_param(ot_runtime* rt, ot_value param, ot_value value);
ot_value oti_param_call(ot_runtime* rt, ot_value param, size_t count);
bool oti_builtins(ot_runtime* rt);
ot_value oti_reflection_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count);
bool oti_classes_init(ot_runtime* rt);
bool oti_generic_prepare(ot_runtime* rt, size_t call, size_t* count);
size_t oti_format_float(double value, char* buffer, size_t size);
bool oti_number(ot_runtime* rt, ot_value value, double* result);
int oti_integer_real(intptr_t integer, double real);
size_t oti_utf8_next(const char* s, size_t n, size_t p, uint32_t* cp);
size_t oti_utf8_encode(uint32_t cp, char bytes[4]);
ot_value oti_string_get(ot_value string, ot_value key, ot_value fallback);
ot_value oti_string_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count);

typedef struct {
  size_t length, capacity, characters;
} oti_buffer_info;

ot_value oti_numeric_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count);
ot_value oti_class_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count);
ot_value oti_condition_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count);
ot_value oti_error_condition(ot_runtime* rt);
ot_value oti_raise(ot_runtime* rt, size_t first, size_t count);
ot_value oti_signal(ot_runtime* rt, size_t first, size_t count);
void oti_signal_error(ot_runtime* rt);
void oti_restore_handlers(ot_runtime* rt, size_t count, size_t top);
bool oti_install_handlers(ot_runtime* rt, size_t first, size_t count);
void oti_remove_handlers(ot_runtime* rt, size_t count);
ot_value oti_support_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count);
ot_value oti_intern(ot_runtime* rt, unsigned kind, const char* text, size_t length);
ot_value oti_call_list(ot_runtime* rt, ot_value function, ot_value args);
uint64_t oti_symbol_id(ot_value symbol);
ot_value oti_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count);
int oti_builtin_index(const char* name);
bool oti_enter(ot_runtime* rt, size_t call, size_t argc, bool tail);
bool oti_leave(ot_runtime* rt, ot_value result);

typedef struct {
  bool abstract, builtin;
} oti_class_info;

bool oti_subtype(ot_value child, ot_value parent);
ot_value oti_dispatch_type(ot_runtime* rt, ot_value value);
ot_value oti_instance_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count);
ot_value oti_instance_call(ot_runtime* rt, ot_value instance, size_t first, size_t count);
ot_value oti_instance_copy(ot_runtime* rt, ot_value instance);
ot_value oti_record_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count);
ot_value oti_record_call(ot_runtime* rt, ot_value record, size_t first, size_t count);

typedef struct {
  size_t length, capacity;
} oti_array_info;

typedef struct {
  size_t count, used, capacity;
} oti_table_info;

ot_value oti_equal(ot_runtime* rt, ot_value a, ot_value b, bool keys);
ot_value oti_table_set(ot_runtime* rt, ot_value table, ot_value key, ot_value value);
ot_value oti_table_get(ot_runtime* rt, ot_value table, ot_value key, ot_value fallback);
ot_value oti_table_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count);

static inline bool oti_collection(ot_value v) {
  return value_is_ptr(v) &&
         (object_kind(v) == K_ARRAY || object_kind(v) == K_TABLE || object_kind(v) == K_KEYWORD);
}

ot_value oti_collection_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count);
ot_value oti_collection_call(ot_runtime* rt, size_t callable, size_t first, size_t count);
void oti_foreign_collect(ot_runtime* rt);
void oti_foreign_stats(const ot_runtime* rt, ot_stats* stats);
bool oti_foreign_init(ot_runtime* rt);
void oti_foreign_destroy(ot_runtime* rt);
ot_value oti_foreign_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count);
bool oti_foreign_owned(ot_value value);
ot_value oti_foreign_call(ot_runtime* rt, size_t callable, size_t first, size_t count);

static inline bool oti_kind(ot_value v, unsigned kind) {
  return value_is_ptr(v) && object_kind(v) == kind;
}

static inline bool oti_truth(ot_value v) { return v != OT_NIL && v != OT_FALSE; }

/* The bytes of a string, symbol or keyword. */
static inline const char* oti_text_bytes(ot_value v, size_t* length) {
  memcpy(length, object_bytes(v), sizeof(*length));
  return (const char*)object_bytes(v) + sizeof(*length);
}

/* A non-negative integer argument as a size. */
static inline bool oti_natural(ot_value v, size_t* n) {
  if (!ot_is_int(v) || ot_to_int(v) < 0) return false;
  *n = (size_t)ot_to_int(v);
  return true;
}

size_t oti_bytes_length(ot_value v);
unsigned char* oti_bytes_data(ot_value v);
ot_value oti_bytes_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count);
ot_value oti_decode_error(ot_runtime* rt, size_t position);

ot_value oti_restart_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count);
bool oti_install_restarts(ot_runtime* rt, size_t first, size_t count, size_t target);
void oti_restore_restarts(ot_runtime* rt, size_t scopes);
bool oti_resume_restart(ot_runtime* rt, size_t stop_frames);
bool oti_install_cleanup(ot_runtime* rt, bool outcome);
bool oti_run_cleanup(ot_runtime* rt, bool unwinding);
void oti_restore_cleanups(ot_runtime* rt, size_t count);
ot_value oti_time_builtin(ot_runtime* rt, unsigned id, size_t count);
ot_value oti_port_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count);
ot_value oti_port_print(ot_runtime* rt, unsigned id, size_t first, size_t count);
ot_value oti_io_error(ot_runtime* rt, const char* reason);
ot_value oti_bytes_new(ot_runtime* rt, size_t n);
size_t oti_utf8_scalar(const unsigned char* s, size_t p, size_t end);
void oti_port_repr(FILE* out, ot_value port);
ot_value oti_read_source(ot_runtime* rt, const char* source, size_t length);
ot_value oti_process_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count);
ot_value oti_tokens(ot_runtime* rt, size_t first, size_t count);
ot_value oti_filesystem_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count);
void oti_ports_destroy(ot_runtime* rt);
ot_value oti_file_error(ot_runtime* rt, const char* op, ot_value path, int error);
ot_value oti_ffi_error(ot_runtime* rt, const char* operation, const char* reason, const char* message);
#endif
