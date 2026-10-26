#ifndef OTIUM_PROTOTYPE_RUNTIME_H
#define OTIUM_PROTOTYPE_RUNTIME_H

#include "heap.h"
#include <stdio.h>
#include <signal.h>

typedef struct ot_runtime ot_runtime;

/* Host functions return errno values (zero on success); read/write return a
 * byte count, zero for read EOF, or a negative errno. Writes must make progress.
 * Callbacks borrow managed buffers: they must not reenter or collect the runtime.
 * The table is copied. Userdata must outlive every port opened with this host. */
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
 * replace them. Existing ports retain their host, userdata and handle. */
bool ot_set_io_host(ot_runtime* rt, const ot_io_host* host, void* userdata);
ot_runtime* ot_create(size_t semispace_bytes, size_t stack_limit);
void ot_destroy(ot_runtime* rt);
/* Heaps are fixed until a larger per-semispace limit is explicitly supplied.
 * Growth occurs only after collection cannot satisfy an allocation. */
bool ot_set_heap_limit(ot_runtime* rt, size_t semispace_bytes);
size_t ot_heap_capacity(const ot_runtime* rt);
/* Copy host script metadata. NULL script means no script; strings are UTF-8. */
bool ot_set_arguments(ot_runtime* rt, const char* script, size_t count,
                           const char* const* arguments);
/* Flush a snapshot of registered output ports at an idle host boundary.
 * Success preserves the preceding evaluation/exit result. A flush failure
 * replaces it and can be inspected or retried. Destruction never calls Otium. */
bool ot_flush_ports(ot_runtime* rt);
/* The host owns the flag and keeps it alive until detached (NULL) or destruction.
 * A signal handler may set it to 1; evaluation consumes it at safe points. */
void ot_set_interrupt_flag(ot_runtime* rt, volatile sig_atomic_t* flag);
bool ot_poll_interrupt(ot_runtime* rt);
bool ot_interrupted(const ot_runtime* rt);
/* Load the shared Otium expander from a library directory. */
bool ot_load_expander(ot_runtime* rt, const char* directory);
bool ot_load_core(ot_runtime* rt, const char* directory);
/* Append a module search directory before the default . and lib directories. */
bool ot_add_load_path(ot_runtime* rt, const char* path);
/* The result is retained until the next evaluation. Definitions survive calls.
 * Errors return false and leave the runtime ready for another evaluation. */
bool ot_eval(ot_runtime* rt, const char* source, size_t length, ot_value* result);
/* Invoke an existing global in the current namespace, or a qualified name,
 * without parsing or compiling a wrapper. Arguments
 * must be in caller-owned storage; the runtime roots them before allocation. */
bool ot_call(ot_runtime* rt, const char* name, const ot_value* args, size_t count, ot_value* result);
/* An exit request returns false from eval/call; the host decides when to exit.
 * A subsequent top-level evaluation clears the request. */
bool ot_exit_status(const ot_runtime* rt, int* status);
const char* ot_error(const ot_runtime* rt);
size_t ot_error_offset(const ot_runtime* rt);
size_t ot_collections(const ot_runtime* rt);
size_t ot_max_frames(const ot_runtime* rt);
void ot_gc_stress(ot_runtime* rt, bool enabled);

/* Record counts include cached signatures and retained library handles.
 * allocations/bytes describe explicitly owned native buffers, not process RSS. */
typedef struct {
  size_t libraries, bindings, signatures, memory_records, allocations, bytes;
  size_t callbacks, callback_records;
} ot_foreign_stats;

ot_foreign_stats ot_get_foreign_stats(const ot_runtime* rt);
size_t ot_foreign_signatures(const ot_runtime* rt);
void ot_write(ot_runtime* rt, FILE* out, ot_value value);
void ot_print(ot_runtime* rt, FILE* out, ot_value value);

#endif
