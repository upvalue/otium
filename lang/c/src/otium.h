#ifndef OTIUM_H
#define OTIUM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct ot_state ots;
typedef uintptr_t otv;

/* Optional contracts for tools/gc-lint. They do not change generated code. */
#if defined(__clang__)
#define OT_GC_MAY_ALLOCATE __attribute__((annotate("otium.gc.may_allocate")))
#define OT_GC_ALLOW_STALE_BORROW(REASON)                                                           \
  __attribute__((annotate("otium.gc.allow_stale_borrow:" REASON)))
#define OT_GC_ALLOW_DIRECT_STORE(REASON)                                                           \
  __attribute__((annotate("otium.gc.allow_direct_store:" REASON)))
#else
#define OT_GC_MAY_ALLOCATE
#define OT_GC_ALLOW_STALE_BORROW(REASON)
#define OT_GC_ALLOW_DIRECT_STORE(REASON)
#endif

#define ot_nil ((otv)2u)
#define ot_true ((otv)6u)
#define ot_false ((otv)10u)
#define ot_null ((otv)14u)

#define OT_INT_MAX ((intptr_t)(INTPTR_MAX >> 1u))
#define OT_INT_MIN (-OT_INT_MAX - (intptr_t)1)

static inline bool ot_is_int(otv value) { return (value & 1u) != 0; }

static inline bool ot_int_fits(intptr_t value) {
  return value >= OT_INT_MIN && value <= OT_INT_MAX;
}

/* Return nil rather than producing a different integer when value does not fit
 * in the tagged representation. */
static inline otv ot_make_int(intptr_t value) {
  return ot_int_fits(value) ? ((otv)(uintptr_t)value << 1u) | 1u : ot_nil;
}

static inline intptr_t ot_get_int(otv value) { return ((intptr_t)value) >> 1u; }

typedef struct ot_config {
  size_t heap_init;
  size_t heap_max;
  unsigned max_depth;
  bool gc_stress;
  bool gc_force_compact;
} ot_config;

typedef struct ot_gc_phase_stats {
  uint64_t collections;
  uint64_t total_pause_ns;
  uint64_t max_pause_ns;
} ot_gc_phase_stats;

#define OT_GC_STATS_FIELDS(STAT, PHASE)                                                            \
  STAT(uint64_t, allocations, "allocations", "allocations", COUNT)                                 \
  STAT(uint64_t, collections, "collections", "collections", COUNT)                                 \
  STAT(uint64_t, allocated_bytes, "allocated_bytes", "allocated bytes", BYTES)                     \
  STAT(uint64_t, copied_bytes, "copied_bytes", "copied bytes", BYTES)                              \
  STAT(uint64_t, promoted_bytes, "promoted_bytes", "promoted bytes", BYTES)                        \
  STAT(uint64_t, moved_bytes, "moved_bytes", "moved bytes", BYTES)                                 \
  STAT(uint64_t, reclaimed_bytes, "reclaimed_bytes", "reclaimed bytes", BYTES)                     \
  STAT(uint64_t, mark_stack_overflows, "mark_stack_overflows", "mark-stack overflows", COUNT)      \
  STAT(size_t, used_bytes, "used_bytes", "heap used bytes", BYTES)                                 \
  STAT(size_t, peak_used_bytes, "peak_used_bytes", "peak heap used bytes", BYTES)                  \
  STAT(size_t, capacity_bytes, "capacity_bytes", "heap capacity bytes", BYTES)                     \
  STAT(size_t, reserved_bytes, "reserved_bytes", "reserved bytes", BYTES)                          \
  STAT(size_t, metadata_bytes, "metadata_bytes", "metadata bytes", BYTES)                          \
  STAT(size_t, fragmentation_bytes, "fragmentation_bytes", "fragmentation bytes", BYTES)           \
  STAT(size_t, largest_free_region_bytes, "largest_free_region_bytes",                             \
       "largest free region bytes", BYTES)                                                         \
  PHASE(full_copy, "full-copy", "full_copy_collections", "full_copy_total_pause_ns",               \
        "full_copy_max_pause_ns")                                                                  \
  PHASE(minor, "minor", "minor_collections", "minor_total_pause_ns", "minor_max_pause_ns")         \
  PHASE(major_sweep, "major-sweep", "major_sweep_collections", "major_sweep_total_pause_ns",       \
        "major_sweep_max_pause_ns")                                                                \
  PHASE(major_compact, "major-compact", "major_compact_collections",                               \
        "major_compact_total_pause_ns", "major_compact_max_pause_ns")                              \
  PHASE(mutator_pause, "mutator pauses", "mutator_pause_collections", "mutator_pause_total_ns",    \
        "mutator_pause_max_ns")

