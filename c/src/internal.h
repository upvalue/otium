#ifndef OTIUM_INTERNAL_H
#define OTIUM_INTERNAL_H

/* Every runtime source includes this header first, so POSIX declarations are
 * visible everywhere, including in the single-file amalgamation. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
/* macOS hides mmap's MAP_ANONYMOUS and MAP_NORESERVE under _POSIX_C_SOURCE. */
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif

#include "heap.h"
#include <assert.h>
#include "reader.h"

/* Clear AddressSanitizer's marks on memory that C frames once used but
 * native code owns now, such as frames the native unwinder jumped over:
 * ASan can't clear those itself on the runtime's own stacks. */
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define OTI_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(OTI_ASAN)
#include <sanitizer/asan_interface.h>
#define OTI_UNPOISON(p, n) __asan_unpoison_memory_region((void*)(p), (n))
#else
#define OTI_UNPOISON(p, n) ((void)0)
#endif

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
  K_FOREIGN_LAYOUT,
  K_NATIVE,
  K_HOST_OBJECT,
  K_THREAD,
  K_WAITQ
};

enum { V_UNBOUND = 18, TAG_CHAR = 22, TAG_BUILTIN = 26 };

_Static_assert(K_CLOSURE == OTI_CODE_PREFIX_KIND, "closures skip their code word");

/* An unbound global cell holds V_UNBOUND with its cell number above the tag,
 * so code that finds it there can name the global. */
static inline ot_value oti_unbound_marker(uint32_t cell) {
  return ((ot_value)cell << 8) | V_UNBOUND;
}

static inline bool oti_unbound(ot_value v) { return (v & 255) == V_UNBOUND; }

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
  B_FOREIGN_P,
  B_SYMBOL_STRING,
  B_STRING_SYMBOL,
  B_KEYWORD,
  B_GENSYM,
  B_STRING_LENGTH,
  B_STRING_REF,
  B_SUBSTRING,
  B_EXP_MACRO,
  B_EXP_INVOKE,
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
  B_DEFINE_CONDITION,
  B_CONDITION_IS,
  B_CLASS_P,
  B_DISPATCH_TYPE,
  B_CLASS_SUBTYPE,
  B_GENERIC_P,
  B_GENERIC_METHODS,
  B_GENERIC_DEFINE,
  B_GENERIC_METHOD,
  B_REMOVE_METHOD,
  B_TYPE_NAME,
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
  B_MEMORY_ALLOCATED,
  B_MEMORY_COLLECTIONS,
  B_MEMORY_HEAP,
  B_READER_TOKENS,
  B_DEV_OPEN,
  B_DEV_CLOSE,
  B_DEV_POLL,
  B_DEV_NEXT,
  B_DEV_CLIENT,
  B_DEV_SEND,
  B_DEV_CLOCK,
  B_DEV_NOW,
  B_DEV_PID,
  B_DEV_UID,
  B_DEV_CWD,
  B_DEV_PRIVATE_DIRECTORY,
  B_DEV_REALPATH,
  B_DEV_MTIME,
  B_DEV_NAMESPACE_NAMES,
  B_DEV_NAMESPACE_BINDINGS,
  B_DEV_EVAL_IN,
  B_NATIVE_INSTALL,
  B_NATIVE_CELL,
  B_NATIVE_CELL_BUILTIN,
  B_NATIVE_CELL_NAME,
  B_NATIVE_NAMESPACE,
  B_NATIVE_P,
  B_SOURCE_OFFSET,
  B_COMPILE_ERROR,
  B_NATIVE_CELL_DEFINE,
  B_NATIVE_CORE_CELL,
  B_NATIVE_BUILTIN,
  B_NATIVE_OBJECT_CLASS,
  B_READ_FORMS,
  B_SET_NAMESPACE,
  B_SYMBOL_SERIAL,
  B_EVALUATE,
  B_NATIVE_CELL_BOUND,
  B_SPAWN,
  B_SELF,
  B_YIELD,
  B_SLEEP,
  B_THREAD_JOIN,
  B_THREAD_STATUS,
  B_THREAD_RESULT,
  B_THREAD_NAME,
  B_THREADS,
  B_THREAD_DAEMON,
  B_THREAD_KILL,
  B_MAKE_WAITQ,
  B_PARK,
  B_WAKE_ONE,
  B_WAKE_ALL,
  B_ATOMIC,
  B_SEND,
  B_MAILBOX_NEXT,
  B_MAILBOX_TAKE,
  B_MAILBOX_REWIND,
  B_THREAD_DEADLINE,
  B_SCHEDULER_CLOCK,
  B_THREAD_INTERRUPT,
  B_MONITOR,
  B_DEMONITOR,
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

