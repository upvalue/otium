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

static Value code(State* state, const u8* bytes, u32 len, Value constants, const CodeSpec* spec) {
  CodeSpec defaultSpec = {0};
  if (!spec) spec = &defaultSpec;
  OT_SCOPE(state);
  Ref pool = ref_push(state, constants);
  if (ref_get(state, pool).tag != Tag_Array) ref_set(state, pool, make_array(state, 0));
  return make_code(state, bytes, len, ref_get(state, pool), spec);
}

static Value function(State* state, Value codeValue) {
  OT_SCOPE(state);
  Ref codeRoot = ref_push(state, codeValue);
  Ref captures = ref_push(state, make_array(state, 0));
  return make_compiled_function(state, ref_get(state, codeRoot), ref_get(state, captures),
                                           symbol_v(state->currentNs),
                                           as_code(ref_get(state, codeRoot))->name, false);
}

TEST(bytecode_executes_and_prints_as_shifted_ascii) {
  State* state = vm_state(1000);
  {
    const u8 bytes[] = {(u8)Op_Int8, 42, (u8)Op_Return};
    OT_SCOPE(state);
    Ref codeRoot = ref_push(state, code(state, bytes, sizeof(bytes), nil_v(), nullptr));
    CHECK(ref_get(state, codeRoot).tag == Tag_Code);
    if (ref_get(state, codeRoot).tag == Tag_Code) {
      Value result = vm_execute_code(state, ref_get(state, codeRoot));
      CHECK(result.tag == Tag_Int);
      CHECK(result.i == 42);

      Buf printed = {0};
      code_print_ascii(ref_get(state, codeRoot), &printed);
      CHECK_MEM(printed.data, printed.len, "\"5ZE\"");
      buf_deinit(&printed);
    }
  }
  state_destroy(state);
}

TEST(code_constants_are_traced_in_pinned_storage) {
  State* state = vm_state(1000);
  {
    OT_SCOPE(state);
    Ref pool = ref_push(state, make_array(state, 1));
    Ref item = ref_push(state, make_string(state, "kept", 4));
    array_push(state, ref_get(state, pool), ref_get(state, item));
    const u8 bytes[] = {(u8)Op_Const, 0, 0, (u8)Op_Return};
    Ref codeRoot = ref_push(state, code(state, bytes, sizeof(bytes), ref_get(state, pool), nullptr));
    heap_collect(&state->heap);
    Value result = vm_execute_code(state, ref_get(state, codeRoot));
    CHECK(result.tag == Tag_String);
    if (result.tag == Tag_String) {
      CHECK(as_string(result)->len == 4);
      CHECK(memcmp(string_bytes(result), "kept", 4) == 0);
    }
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
    OT_SCOPE(state);
    Ref fn = ref_push(state, function(state, code(state, bytes, sizeof(bytes), nil_v(), &spec)));
    Ref arg = ref_push(state, int_v(42));
    Value result = vm_call(state, ref_get(state, fn), arg.i, 1);
    CHECK(result.tag == Tag_Int);
    CHECK(result.i == 42);
    CHECK(state->frames.len == 0);
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
    OT_SCOPE(state);
    Ref inner = ref_push(
        state, function(state, code(state, innerBytes, sizeof(innerBytes), nil_v(), &innerSpec)));
    Ref constants = ref_push(state, make_array(state, 1));
    array_push(state, ref_get(state, constants), ref_get(state, inner));
    const u8 outerBytes[] = {(u8)Op_Const, 0, 0, (u8)Op_Int8, 42, (u8)Op_Call, 1, 0, (u8)Op_Return};
    CodeSpec outerSpec = {0};
    outerSpec.maxStack = 3;
    Ref outer = ref_push(
        state, code(state, outerBytes, sizeof(outerBytes), ref_get(state, constants), &outerSpec));
    Value result = vm_execute_code(state, ref_get(state, outer));
    CHECK(result.tag == Tag_Int);
    CHECK(result.i == 42);
    CHECK(state->frames.len == 0);
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
    OT_SCOPE(state);
    Ref inner = ref_push(
        state, function(state, code(state, innerBytes, sizeof(innerBytes), nil_v(), &innerSpec)));
    Ref constants = ref_push(state, make_array(state, 1));
    array_push(state, ref_get(state, constants), ref_get(state, inner));
    const u8 outerBytes[] = {(u8)Op_Const, 0, 0, (u8)Op_GetLocal, 0, 0, (u8)Op_TailCall, 1, 0};
    CodeSpec outerSpec = {0};
    outerSpec.nfixed = 1;
    outerSpec.nlocals = 1;
    outerSpec.maxStack = 2;
    Ref outerCode = ref_push(
        state, code(state, outerBytes, sizeof(outerBytes), ref_get(state, constants), &outerSpec));
    Ref outer = ref_push(state, function(state, ref_get(state, outerCode)));
    Ref arg = ref_push(state, int_v(77));
    Value result = vm_call(state, ref_get(state, outer), arg.i, 1);
    CHECK(result.tag == Tag_Int);
    CHECK(result.i == 77);
    CHECK(state->frames.len == 0);
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
    OT_SCOPE(state);
    Ref fn = ref_push(state, function(state, code(state, bytes, sizeof(bytes), nil_v(), &spec)));
    u32 args = state->stack.len;
    state_push(state, int_v(1));
    state_push(state, int_v(2));
    state_push(state, int_v(3));
    Value result = vm_call(state, ref_get(state, fn), args, 3);
    CHECK(result.tag == Tag_Pair);
    if (result.tag == Tag_Pair) {
      CHECK(as_pair(result)->car.i == 2);
      CHECK(as_pair(result)->cdr.tag == Tag_Pair);
      if (as_pair(result)->cdr.tag == Tag_Pair) {
        CHECK(as_pair(as_pair(result)->cdr)->car.i == 3);
        CHECK(as_pair(as_pair(result)->cdr)->cdr.tag == Tag_Null);
      }
    }
  }
  state_destroy(state);
}