typedef struct ot_gc_stats {
#define OT_GC_STAT(TYPE, MEMBER, JSON, HUMAN, UNIT) TYPE MEMBER;
#define OT_GC_PHASE(MEMBER, HUMAN, COLLECTIONS_JSON, TOTAL_JSON, MAX_JSON) ot_gc_phase_stats MEMBER;
  OT_GC_STATS_FIELDS(OT_GC_STAT, OT_GC_PHASE)
#undef OT_GC_PHASE
#undef OT_GC_STAT
} ot_gc_stats;

typedef enum ot_type {
  OT_TYPE_NIL,
  OT_TYPE_NULL,
  OT_TYPE_BOOLEAN,
  OT_TYPE_INT,
  OT_TYPE_FLOAT,
  OT_TYPE_SYMBOL,
  OT_TYPE_KEYWORD,
  OT_TYPE_STRING,
  OT_TYPE_PAIR,
  OT_TYPE_ARRAY,
  OT_TYPE_TABLE,
  OT_TYPE_BUFFER,
  OT_TYPE_FUNCTION,
  OT_TYPE_MACRO,
  OT_TYPE_PARAM,
  OT_TYPE_RESTART,
  OT_TYPE_EXT,
  OT_TYPE_DESCRIPTOR,
  OT_TYPE_INSTANCE,
  OT_TYPE_INTERNAL,
} ot_type;

typedef otv (*ot_nat)(ots* state, otv* args, int argc);
typedef otv (*ot_context_nat)(ots* state, otv context, otv* args, int argc);
typedef void (*ot_writer)(void* userdata, const char* bytes, size_t length);
typedef bool (*ot_loader)(void* userdata, const char* namespace_name, char** source,
                          size_t* length);

typedef enum ot_interrupt_action {
  OT_INTERRUPT_CONTINUE,
  OT_INTERRUPT_ABORT,
  OT_INTERRUPT_ERROR,
} ot_interrupt_action;

typedef ot_interrupt_action (*ot_interrupt_hook)(ots* state, void* userdata);
typedef void (*ot_ext_finalizer)(ots* state, void* payload);
typedef void (*ot_module_init)(ots* state);

typedef struct ot_frame {
  struct ot_frame* prev;
  ots* state;
  size_t count;
  otv** slots;
} ot_frame;

/* Return the build defaults, suitable for passing back to ot_create. */
ot_config ot_config_default(void);
/* Create a fully bootstrapped state, or NULL when host allocation fails. */
ots* ot_create(const ot_config* config);
/* Finalize live extension values and release all storage owned by the state. */
void ot_destroy(ots* state);

/* Replace the state's output and namespace-loading callbacks. */
void ot_set_writer(ots* state, ot_writer writer, void* userdata);
void ot_set_loader(ots* state, ot_loader loader, void* userdata);
/* Run synchronously at an interrupt safepoint, never in the signal handler.
 * The hook may re-enter evaluation; continue and abort restarts are active. */
void ot_set_interrupt_hook(ots* state, ot_interrupt_hook hook, void* userdata);
/* Request a cooperative VM interruption; safe from a signal handler. */
void ot_interrupt(ots* state);

/* Evaluate all forms in source. On failure, inspect ot_condition(state). */
OT_GC_MAY_ALLOCATE bool ot_eval_src(ots* state, const char* source, size_t length, const char* name,
                                    otv* out);
otv ot_condition(const ots* state);
void ot_clear_condition(ots* state);
/* A quit/exit form makes evaluation return false. If one was requested,
 * write its 0..255 process-style status and return true. */
bool ot_exit_status(const ots* state, int* status);

/* Copy application arguments into this state. A NULL script denotes no script. */
OT_GC_MAY_ALLOCATE void ot_set_arguments(ots* state, const char* script, int argc,
                                         char* const* argv);