/* Builtins the compiler turns into opcodes. Their own cells are immutable. */
static inline bool oti_inlined_builtin(unsigned id) {
  switch (id) {
    case B_ADD:
    case B_SUB:
    case B_MUL:
    case B_LT:
    case B_LE:
    case B_GT:
    case B_GE:
    case B_NUMEQ:
    case B_EQ:
    case B_CONS:
    case B_CAR:
    case B_CDR:
    case B_NOT:
    case B_NULL:
    case B_PAIR: return true;
    default: return false;
  }
}

typedef struct {
  uint32_t index;
  bool local, boxed;
} oti_capture;

/* From instruction pc on, code comes from the form at source offset. */
typedef struct {
  uint32_t pc, offset;
} oti_source_point;

/* The source of code from no file, such as runtime libraries and evaluated
 * data. Source 0 is the text the host passed to ot_eval. */
#define OTI_NO_SOURCE UINT32_MAX

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
  /* Cold metadata for error reports: source points in pc order, the source
   * the function was compiled from, and its name, owned, or NULL. */
  uint32_t source;
  char* name;
  oti_source_point* points;
  size_t point_count, point_capacity;
  /* Native functions have no code here: entry is their external entry point
   * and offset the source offset of the form that made them. */
  uintptr_t entry;
  uint32_t offset;
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
typedef struct oti_native_state oti_native_state;
typedef struct oti_native_runtime oti_native_runtime;
typedef struct oti_boot oti_boot;
typedef struct oti_threads oti_threads;

typedef struct {
  char* text;
  size_t length;
} oti_documentation;

/* A global's value, and in native processes the address its callers jump to:
 * the entry of a closure, or a stub that handles every other value. Native
 * code addresses cells at fixed offsets from the state, so there the array
 * is a reservation that never moves. */
typedef struct {
  ot_value value;
  uintptr_t entry;
} oti_cell;

/* Host-side metadata for a global. The values themselves are in
 * ot_state.globals, which the collector's root hook visits. */
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

/* A source position in an error report and the function running there, by
 * its owned name or NULL. */
typedef struct {
  uint32_t source, offset;
  char* name;
} oti_error_place;

/* The error being reported. Error sites often hold unrooted values and return
 * straight after recording, so recording never allocates on the managed heap;
 * oti_error_condition builds the condition where the error is caught. The
 * first error wins until it is cleared or taken. */
typedef struct {
  bool set;
  /* Owned and NUL-terminated, or the static out-of-memory message. */
  char* message;
  bool owned;
  /* A diagnostic code such as E0301, or empty. */
  char code[32];
  /* The source offset of the failing form, in source, when located. */
  size_t offset;
  uint32_t source;
  bool located;
  /* Owned host copies, or NULL: :name and :label, such as the unbound name
   * of E0201 or the callee and its arity for E0301. space is the namespace
   * an unbound name was looked up in, and qualifier the length of the
   * namespace prefix the name was written with, such as "json/". */
  char *name, *label;
  uint32_t space;
  size_t qualifier;
  /* Where an uncaught error happened, recorded before unwinding: the frames
   * with source, innermost first. reached are top-level forms in other files
   * that led to it, such as requires, innermost first. */
  oti_error_place *frames, *reached;
  size_t frame_count, reached_count;
  bool traced;
  /* Static strings, or NULL: the operation for :who, the symbol that starts
   * :data, and the reason of an FFI error. */
  const char *who, *tag, *ffi_reason;
  /* Managed values, in rooted slots so storing one is safe at any error site:
   * the :data values after the tag, then :value at OTI_ERROR_VALUE. */
  ot_value slots[4];
  size_t data_count;
  bool has_value;
  /* The message already names the module whose load failed. */
  bool in_module;
} oti_error_record;

#define OTI_ERROR_VALUE 3

/* What native unwinding had in flight while a cleanup runs (native.c). */
typedef struct {
  /* The cleanup stub's stack pointer: unwinding past it drops the save. */
  uintptr_t sp;
  oti_error_record error;
  ot_value condition, transfer[2];
  int status;
  bool raised, signaled, restart, exiting, interrupted;
} oti_unwind_save;

typedef struct oti_segment oti_segment;

/* Native execution state. Trampolines and generated code address these
 * fields at fixed offsets, so the block comes first in ot_state; native.c
 * checks the offsets. */
