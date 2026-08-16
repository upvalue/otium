// builtins.hpp — native core library: registration entry points + shared helpers.
#pragma once
#include "vm.hpp"

namespace ot {

// Registration: each defines its natives into otium.core via ns_define.
void register_arith(Vm&);
void register_cond(Vm&);
void register_data(Vm&);
void register_expand(Vm&);
void register_string(Vm&);
void register_sys(Vm&);
void register_time(Vm&);

// Wrap a NativeFn in a Function object and ns_define it (implemented in sys.cpp).
void def_native(Vm& vm, const char* name, NativeFn f);

// Argument accessor for natives: reads the rooted stack slot at use time.
// Use ARG(n) (or a Slot) at the point of use — never copy it
// into a local that lives across an allocating call; the semispace collector
// moves everything. See vm.hpp (Slot/Scope).
#define ARG(n) vm.stack[base + (n)]

// Shared native validation. These helpers return nil on success and an unwind
// condition on failure, so callers use them through OT_TRY.
inline Value need_argc(Vm& vm, const char* who, u32 argc, u32 min, u32 max) {
  if (argc < min || (max != UINT32_MAX && argc > max))
    return raise_error(vm, "%s: wrong number of arguments (%u)", who, argc);
  return nil_v();
}

inline Value need_tag(Vm& vm, const char* who, Value v, Tag tag, const char* expected) {
  if (v.tag != tag) return raise_error(vm, "%s: expected %s", who, expected);
  return nil_v();
}

inline Value need_string(Vm& vm, const char* who, Value v) {
  return need_tag(vm, who, v, Tag::String, "string");
}
inline Value need_pair(Vm& vm, const char* who, Value v) {
  return need_tag(vm, who, v, Tag::Pair, "pair");
}
inline Value need_array(Vm& vm, const char* who, Value v) {
  return need_tag(vm, who, v, Tag::Array, "array");
}
inline Value need_table(Vm& vm, const char* who, Value v) {
  return need_tag(vm, who, v, Tag::Table, "table");
}
inline Value need_buffer(Vm& vm, const char* who, Value v) {
  return need_tag(vm, who, v, Tag::Buffer, "buffer");
}
inline Value need_symbol(Vm& vm, const char* who, Value v) {
  return need_tag(vm, who, v, Tag::Symbol, "symbol");
}
inline Value need_restart(Vm& vm, const char* who, Value v) {
  return need_tag(vm, who, v, Tag::Restart, "restart");
}
inline Value need_int(Vm& vm, const char* who, Value v) {
  return need_tag(vm, who, v, Tag::Int, "int");
}

inline bool is_num(Value v) { return v.tag == Tag::Int || v.tag == Tag::Float; }
inline Value need_nums(Vm& vm, const char* who, u32 base, u32 argc) {
  for (u32 i = 0; i < argc; i++)
    if (!is_num(vm.stack[base + i])) return raise_error(vm, "%s: expected number", who);
  return nil_v();
}
inline Value need_strings(Vm& vm, const char* who, u32 base, u32 argc) {
  for (u32 i = 0; i < argc; i++) OT_TRY(need_string(vm, who, vm.stack[base + i]));
  return nil_v();
}

// Structural hash with equal? semantics (data.cpp): mixes the type tag
// (equal? is type-strict), normalizes -0.0 to 0.0, hashes NaN to a constant,
// immutables structurally, mutables via heap.identityOf (stable across GC).
u64 val_hash(Vm& vm, Value v);

// val_equal is declared in value.hpp; defined in data.cpp.
// val_eq is inline in value.hpp.

// Table/array API declared in heap.hpp and implemented by the builtins.
Value table_get(Vm&, Value table, Value key);                        // nil on miss
Value table_put(Vm&, Value table, Value key, Value v);               // nil deletes; returns table
u32 table_entry_count(Value table);                                  // live entries
bool table_iter_next(Value table, u32* cursor, Value* k, Value* v);  // cursor starts at 0
Value array_get(Value arr, i64 idx);                                 // nil out of range
void array_push(Vm&, Value arr, Value v);

// Exposed for tests (normally reached through otium.core).
Value nat_add(Vm&, u32 base, u32 argc);
Value nat_sub(Vm&, u32 base, u32 argc);
Value nat_mul(Vm&, u32 base, u32 argc);
Value nat_div(Vm&, u32 base, u32 argc);

}  // namespace ot