/* Inspect and construct values. Constructors may move every unrooted value. */
ot_type ot_value_type(otv value);
OT_GC_MAY_ALLOCATE otv ot_make_float(ots* state, double value);
OT_GC_MAY_ALLOCATE otv ot_make_string(ots* state, const char* bytes, size_t length);
OT_GC_MAY_ALLOCATE otv ot_make_symbol(ots* state, const char* bytes, size_t length);
OT_GC_MAY_ALLOCATE otv ot_make_keyword(ots* state, const char* bytes, size_t length);
OT_GC_MAY_ALLOCATE otv ot_cons(ots* state, otv car, otv cdr);
OT_GC_MAY_ALLOCATE otv ot_array_new(ots* state, size_t capacity);
OT_GC_MAY_ALLOCATE otv ot_array_append(ots* state, otv array, otv value);
otv ot_array_get(otv array, size_t index, otv fallback);
size_t ot_array_length(otv array);
OT_GC_MAY_ALLOCATE otv ot_table_new(ots* state, size_t capacity);
otv ot_table_get(ots* state, otv table, otv key, otv fallback);
OT_GC_MAY_ALLOCATE otv ot_table_put(ots* state, otv table, otv key, otv value);
size_t ot_table_length(otv table);
bool ot_pair_values(otv pair, otv* car, otv* cdr);
bool ot_equal(ots* state, otv left, otv right, bool structural);
bool ot_float_value(otv value, double* out);
/* The returned byte span stays valid only until the next heap allocation. */
bool ot_string_bytes(otv value, const char** out, size_t* length);
/* Inspect an interpreted function's compact bytecode. The span stays
 * valid only until the next heap allocation. */
bool ot_function_bytecode(otv function, const char** out, size_t* length);

/* Stack frames let the moving collector update C locals in place. */
void ot_frame_push(ots* state, ot_frame* frame, otv** slots, size_t count);
void ot_frame_pop(ots* state, ot_frame* frame);
void ot_frame_cleanup(ot_frame* frame);
void ot_global_add(ots* state, otv* slot);
void ot_global_remove(ots* state, otv* slot);

#define OT_FRAME(S, ...)                                                                           \
  otv* _ot_frame_slots[] = {__VA_ARGS__};                                                          \
  ot_frame _ot_frame;                                                                              \
  ot_frame_push((S), &_ot_frame, _ot_frame_slots,                                                  \
                sizeof _ot_frame_slots / sizeof _ot_frame_slots[0])
#define OT_FRAME_POP(S) ot_frame_pop((S), &_ot_frame)
#define OT_JOIN_INNER(A, B) A##B
#define OT_JOIN(A, B) OT_JOIN_INNER(A, B)
#define OT_FRAME_SCOPED_IMPL(S, N, ...)                                                            \
  otv* OT_JOIN(_ot_frame_slots_, N)[] = {__VA_ARGS__};                                             \
  ot_frame OT_JOIN(_ot_frame_, N) [[gnu::cleanup(ot_frame_cleanup)]];                              \
  ot_frame_push((S), &OT_JOIN(_ot_frame_, N), OT_JOIN(_ot_frame_slots_, N),                        \
                sizeof OT_JOIN(_ot_frame_slots_, N) / sizeof OT_JOIN(_ot_frame_slots_, N)[0])
#define OT_FRAME_SCOPED(S, ...) OT_FRAME_SCOPED_IMPL(S, __COUNTER__, __VA_ARGS__)
#define OT_GLOBAL(S, SLOT) ot_global_add((S), (SLOT))

/* Install C callables in the current namespace or as a require-able module. */
OT_GC_MAY_ALLOCATE void ot_def_nat(ots* state, const char* name, ot_nat function);
void ot_register_module(ots* state, const char* name, ot_module_init init);
OT_GC_MAY_ALLOCATE otv ot_make_native(ots* state, const char* name, ot_context_nat function,
                                      otv context);

/* Extension values hold inline bytes or an owned host pointer. */
unsigned ot_ext_type(ots* state, const char* name, ot_ext_finalizer finalizer);
OT_GC_MAY_ALLOCATE otv ot_ext_inline(ots* state, unsigned type, const void* payload, size_t size);
OT_GC_MAY_ALLOCATE otv ot_ext_pointer(ots* state, unsigned type, void* payload);
OT_GC_MAY_ALLOCATE bool ot_ext_check(ots* state, const char* who, otv value, unsigned type,
                                     void** payload);