typedef struct {
  /* The stack pointer of the native frame that last called into C. */
  uintptr_t sp;
  /* The innermost entry record a trampoline left on the stack, or 0. */
  uintptr_t entry;
  /* Function entries and loop back edges compare the stack pointer against
   * this. ot_request_interrupt raises it so every check fails. */
  uintptr_t limit;
  uintptr_t stack_top;
  /* Arguments past the fifth, and every argument where C hands a call to
   * native code or back. Not a root; see oti_native_visit_roots. */
  ot_value* args;
  size_t depth;
  /* A callee and argument count that C asks a stub to enter. Rooted. */
  ot_value callee;
  size_t argc;
  size_t args_capacity;
  /* The limit when no interrupt or preemption is requested. */
  uintptr_t base_limit;
  /* While a builtin call is parked, the arguments it will be dispatched
   * with again: they are roots until then. */
  size_t args_live;
  /* The Otium stack is a chain of segments (native.c): the one in use and
   * the bytes up to and including it. */
  oti_segment* segment;
  size_t stack_bytes;
} oti_native_exec;

/* What each thread of execution has of its own (see threads.c). The fields
 * are declared once here and appear in ot_state through a union, so code
 * names them as rt->stack, rt->sp and so on while a thread switch copies
 * rt->exec as one block. The native block comes first: trampolines and
 * generated code address it at fixed offsets. */
#define OTI_EXEC_FIELDS                                                                            \
  oti_native_exec native;                                                                          \
  ot_value* stack;                                                                                 \
  size_t sp, stack_capacity;                                                                       \
  /* Slots usable without growing the stack: the lesser of capacity and limit. */                  \
  size_t stack_room;                                                                               \
  /* Every stack slot below stack_stale holds a valid value, even above sp. Slots                  \
   * above sp at a collection, or never written, may not. */                                       \
  size_t stack_stale;                                                                              \
  oti_frame* frames;                                                                               \
  /* frame_capacity never exceeds frame_limit, so the call fast path needs no                      \
   * separate depth check. In native code frame_count counts entries. */                           \
  size_t frame_count, frame_capacity;                                                              \
  ot_value* handlers;                                                                              \
  size_t* handler_previous;                                                                        \
  size_t handler_count, handler_capacity, handler_previous_capacity, handler_top, callback_depth;  \
  ot_value *restarts, restart_transfer[2];                                                         \
  size_t restart_count, restart_capacity, restart_scope_count, restart_scope_capacity;             \
  oti_restart_scope* restart_scopes;                                                               \
  bool restart_pending;                                                                            \
  ot_value* cleanups;                                                                              \
  oti_cleanup* cleanup_states;                                                                     \
  size_t cleanup_count, cleanup_capacity, cleanup_state_capacity;                                  \
  bool interrupted, signaled, raised;                                                              \
  ot_value* params;                                                                                \
  size_t param_count, param_capacity;                                                              \
  /* The condition being raised. */                                                                \
  ot_value condition_values[1];                                                                    \
  oti_try_frame* tries;                                                                            \
  size_t try_count, try_capacity;                                                                  \
  oti_error_record error;                                                                          \
  uint32_t current_namespace, required_namespace;                                                  \
  /* The source being evaluated now, an index into ot_state.sources. */                            \
  uint32_t source;                                                                                 \
  bool compiling, evaluating, expanding;                                                           \
  size_t expansion_calls, eval_depth;                                                              \
  /* Nesting of sections no other thread may preempt. */                                           \
  size_t atomic;                                                                                   \
  oti_unwind_save* unwinds;                                                                        \
  size_t unwind_count, unwind_capacity;

typedef struct {
  OTI_EXEC_FIELDS
} oti_exec;

