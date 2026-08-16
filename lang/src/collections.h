// collections.h — core collection operations: the compact dict, array and
// buffer mutators, structural equality and hashing.
//
// The allocating mutators take Ref for the collection and for every
// heap-valued argument: growth can collect, and a raw Value argument would be
// the caller's stale copy by the time it is stored. Immediates carry no heap
// pointer, so the _im/_iv/_ii variants accept them raw and reject heap values
// at runtime.
//
// The read-side functions are allocation-free and take raw Values, so they
// are for files with direct heap access only; everyone else reaches these
// structures through slots.h, which cannot go stale.
#pragma once
#include "state.h"

// --- reads (allocation-free) ------------------------------------------------

Value table_get(State* vm, Value table, Value key);  // nil on miss
u32 table_entry_count(Value table);                  // live entries
// Advance through insertion-order storage, skipping tombstones; *cursor
// starts at 0 and is a storage position, so a traversal examines every stored
// entry at most once.
bool table_iter_next(Value table, u32* cursor, Value* k, Value* v);
Value array_get(Value arr, i64 idx);  // nil out of range

// Structural hash with equal? semantics: mixes the type tag, normalizes -0.0,
// hashes NaN to a constant, immutables structurally, mutables via
// heap_identity_of (stable across GC).
u64 val_hash(State* vm, Value v);
// Deep equal? semantics; val_eq in value.h is the identity operation.
bool val_equal(State* vm, Value a, Value b);

// Pair-as-table-key support: keys freeze (mutation refused) so their hashes
// stay stable; reachability backs the set-car!/set-cdr! cycle check.
bool pair_key_frozen(Value pair);
bool pair_contains(Value root, Value needle);  // needle reachable in root's pair graph?
void freeze_pair_key(Value root);

// --- allocating mutators (handles only) --------------------------------------

void table_put(State* vm, Ref table, Ref key, Ref v);  // nil value deletes
void table_put_iv(State* vm, Ref table, Value immKey, Ref v);
void table_put_ii(State* vm, Ref table, Value immKey, Value immVal);
void array_push(State* vm, Ref arr, Ref v);
void array_push_im(State* vm, Ref arr, Value imm);
void array_reserve(State* vm, Ref arr, u32 n);
// `src` must not point into the GC heap: growth would move it underneath us.
void buffer_append(State* vm, Ref buffer, const char* src, u32 n);