OT_GC_MAY_ALLOCATE otv ot_ext_release(ots* state, const char* who, otv value, unsigned type);

/* Read collector counters or request a collection at a safe embedding point. */
ot_gc_stats ot_get_gc_stats(const ots* state);
void ot_reset_gc_stats(ots* state);
OT_GC_MAY_ALLOCATE void ot_collect(ots* state);

#ifdef OT_INTERNAL

#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>

#define OT_UNDEFINED ((otv)18u)
#define OT_UNWIND ((otv)22u)
#define OT_TOMBSTONE ((otv)26u)

typedef enum ot_obj_type {
  OBJ_FLOAT = 1,
  OBJ_SYMBOL,
  OBJ_KEYWORD,
  OBJ_BYTES,
  OBJ_STRING,
  OBJ_PAIR,
  OBJ_SLOTS,
  OBJ_ARRAY,
  OBJ_ENTRIES,
  OBJ_TABLE,
  OBJ_BUFFER,
  OBJ_BINDING,
  OBJ_ENV,
  OBJ_VAR,
  OBJ_ALIAS,
  OBJ_NAMESPACE,
  OBJ_CODE,
  OBJ_FUNCTION,
  OBJ_NAT,
  OBJ_MACRO,
  OBJ_PARAM,
  OBJ_RESTART,
  OBJ_EXT,
  OBJ_DISPATCH_TYPE,
  OBJ_GENERIC,
  OBJ_LAYOUT,
  OBJ_INSTANCE,
  OBJ_CELL,
  OBJ_FREE,
} ot_obj_type;

typedef struct ot_obj {
  uintptr_t header;
} ot_obj;

typedef struct ot_float_obj {
  uintptr_t header;
  double value;
} ot_float_obj;

typedef struct ot_name_obj {
  uintptr_t header;
  otv next;
  otv cache_namespace;
  otv cache_var;
  otv qualified_prefix;
  otv qualified_member;
  uint32_t hash;
  uint32_t length;
  bool special_form;
  char bytes[];
} ot_name_obj;

typedef struct ot_bytes_obj {
  uintptr_t header;
  size_t length;
  unsigned char data[];
} ot_bytes_obj;

typedef struct ot_string_obj {
  uintptr_t header;
  otv bytes;
  size_t length;
} ot_string_obj;

typedef struct ot_pair_obj {
  uintptr_t header;
  otv car;
  otv cdr;
  uint64_t stable_id;
  bool frozen;
} ot_pair_obj;

typedef struct ot_slots_obj {
  uintptr_t header;
  size_t capacity;
  otv values[];
} ot_slots_obj;

typedef struct ot_array_obj {
  uintptr_t header;
  otv slots;
  size_t length;
  uint64_t stable_id;
} ot_array_obj;

typedef struct ot_entry {
  otv key;
  otv value;
  uint32_t hash;
  bool live;
} ot_entry;

typedef struct ot_entries_obj {
  uintptr_t header;
  size_t capacity;
  ot_entry values[];
} ot_entries_obj;

typedef struct ot_table_obj {
  uintptr_t header;
  otv entries;
  otv index;
  size_t length;
  size_t used;
  uint64_t stable_id;
} ot_table_obj;

typedef struct ot_dispatch_type_obj {
  uintptr_t header;
  otv name;
  otv owner;
  otv parent;
  otv layout;
  uint64_t stable_id;
  bool builtin;
  bool abstract;
} ot_dispatch_type_obj;

typedef struct ot_layout_obj {
  uintptr_t header;
  otv type;
  otv keys;
  otv defaults;
  otv states;
  otv indices;
} ot_layout_obj;

/* A migrated instance forwards through layout; otherwise slots are inline. */
typedef struct ot_instance_obj {
  uintptr_t header;
  otv layout;
  uint64_t stable_id;
  otv values[];
} ot_instance_obj;

typedef struct ot_generic_obj {
  uintptr_t header;
  otv name;
  otv owner;
  otv methods;
  otv cache;
  otv positions;
  uint64_t epoch;
  size_t arity;
  uint64_t stable_id;
} ot_generic_obj;