struct ot_state {
  union {
    oti_exec exec;
    struct {
      OTI_EXEC_FIELDS
    };
  };
  oti_heap heap;
  ot_value process_values[2];
  oti_roots process_roots;
  ot_value* open_ports;
  size_t open_port_count, open_port_capacity;
  oti_roots open_port_roots;
  ot_value standard_ports[3];
  oti_roots standard_port_roots;
  ot_value* constants;
  oti_cell* globals;
  size_t constant_count, constant_capacity, global_count, global_capacity;
  /* Constant states: 0 free, 1 retained until code reclamation. */
  unsigned char* constant_state;
  size_t constant_state_capacity, constant_free, function_free, permanent_constants;
  oti_global_info* global_info;
  size_t global_info_capacity;
  char** namespaces;
  size_t namespace_count, namespace_capacity;
  oti_module_state* modules;
  oti_native_state* natives;
  size_t stack_limit;
  oti_function** functions;
  size_t function_count, function_capacity;
  size_t max_frames, frame_limit;
  volatile sig_atomic_t* interrupt_flag;
  /* Globals named by ot_set_interrupt_handler and ot_set_load_hook, or UINT32_MAX. */
  uint32_t interrupt_handler, load_hook;
  bool exit_pending;
  int exit_status;
  ot_value classes[C_COUNT];
  /* Global cell of each builtin; inlined builtins cannot be redefined. */
  uint32_t builtin_cells[B_COUNT];
  oti_builtin_fn builtin_run[B_COUNT];
  oti_roots class_roots;
  bool classes_ready, core_loaded;
  oti_roots constant_roots;
  /* Each condition type's parent, a table. */
  ot_value condition_types;
  /* Paths of the files code was compiled from, by source id; entry 0 is
   * unused. */
  char** sources;
  size_t source_count, source_capacity;
  bool stress_gc, shared_expander;
  uint64_t gensym_counter;
  ot_io_host io_host;
  void* io_userdata;
  bool io_host_set, host_io_active, host_io_reentry;
  oti_foreign_state* foreign;
  /* Native code: whether this process runs it, its code objects and stack,
   * and the stubs cells and calls jump to (see native.c). */
  bool native_mode;
  oti_native_runtime* native_runtime;
  uintptr_t native_stubs[4];
  /* The boot file a native process started from (fasl.c). */
  oti_boot* boot;
  /* Native code runs on Otium stacks and calls C through oti_c_bridge,
   * which switches to native_c_sp on the C stack. Each oti_native_enter
   * moves it below its own frame; native_c_base is its value under the
   * outermost one. While they differ, C is waiting on Otium code it called
   * (a callback), and threads don't switch. */
  uintptr_t native_c_sp, native_c_base;
  /* Green threads, once one is spawned (threads.c). */
  oti_threads* threads;
  /* What the host named the text it gives ot_eval (ot_set_source_path). */
  char* host_path;
  /* The interrupt flag when the host hasn't installed one. */
  volatile sig_atomic_t interrupt_word;
};

#ifdef OTIUM_NATIVE
/* Native processes keep the state, its global cells and its constants in one
 * reservation, at these offsets. */
#define OTI_CELL_RESERVE ((size_t)1 << 20)
#define OTI_CONSTANT_RESERVE ((size_t)1 << 21)
#define OTI_CELLS_OFFSET ((sizeof(ot_state) + 4095) & ~(size_t)4095)
#define OTI_CONSTANTS_OFFSET (OTI_CELLS_OFFSET + OTI_CELL_RESERVE * sizeof(oti_cell))
#define OTI_STATE_RESERVATION (OTI_CONSTANTS_OFFSET + OTI_CONSTANT_RESERVE * sizeof(ot_value))
#endif

/* Upper bound on a single source file or a nest of module loads. */
#define OTI_SOURCE_LIMIT ((size_t)16 * 1024 * 1024)

void oti_free_function(oti_function* fn);
/* Print function id and the functions it creates, as a listing. */
void oti_disassemble(ot_state* rt, uint32_t id, FILE* out);
const char* oti_opcode_name(unsigned op);
/* Read a whole source file. The result is NUL-terminated and owned by the
 * caller; what names the file in error messages. When the file can't be
 * opened, returns NULL with *open_error set and no error recorded. Builds
 * without an operating system read through the I/O host. */
char* oti_read_source_file(ot_state* rt, const char* path, size_t limit, const char* what,
                           size_t* length, int* open_error);
bool oti_reclaim(ot_state* rt);
bool oti_coded_error(ot_state* rt, const char* code, const char* message);
bool oti_coded_errorf(ot_state* rt, const char* code, const char* format, ...)
    __attribute__((format(printf, 3, 4)));
/* Record an error and return 0, the failure result of a builtin. */
ot_value oti_fail(ot_state* rt, const char* message);
/* A new pair. car and cdr are rooted across the allocation. */
ot_value oti_cons(ot_state* rt, ot_value car, ot_value cdr);
bool oti_error(ot_state* rt, const char* message);
bool oti_errorf(ot_state* rt, const char* format, ...) __attribute__((format(printf, 2, 3)));
/* Record an FFI error with reason, such as "type", and message "FFI reason: ...". */
bool oti_ffi_errorf(ot_state* rt, const char* reason, const char* format, ...)
    __attribute__((format(printf, 3, 4)));

/* Whether an error is recorded. */
static inline bool oti_failed(const ot_state* rt) { return rt->error.set; }

void oti_clear_error(ot_state* rt);
/* Take the recorded error out of rt, leaving none, and put it back. The saved
 * record's data slots are rooted between the two calls. */
void oti_take_error(ot_state* rt, oti_error_record* saved, oti_roots* roots);
void oti_restore_error(ot_state* rt, oti_error_record* saved, oti_roots* roots);
/* Release a taken record instead of restoring it. */
void oti_drop_error(ot_state* rt, oti_error_record* saved, oti_roots* roots);
/* Record an error whose condition has :who and :data, which starts with the
 * symbol tag and continues with up to three values. All strings are static. */
bool oti_tagged_error(ot_state* rt, const char* who, const char* tag, const char* message,
                      size_t count, const ot_value* values);