TEST(flat_closures_retain_boxed_captures_across_collection) {
  State* state = vm_state(1000);
  {
    OT_SCOPE(state);
    const u8 innerBytes[] = {(u8)Op_GetUpval, 0, 0, (u8)Op_Return};
    CodeSpec innerSpec = {0};
    innerSpec.nupvals = 1;
    innerSpec.maxStack = 1;
    Ref innerCode =
        ref_push(state, code(state, innerBytes, sizeof(innerBytes), nil_v(), &innerSpec));

    Ref descriptor = ref_push(state, make_array(state, 2));
    array_push(state, ref_get(state, descriptor), ref_get(state, innerCode));
    array_push(state, ref_get(state, descriptor), int_v(0));
    Ref constants = ref_push(state, make_array(state, 1));
    array_push(state, ref_get(state, constants), ref_get(state, descriptor));
    const u8 outerBytes[] = {
        (u8)Op_GetLocal, 0, 0, (u8)Op_MakeBox, (u8)Op_SetLocal, 0, 0, (u8)Op_Pop,
        (u8)Op_Closure,  0, 0, (u8)Op_Return,
    };
    CodeSpec outerSpec = {0};
    outerSpec.nfixed = 1;
    outerSpec.nlocals = 1;
    outerSpec.maxStack = 1;
    Ref outerCode = ref_push(
        state, code(state, outerBytes, sizeof(outerBytes), ref_get(state, constants), &outerSpec));
    Ref outer = ref_push(state, function(state, ref_get(state, outerCode)));
    Ref arg = ref_push(state, int_v(55));
    Ref closure = ref_push(state, vm_call(state, ref_get(state, outer), arg.i, 1));
    CHECK(ref_get(state, closure).tag == Tag_Function);
    if (ref_get(state, closure).tag == Tag_Function) {
      heap_collect(&state->heap);
      Value result = vm_call(state, ref_get(state, closure), state->stack.len, 0);
      CHECK(result.tag == Tag_Int);
      CHECK(result.i == 55);
    }
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
    OT_SCOPE(state);
    Ref callback = ref_push(
        state,
        function(state, code(state, callbackBytes, sizeof(callbackBytes), nil_v(), &callbackSpec)));
    Ref native = ref_push(state, make_native(state, "native-reenter", native_reenter));
    Ref constants = ref_push(state, make_array(state, 2));
    array_push(state, ref_get(state, constants), ref_get(state, native));
    array_push(state, ref_get(state, constants), ref_get(state, callback));
    const u8 outerBytes[] = {(u8)Op_Const, 0, 0, (u8)Op_Const, 1, 0, (u8)Op_Int8, 9,
                             (u8)Op_Call,  2, 0, (u8)Op_Return};
    CodeSpec outerSpec = {0};
    outerSpec.maxStack = 4;
    Ref outer = ref_push(
        state, code(state, outerBytes, sizeof(outerBytes), ref_get(state, constants), &outerSpec));
    Value result = vm_execute_code(state, ref_get(state, outer));
    CHECK(result.tag == Tag_Int);
    CHECK(result.i == 9);
    CHECK(state->frames.len == 0);
  }
  state_destroy(state);
}

TEST(unwinds_discard_compiled_frames_to_the_execution_floor) {
  State* state = vm_state(1000);
  {
    OT_SCOPE(state);
    Ref native = ref_push(state, make_native(state, "native-fail", native_fail));
    Ref constants = ref_push(state, make_array(state, 1));
    array_push(state, ref_get(state, constants), ref_get(state, native));
    const u8 bytes[] = {(u8)Op_Const, 0, 0, (u8)Op_Call, 0, 0, (u8)Op_Return};
    CodeSpec spec = {0};
    spec.maxStack = 1;
    Ref codeValue =
        ref_push(state, code(state, bytes, sizeof(bytes), ref_get(state, constants), &spec));
    u32 savedNs = state->currentNs;
    Value result = vm_execute_code(state, ref_get(state, codeValue));
    CHECK(result.tag == Tag_Unwind);
    CHECK(state->unwindKind == UnwindKind_Condition);
    CHECK(state->frames.len == 0);
    CHECK(state->currentNs == savedNs);
    state_cancel_unwind(state);
  }
  state_destroy(state);
}

TEST(cached_global_cells_observe_later_redefinition) {
  State* state = vm_state(1000);
  {
    OT_SCOPE(state);
    u32 name = intern_id(&state->intern, "vm-global", 9);
    ns_define(state, name, int_v(1), false, nil_v());
    Ref constants = ref_push(state, make_array(state, 1));
    array_push(state, ref_get(state, constants), symbol_v(name));
    const u8 bytes[] = {(u8)Op_GetGlobal, 0, 0, (u8)Op_Return};
    CodeSpec spec = {0};
    spec.maxStack = 1;
    Ref codeValue =
        ref_push(state, code(state, bytes, sizeof(bytes), ref_get(state, constants), &spec));
    Ref fn = ref_push(state, function(state, ref_get(state, codeValue)));
    Value first = vm_call(state, ref_get(state, fn), state->stack.len, 0);
    CHECK(first.tag == Tag_Int);
    CHECK(first.i == 1);
    ns_define(state, name, int_v(2), false, nil_v());
    Value second = vm_call(state, ref_get(state, fn), state->stack.len, 0);
    CHECK(second.tag == Tag_Int);
    CHECK(second.i == 2);
    CHECK(as_code(ref_get(state, codeValue))->consts[0].tag == Tag_Array);
  }
  state_destroy(state);
}