typedef struct ot_buffer_obj {
  uintptr_t header;
  otv bytes;
  size_t length;
} ot_buffer_obj;

typedef struct ot_binding_obj {
  uintptr_t header;
  otv name;
  otv value;
  otv next;
  size_t slot;
  bool assigned;
  bool captured;
  bool recursive;
} ot_binding_obj;

typedef struct ot_cell_obj {
  uintptr_t header;
  otv value;
} ot_cell_obj;

typedef struct ot_env_obj {
  uintptr_t header;
  otv parent;
  otv data;
  otv namespace_value;
  size_t slot_count;
  bool compile_time;
  otv compile_owner;
  otv layout;
} ot_env_obj;

typedef struct ot_var_obj {
  uintptr_t header;
  otv name;
  otv value;
  otv doc;
  otv next;
  bool private_value;
} ot_var_obj;

/* Borrowed documentation bytes; these helpers do not allocate managed objects. */
const char* ot_runtime_documentation(const char* name);
void ot_binding_documentation(otv var_value, const char** text, size_t* length);

typedef struct ot_alias_obj {
  uintptr_t header;
  otv name;
  otv value;
  otv next;
} ot_alias_obj;

typedef struct ot_namespace_obj {
  uintptr_t header;
  otv name;
  otv vars;
  otv var_index;
  otv refers;
  otv refer_index;
  otv aliases;
  otv next;
  bool loaded;
  bool loading;
} ot_namespace_obj;

typedef struct ot_code_obj {
  uintptr_t header;
  otv bytes;
  otv constants;
  otv params;
  otv name;
  size_t length;
  size_t constant_count;
  size_t fixed_arity;
  size_t local_count;
  /* Compilation keeps binding identities until all nested writes are known.
   * Final locals contain cell flags; captures contain [cell?, source] recipes.
   * Children are discarded after finalization. */
  otv locals;
  otv captures;
  otv children;
  bool boxed_parameters;
} ot_code_obj;

typedef struct ot_function_obj {
  uintptr_t header;
  otv code;
  otv namespace_value;
  otv name;
  /* Flat values or shared cells. No enclosing environment is retained. */
  otv captures;
} ot_function_obj;

typedef struct ot_nat_obj {
  uintptr_t header;
  ot_nat function;
  ot_context_nat context_function;
  otv context;
  otv name;
  bool vm_direct;
} ot_nat_obj;

typedef struct ot_macro_obj {
  uintptr_t header;
  otv function;
} ot_macro_obj;

typedef struct ot_param_obj {
  uintptr_t header;
  otv name;
  otv value;
} ot_param_obj;

typedef struct ot_restart_obj {
  uintptr_t header;
  otv name;
  otv description;
  uint64_t id;
} ot_restart_obj;

typedef struct ot_ext_obj {
  uintptr_t header;
  otv next;
  unsigned type;
  bool pointer_payload;
  bool released;
  size_t size;

  union {
    void* pointer;
    max_align_t align;
    unsigned char bytes[1];
  } payload;
} ot_ext_obj;

typedef struct ot_global_root {
  struct ot_global_root* next;
  otv* slot;
} ot_global_root;

typedef struct ot_handler_frame {
  struct ot_handler_frame* prev;
  otv pred;
  otv handler;
} ot_handler_frame;

typedef struct ot_restart_clause {
  otv restart;
  otv params;
  otv body;
} ot_restart_clause;

typedef struct ot_restart_frame {
  struct ot_restart_frame* prev;
  ot_restart_clause* clauses;
  size_t count;
} ot_restart_frame;

typedef struct ot_param_frame {
  struct ot_param_frame* prev;
  otv param;
  otv value;
} ot_param_frame;

typedef struct ot_module {
  struct ot_module* next;
  char* name;
  ot_module_init init;
  bool initialized;
} ot_module;

typedef struct ot_ext_type_info {
  char* name;
  ot_ext_finalizer finalizer;
} ot_ext_type_info;

typedef struct ot_vm_frame {
  otv function;
  otv env;
  otv next_method;
  size_t ip;
  size_t base;
  size_t local_count;
  bool stack_locals;
} ot_vm_frame;

typedef enum ot_unwind_kind {
  UNWIND_NONE,
  UNWIND_CONDITION,
  UNWIND_RESTART,
  UNWIND_QUIT,
} ot_unwind_kind;

