// builtins.hpp — native core library: registration entry points + shared helpers.
#pragma once
#include "state.hpp"

namespace ot {

// Registration: each defines its natives into otium.core via ns_define.
void register_arith(State&);
void register_cond(State&);
void register_data(State&);
void register_expand(State&);
void register_string(State&);
void register_sys(State&);

// Wrap a NativeFn in a Function object and ns_define it (implemented in sys.cpp).
void def_native(State& vm, const char* name, NativeFn f);

// Argument accessor for natives: reads the rooted stack slot at use time.
// Use ARG(n) (or a Slot) at the point of use — never copy it
// into a local that lives across an allocating call; the semispace collector
// moves everything. See state.hpp (Slot/Scope).
#define ARG(n) vm.stack[base + (n)]

// Shared native validation. These helpers return nil on success and an unwind
// condition on failure, so callers use them through OT_TRY.
inline Value need_argc(State& vm, const char* who, u32 argc, u32 min, u32 max) {
  if (argc < min || (max != UINT32_MAX && argc > max))
    return raise_error(vm, "%s: wrong number of arguments (%u)", who, argc);
  return nil_v();
}

inline Value need_tag(State& vm, const char* who, Value v, Tag tag, const char* expected) {
  if (v.tag != tag) return raise_error(vm, "%s: expected %s", who, expected);
  return nil_v();
}

inline Value need_string(State& vm, const char* who, Value v) {
  return need_tag(vm, who, v, Tag::String, "string");
}
inline Value need_pair(State& vm, const char* who, Value v) {
  return need_tag(vm, who, v, Tag::Pair, "pair");
}
inline Value need_array(State& vm, const char* who, Value v) {
  return need_tag(vm, who, v, Tag::Array, "array");
}
inline Value need_table(State& vm, const char* who, Value v) {
  return need_tag(vm, who, v, Tag::Table, "table");
}
inline Value need_buffer(State& vm, const char* who, Value v) {
  return need_tag(vm, who, v, Tag::Buffer, "buffer");
}
inline Value need_symbol(State& vm, const char* who, Value v) {
  return need_tag(vm, who, v, Tag::Symbol, "symbol");
}
inline Value need_restart(State& vm, const char* who, Value v) {
  return need_tag(vm, who, v, Tag::Restart, "restart");
}
inline Value need_int(State& vm, const char* who, Value v) {
  return need_tag(vm, who, v, Tag::Int, "int");
}

inline bool is_num(Value v) { return v.tag == Tag::Int || v.tag == Tag::Float; }
inline Value need_nums(State& vm, const char* who, u32 base, u32 argc) {
  for (u32 i = 0; i < argc; i++)
    if (!is_num(vm.stack[base + i])) return raise_error(vm, "%s: expected number", who);
  return nil_v();
}
inline Value need_strings(State& vm, const char* who, u32 base, u32 argc) {
  for (u32 i = 0; i < argc; i++) OT_TRY(need_string(vm, who, vm.stack[base + i]));
  return nil_v();
}

// Structural hash with equal? semantics (data.cpp): mixes the type tag
// (equal? is type-strict), normalizes -0.0 to 0.0, hashes NaN to a constant,
// immutables structurally, mutables via heap.identityOf (stable across GC).
u64 val_hash(State& vm, Value v);

// val_equal is declared in value.hpp; defined in data.cpp.
// val_eq is inline in value.hpp.

// Table/array API declared in heap.hpp and implemented by the builtins.
Value table_get(State&, Value table, Value key);                        // nil on miss
Value table_put(State&, Value table, Value key, Value v);               // nil deletes; returns table
u32 table_entry_count(Value table);                                  // live entries
bool table_iter_next(Value table, u32* cursor, Value* k, Value* v);  // cursor starts at 0
Value array_get(Value arr, i64 idx);                                 // nil out of range
void array_push(State&, Value arr, Value v);

// Exposed for tests (normally reached through otium.core).
Value nat_add(State&, u32 base, u32 argc);
Value nat_sub(State&, u32 base, u32 argc);
Value nat_mul(State&, u32 base, u32 argc);
Value nat_div(State&, u32 base, u32 argc);

}  // namespace ot
