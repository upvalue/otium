// A dependency-free example of the extension API. This file is linked only
// into the executable when -Dext_demo=true; libotium contains no demo code.
// Extensions are written against slots.h (via builtins.h) alone.
#include "demo_ext.h"
#include "builtins.h"
#include <stdlib.h>

static void free_counter(State* vm, void* payload) {
  (void)vm;
  free(payload);
}

static u32 inline_counter_type(State* vm) {
  return ot_register_foreign_type(vm, "demo/inline-counter", nullptr);
}

static u32 owned_counter_type(State* vm) {
  return ot_register_foreign_type(vm, "demo/owned-counter", free_counter);
}

static Value make_counter(State* vm, u32 base, u32 argc, bool owned) {
  const char* who = owned ? "make-owned-counter" : "make-inline-counter";
  OT_TRY(need_argc(vm, who, argc, 1, 1));
  OT_TRY(need_int(vm, who, ARG(0)));
  i64 value = ot_int(vm, ARG(0));
  OT_SCOPE(vm);
  Ref out = ot_push(vm);
  if (!owned) {
    ot_make_foreign_inline(vm, out, inline_counter_type(vm), &value, sizeof value);
    return ot_ret(vm, out);
  }
  i64* payload = (i64*)malloc(sizeof *payload);
  if (!payload) ot_fatal("demo: out of memory");
  *payload = value;
  ot_make_foreign_pointer(vm, out, owned_counter_type(vm), payload);
  return ot_ret(vm, out);
}

static Value nat_make_inline_counter(State* vm, u32 base, u32 argc) {
  return make_counter(vm, base, argc, false);
}

static Value nat_make_owned_counter(State* vm, u32 base, u32 argc) {
  return make_counter(vm, base, argc, true);
}

// The payload pointer is into the GC heap for inline counters: use it before
// the next allocating call (the callers below only read or write the i64).
static Value counter_payload(State* vm, const char* who, Ref value, i64** out) {
  u32 typeId = ot_foreign_type_id(vm, value);
  if (typeId == 0 || (typeId != inline_counter_type(vm) && typeId != owned_counter_type(vm)))
    return raise_error(vm, "%s: expected demo counter", who);
  void* payload = nullptr;
  OT_TRY(ot_foreign_check(vm, who, value, typeId, &payload));
  *out = (i64*)payload;
  return nil_v();
}

static Value nat_counter_value(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "counter-value", argc, 1, 1));
  i64* payload = nullptr;
  OT_TRY(counter_payload(vm, "counter-value", ARG(0), &payload));
  return int_v(*payload);
}

static Value nat_counter_inc(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "counter-inc!", argc, 1, 1));
  i64* payload = nullptr;
  OT_TRY(counter_payload(vm, "counter-inc!", ARG(0), &payload));
  *payload = (i64)((u64)*payload + 1u);
  return ot_ret(vm, ARG(0));
}

static Value nat_release_counter(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "release-counter!", argc, 1, 1));
  u32 typeId = ot_foreign_type_id(vm, ARG(0));
  if (typeId == 0 || (typeId != inline_counter_type(vm) && typeId != owned_counter_type(vm)))
    return raise_error(vm, "release-counter!: expected demo counter");
  return ot_foreign_release(vm, "release-counter!", ARG(0), typeId);
}

static void init_demo(State* vm) {
  (void)inline_counter_type(vm);
  (void)owned_counter_type(vm);
  ot_def_native(vm, "make-inline-counter", nat_make_inline_counter);
  ot_def_native(vm, "make-owned-counter", nat_make_owned_counter);
  ot_def_native(vm, "counter-value", nat_counter_value);
  ot_def_native(vm, "counter-inc!", nat_counter_inc);
  ot_def_native(vm, "release-counter!", nat_release_counter);
}

void register_demo_extension(State* vm) { ot_register_native_module(vm, "demo", init_demo); }