/* Locate the recorded error at offset in the current source, unless it is
 * located already. */
void oti_error_at(ot_state* rt, size_t offset);
/* Record the frames of an uncaught error before they unwind. */
void oti_trace_error(ot_state* rt);
/* After the top-level form at offset in the current source failed: locate
 * the error at the form if nothing located it, or note that the form led to
 * an error located in another file. */
void oti_locate_error(ot_state* rt, size_t offset);
/* Record E0301 for a call of fn with argc arguments, with :name and :label. */
bool oti_arity_error(ot_state* rt, const oti_function* fn, size_t argc);
/* Record E0302, "value is not callable", with :value. */
bool oti_callable_error(ot_state* rt, ot_value value);
/* Record E0201 for the unbound global cell, with :name. */
bool oti_unbound_error(ot_state* rt, uint32_t global);
/* The source id of a file path, registered on first use, or OTI_NO_SOURCE. */
uint32_t oti_source_id(ot_state* rt, const char* path);
/* Free what an error record owns. */
void oti_release_error(oti_error_record* e);
/* Record a reader's error with its catalog message, at its offset. */
bool oti_reader_error(ot_state* rt, const oti_reader* reader);
/* The message template of a diagnostic code, or NULL for an unknown code. */
const char* oti_diagnostic_message(const char* code);
/* Record E0401, "`who` expected what, got value", with :who and :value. */
bool oti_type_error(ot_state* rt, const char* who, const char* what, ot_value value);
bool oti_host_entry(ot_state* rt);
bool oti_grow(ot_state* rt, void** data, size_t* capacity, size_t needed, size_t width);
bool oti_stack(ot_state* rt, size_t needed);
ot_value oti_alloc_slow(ot_state* rt, uint32_t kind, uint32_t fields, size_t bytes);

/* Allocate by bumping the heap pointer into space the heap has already filled
 * with nil. Running out of filled space, collection, interrupts, stress GC and
 * host IO all go through oti_alloc_slow. */
static inline ot_value oti_alloc(ot_state* rt, uint32_t kind, uint32_t fields, size_t bytes) {
  oti_heap* h = &rt->heap;
  if (__builtin_expect(fields < 65536 && bytes < SIZE_MAX / 2, 1)) {
    size_t base = sizeof(oti_object_header) + (size_t)fields * sizeof(ot_value);
    size_t size = (base + bytes + 7) & ~(size_t)7;
    if (__builtin_expect(size <= h->ready - h->used, 1)) {
      oti_object_header* o = (oti_object_header*)(h->start + h->used);
      h->used += size;
      *o = (oti_object_header){.size = size, .fields = fields, .kind = kind & 255};
      if (size > base) memset((unsigned char*)o + base, 0, size - base);
      return (ot_value)o;
    }
  }
  return oti_alloc_slow(rt, kind, fields, bytes);
}

uint32_t oti_global(ot_state* rt, const char* name, size_t length);
uint32_t oti_lookup_global(ot_state* rt, const char* name, size_t length);
uint32_t oti_resolve_global(ot_state* rt, const char* name, size_t length);
uint32_t oti_namespace(ot_state* rt, const char* name, size_t length);
bool oti_require(ot_state* rt, const oti_ast* specs);
void oti_modules_destroy(ot_state* rt);
/* Host procedures from ot_define_natives. */
ot_value oti_native_call(ot_state* rt, ot_value native, size_t first, size_t count);
bool oti_native_namespace(const ot_state* rt, uint32_t space);
const char* oti_host_object_name(ot_value v);
void oti_natives_destroy(ot_state* rt);
uint32_t oti_binding_global(ot_state* rt, uint32_t space, const char* name, size_t length,
                            bool create);
uint32_t oti_import_global_in(ot_state* rt, uint32_t space, const char* name, size_t length);
uint32_t oti_import_global(ot_state* rt, const char* name, size_t length);
uint32_t oti_alias_namespace(ot_state* rt, const char* name, size_t length);
/* The alias owner gave namespace target, or NULL. */
const char* oti_namespace_alias(ot_state* rt, uint32_t owner, uint32_t target);
uint32_t oti_constant(ot_state* rt, ot_value value);
ot_value oti_text(ot_state* rt, uint32_t kind, const char* text, size_t length);
ot_value oti_float(ot_state* rt, double number);
ot_value oti_evaluator(ot_state* rt, uint32_t space);
ot_value oti_eval_value(ot_state* rt, ot_value value);
oti_ast* oti_syntax_value(ot_state* rt, ot_value value, oti_arena* arena);
ot_value oti_read_string(ot_state* rt, ot_value string);
/* A form as data. With locate, lists carry their source offsets, as the
 * expander sees them. */
