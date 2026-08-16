// builtins.h — native core library: registration entry points + shared
// helpers. Builtins are written against slots.h alone: arguments arrive as
// rooted slots (ARG), results leave as immediates or via ot_ret.
#pragma once
#include "slots.h"

// Registration: each defines its natives into otium.core via ot_def_native.
void register_arith(State* vm);
void register_cond(State* vm);
void register_data(State* vm);
void register_expand(State* vm);
void register_string(State* vm);
void register_sys(State* vm);
void register_time(State* vm);

// Argument accessor for natives: the n-th argument's rooted stack slot.
#define ARG(n) ((Ref){base + (n)})

// Shared native validation. These helpers return nil on success and an unwind
// condition on failure, so callers use them through OT_TRY.
static inline Value need_argc(State* vm, const char* who, u32 argc, u32 min, u32 max) {
  if (argc < min || (max != UINT32_MAX && argc > max))
    return raise_error(vm, "%s: wrong number of arguments (%u)", who, argc);
  return nil_v();
}

static inline Value need_tag(State* vm, const char* who, Ref r, Tag tag, const char* expected) {
  if (ot_tag(vm, r) != tag) return raise_error(vm, "%s: expected %s", who, expected);
  return nil_v();
}

static inline Value need_string(State* vm, const char* who, Ref r) {
  return need_tag(vm, who, r, Tag_String, "string");
}
static inline Value need_pair(State* vm, const char* who, Ref r) {
  return need_tag(vm, who, r, Tag_Pair, "pair");
}
static inline Value need_array(State* vm, const char* who, Ref r) {
  return need_tag(vm, who, r, Tag_Array, "array");
}
static inline Value need_buffer(State* vm, const char* who, Ref r) {
  return need_tag(vm, who, r, Tag_Buffer, "buffer");
}
static inline Value need_symbol(State* vm, const char* who, Ref r) {
  return need_tag(vm, who, r, Tag_Symbol, "symbol");
}
static inline Value need_restart(State* vm, const char* who, Ref r) {
  return need_tag(vm, who, r, Tag_Restart, "restart");
}
static inline Value need_int(State* vm, const char* who, Ref r) {
  return need_tag(vm, who, r, Tag_Int, "int");
}

static inline bool num_tag(Tag t) { return t == Tag_Int || t == Tag_Float; }
static inline Value need_nums(State* vm, const char* who, u32 base, u32 argc) {
  for (u32 i = 0; i < argc; i++)
    if (!num_tag(ot_tag(vm, ARG(i)))) return raise_error(vm, "%s: expected number", who);
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

// Exposed for tests (normally reached through otium.core).
Value nat_add(State* vm, u32 base, u32 argc);
Value nat_sub(State* vm, u32 base, u32 argc);
Value nat_mul(State* vm, u32 base, u32 argc);
Value nat_div(State* vm, u32 base, u32 argc);
