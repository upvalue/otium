// builtins.hpp — native core library: registration entry points + shared helpers.
#pragma once
#include "heap.hpp"

namespace ot {

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
