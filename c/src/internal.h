#ifndef OTIUM_INTERNAL_H
#define OTIUM_INTERNAL_H

/* Every runtime source includes this header first, so POSIX declarations are
 * visible everywhere, including in the single-file amalgamation. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "heap.h"
#include <assert.h>
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

/* Builtin procedures. Each module lists its builtins' names and functions in
 * an oti_builtin_def table; builtins.c combines them. The order here doesn't
 * matter. */
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
  B_TYPE_NAME,
  B_TYPE_DESCRIPTOR_P,
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
  /* Compiler-only helpers have no name, so they aren't globals. */
  B_QQ_APPEND,
  B_EVAL_IN_NAMESPACE,
  B_COUNT
};

/* Register VM opcodes. Register r of a frame is stack[base + r]; register 0
 * holds the running closure. See vm-design.md for operand conventions. */
typedef enum {
  OP_MOVE,
  OP_IMM,
  OP_CONST,
  OP_GLOBAL,
  OP_SET_GLOBAL,
  OP_DEFINE_GLOBAL,
  OP_UPVALUE,
  OP_UPVALUE_BOX,
  OP_SET_UPVALUE,
  OP_BOX,
  OP_UNBOX,
  OP_SET_BOX,
  OP_CHECK,
  OP_CLEAR,
  OP_CLOSURE,
  OP_JUMP,
  OP_JFALSE,
  OP_JTRUE,
  OP_JBOUND,
  OP_CALL,
  OP_TAIL_CALL,
  /* Call or tail call G[bc], placed in rA, with x arguments above it. */
  OP_CALL_GLOBAL,
  OP_TAIL_CALL_GLOBAL,
  OP_RETURN,
  /* Return the 32-bit immediate in bc, sign extended, as IMM loads it. */
  OP_RETURN_IMM,
  OP_TRY,
  OP_END_TRY,
  OP_RAISE,
  OP_CLEANUP,
  OP_RUN_CLEANUP,
  OP_RESTARTS,
  OP_RESULT,
  OP_END_RESTARTS,
  OP_BIND_HANDLERS,
  OP_UNBIND_HANDLERS,
  OP_MAKE_PARAM,
  OP_BIND_PARAM,
  OP_UNBIND_PARAMS,
  OP_ADD,
  OP_SUB,
  OP_MUL,
  OP_ADDI,
  OP_LT,
  OP_LE,
  OP_GT,
  OP_GE,
  OP_NUMEQ,
  OP_EQ,
  OP_CONS,
  OP_CAR,
  OP_CDR,
  OP_NOT,
  OP_NULLP,
  OP_PAIRP,
  /* Branches jump to the target word that follows when the test equals x. */
  OP_BR_LT,
  OP_BR_LE,
  OP_BR_GT,
  OP_BR_GE,
  OP_BR_NUMEQ,
  OP_BR_EQ,
  OP_BR_LTI,
  OP_BR_LEI,
  OP_BR_GTI,
  OP_BR_GEI,
  OP_BR_NUMEQI,
  OP_BR_NULL,
  OP_BR_PAIR,
  /* Branch on rA being the constant K[bc]. */
  OP_BR_EQK,
  OP_COUNT
} oti_opcode;

/* b and c together form a 32-bit operand (oti_instruction_bc). */
typedef struct {
  uint8_t op, x;
  uint16_t a, b, c;
} oti_instruction;

static inline uint32_t oti_instruction_bc(oti_instruction ins) {
  return (uint32_t)ins.b | (uint32_t)ins.c << 16;
}

typedef struct {
  uint32_t index;
  bool local, boxed;
} oti_capture;

typedef struct {
  oti_instruction* code;
  size_t count, capacity;
  /* Registers above register 0; a frame occupies 1 + locals stack slots. */
  size_t locals, arity, optional;
  oti_capture* captures;
  size_t capture_count, capture_capacity;
  /* variadic is rest || optional, so a call can check its arity with one test. */
  bool rest, variadic, evaluator;
  uint32_t namespace;
} oti_function;

/* floor is the stack height when the frame was entered. pc is where the frame
 * resumes. Code is only reclaimed when no frames are live. */
typedef struct {
  oti_function* fn;
  const oti_instruction* pc;
  size_t base, floor;
} oti_frame;

