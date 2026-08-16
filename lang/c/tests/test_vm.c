#include "ctest.h"
#include "../src/code.h"
#include "../src/eval.h"
#include "../src/heap.h"
#include "../src/ns.h"
#include "../src/state.h"
#include "../src/vm.h"
#include <string.h>

static State* vm_state(u32 maxDepth) {
  StateConfig cfg = state_config_default();
  cfg.heapBytes = 256 * 1024;
  cfg.stackSlots = 4096;
  cfg.maxDepth = 1000;
  State* state = state_create(&cfg);
  state->cfg.maxDepth = maxDepth;
  return state;
}

static Value code(State* state, const u8* bytes, u32 len, Value constants,
                  const CodeSpec* spec) {
  CodeSpec defaultSpec = {0};
  if (!spec) spec = &defaultSpec;
  u32 sc = scope_begin(state);
  Slot pool = scope_push(state, constants);
  if (slot_get(pool).tag != Tag_Array) slot_set(pool, make_array(state, 0));
  return scope_exit(state, sc, make_code(state, bytes, len, slot_get(pool), spec));
}

static Value function(State* state, Value codeValue) {
  u32 sc = scope_begin(state);
  Slot codeRoot = scope_push(state, codeValue);
  Slot captures = scope_push(state, make_array(state, 0));
  return scope_exit(state, sc,
                    make_compiled_function(state, slot_get(codeRoot), slot_get(captures),
                                           symbol_v(state->currentNs),
                                           as_code(slot_get(codeRoot))->name, false));
}

TEST(bytecode_executes_and_prints_as_shifted_ascii) {
  State* state = vm_state(1000);
  {
    const u8 bytes[] = {(u8)Op_Int8, 42, (u8)Op_Return};
    u32 sc = scope_begin(state);
    Slot codeRoot = scope_push(state, code(state, bytes, sizeof(bytes), nil_v(), nullptr));
    CHECK(slot_get(codeRoot).tag == Tag_Code);
    if (slot_get(codeRoot).tag == Tag_Code) {
      Value result = vm_execute_code(state, slot_get(codeRoot));
      CHECK(result.tag == Tag_Int);
      CHECK(result.i == 42);

      Buf printed = {0};
      code_print_ascii(slot_get(codeRoot), &printed);
      CHECK_MEM(printed.data, printed.len, "\"5ZE\"");
      buf_deinit(&printed);
    }
    scope_pop_to(state, sc);
  }
  state_destroy(state);
}

TEST(code_constants_are_traced_in_pinned_storage) {
  State* state = vm_state(1000);
  {
    u32 sc = scope_begin(state);
    Slot pool = scope_push(state, make_array(state, 1));
    Slot item = scope_push(state, make_string(state, "kept", 4));
    array_push(state, slot_get(pool), slot_get(item));
    const u8 bytes[] = {(u8)Op_Const, 0, 0, (u8)Op_Return};
    Slot codeRoot = scope_push(state, code(state, bytes, sizeof(bytes), slot_get(pool), nullptr));
    heap_collect(&state->heap);
    Value result = vm_execute_code(state, slot_get(codeRoot));
    CHECK(result.tag == Tag_String);
    if (result.tag == Tag_String) {
      CHECK(as_string(result)->len == 4);
      CHECK(memcmp(string_bytes(result), "kept", 4) == 0);
    }
    scope_pop_to(state, sc);
  }
  state_destroy(state);
}

TEST(bytecode_verifier_rejects_bad_instructions_and_jump_targets) {
  State* state = vm_state(1000);
  {
    const u8 bytes[] = {255};
    Value bad = code(state, bytes, sizeof(bytes), nil_v(), nullptr);
    CHECK(!code_verify(bad, nullptr));
  }
  {
    const u8 bytes[] = {(u8)Op_Jump, 1, 0, 0, 0, (u8)Op_Return};
    Value bad = code(state, bytes, sizeof(bytes), nil_v(), nullptr);
    CHECK(!code_verify(bad, nullptr));
  }
  state_destroy(state);
}

