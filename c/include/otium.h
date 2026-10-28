#ifndef OTIUM_H
#define OTIUM_H

/* Public embedding API for the Otium runtime. This is the only header an
 * embedding host needs; everything else in the source tree is internal. */

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct ot_state ot_state;

/* Values are tagged words. Managed values move during collection, so a value
 * returned to the host is only valid until the next call into the runtime. */
typedef uintptr_t ot_value;

enum { OT_NIL = 2, OT_FALSE = 6, OT_TRUE = 10, OT_EMPTY = 14, OT_EOF = 30 };

static inline ot_value ot_int(intptr_t n) { return ((uintptr_t)n << 1) | 1; }

static inline bool ot_is_int(ot_value v) { return (v & 1) != 0; }

static inline ot_value ot_bool(bool b) { return b ? OT_TRUE : OT_FALSE; }

#if !defined(OTIUM_PORTABLE_INTS) && INTPTR_MIN == (-INTPTR_MAX - 1) && (-1 >> 1) == -1
#define OTIUM_FAST_INT_REPR 1

/* The tagged word reinterpreted as a signed integer. */
static inline intptr_t ot_int_bits(ot_value v) {
  intptr_t bits;
  _Static_assert(sizeof(bits) == sizeof(v), "matching integer widths");
  memcpy(&bits, &v, sizeof(bits));
  return bits;
}
#else
#define OTIUM_FAST_INT_REPR 0
#endif

static inline intptr_t ot_to_int(ot_value v) {
#if OTIUM_FAST_INT_REPR
  return ot_int_bits(v) >> 1;
#else
  return v & ((uintptr_t)1 << (sizeof(v) * 8 - 1)) ? -(intptr_t)((~v >> 1) + 1)
                                                   : (intptr_t)(v >> 1);
#endif
}

/* The UTF-8 bytes of a string value, or NULL when the value is not a string.
 * The pointer is invalidated by the next call into the runtime. */
const char* ot_string_data(ot_value v, size_t* length);

/* Creation and configuration. Heaps are fixed until a larger per-semispace
 * limit is explicitly supplied; growth only happens after collection cannot
 * satisfy an allocation. */
ot_state* ot_create(size_t semispace_bytes, size_t stack_limit);
void ot_destroy(ot_state* rt);
bool ot_set_heap_limit(ot_state* rt, size_t semispace_bytes);
/* Limit how many VM frames can be live at once. There is no limit by default
 * beyond the stack limit. Zero, or fewer frames than are live, is refused. */
bool ot_set_frame_limit(ot_state* rt, size_t frames);
/* Copy host script metadata. NULL script means no script; strings are UTF-8. */
bool ot_set_arguments(ot_state* rt, const char* script, size_t count, const char* const* arguments);

/* Loading. The expander must be loaded before the core library. */
bool ot_load_expander(ot_state* rt, const char* directory);
bool ot_load_core(ot_state* rt, const char* directory);
/* Append a module search directory before the default . and lib directories. */
bool ot_add_load_path(ot_state* rt, const char* path);
/* Add the module directories listed by (paths ...) forms in a project file.
 * Relative entries resolve against the project file's directory. */
bool ot_add_project_paths(ot_state* rt, const char* project_file);

/* Evaluation. The result is retained until the next evaluation. Definitions
 * survive calls. Errors return false and leave the runtime ready for another
 * evaluation. */
bool ot_eval(ot_state* rt, const char* source, size_t length, ot_value* result);
/* Invoke an existing global in the current namespace, or a qualified name,
 * without parsing or compiling a wrapper. Arguments must be in caller-owned
 * storage; the runtime roots them before allocation. */
bool ot_call(ot_state* rt, const char* name, const ot_value* args, size_t count, ot_value* result);
const char* ot_error(const ot_state* rt);
size_t ot_error_offset(const ot_state* rt);
/* Write the pending error, after eval or call returned false: rendered by
 * otium.diagnostics with its source span, frames and notes, else as plain
 * text. path names the text given to ot_eval, for spans in it. The error
 * stays pending. False, writing nothing, when no error is pending or the
 * failure was an interrupt or exit. */
bool ot_report_error(ot_state* rt, const char* path, FILE* out);
/* An exit request returns false from eval/call; the host decides when to exit.
 * A subsequent top-level evaluation clears the request. */
bool ot_exit_status(const ot_state* rt, int* status);

/* Find the next complete form in source starting at start, without
 * evaluating it. On OT_FORM_COMPLETE, *end is the offset just past the form.
 * On OT_FORM_ERROR, *error and *error_offset describe the problem. */
typedef enum { OT_FORM_COMPLETE, OT_FORM_END, OT_FORM_INCOMPLETE, OT_FORM_ERROR } ot_form_status;

ot_form_status ot_scan_form(const char* source, size_t length, size_t start, size_t* end,
                            const char** error, size_t* error_offset);

/* Interrupts. The host owns the flag and keeps it alive until detached (NULL)
 * or destruction. A signal handler may set it to 1; evaluation consumes it at
 * safe points. */
void ot_set_interrupt_flag(ot_state* rt, volatile sig_atomic_t* flag);
bool ot_poll_interrupt(ot_state* rt);
/* Call the named global, with the flag's value, when the VM finds the flag set
 * between instructions or during a blocking read, instead of interrupting. It
 * runs on top of the interrupted code, and returning #t interrupts that code;
 * any other result resumes it. NULL removes the handler. The name resolves in
 * the current namespace when this is called; redefining the global changes
 * the handler. */