typedef struct ot_vm {
  ot_handler_frame* handlers;
  ot_restart_frame* restarts;
  ot_param_frame* params;
  otv* vm_stack;
  size_t vm_stack_count;
  size_t vm_stack_capacity;
  ot_vm_frame* vm_frames;
  size_t vm_frame_count;
  size_t vm_frame_capacity;
  otv current_namespace;
  otv condition;
  otv unwind_args;
  ot_unwind_kind unwind_kind;
  uint64_t unwind_restart_id;
  uint64_t next_restart_id;
  unsigned frame_limit;
  unsigned poll_count;
  atomic_bool interrupted;
  bool in_interrupt_hook;
} ot_vm;

struct ot_state {
  ot_config config;
  struct ot_gc_heap* gc;
  void* compiler;
  ot_frame* frames;
  ot_global_root* globals;
  ot_module* modules;
  ot_ext_type_info* ext_types;
  size_t ext_type_count;
  size_t ext_type_capacity;
  ot_vm vm;
  otv symbols;
  otv namespaces;
  otv core_namespace;
  otv expander;
  otv expand_head;
  otv type_parents;
  otv dispatch_types;
  otv object_class;
  otv update_instance;
  otv next_method;
  uint64_t class_epoch;
  void* ffi_state;
  otv exts;
  uint64_t next_stable_id;
  uint64_t gensym_id;
  otv process_arguments;
  otv process_script;
  bool quit_requested;
  int quit_status;
  ot_writer writer;
  void* writer_userdata;
  ot_loader loader;
  void* loader_userdata;
  bool (*poll_hook)(void*);
  unsigned dev_poll_count;
  ot_interrupt_hook interrupt_hook;
  void* interrupt_userdata;
  ot_gc_stats stats;
};

static inline bool ot_is_ptr(otv value) { return value != 0 && (value & 3u) == 0; }

static inline ot_obj* ot_as_obj(otv value) { return (ot_obj*)(uintptr_t)value; }

static inline otv ot_from_obj(const void* object) { return (otv)(uintptr_t)object; }

static inline ot_obj_type ot_object_type(otv value) {
  return (ot_obj_type)((ot_as_obj(value)->header >> 1u) & 0x7fu);
}

static inline bool ot_has_type(otv value, ot_obj_type type) {
  return ot_is_ptr(value) && ot_object_type(value) == type;
}

static inline size_t ot_object_size(otv value) { return (size_t)(ot_as_obj(value)->header >> 8u); }

OT_GC_MAY_ALLOCATE void* ot_alloc(ots* state, size_t size, ot_obj_type type);

/* Host memory is zeroed. */
void* ot_host_alloc(size_t size);
void* ot_host_realloc(void* memory, size_t size);
void ot_host_free(void* memory);
uint64_t ot_platform_monotonic_ns(void);
double ot_platform_current_second(void);
bool ot_platform_read_file(const char* path, char** source, size_t* length);

void ot_default_write(void* userdata, const char* bytes, size_t length);
OT_GC_MAY_ALLOCATE otv ot_raise(ots* state, const char* format, ...);
OT_GC_MAY_ALLOCATE otv ot_raise_value(ots* state, otv condition);
/* Call any callable value; arguments are rooted for the call. */
OT_GC_MAY_ALLOCATE otv ot_apply(ots* state, otv callable, otv* args, size_t argc);
OT_GC_MAY_ALLOCATE otv ot_intern(ots* state, const char* bytes, size_t length, bool keyword);
void ot_repr_to(ots* state, otv value, bool display, void (*write)(void*, const char*, size_t),
                void* userdata);
void ot_register_cli(ots* state);
void ot_register_ffi(ots* state);
void ot_destroy_ffi(ots* state);
void ot_register_demo_extension(ots* state);
OT_GC_MAY_ALLOCATE void ot_select_namespace(ots* state, const char* name, size_t length);
OT_GC_MAY_ALLOCATE bool ot_eval_results(ots* state, const char* source, size_t length,
                                        const char* name, otv* results, size_t* line);
OT_GC_MAY_ALLOCATE bool ot_eval_partial(ots* state, const char* source, size_t length,
                                        const char* name, size_t* consumed, bool* incomplete,
                                        otv* out);

#endif
#endif
