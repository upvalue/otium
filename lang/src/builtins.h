// builtins.h — native core library: registration entry points + shared helpers.
#pragma once
#include "state.h"

// Registration: each defines its natives into otium.core via ns_define.
void register_arith(State* vm);
void register_cond(State* vm);
void register_data(State* vm);
void register_expand(State* vm);
void register_string(State* vm);
void register_sys(State* vm);
void register_time(State* vm);

// Wrap a NativeFn in a Function object and ns_define it (implemented in sys.c).
void def_native(State* vm, const char* name, NativeFn f);

// Symbol/keyword id; strings are interned; unsupported values return 0.
static inline u32 name_id_of(State* vm, Value v) {
  if (v.tag == Tag_Symbol || v.tag == Tag_Keyword) return v.id;
  if (v.tag == Tag_String) {
    StringData* string = as_string(v);
    return intern_id(&vm->intern, string_data_bytes(string), string->len);
  }
  return 0;
}

// Argument accessor for natives: reads the rooted stack slot at use time.
// Use ARG(n) (or a Slot) at the point of use — never copy it
// into a local that lives across an allocating call; the semispace collector
// moves everything. See state.h (Slot/scope_begin).
#define ARG(n) (vm->stack.data[base + (n)])

// Shared native validation. These helpers return nil on success and an unwind
// condition on failure, so callers use them through OT_TRY.
static inline Value need_argc(State* vm, const char* who, u32 argc, u32 min, u32 max) {
  if (argc < min || (max != UINT32_MAX && argc > max))
    return raise_error(vm, "%s: wrong number of arguments (%u)", who, argc);
  return nil_v();
}

static inline Value need_tag(State* vm, const char* who, Value v, Tag tag, const char* expected) {
  if (v.tag != tag) return raise_error(vm, "%s: expected %s", who, expected);
  return nil_v();
}

static inline Value need_string(State* vm, const char* who, Value v) {
  return need_tag(vm, who, v, Tag_String, "string");
}
static inline Value need_pair(State* vm, const char* who, Value v) {
  return need_tag(vm, who, v, Tag_Pair, "pair");
}
static inline Value need_array(State* vm, const char* who, Value v) {
  return need_tag(vm, who, v, Tag_Array, "array");
}
static inline Value need_buffer(State* vm, const char* who, Value v) {
  return need_tag(vm, who, v, Tag_Buffer, "buffer");
}
static inline Value need_symbol(State* vm, const char* who, Value v) {
  return need_tag(vm, who, v, Tag_Symbol, "symbol");
}
static inline Value need_restart(State* vm, const char* who, Value v) {
  return need_tag(vm, who, v, Tag_Restart, "restart");
}
static inline Value need_int(State* vm, const char* who, Value v) {
  return need_tag(vm, who, v, Tag_Int, "int");
}

static inline bool is_num(Value v) { return v.tag == Tag_Int || v.tag == Tag_Float; }
static inline Value need_nums(State* vm, const char* who, u32 base, u32 argc) {
  for (u32 i = 0; i < argc; i++)
    if (!is_num(ARG(i))) return raise_error(vm, "%s: expected number", who);
  return nil_v();
}
static inline Value need_num_args(State* vm, const char* who, u32 base, u32 argc, u32 min,
                                  u32 max) {
  OT_TRY(need_argc(vm, who, argc, min, max));
  return need_nums(vm, who, base, argc);
}
static inline Value need_strings(State* vm, const char* who, u32 base, u32 argc) {
  for (u32 i = 0; i < argc; i++) OT_TRY(need_string(vm, who, ARG(i)));
  return nil_v();
}

// Structural hash with equal? semantics (data.c): mixes the type tag
// (equal? is type-strict), normalizes -0.0 to 0.0, hashes NaN to a constant,
// immutables structurally, mutables via heap_identity_of (stable across GC).
u64 val_hash(State* vm, Value v);

// Deep equal? semantics; val_eq is the inline identity operation in value.h.
bool val_equal(State* vm, Value a, Value b);

// Table/array API declared in heap.h and implemented by the builtins.
u32 table_entry_count(Value table);                                  // live entries
bool table_iter_next(Value table, u32* cursor, Value* k, Value* v);  // cursor starts at 0

// Exposed for tests (normally reached through otium.core).
Value nat_add(State* vm, u32 base, u32 argc);
Value nat_sub(State* vm, u32 base, u32 argc);
Value nat_mul(State* vm, u32 base, u32 argc);
Value nat_div(State* vm, u32 base, u32 argc);