bool ot_set_interrupt_handler(ot_state* rt, const char* name);
/* Call the named global with a namespace symbol and a path before a module
 * file is loaded. An error from it stops the load. NULL removes the hook. */
bool ot_set_load_hook(ot_state* rt, const char* name);
bool ot_interrupted(const ot_state* rt);

/* Host IO. Host functions return errno values (zero on success); read/write
 * return a byte count, zero for read EOF, or a negative errno. Writes must make
 * progress. Callbacks borrow managed buffers: they must not reenter or collect
 * the runtime. The table is copied. Userdata must outlive every port opened
 * with this host. */
enum {
  OT_IO_READ = 1,
  OT_IO_WRITE = 2,
  OT_IO_CREATE = 4,
  OT_IO_TRUNCATE = 8,
  OT_IO_APPEND = 16,
  OT_IO_EXCLUSIVE = 32
};

typedef struct {
  int (*open)(void* userdata, const char* path, unsigned flags, intptr_t* handle);
  long (*read)(void* userdata, intptr_t handle, uint8_t* bytes, size_t length);
  long (*write)(void* userdata, intptr_t handle, const uint8_t* bytes, size_t length);
  int (*close)(void* userdata, intptr_t handle);
  int (*flush)(void* userdata, intptr_t handle); /* Optional. */
} ot_io_host;

/* Affects subsequently opened ports. NULL restores POSIX defaults. Standard
 * ports use handles 0/1/2 and are borrowed; install before loading the core to
 * replace them. Existing ports retain their host, userdata and handle.
 *
 * A runtime built with OTIUM_FREESTANDING, for targets without an operating
 * system, has no POSIX defaults, foreign calls or filesystem procedures. Its
 * host must install an I/O host before loading, and the runtime also reads
 * library, module and project files through the host's open, read and close. */
bool ot_set_io_host(ot_state* rt, const ot_io_host* host, void* userdata);
/* Flush a snapshot of registered output ports at an idle host boundary.
 * Success preserves the preceding evaluation/exit result. A flush failure
 * replaces it and can be inspected or retried. Destruction never calls Otium. */
bool ot_flush_ports(ot_state* rt);

/* Printing. ot_write uses read syntax; ot_print shows strings without quotes. */
void ot_write(ot_state* rt, FILE* out, ot_value value);
void ot_print(ot_state* rt, FILE* out, ot_value value);

/* Host procedures. ot_define_natives binds C functions as procedures in a
 * namespace, creating it if needed; names are copied. A later require of the
 * namespace still loads its source file when one is found, and otherwise
 * succeeds with just these bindings. The runtime checks the argument count
 * against min_args and max_args (OT_ANY_ARGS for no limit) before calling.
 *
 * args point at rooted runtime slots: after an allocation, read them again
 * rather than keeping copies. A native returns a value, or 0 after recording
 * an error with ot_native_error, which Otium code can catch. Natives must not
 * call ot_eval, ot_call or the loading functions. */
#define OT_ANY_ARGS SIZE_MAX

typedef ot_value (*ot_native_fn)(ot_state* rt, void* userdata, const ot_value* args, size_t count);

typedef struct {
  const char* name;
  ot_native_fn function;
  size_t min_args, max_args;
  void* userdata;
} ot_native;

bool ot_define_natives(ot_state* rt, const char* space, const ot_native* natives, size_t count);
/* Record an error for the running native. Always returns 0. */
ot_value ot_native_error(ot_state* rt, const char* format, ...);

/* Values for natives. Constructors return 0 after recording an error when
 * memory runs out. ot_make_list roots its items while it allocates. */
ot_value ot_make_string(ot_state* rt, const char* bytes, size_t length);
ot_value ot_make_float(ot_state* rt, double number);
ot_value ot_make_list(ot_state* rt, const ot_value* items, size_t count);
/* An integer or float as a double; false for anything else. */
bool ot_number_value(ot_value v, double* out);

/* Host objects carry size bytes of host data, copied in and 8-byte aligned,
 * tagged with a type the host owns. foreign? is true of them, and they print
 * as #<name>. The data pointer is valid until the next allocation. */
typedef struct {
  const char* name;
} ot_host_type;

ot_value ot_make_host_object(ot_state* rt, const ot_host_type* type, const void* data, size_t size);
/* The object's data, or NULL when v is not a host object of this type. */
void* ot_host_object_data(ot_value v, const ot_host_type* type);

/* Diagnostics. The heap fields describe the copying collector: capacity and
 * limit are per semispace, copied bytes are what collections moved, and
 * reclaimed bytes are what they freed. Foreign record counts include cached
 * signatures and retained library handles. allocations/bytes describe
 * explicitly owned native buffers, not process RSS. */
typedef struct {
  size_t collections, max_frames, heap_capacity, heap_limit;
  size_t heap_allocations, heap_allocated_bytes, heap_copied_bytes, heap_reclaimed_bytes;
  size_t heap_used, heap_peak_used;
  size_t libraries, bindings, signatures, memory_records, allocations, bytes;
  size_t callbacks, callback_records;
} ot_stats;

ot_stats ot_get_stats(const ot_state* rt);
/* Collect before every allocation. Slow; for testing rooting. */
void ot_gc_stress(ot_state* rt, bool enabled);

#endif
