#ifndef OTIUM_PROTOTYPE_RUNTIME_H
#define OTIUM_PROTOTYPE_RUNTIME_H

#include "heap.h"
#include <stdio.h>
#include <signal.h>

typedef struct Runtime Runtime;

/* Host functions return errno values (zero on success); read/write return a
 * byte count, zero for read EOF, or a negative errno. Writes must make progress.
 * Callbacks borrow managed buffers: they must not reenter or collect the runtime.
 * The table is copied. Userdata must outlive every port opened with this host. */
enum {
  RUNTIME_IO_READ = 1,
  RUNTIME_IO_WRITE = 2,
  RUNTIME_IO_CREATE = 4,
  RUNTIME_IO_TRUNCATE = 8,
  RUNTIME_IO_APPEND = 16,
  RUNTIME_IO_EXCLUSIVE = 32
};

typedef struct {
  int (*open)(void* userdata, const char* path, unsigned flags, intptr_t* handle);
  long (*read)(void* userdata, intptr_t handle, uint8_t* bytes, size_t length);
  long (*write)(void* userdata, intptr_t handle, const uint8_t* bytes, size_t length);
  int (*close)(void* userdata, intptr_t handle);
  int (*flush)(void* userdata, intptr_t handle); /* Optional. */
} RuntimeIOHost;

/* Affects subsequently opened ports. NULL restores POSIX defaults. Standard
 * ports use handles 0/1/2 and are borrowed; install before loading the core to
 * replace them. Existing ports retain their host, userdata and handle. */
bool runtime_set_io_host(Runtime* rt, const RuntimeIOHost* host, void* userdata);
Runtime* runtime_create(size_t semispace_bytes, size_t stack_limit);
void runtime_destroy(Runtime* rt);
/* Heaps are fixed until a larger per-semispace limit is explicitly supplied.
 * Growth occurs only after collection cannot satisfy an allocation. */
bool runtime_heap_limit(Runtime* rt, size_t semispace_bytes);
size_t runtime_heap_capacity(const Runtime* rt);
/* Copy host script metadata. NULL script means no script; strings are UTF-8. */
bool runtime_set_arguments(Runtime* rt, const char* script, size_t count,
                           const char* const* arguments);
/* Flush a snapshot of registered output ports at an idle host boundary.
 * Success preserves the preceding evaluation/exit result. A flush failure
 * replaces it and can be inspected or retried. Destruction never calls Otium. */
bool runtime_flush_ports(Runtime* rt);
/* The host owns the flag and keeps it alive until detached (NULL) or destruction.
 * A signal handler may set it to 1; evaluation consumes it at safe points. */
void runtime_interrupt_flag(Runtime* rt, volatile sig_atomic_t* flag);
bool runtime_poll_interrupt(Runtime* rt);
bool runtime_interrupted(const Runtime* rt);
/* Load the shared Otium expander from a library directory. */
bool runtime_load_expander(Runtime* rt, const char* directory);
bool runtime_load_core(Runtime* rt, const char* directory);
/* Append a module search directory before the default . and lib directories. */
bool runtime_load_path(Runtime* rt, const char* path);
/* The result is retained until the next evaluation. Definitions survive calls.
 * Errors return false and leave the runtime ready for another evaluation. */
bool runtime_eval(Runtime* rt, const char* source, size_t length, Value* result);
/* Invoke an existing global in the current namespace, or a qualified name,
 * without parsing or compiling a wrapper. Arguments
 * must be in caller-owned storage; the runtime roots them before allocation. */
bool runtime_call(Runtime* rt, const char* name, const Value* args, size_t count, Value* result);
/* An exit request returns false from eval/call; the host decides when to exit.
 * A subsequent top-level evaluation clears the request. */
bool runtime_exit_status(const Runtime* rt, int* status);
const char* runtime_error(const Runtime* rt);
size_t runtime_error_offset(const Runtime* rt);
size_t runtime_collections(const Runtime* rt);
size_t runtime_max_frames(const Runtime* rt);
void runtime_gc_stress(Runtime* rt, bool enabled);

/* Record counts include cached signatures and retained library handles.
 * allocations/bytes describe explicitly owned native buffers, not process RSS. */
typedef struct {
  size_t libraries, bindings, signatures, memory_records, allocations, bytes;
  size_t callbacks, callback_records;
} RuntimeForeignStats;

RuntimeForeignStats runtime_foreign_stats(const Runtime* rt);
size_t runtime_foreign_signatures(const Runtime* rt);
void runtime_write(Runtime* rt, FILE* out, Value value);
void runtime_print(Runtime* rt, FILE* out, Value value);

#endif
