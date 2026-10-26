#ifndef OTIUM_PROTOTYPE_RUNTIME_H
#define OTIUM_PROTOTYPE_RUNTIME_H

#include "heap.h"
#include <stdio.h>

typedef struct Runtime Runtime;
Runtime *runtime_create(size_t semispace_bytes, size_t stack_limit);
void runtime_destroy(Runtime *rt);
/* Append a module search directory before the default . and lib directories. */
bool runtime_load_path(Runtime *rt, const char *path);
/* The result is retained until the next evaluation. Definitions survive calls.
 * Errors return false and leave the runtime ready for another evaluation. */
bool runtime_eval(Runtime *rt, const char *source, size_t length, Value *result);
/* Invoke an existing global in the current namespace, or a qualified name,
 * without parsing or compiling a wrapper. Arguments
 * must be in caller-owned storage; the runtime roots them before allocation. */
bool runtime_call(Runtime *rt, const char *name, const Value *args, size_t count, Value *result);
const char *runtime_error(const Runtime *rt);
size_t runtime_error_offset(const Runtime *rt);
size_t runtime_collections(const Runtime *rt);
size_t runtime_max_frames(const Runtime *rt);
void runtime_gc_stress(Runtime *rt, bool enabled);
/* Zero disables native execution; a positive value is the bytecode heat
 * threshold for compiling a function with the host C compiler. */
void runtime_native(Runtime *rt, size_t threshold);
size_t runtime_native_functions(const Runtime *rt);
size_t runtime_native_entries(const Runtime *rt);
/* Maximum nested direct self calls; deeper calls use the VM dispatch loop. */
size_t runtime_max_native_depth(const Runtime *rt);
/* Private-frame attempts and replays after an in-body guard fails. */
size_t runtime_native_scalar_entries(const Runtime *rt);
size_t runtime_native_scalar_fallbacks(const Runtime *rt);
double runtime_native_compile_seconds(const Runtime *rt);
/* Record counts include cached signatures and retained library handles.
 * allocations/bytes describe explicitly owned native buffers, not process RSS. */
typedef struct {
    size_t libraries, bindings, signatures, memory_records, allocations, bytes;
} RuntimeForeignStats;
RuntimeForeignStats runtime_foreign_stats(const Runtime *rt);
size_t runtime_foreign_signatures(const Runtime *rt);
double runtime_foreign_compile_seconds(const Runtime *rt);
void runtime_print(Runtime *rt, FILE *out, Value value);

#endif
