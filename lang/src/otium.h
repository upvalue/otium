#ifndef OTIUM_H
#define OTIUM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct ot_state ots;
typedef uintptr_t otv;

#define ot_nil ((otv)2u)
#define ot_true ((otv)6u)
#define ot_false ((otv)10u)
#define ot_null ((otv)14u)

static inline bool ot_is_int(otv value) { return (value & 1u) != 0; }
static inline otv ot_make_int(intptr_t value) { return ((otv)(uintptr_t)value << 1u) | 1u; }
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

typedef struct ot_gc_stats {
  uint64_t allocations;
  uint64_t collections;
  uint64_t allocated_bytes;
  uint64_t copied_bytes;
  uint64_t promoted_bytes;
  uint64_t moved_bytes;
  uint64_t reclaimed_bytes;
  uint64_t mark_stack_overflows;
  size_t used_bytes;
  size_t peak_used_bytes;
  size_t capacity_bytes;
  size_t reserved_bytes;
  size_t metadata_bytes;
  size_t fragmentation_bytes;
  size_t largest_free_region_bytes;
  ot_gc_phase_stats full_copy;
  ot_gc_phase_stats minor;
  ot_gc_phase_stats major_sweep;
  ot_gc_phase_stats major_compact;
  ot_gc_phase_stats mutator_pause;
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
  OT_TYPE_INTERNAL,
} ot_type;

typedef otv (*ot_nat)(ots* state, otv* args, int argc);
typedef void (*ot_writer)(void* userdata, const char* bytes, size_t length);
typedef bool (*ot_loader)(void* userdata, const char* namespace_name, char** source,
                          size_t* length);
typedef enum ot_interrupt_action {
  OT_INTERRUPT_CONTINUE,
  OT_INTERRUPT_ABORT,
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
bool ot_eval_src(ots* state, const char* source, size_t length, const char* name, otv* out);
otv ot_condition(const ots* state);
void ot_clear_condition(ots* state);

/* Inspect and construct values. Constructors may move every unrooted value. */
ot_type ot_value_type(otv value);
otv ot_make_float(ots* state, double value);
otv ot_make_string(ots* state, const char* bytes, size_t length);
otv ot_make_symbol(ots* state, const char* bytes, size_t length);
otv ot_make_keyword(ots* state, const char* bytes, size_t length);
otv ot_cons(ots* state, otv car, otv cdr);
otv ot_array_new(ots* state, size_t capacity);
otv ot_array_append(ots* state, otv array, otv value);
otv ot_array_get(otv array, size_t index, otv fallback);
size_t ot_array_length(otv array);
otv ot_table_new(ots* state, size_t capacity);
otv ot_table_get(ots* state, otv table, otv key, otv fallback);
otv ot_table_put(ots* state, otv table, otv key, otv value);
size_t ot_table_length(otv table);
bool ot_pair_values(otv pair, otv* car, otv* cdr);
bool ot_equal(ots* state, otv left, otv right, bool structural);
bool ot_float_value(otv value, double* out);
/* The returned byte span stays valid only until the next heap allocation. */
bool ot_string_bytes(otv value, const char** out, size_t* length);
/* Inspect an interpreted function's printable ASCII bytecode. The span stays
 * valid only until the next heap allocation. */
bool ot_function_bytecode(otv function, const char** out, size_t* length);

/* Stack frames let the moving collector update C locals in place. */
void ot_frame_push(ots* state, ot_frame* frame, otv** slots, size_t count);
void ot_frame_pop(ots* state, ot_frame* frame);
void ot_frame_cleanup(ot_frame* frame);
void ot_global_add(ots* state, otv* slot);

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
void ot_def_nat(ots* state, const char* name, ot_nat function);
void ot_register_module(ots* state, const char* name, ot_module_init init);

/* Extension values hold inline bytes or an owned host pointer. */
unsigned ot_ext_type(ots* state, const char* name, ot_ext_finalizer finalizer);
otv ot_ext_inline(ots* state, unsigned type, const void* payload, size_t size);
otv ot_ext_pointer(ots* state, unsigned type, void* payload);
bool ot_ext_check(ots* state, const char* who, otv value, unsigned type, void** payload);
otv ot_ext_release(ots* state, const char* who, otv value, unsigned type);

/* Read collector counters or request a collection at a safe embedding point. */
ot_gc_stats ot_get_gc_stats(const ots* state);
void ot_reset_gc_stats(ots* state);
void ot_collect(ots* state);

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
} ot_binding_obj;

typedef struct ot_env_obj {
  uintptr_t header;
  otv parent;
  otv bindings;
  otv namespace_value;
} ot_env_obj;

typedef struct ot_var_obj {
  uintptr_t header;
  otv name;
  otv value;
  otv doc;
  otv next;
  bool private_value;
} ot_var_obj;

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
} ot_code_obj;

typedef struct ot_function_obj {
  uintptr_t header;
  otv code;
  otv env;
  otv namespace_value;
  otv name;
} ot_function_obj;

typedef struct ot_nat_obj {
  uintptr_t header;
  ot_nat function;
  otv name;
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
  size_t ip;
  size_t base;
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
  otv type_parents;
  otv exts;
  uint64_t next_stable_id;
  uint64_t gensym_id;
  bool quit_requested;
  ot_writer writer;
  void* writer_userdata;
  ot_loader loader;
  void* loader_userdata;
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

void* ot_alloc(ots* state, size_t size, ot_obj_type type);

void* ot_host_alloc(size_t size);
void* ot_host_realloc(void* memory, size_t size);
void ot_host_free(void* memory);
uint64_t ot_platform_monotonic_ns(void);
double ot_platform_current_second(void);
bool ot_platform_read_file(const char* path, char** source, size_t* length);

void ot_default_write(void* userdata, const char* bytes, size_t length);
otv ot_raise(ots* state, const char* format, ...);
otv ot_raise_value(ots* state, otv condition);
otv ot_intern(ots* state, const char* bytes, size_t length, bool keyword);
void ot_repr_to(ots* state, otv value, bool display, void (*write)(void*, const char*, size_t),
                void* userdata);
void ot_register_demo_extension(ots* state);
bool ot_eval_partial(ots* state, const char* source, size_t length, const char* name,
                     size_t* consumed, bool* incomplete, otv* out);
#ifdef OT_WITH_RAY
void ot_register_ray_extension(ots* state);
#endif

#endif
#endif
