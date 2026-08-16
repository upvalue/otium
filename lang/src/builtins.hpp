// builtins.hpp — native core library: registration entry points + shared helpers.
#pragma once
#include "common.hpp"
#include "value.hpp"
#include "vec.hpp"

namespace ot {

struct Vm;
struct Obj;

// Native calling convention (must match vm.hpp).
using NativeFn = Value (*)(Vm& vm, u32 base, u32 argc);

// Registration: each defines its natives into otium.core via ns_define.
void register_arith(Vm&);
void register_data(Vm&);
void register_string(Vm&);
void register_sys(Vm&);

// Wrap a NativeFn in a Function object and ns_define it (implemented in sys.cpp).
void def_native(Vm& vm, const char* name, NativeFn f);

// Argument accessor for natives: reads the rooted stack slot at use time.
// GC DISCIPLINE: use ARG(n) (or a Slot) at the point of use — never copy it
// into a local that lives across an allocating call; the semispace collector
// moves everything. See vm.hpp (Slot/Scope) and lan-6mpt.
#define ARG(n) vm.stack[base + (n)]

// Structural hash with equal? semantics (data.cpp): mixes the type tag
// (equal? is type-strict), normalizes -0.0 to 0.0, hashes NaN to a constant,
// immutables structurally, mutables via heap.identityOf (stable across GC).
u64 val_hash(Vm& vm, Value v);

// val_equal is declared in value.hpp; defined in data.cpp.
// val_eq is inline in value.hpp.

// ---------------------------------------------------------------------------
// Compact dict internals (owned by builtins/data.cpp).
// INTEGRATION: heap.hpp declares `struct TableData` — its layout must be this
// one (or heap.hpp should #include this header for it). The GC must trace
// `entries[i].key/.val` for non-tombstone entries and free `entries`/`index`
// (C heap) when a table dies. Tombstones are marked key.tag == Tag::Unwind.
#ifndef OT_TABLEDATA_DEFINED
#define OT_TABLEDATA_DEFINED
struct TableEntry {
  u64 hash;
  Value key;
  Value val;
};
struct TableData {
  u32 count;       // live entries (contract: first field)
  u32 tombstones;  // dead entries still in `entries`
  TableEntry* entries;
  u32 entriesLen;
  u32 entriesCap;  // insertion order
  u8* index;       // open-addressed slot array, scaled width; 0 = empty,
  u32 indexCap;    // else entryIndex+1. Power-of-two capacity.
  u32 indexWidth;  // bytes per slot: 1, 2, or 4
};
#endif

// Table/array API from the contract (declared in heap.hpp, implemented here).
Value table_get(Vm&, Value table, Value key);                 // nil on miss
Value table_put(Vm&, Value table, Value key, Value v);        // nil deletes; returns table
u32 table_entry_count(Value table);                           // live entries
bool table_entry_at(Value table, u32 i, Value* k, Value* v);  // i-th LIVE entry in order
Value array_get(Value arr, i64 idx);                          // nil out of range
void array_push(Vm&, Value arr, Value v);

// Exposed for tests (normally reached through otium.core).
Value nat_add(Vm&, u32 base, u32 argc);
Value nat_sub(Vm&, u32 base, u32 argc);
Value nat_mul(Vm&, u32 base, u32 argc);
Value nat_div(Vm&, u32 base, u32 argc);

}  // namespace ot