/* slot receives a caught condition; sp is the frame top to restore. */
typedef struct {
  size_t frames, sp, target, params, handlers, handler_top, restarts, cleanups, slot;
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
  C_OTHER,
  C_COUNT
};
typedef struct oti_foreign_state oti_foreign_state;
typedef struct oti_module_state oti_module_state;

typedef struct {
  char* text;
  size_t length;
} oti_documentation;

/* Host-side metadata for a global. The values themselves are in
 * ot_state.globals, which is a collector root array. */
typedef struct {
  char* name;
  oti_documentation doc;
  uint32_t space;
  bool defined, is_private, macro;
} oti_global_info;

/* A builtin reads its arguments from rt->stack[first, first + count). It
 * returns 0 after recording an error. */
typedef ot_value (*oti_builtin_fn)(ot_state* rt, size_t first, size_t count);

/* name is NULL for compiler-only helpers. A table ends with a NULL run. */
typedef struct {
  unsigned id;
  const char* name;
  oti_builtin_fn run;
} oti_builtin_def;

struct ot_state {
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
  oti_global_info* global_info;
  size_t global_info_capacity;
  char** namespaces;
  size_t namespace_count, namespace_capacity;
  uint32_t current_namespace, required_namespace;
  oti_module_state* modules;
  size_t sp, stack_capacity, stack_limit;
  /* Slots usable without growing the stack: the lesser of capacity and limit. */
  size_t stack_room;
  /* Every stack slot below stack_stale holds a valid value, even above sp. Slots
   * above sp at a collection, or never written, may not. */
  size_t stack_stale;
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
  /* Global cell of each builtin; inlined builtins cannot be redefined. */
  uint32_t builtin_cells[B_COUNT];
  oti_builtin_fn builtin_run[B_COUNT];
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
char* oti_read_file(ot_state* rt, FILE* file, size_t limit, const char* what, size_t* length);
bool oti_reclaim(ot_state* rt);
bool oti_coded_error(ot_state* rt, const char* code, const char* message);
/* Record an error and return 0, the failure result of a builtin. */
ot_value oti_fail(ot_state* rt, const char* message);
/* A new pair. car and cdr are rooted across the allocation. */
ot_value oti_cons(ot_state* rt, ot_value car, ot_value cdr);
bool oti_error(ot_state* rt, const char* message);
bool oti_host_entry(ot_state* rt);
bool oti_grow(ot_state* rt, void** data, size_t* capacity, size_t needed, size_t width);
bool oti_stack(ot_state* rt, size_t needed);
ot_value oti_alloc(ot_state* rt, uint32_t kind, uint32_t fields, size_t bytes);
uint32_t oti_global(ot_state* rt, const char* name, size_t length);
uint32_t oti_lookup_global(ot_state* rt, const char* name, size_t length);
uint32_t oti_resolve_global(ot_state* rt, const char* name, size_t length);
uint32_t oti_namespace(ot_state* rt, const char* name, size_t length);
bool oti_require(ot_state* rt, const oti_ast* specs);
void oti_modules_destroy(ot_state* rt);
uint32_t oti_binding_global(ot_state* rt, uint32_t space, const char* name, size_t length,
                            bool create);
uint32_t oti_import_global_in(ot_state* rt, uint32_t space, const char* name, size_t length);
uint32_t oti_import_global(ot_state* rt, const char* name, size_t length);
uint32_t oti_alias_namespace(ot_state* rt, const char* name, size_t length);
uint32_t oti_constant(ot_state* rt, ot_value value);
ot_value oti_text(ot_state* rt, uint32_t kind, const char* text, size_t length);
ot_value oti_float(ot_state* rt, double number);
ot_value oti_evaluator(ot_state* rt, uint32_t space);
ot_value oti_eval_value(ot_state* rt, ot_value value);
oti_ast* oti_syntax_value(ot_state* rt, ot_value value, oti_arena* arena);
ot_value oti_read_string(ot_state* rt, ot_value string);
oti_ast* oti_expand_form(ot_state* rt, const oti_ast* form, oti_arena* arena);
bool oti_compile(ot_state* rt, const oti_ast* form, uint32_t* function);
bool oti_execute(ot_state* rt, uint32_t function, ot_value* result);
bool oti_drive(ot_state* rt, size_t stop_frames);
bool oti_invoke(ot_state* rt, size_t argc, ot_value* result);
bool oti_apply(ot_state* rt, size_t call, size_t argc, bool tail);
void oti_restore_params(ot_state* rt, size_t count);
bool oti_bind_param(ot_state* rt, ot_value param, ot_value value);
ot_value oti_param_call(ot_state* rt, ot_value param, size_t count);
bool oti_builtins(ot_state* rt);
bool oti_classes_init(ot_state* rt);
bool oti_generic_prepare(ot_state* rt, size_t call, size_t* count);
size_t oti_format_float(double value, char* buffer, size_t size);
bool oti_number(ot_state* rt, ot_value value, double* result);
int oti_integer_real(intptr_t integer, double real);
size_t oti_utf8_next(const char* s, size_t n, size_t p, uint32_t* cp);
size_t oti_utf8_encode(uint32_t cp, char bytes[4]);
ot_value oti_string_get(ot_value string, ot_value key, ot_value fallback);

ot_value oti_error_condition(ot_state* rt);
ot_value oti_raise(ot_state* rt, size_t first, size_t count);
ot_value oti_signal(ot_state* rt, size_t first, size_t count);
void oti_signal_error(ot_state* rt);
void oti_restore_handlers(ot_state* rt, size_t count, size_t top);
bool oti_install_handlers(ot_state* rt, size_t first, size_t count);
void oti_remove_handlers(ot_state* rt, size_t count);
ot_value oti_intern(ot_state* rt, unsigned kind, const char* text, size_t length);
ot_value oti_call_list(ot_state* rt, ot_value function, ot_value args);
uint64_t oti_symbol_id(ot_value symbol);
ot_value oti_builtin(ot_state* rt, unsigned id, size_t first, size_t count);
int oti_builtin_index(const char* name);
bool oti_enter(ot_state* rt, size_t call, size_t argc, bool tail);
bool oti_leave(ot_state* rt, ot_value result);

bool oti_subtype(ot_value child, ot_value parent);
ot_value oti_dispatch_type(ot_state* rt, ot_value value);
ot_value oti_instance_call(ot_state* rt, ot_value instance, size_t first, size_t count);
ot_value oti_instance_copy(ot_state* rt, ot_value instance);
ot_value oti_record_call(ot_state* rt, ot_value record, size_t first, size_t count);

ot_value oti_equal(ot_state* rt, ot_value a, ot_value b, bool keys);
ot_value oti_table_set(ot_state* rt, ot_value table, ot_value key, ot_value value);
ot_value oti_table_get(ot_state* rt, ot_value table, ot_value key, ot_value fallback);

static inline bool oti_collection(ot_value v) {
  return value_is_ptr(v) &&
         (object_kind(v) == K_ARRAY || object_kind(v) == K_TABLE || object_kind(v) == K_KEYWORD);
}

ot_value oti_collection_call(ot_state* rt, size_t callable, size_t first, size_t count);
void oti_foreign_collect(ot_state* rt);
void oti_foreign_stats(const ot_state* rt, ot_stats* stats);
bool oti_foreign_init(ot_state* rt);
void oti_foreign_destroy(ot_state* rt);
bool oti_foreign_owned(ot_value value);
ot_value oti_foreign_call(ot_state* rt, size_t callable, size_t first, size_t count);

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
ot_value oti_decode_error(ot_state* rt, size_t position);

bool oti_install_restarts(ot_state* rt, size_t first, size_t count, size_t target);
void oti_restore_restarts(ot_state* rt, size_t scopes);
bool oti_resume_restart(ot_state* rt, size_t stop_frames);
bool oti_install_cleanup(ot_state* rt, bool outcome);
bool oti_run_cleanup(ot_state* rt, bool unwinding);
void oti_restore_cleanups(ot_state* rt, size_t count);
ot_value oti_io_error(ot_state* rt, const char* reason);
ot_value oti_bytes_new(ot_state* rt, size_t n);
size_t oti_utf8_scalar(const unsigned char* s, size_t p, size_t end);
void oti_port_repr(FILE* out, ot_value port);
ot_value oti_read_source(ot_state* rt, const char* source, size_t length);
ot_value oti_tokens(ot_state* rt, size_t first, size_t count);
void oti_ports_destroy(ot_state* rt);
ot_value oti_file_error(ot_state* rt, const char* op, ot_value path, int error);
ot_value oti_ffi_error(ot_state* rt, const char* operation, const char* reason,
                       const char* message);
/* Object layouts. Each struct mirrors what oti_alloc lays out for its kind:
 * the header, then the traced fields, then the opaque bytes. Kinds with a
 * variable number of fields end in a flexible array. OTI_LAYOUT checks that
 * the traced fields come first; allocation sites must pass the same field
 * count. Text, floats, bytevectors, closures and record types are still
 * accessed through object_fields and object_bytes. */
#define OTI_FIELDS_SIZE(n) (sizeof(oti_object_header) + (n) * sizeof(ot_value))
#define OTI_LAYOUT(type, fields, first_byte)                                                       \
  _Static_assert(offsetof(type, first_byte) == OTI_FIELDS_SIZE(fields), #type " layout")
#define OTI_FIELDS_ONLY(type, fields)                                                              \
  _Static_assert(sizeof(type) == OTI_FIELDS_SIZE(fields), #type " layout")
/* The opaque byte count to pass to oti_alloc for a struct with these fields. */
#define OTI_BYTES(type, fields) (sizeof(type) - OTI_FIELDS_SIZE(fields))

/* Sanitizer builds check the kind behind every cast. */
#ifdef OTIUM_CHECK_KINDS
#define OTI_CAST(type, kind, v) ((type*)(assert(oti_kind((v), (kind))), (v)))
#else
#define OTI_CAST(type, kind, v) ((type*)(v))
#endif

typedef struct {
  oti_object_header header;
  ot_value car, cdr;
} oti_pair_obj;

OTI_FIELDS_ONLY(oti_pair_obj, 2);

typedef struct {
  oti_object_header header;
  ot_value value;
} oti_box_obj;

OTI_FIELDS_ONLY(oti_box_obj, 1);

typedef struct {
  oti_object_header header;
  ot_value expander;
} oti_macro_obj;

OTI_FIELDS_ONLY(oti_macro_obj, 1);

/* data is a K_ARRAY_DATA object of capacity fields. */
typedef struct {
  oti_object_header header;
  ot_value data;
  size_t length, capacity;
} oti_array_obj;

OTI_LAYOUT(oti_array_obj, 1, length);

/* data is a K_TABLE_DATA object: key/value fields, then the bucket index in
 * its opaque bytes. */
typedef struct {
  oti_object_header header;
  ot_value data;
  size_t count, used, capacity;
} oti_table_obj;

OTI_LAYOUT(oti_table_obj, 1, count);

/* data is a K_BUFFER_DATA object; characters counts UTF-8 scalars. */
typedef struct {
  oti_object_header header;
  ot_value data;
  size_t length, capacity, characters;
} oti_buffer_obj;

OTI_LAYOUT(oti_buffer_obj, 1, length);

/* layout is a K_LAYOUT object with LAYOUT_STRIDE fields per slot. */
typedef struct {
  oti_object_header header;
  ot_value name, parent, layout;
  /* The symbol naming the namespace that defined the class. */
  ot_value owner;
  bool abstract, builtin;
} oti_class_obj;

OTI_LAYOUT(oti_class_obj, 4, abstract);

/* A slot's key, initializer and whether it is a property (true) or state. */
enum { LAYOUT_KEY, LAYOUT_INIT, LAYOUT_PROPERTY, LAYOUT_STRIDE };

/* layout is the class layout the instance was built with; data is a
 * K_ARRAY_DATA object of its slot values. */
typedef struct {
  oti_object_header header;
  ot_value type, layout, data;
} oti_instance_obj;

OTI_FIELDS_ONLY(oti_instance_obj, 3);

typedef struct {
  oti_object_header header;
  ot_value name, methods;
  size_t arity;
} oti_generic_obj;

OTI_LAYOUT(oti_generic_obj, 2, arity);

typedef struct {
  oti_object_header header;
  ot_value signature, procedure;
  bool around, next;
} oti_method_obj;

OTI_LAYOUT(oti_method_obj, 2, around);

/* The remaining method chain for next-method, and the call's argument count. */
typedef struct {
  oti_object_header header;
  ot_value methods;
  size_t count;
} oti_next_obj;

OTI_LAYOUT(oti_next_obj, 1, count);

typedef struct {
  oti_object_header header;
  ot_value name, value;
} oti_param_obj;

OTI_FIELDS_ONLY(oti_param_obj, 2);

typedef struct {
  oti_object_header header;
  ot_value name, description, handler;
} oti_restart_obj;

OTI_FIELDS_ONLY(oti_restart_obj, 3);

typedef struct {
  oti_object_header header;
  ot_value type;
  ot_value values[];
} oti_record_obj;

OTI_FIELDS_ONLY(oti_record_obj, 1);

static inline oti_pair_obj* oti_as_pair(ot_value v) { return OTI_CAST(oti_pair_obj, K_PAIR, v); }

static inline oti_box_obj* oti_as_box(ot_value v) { return OTI_CAST(oti_box_obj, K_BOX, v); }

static inline oti_macro_obj* oti_as_macro(ot_value v) {
  return OTI_CAST(oti_macro_obj, K_MACRO, v);
}

static inline oti_array_obj* oti_as_array(ot_value v) {
  return OTI_CAST(oti_array_obj, K_ARRAY, v);
}

static inline oti_table_obj* oti_as_table(ot_value v) {
  return OTI_CAST(oti_table_obj, K_TABLE, v);
}

static inline oti_buffer_obj* oti_as_buffer(ot_value v) {
  return OTI_CAST(oti_buffer_obj, K_BUFFER, v);
}

static inline oti_class_obj* oti_as_class(ot_value v) {
  return OTI_CAST(oti_class_obj, K_CLASS, v);
}

static inline oti_instance_obj* oti_as_instance(ot_value v) {
  return OTI_CAST(oti_instance_obj, K_INSTANCE, v);
}

static inline oti_generic_obj* oti_as_generic(ot_value v) {
  return OTI_CAST(oti_generic_obj, K_GENERIC, v);
}

static inline oti_method_obj* oti_as_method(ot_value v) {
  return OTI_CAST(oti_method_obj, K_METHOD, v);
}

static inline oti_next_obj* oti_as_next(ot_value v) { return OTI_CAST(oti_next_obj, K_NEXT, v); }

static inline oti_param_obj* oti_as_param(ot_value v) {
  return OTI_CAST(oti_param_obj, K_PARAM, v);
}

static inline oti_restart_obj* oti_as_restart(ot_value v) {
  return OTI_CAST(oti_restart_obj, K_RESTART, v);
}

static inline oti_record_obj* oti_as_record(ot_value v) {
  return OTI_CAST(oti_record_obj, K_RECORD, v);
}

static inline ot_value oti_car(ot_value pair) { return oti_as_pair(pair)->car; }

static inline ot_value oti_cdr(ot_value pair) { return oti_as_pair(pair)->cdr; }

/* Builtin tables, one per module. */
extern const oti_builtin_def oti_bytes_builtins[], oti_class_builtins[], oti_collection_builtins[],
    oti_condition_builtins[], oti_filesystem_builtins[], oti_foreign_builtins[],
    oti_instance_builtins[], oti_numeric_builtins[], oti_port_builtins[], oti_process_builtins[],
    oti_record_builtins[], oti_reflection_builtins[], oti_restart_builtins[], oti_string_builtins[],
    oti_support_builtins[], oti_table_builtins[], oti_time_builtins[];

/* Plain helpers other modules use in place of calling a builtin. */
ot_value oti_table_new(ot_state* rt);
ot_value oti_array_from(ot_state* rt, size_t first, size_t count);
ot_value oti_list_from(ot_state* rt, size_t first, size_t count);
bool oti_procedure(ot_value v);

/* The table, string and bytevector cases of the generic collection builtins,
 * which collections.c hands on with the same arguments. */
ot_value oti_table_get_builtin(ot_state* rt, size_t first, size_t count);
ot_value oti_table_length_builtin(ot_state* rt, size_t first, size_t count);
ot_value oti_table_put_builtin(ot_state* rt, size_t first, size_t count);
ot_value oti_table_push_builtin(ot_state* rt, size_t first, size_t count);
ot_value oti_table_pop_builtin(ot_state* rt, size_t first, size_t count);
ot_value oti_table_copy_builtin(ot_state* rt, size_t first, size_t count);
ot_value oti_table_contains_builtin(ot_state* rt, size_t first, size_t count);
ot_value oti_string_length_builtin(ot_state* rt, size_t first, size_t count);
ot_value oti_bytes_copy_builtin(ot_state* rt, size_t first, size_t count);

#endif
