// A dependency-free example of the extension API. This file is linked only
// into the executable when -Dext_demo=true; libotium contains no demo code.
#include "demo_ext.hpp"
#include "builtins.hpp"
#include "heap.hpp"
#include "vm.hpp"

namespace ot {

static void free_counter(Vm&, void* payload) { free(payload); }

static u32 inline_counter_type(Vm& vm) { return register_foreign_type(vm, "demo/inline-counter"); }

static u32 owned_counter_type(Vm& vm) {
  return register_foreign_type(vm, "demo/owned-counter", free_counter);
}

static Value make_counter(Vm& vm, u32 base, u32 argc, bool owned) {
  const char* who = owned ? "make-owned-counter" : "make-inline-counter";
  OT_TRY(need_argc(vm, who, argc, 1, 1));
  OT_TRY(need_int(vm, who, ARG(0)));
  i64 value = ARG(0).i;
  if (!owned) return make_foreign_inline(vm, inline_counter_type(vm), &value, sizeof value);
  i64* payload = (i64*)malloc(sizeof *payload);
  if (!payload) ot_fatal("demo: out of memory");
  *payload = value;
  return make_foreign_pointer(vm, owned_counter_type(vm), payload);
}

static Value nat_make_inline_counter(Vm& vm, u32 base, u32 argc) {
  return make_counter(vm, base, argc, false);
}

static Value nat_make_owned_counter(Vm& vm, u32 base, u32 argc) {
  return make_counter(vm, base, argc, true);
}

static Value counter_payload(Vm& vm, const char* who, Value value, i64** out) {
  if (value.tag != Tag::Foreign) return raise_error(vm, "%s: expected demo counter", who);
  u32 typeId = as_foreign(value)->typeId;
  if (typeId != inline_counter_type(vm) && typeId != owned_counter_type(vm))
    return raise_error(vm, "%s: expected demo counter", who);
  void* payload = nullptr;
  OT_TRY(foreign_check(vm, who, value, typeId, &payload));
  *out = (i64*)payload;
  return nil_v();
}

static Value nat_counter_value(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "counter-value", argc, 1, 1));
  i64* payload = nullptr;
  OT_TRY(counter_payload(vm, "counter-value", ARG(0), &payload));
  return int_v(*payload);
}

static Value nat_counter_inc(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "counter-inc!", argc, 1, 1));
  i64* payload = nullptr;
  OT_TRY(counter_payload(vm, "counter-inc!", ARG(0), &payload));
  *payload = (i64)((u64)*payload + 1u);
  return ARG(0);
}

static Value nat_release_counter(Vm& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "release-counter!", argc, 1, 1));
  if (ARG(0).tag != Tag::Foreign) return raise_error(vm, "release-counter!: expected demo counter");
  u32 typeId = as_foreign(ARG(0))->typeId;
  if (typeId != inline_counter_type(vm) && typeId != owned_counter_type(vm))
    return raise_error(vm, "release-counter!: expected demo counter");
  return foreign_release(vm, "release-counter!", ARG(0), typeId);
}

static void init_demo(Vm& vm) {
  (void)inline_counter_type(vm);
  (void)owned_counter_type(vm);
  def_native(vm, "make-inline-counter", nat_make_inline_counter);
  def_native(vm, "make-owned-counter", nat_make_owned_counter);
  def_native(vm, "counter-value", nat_counter_value);
  def_native(vm, "counter-inc!", nat_counter_inc);
  def_native(vm, "release-counter!", nat_release_counter);
}

void register_demo_extension(Vm& vm) { register_native_module(vm, "demo", init_demo); }

}  // namespace ot