ot_value oti_ast_datum(ot_state* rt, const oti_ast* form, bool locate);
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

/* The condition for a recorded error, or the raised condition. Clears the error. */
ot_value oti_error_condition(ot_state* rt);
/* The condition for a saved error record, which stays as it is. */
ot_value oti_condition_of(ot_state* rt, const oti_error_record* e);
ot_value oti_raise(ot_state* rt, size_t first, size_t count);
ot_value oti_signal(ot_state* rt, size_t first, size_t count);
void oti_signal_error(ot_state* rt);
void oti_restore_handlers(ot_state* rt, size_t count, size_t top);
bool oti_install_handlers(ot_state* rt, size_t first, size_t count);
void oti_remove_handlers(ot_state* rt, size_t count);
ot_value oti_intern(ot_state* rt, unsigned kind, const char* text, size_t length);
ot_value oti_call_list(ot_state* rt, ot_value function, ot_value args);
/* A point where Otium code may run: the VM between instructions and blocking
 * reads. Calls the interrupt handler, if any, when the flag is set. */
bool oti_interrupt_point(ot_state* rt);
uint64_t oti_symbol_id(ot_value symbol);
void oti_symbol_set_id(ot_value symbol, uint64_t id);
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
/* Visit the roots of one thread's execution state, and of every thread but
 * the running one (threads.c). */
void oti_visit_exec(ot_state* rt, oti_exec* x, oti_root_visitor visit, void* context);
void oti_visit_threads(ot_state* rt, oti_root_visitor visit, void* context);
void oti_threads_destroy(ot_state* rt);
/* A poll found the stack limit tripped while threads exist: yield when it's
 * time to. False when the thread must unwind. */
bool oti_thread_poll(ot_state* rt);
/* Whether the running thread should take a pending interrupt now. */
bool oti_thread_foreground(ot_state* rt);
/* Continue native code on stack sp at pc with value in R0. */
_Noreturn void oti_native_resume(ot_state* rt, uintptr_t sp, uintptr_t pc, ot_value value);
/* Where a thread that stopped in a C call called from native code resumes
 * (with native.sp), and where a new thread starts (native.c). */
extern char oti_c_resume[], oti_thread_begin[];
uintptr_t oti_native_entry_of(ot_state* rt, ot_value callee);
/* Give x an Otium stack of one segment, and free one. Trimming releases
 * the segments kept above the one in use. */
bool oti_native_stack_new(ot_state* rt, oti_native_exec* x);
void oti_native_stack_free(ot_state* rt, oti_native_exec* x);
void oti_native_stack_trim(ot_state* rt, oti_native_exec* x);
/* Where a builtin call that parked is dispatched again. */
uintptr_t oti_native_redispatch(ot_state* rt);
/* A builtin's result when it parked the running thread; dispatch hands the
 * thread to the scheduler and calls it again once the thread is woken. */
#define OTI_PARKED ((ot_value)34)
_Noreturn void oti_thread_wait(ot_state* rt);
_Noreturn void oti_thread_finished(ot_state* rt, ot_value result);
/* Grow every thread's native.args to the running thread's capacity. */
bool oti_threads_grow_args(ot_state* rt);
/* Live threads, counting the running one; 1 before any is spawned. */
size_t oti_thread_count(ot_state* rt);
/* How many module loads are in progress; threads don't switch inside one. */
size_t oti_module_depth(ot_state* rt);
/* Callables the VM calls as C: builtins, records, instances, params,
 * collections, foreign functions and natives. */
bool oti_primitive_value(ot_value value);
ot_value oti_call_primitive(ot_state* rt, size_t call, size_t argc);
/* The function a closure runs, in either mode. NULL for runtime stubs. */
oti_function* oti_closure_fn(ot_state* rt, ot_value closure);

#ifdef OTIUM_NATIVE
/* Native code (native.c). */
bool oti_native_init(ot_state* rt);
void oti_native_destroy(ot_state* rt);
ot_value oti_native_install(ot_state* rt, size_t slot);
ot_value oti_native_invoke(ot_state* rt, ot_value callee, size_t argc);
ot_value oti_native_invoke_list(ot_state* rt, ot_value function, ot_value args);
/* Make room for count arguments in native.args. */
bool oti_native_args(ot_state* rt, size_t count);
uintptr_t oti_native_cell_entry(ot_state* rt, ot_value value);
void oti_native_visit_exec(ot_state* rt, oti_exec* x, oti_root_visitor visit, void* context);
bool oti_native_mark_closure(ot_state* rt, ot_value closure, bool (*mark)(void*, uint32_t),
                             void* context);