TEST(compiled_calls_use_locals_and_return_through_vm_frames) {
  State* state = vm_state(1000);
  {
    const u8 bytes[] = {(u8)Op_GetLocal, 0, 0, (u8)Op_Return};
    CodeSpec spec = {0};
    spec.nfixed = 1;
    spec.nlocals = 1;
    spec.maxStack = 1;
    u32 sc = scope_begin(state);
    Slot fn = scope_push(state, function(state, code(state, bytes, sizeof(bytes), nil_v(), &spec)));
    Slot arg = scope_push(state, int_v(42));
    Value result = vm_call(state, slot_get(fn), arg.idx, 1);
    CHECK(result.tag == Tag_Int);
    CHECK(result.i == 42);
    CHECK(state->frames.len == 0);
    scope_pop_to(state, sc);
  }
  state_destroy(state);
}

TEST(call_enters_compiled_functions_without_a_c_apply) {
  State* state = vm_state(1000);
  {
    const u8 innerBytes[] = {(u8)Op_GetLocal, 0, 0, (u8)Op_Return};
    CodeSpec innerSpec = {0};
    innerSpec.nfixed = 1;
    innerSpec.nlocals = 1;
    innerSpec.maxStack = 1;
    u32 sc = scope_begin(state);
    Slot inner = scope_push(
        state, function(state, code(state, innerBytes, sizeof(innerBytes), nil_v(), &innerSpec)));
    Slot constants = scope_push(state, make_array(state, 1));
    array_push(state, slot_get(constants), slot_get(inner));
    const u8 outerBytes[] = {(u8)Op_Const, 0, 0, (u8)Op_Int8,  42,
                             (u8)Op_Call,  1, 0, (u8)Op_Return};
    CodeSpec outerSpec = {0};
    outerSpec.maxStack = 3;
    Slot outer = scope_push(
        state, code(state, outerBytes, sizeof(outerBytes), slot_get(constants), &outerSpec));
    Value result = vm_execute_code(state, slot_get(outer));
    CHECK(result.tag == Tag_Int);
    CHECK(result.i == 42);
    CHECK(state->frames.len == 0);
    scope_pop_to(state, sc);
  }
  state_destroy(state);
}

TEST(tailcall_reuses_the_frame_under_a_depth_limit_of_one) {
  State* state = vm_state(1);
  {
    const u8 innerBytes[] = {(u8)Op_GetLocal, 0, 0, (u8)Op_Return};
    CodeSpec innerSpec = {0};
    innerSpec.nfixed = 1;
    innerSpec.nlocals = 1;
    innerSpec.maxStack = 1;
    u32 sc = scope_begin(state);
    Slot inner = scope_push(
        state, function(state, code(state, innerBytes, sizeof(innerBytes), nil_v(), &innerSpec)));
    Slot constants = scope_push(state, make_array(state, 1));
    array_push(state, slot_get(constants), slot_get(inner));
    const u8 outerBytes[] = {(u8)Op_Const, 0, 0, (u8)Op_GetLocal, 0, 0, (u8)Op_TailCall, 1, 0};
    CodeSpec outerSpec = {0};
    outerSpec.nfixed = 1;
    outerSpec.nlocals = 1;
    outerSpec.maxStack = 2;
    Slot outerCode = scope_push(
        state, code(state, outerBytes, sizeof(outerBytes), slot_get(constants), &outerSpec));
    Slot outer = scope_push(state, function(state, slot_get(outerCode)));
    Slot arg = scope_push(state, int_v(77));
    Value result = vm_call(state, slot_get(outer), arg.idx, 1);
    CHECK(result.tag == Tag_Int);
    CHECK(result.i == 77);
    CHECK(state->frames.len == 0);
    scope_pop_to(state, sc);
  }
  state_destroy(state);
}

