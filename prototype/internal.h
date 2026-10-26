#ifndef OTIUM_PROTOTYPE_INTERNAL_H
#define OTIUM_PROTOTYPE_INTERNAL_H

#include "runtime.h"
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

enum { OT_EOF = 30, V_UNBOUND = 18, TAG_CHAR = 22, TAG_BUILTIN = 26 };

enum {

#define BUILTIN(id, name) id,
#include "builtins.def"
#undef BUILTIN
  /* Compiler-only helpers, absent from the global environment. */
  B_QQ_APPEND,
  B_EVAL_IN_NAMESPACE
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

void oti_free_function(oti_function* fn);
bool oti_reclaim(ot_runtime* rt);
bool oti_coded_error(ot_runtime* rt, const char* code, const char* message);
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
oti_ast* oti_syntax_value(ot_runtime* rt, ot_value value, oti_reader* arena);
ot_value oti_read_string(ot_runtime* rt, ot_value string);
oti_ast* oti_expand_form(ot_runtime* rt, const oti_ast* form, oti_reader* arena);
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
bool oti_foreign_init(ot_runtime* rt);
void oti_foreign_destroy(ot_runtime* rt);
ot_value oti_foreign_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count);
bool oti_foreign_owned(ot_value value);
ot_value oti_foreign_call(ot_runtime* rt, size_t callable, size_t first, size_t count);

static inline bool oti_kind(ot_value v, unsigned kind) {
  return value_is_ptr(v) && object_kind(v) == kind;
}

static inline bool oti_truth(ot_value v) { return v != OT_NIL && v != OT_FALSE; }

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