void oti_native_sweep(ot_state* rt);
bool oti_native_evaluator(ot_state* rt, ot_value v, uint32_t space);
ot_value oti_native_make_evaluator(ot_state* rt, uint32_t space);
_Noreturn void oti_native_raise(ot_state* rt);
/* Boot files and fasls (fasl.c). */
bool oti_load_boot(ot_state* rt, const char* path);
bool oti_load_fasl(ot_state* rt, const char* path);
bool oti_boot_module(ot_state* rt, const char* name, bool* found);
void oti_boot_destroy(ot_state* rt);
#endif
/* Evaluate a form read at top level, as from a file (runtime.c). */
bool oti_evaluate_form(ot_state* rt, const oti_ast* form, ot_value* result);
/* Load a module by namespace, as require does without options. */
bool oti_load_module(ot_state* rt, uint32_t space);
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

/* A non-negative integer argument as a size. */
static inline bool oti_natural(ot_value v, size_t* n) {
  if (!ot_is_int(v) || ot_to_int(v) < 0) return false;
  *n = (size_t)ot_to_int(v);
  return true;
}

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
 * count. Kinds with both a variable number of fields and opaque bytes reach
 * the bytes through an accessor next to their struct. */
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

typedef struct {
  oti_object_header header;
  double value;
} oti_float_obj;

OTI_LAYOUT(oti_float_obj, 0, value);

/* Strings, symbols, keywords and bytevectors: a length, then that many bytes.
 * Text follows its bytes with a NUL, and a symbol then holds its gensym
 * serial (see oti_symbol_id). */
typedef struct {
  oti_object_header header;
  size_t length;
  unsigned char bytes[];
} oti_bytes_obj;

OTI_LAYOUT(oti_bytes_obj, 0, length);

/* K_BUFFER_DATA: the bytes of a string buffer or string output port. */
typedef struct {
  oti_object_header header;
  unsigned char bytes[];
} oti_buffer_data_obj;

OTI_FIELDS_ONLY(oti_buffer_data_obj, 0);

/* The code word, which the collector skips, then one field per captured
 * value. The code is a function id in the VM and an entry address in native
 * code (see oti_closure_function). */
typedef struct {
  oti_object_header header;
  uintptr_t code;
  ot_value captures[];
} oti_closure_obj;

_Static_assert(offsetof(oti_closure_obj, captures) == OTI_FIELDS_SIZE(1), "oti_closure_obj layout");

/* K_ARRAY_DATA: the items of an array or the slot values of an instance. */
typedef struct {
  oti_object_header header;
  ot_value items[];
} oti_array_data_obj;

OTI_FIELDS_ONLY(oti_array_data_obj, 0);

/* K_TABLE_DATA: key/value pairs as fields, then the bucket index in the
 * opaque bytes (see oti_table_buckets). */
typedef struct {
  oti_object_header header;
  ot_value entries[];
} oti_table_data_obj;

OTI_FIELDS_ONLY(oti_table_data_obj, 0);

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

/* layout is a K_LAYOUT object. */
typedef struct {
  oti_object_header header;
  ot_value name, parent, layout;
  /* The symbol naming the namespace that defined the class. */
  ot_value owner;
  bool abstract, builtin;
} oti_class_obj;

OTI_LAYOUT(oti_class_obj, 4, abstract);

/* A slot's key, initializer and whether it is a property (true) or state. */
typedef struct {
  ot_value key, init, property;
} oti_slot;

/* The number of fields each slot takes in a layout. */
#define OTI_SLOT_FIELDS (sizeof(oti_slot) / sizeof(ot_value))

_Static_assert(sizeof(oti_slot) == 3 * sizeof(ot_value), "oti_slot layout");

/* K_LAYOUT: a class's slots, in instance data order. */
typedef struct {
  oti_object_header header;
  oti_slot slots[];
} oti_layout_obj;

OTI_FIELDS_ONLY(oti_layout_obj, 0);

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

/* One keyword per record field, then whether records of the type are
 * immutable in the opaque bytes (see oti_record_type_immutable). */
typedef struct {
  oti_object_header header;
  ot_value name;
  ot_value keys[];
} oti_record_type_obj;

OTI_FIELDS_ONLY(oti_record_type_obj, 1);

static inline oti_pair_obj* oti_as_pair(ot_value v) { return OTI_CAST(oti_pair_obj, K_PAIR, v); }

/* Pairs that start lists read for the expander carry the list's source offset
 * as trailing bytes, so expanded code keeps its position. */
#define OTI_LOCATED_PAIR_BYTES sizeof(size_t)

static inline bool oti_pair_offset(ot_value v, size_t* offset) {
  if (!oti_kind(v, K_PAIR) ||
      oti_as_pair(v)->header.size < sizeof(oti_pair_obj) + OTI_LOCATED_PAIR_BYTES)
    return false;
  memcpy(offset, object_bytes(v), sizeof(*offset));
  return true;
}