TEST(rest_arguments_are_packed_without_leaving_the_vm) {
  State* state = vm_state(1000);
  {
    const u8 bytes[] = {(u8)Op_GetLocal, 1, 0, (u8)Op_Return};
    CodeSpec spec = {0};
    spec.nfixed = 1;
    spec.hasRest = 1;
    spec.nlocals = 2;
    spec.maxStack = 1;
    u32 sc = scope_begin(state);
    Slot fn = scope_push(state, function(state, code(state, bytes, sizeof(bytes), nil_v(), &spec)));
    u32 args = state->stack.len;
    state_push(state, int_v(1));
    state_push(state, int_v(2));
    state_push(state, int_v(3));
    Value result = vm_call(state, slot_get(fn), args, 3);
    CHECK(result.tag == Tag_Pair);
    if (result.tag == Tag_Pair) {
      CHECK(as_pair(result)->car.i == 2);
      CHECK(as_pair(result)->cdr.tag == Tag_Pair);
      if (as_pair(result)->cdr.tag == Tag_Pair) {
        CHECK(as_pair(as_pair(result)->cdr)->car.i == 3);
        CHECK(as_pair(as_pair(result)->cdr)->cdr.tag == Tag_Null);
      }
    }
    scope_pop_to(state, sc);
  }
  state_destroy(state);
}

TEST(flat_closures_retain_boxed_captures_across_collection) {
  State* state = vm_state(1000);
  {
    u32 sc = scope_begin(state);
    const u8 innerBytes[] = {(u8)Op_GetUpval, 0, 0, (u8)Op_Return};
    CodeSpec innerSpec = {0};
    innerSpec.nupvals = 1;
    innerSpec.maxStack = 1;
    Slot innerCode =
        scope_push(state, code(state, innerBytes, sizeof(innerBytes), nil_v(), &innerSpec));

    Slot descriptor = scope_push(state, make_array(state, 2));
    array_push(state, slot_get(descriptor), slot_get(innerCode));
    array_push(state, slot_get(descriptor), int_v(0));
    Slot constants = scope_push(state, make_array(state, 1));
    array_push(state, slot_get(constants), slot_get(descriptor));
    const u8 outerBytes[] = {
        (u8)Op_GetLocal, 0, 0, (u8)Op_MakeBox, (u8)Op_SetLocal, 0, 0, (u8)Op_Pop,
        (u8)Op_Closure,  0, 0, (u8)Op_Return,
    };
    CodeSpec outerSpec = {0};
    outerSpec.nfixed = 1;
    outerSpec.nlocals = 1;
    outerSpec.maxStack = 1;
    Slot outerCode = scope_push(
        state, code(state, outerBytes, sizeof(outerBytes), slot_get(constants), &outerSpec));
    Slot outer = scope_push(state, function(state, slot_get(outerCode)));
    Slot arg = scope_push(state, int_v(55));
    Slot closure = scope_push(state, vm_call(state, slot_get(outer), arg.idx, 1));
    CHECK(slot_get(closure).tag == Tag_Function);
    if (slot_get(closure).tag == Tag_Function) {
      heap_collect(&state->heap);
      Value result = vm_call(state, slot_get(closure), state->stack.len, 0);
      CHECK(result.tag == Tag_Int);
      CHECK(result.i == 55);
    }
    scope_pop_to(state, sc);
  }
  state_destroy(state);
}

static Value native_reenter(State* state, u32 base, u32 argc) {
  if (argc != 2) return raise_error(state, "native_reenter: expected callback and value");
  return vm_call(state, state->stack.data[base], base + 1, 1);
}

static Value native_fail(State* state, u32 base, u32 argc) {
  (void)base;
  if (argc != 0) return raise_error(state, "native_fail: expected no arguments");
  return raise_error(state, "vm test failure");
}

