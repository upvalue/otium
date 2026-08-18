#define OT_INTERNAL
#include "otium.h"

#include <string.h>

static unsigned inline_counter_type(ots* state) {
  return ot_ext_type(state, "demo/inline-counter", NULL);
}

static void finish_counter(ots* state, void* payload) {
  (void)state;
  ot_host_free(payload);
}

static unsigned owned_counter_type(ots* state) {
  return ot_ext_type(state, "demo/owned-counter", finish_counter);
}

static otv make_counter(ots* state, otv* args, int argc, bool owned) {
  if (argc != 1 || !ot_is_int(args[0])) return ot_raise(state, "make-counter: expected int");
  intptr_t value = ot_get_int(args[0]);
  if (!owned) return ot_ext_inline(state, inline_counter_type(state), &value, sizeof value);
  intptr_t* payload = ot_host_alloc(sizeof(*payload));
  if (payload == NULL) return ot_raise(state, "make-owned-counter: out of memory");
  *payload = value;
  return ot_ext_pointer(state, owned_counter_type(state), payload);
}

static otv nat_make_inline(ots* state, otv* args, int argc) {
  return make_counter(state, args, argc, false);
}

static otv nat_make_owned(ots* state, otv* args, int argc) {
  return make_counter(state, args, argc, true);
}

static bool counter_payload(ots* state, const char* who, otv value, intptr_t** payload) {
  if (ot_ext_check(state, who, value, inline_counter_type(state), (void**)payload)) return true;
  ot_clear_condition(state);
  return ot_ext_check(state, who, value, owned_counter_type(state), (void**)payload);
}

static otv nat_counter_value(ots* state, otv* args, int argc) {
  if (argc != 1) return ot_raise(state, "counter-value: expected counter");
  intptr_t* payload;
  if (!counter_payload(state, "counter-value", args[0], &payload)) return OT_UNWIND;
  return ot_make_int(*payload);
}

static otv nat_counter_inc(ots* state, otv* args, int argc) {
  if (argc != 1) return ot_raise(state, "counter-inc!: expected counter");
  intptr_t* payload;
  if (!counter_payload(state, "counter-inc!", args[0], &payload)) return OT_UNWIND;
  *payload += 1;
  return args[0];
}

static otv nat_counter_release(ots* state, otv* args, int argc) {
  if (argc != 1 || !ot_has_type(args[0], OBJ_EXT))
    return ot_raise(state, "release-counter!: expected counter");
  ot_ext_obj* ext = (ot_ext_obj*)ot_as_obj(args[0]);
  return ot_ext_release(state, "release-counter!", args[0], ext->type);
}

static void init_demo(ots* state) {
  ot_def_nat(state, "make-inline-counter", nat_make_inline);
  ot_def_nat(state, "make-owned-counter", nat_make_owned);
  ot_def_nat(state, "counter-value", nat_counter_value);
  ot_def_nat(state, "counter-inc!", nat_counter_inc);
  ot_def_nat(state, "release-counter!", nat_counter_release);
}

void ot_register_demo_extension(ots* state) {
  (void)inline_counter_type(state);
  (void)owned_counter_type(state);
  ot_register_module(state, "demo", init_demo);
}