/* Record offset in a pair allocated with OTI_LOCATED_PAIR_BYTES. */
static inline void oti_pair_set_offset(ot_value v, size_t offset) {
  (void)oti_as_pair(v);
  memcpy(object_bytes(v), &offset, sizeof(offset));
}

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

static inline oti_box_obj* oti_as_box(ot_value v) { return OTI_CAST(oti_box_obj, K_BOX, v); }

static inline oti_float_obj* oti_as_float(ot_value v) {
  return OTI_CAST(oti_float_obj, K_FLOAT, v);
}

/* Unchecked: several kinds share this layout. */
static inline oti_bytes_obj* oti_as_bytes(ot_value v) { return (oti_bytes_obj*)v; }

static inline size_t oti_bytes_length(ot_value v) { return oti_as_bytes(v)->length; }

static inline unsigned char* oti_bytes_data(ot_value v) { return oti_as_bytes(v)->bytes; }

/* The bytes of a string, symbol or keyword. */
static inline const char* oti_text_bytes(ot_value v, size_t* length) {
  *length = oti_as_bytes(v)->length;
  return (const char*)oti_as_bytes(v)->bytes;
}

static inline oti_buffer_data_obj* oti_as_buffer_data(ot_value v) {
  return OTI_CAST(oti_buffer_data_obj, K_BUFFER_DATA, v);
}

static inline oti_closure_obj* oti_as_closure(ot_value v) {
  return OTI_CAST(oti_closure_obj, K_CLOSURE, v);
}

static inline uint32_t oti_closure_function(ot_value v) {
  return (uint32_t)oti_as_closure(v)->code;
}

static inline void oti_closure_set_function(ot_value v, uint32_t id) {
  oti_as_closure(v)->code = id;
}

/* A closure with captures fields, its code word unset. */
static inline ot_value oti_alloc_closure(ot_state* rt, uint32_t captures) {
  return oti_alloc(rt, K_CLOSURE, captures, sizeof(uintptr_t));
}

static inline ot_value oti_global_value(const ot_state* rt, uint32_t cell) {
  return rt->globals[cell].value;
}

/* Store a global's value, keeping its entry in step in native processes. */
void oti_set_global(ot_state* rt, uint32_t cell, ot_value value);

static inline oti_array_data_obj* oti_as_array_data(ot_value v) {
  return OTI_CAST(oti_array_data_obj, K_ARRAY_DATA, v);
}

static inline oti_table_data_obj* oti_as_table_data(ot_value v) {
  return OTI_CAST(oti_table_data_obj, K_TABLE_DATA, v);
}

static inline uint32_t* oti_table_buckets(ot_value v) {
  (void)oti_as_table_data(v);
  return object_bytes(v);
}

static inline oti_layout_obj* oti_as_layout(ot_value v) {
  return OTI_CAST(oti_layout_obj, K_LAYOUT, v);
}

static inline size_t oti_layout_slots(ot_value v) {
  return oti_as_layout(v)->header.fields / OTI_SLOT_FIELDS;
}

static inline oti_record_type_obj* oti_as_record_type(ot_value v) {
  return OTI_CAST(oti_record_type_obj, K_RECORD_TYPE, v);
}

static inline size_t oti_record_type_fields(ot_value v) {
  return oti_as_record_type(v)->header.fields - 1;
}

static inline bool* oti_record_type_immutable(ot_value v) {
  (void)oti_as_record_type(v);
  return object_bytes(v);
}

static inline ot_value oti_car(ot_value pair) { return oti_as_pair(pair)->car; }

static inline ot_value oti_cdr(ot_value pair) { return oti_as_pair(pair)->cdr; }

/* Builtin tables, one per module. */
extern const oti_builtin_def oti_bytes_builtins[], oti_class_builtins[], oti_collection_builtins[],
    oti_condition_builtins[], oti_dev_builtins[], oti_filesystem_builtins[], oti_foreign_builtins[],
    oti_codegen_builtins[], oti_thread_builtins[],
    oti_instance_builtins[], oti_numeric_builtins[], oti_port_builtins[], oti_process_builtins[],
    oti_record_builtins[], oti_reflection_builtins[], oti_restart_builtins[], oti_string_builtins[],
    oti_support_builtins[], oti_table_builtins[], oti_time_builtins[];

/* Plain helpers other modules use in place of calling a builtin. */
ot_value oti_table_new(ot_state* rt);
ot_value oti_array_from(ot_state* rt, size_t first, size_t count);
/* Whether target is reachable from from through pairs, arrays and tables: 1 if
 * so, 0 if not, -1 if host memory ran out. Never allocates on the managed heap. */
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