TEST(native_callbacks_reenter_only_to_their_frame_floor) {
  State* state = vm_state(1000);
  {
    const u8 callbackBytes[] = {(u8)Op_GetLocal, 0, 0, (u8)Op_Return};
    CodeSpec callbackSpec = {0};
    callbackSpec.nfixed = 1;
    callbackSpec.nlocals = 1;
    callbackSpec.maxStack = 1;
    u32 sc = scope_begin(state);
    Slot callback = scope_push(
        state,
        function(state, code(state, callbackBytes, sizeof(callbackBytes), nil_v(), &callbackSpec)));
    Slot native = scope_push(state, make_native(state, "native-reenter", native_reenter));
    Slot constants = scope_push(state, make_array(state, 2));
    array_push(state, slot_get(constants), slot_get(native));
    array_push(state, slot_get(constants), slot_get(callback));
    const u8 outerBytes[] = {(u8)Op_Const, 0, 0, (u8)Op_Const, 1, 0, (u8)Op_Int8, 9,
                             (u8)Op_Call,  2, 0, (u8)Op_Return};
    CodeSpec outerSpec = {0};
    outerSpec.maxStack = 4;
    Slot outer = scope_push(
        state, code(state, outerBytes, sizeof(outerBytes), slot_get(constants), &outerSpec));
    Value result = vm_execute_code(state, slot_get(outer));
    CHECK(result.tag == Tag_Int);
    CHECK(result.i == 9);
    CHECK(state->frames.len == 0);
    scope_pop_to(state, sc);
  }
  state_destroy(state);
}

TEST(unwinds_discard_compiled_frames_to_the_execution_floor) {
  State* state = vm_state(1000);
  {
    u32 sc = scope_begin(state);
    Slot native = scope_push(state, make_native(state, "native-fail", native_fail));
    Slot constants = scope_push(state, make_array(state, 1));
    array_push(state, slot_get(constants), slot_get(native));
    const u8 bytes[] = {(u8)Op_Const, 0, 0, (u8)Op_Call, 0, 0, (u8)Op_Return};
    CodeSpec spec = {0};
    spec.maxStack = 1;
    Slot codeValue =
        scope_push(state, code(state, bytes, sizeof(bytes), slot_get(constants), &spec));
    u32 savedNs = state->currentNs;
    Value result = vm_execute_code(state, slot_get(codeValue));
    CHECK(result.tag == Tag_Unwind);
    CHECK(state->unwindKind == UnwindKind_Condition);
    CHECK(state->frames.len == 0);
    CHECK(state->currentNs == savedNs);
    state_cancel_unwind(state);
    scope_pop_to(state, sc);
  }
  state_destroy(state);
}

TEST(cached_global_cells_observe_later_redefinition) {
  State* state = vm_state(1000);
  {
    u32 sc = scope_begin(state);
    u32 name = intern_id(&state->intern, "vm-global", 9);
    ns_define(state, name, int_v(1), false, nil_v());
    Slot constants = scope_push(state, make_array(state, 1));
    array_push(state, slot_get(constants), symbol_v(name));
    const u8 bytes[] = {(u8)Op_GetGlobal, 0, 0, (u8)Op_Return};
    CodeSpec spec = {0};
    spec.maxStack = 1;
    Slot codeValue =
        scope_push(state, code(state, bytes, sizeof(bytes), slot_get(constants), &spec));
    Slot fn = scope_push(state, function(state, slot_get(codeValue)));
    Value first = vm_call(state, slot_get(fn), state->stack.len, 0);
    CHECK(first.tag == Tag_Int);
    CHECK(first.i == 1);
    ns_define(state, name, int_v(2), false, nil_v());
    Value second = vm_call(state, slot_get(fn), state->stack.len, 0);
    CHECK(second.tag == Tag_Int);
    CHECK(second.i == 2);
    CHECK(as_code(slot_get(codeValue))->consts[0].tag == Tag_Array);
    scope_pop_to(state, sc);
  }
  state_destroy(state);
}
