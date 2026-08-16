#include "doctest.h"
#include "../src/code.hpp"
#include "../src/eval.hpp"
#include "../src/heap.hpp"
#include "../src/ns.hpp"
#include "../src/state.hpp"
#include "../src/vm.hpp"
#include <cstring>

using namespace ot;

static State* vm_state(u32 maxDepth = 1000) {
  StateConfig cfg{256 * 1024, 4096, 1000};
  State* state = State::create(cfg);
  state->cfg.maxDepth = maxDepth;
  return state;
}

static Value code(State& state, const u8* bytes, u32 len, Value constants = nil_v(),
                  CodeSpec spec = {}) {
  Scope roots(state);
  Slot pool = roots.push(constants);
  if (pool.get().tag != Tag::Array) pool.set(make_array(state, 0));
  return make_code(state, bytes, len, pool.get(), spec);
}

static Value function(State& state, Value codeValue) {
  Scope roots(state);
  Slot codeRoot = roots.push(codeValue);
  Slot captures = roots.push(make_array(state, 0));
  return make_compiled_function(state, codeRoot.get(), captures.get(), symbol_v(state.currentNs),
                                as_code(codeRoot.get())->name);
}

TEST_CASE("bytecode executes and prints as shifted ASCII") {
  State* state = vm_state();
  {
    const u8 bytes[] = {(u8)Op::Int8, 42, (u8)Op::Return};
    Scope roots(*state);
    Slot codeRoot = roots.push(code(*state, bytes, sizeof(bytes)));
    REQUIRE(codeRoot.get().tag == Tag::Code);
    Value result = vm_execute_code(*state, codeRoot.get());
    CHECK(result.tag == Tag::Int);
    CHECK(result.i == 42);

    Buf printed;
    code_print_ascii(codeRoot.get(), printed);
    CHECK(std::string(printed.data, printed.len) == "\"6ZG\"");
  }
  state->destroy();
}

TEST_CASE("code constants are traced in pinned storage") {
  State* state = vm_state();
  {
    Scope roots(*state);
    Slot pool = roots.push(make_array(*state, 1));
    Slot item = roots.push(make_string(*state, "kept", 4));
    array_push(*state, pool.get(), item.get());
    const u8 bytes[] = {(u8)Op::Const, 0, 0, (u8)Op::Return};
    Slot codeRoot = roots.push(code(*state, bytes, sizeof(bytes), pool.get()));
    state->heap.collect();
    Value result = vm_execute_code(*state, codeRoot.get());
    REQUIRE(result.tag == Tag::String);
    CHECK(as_string(result)->len == 4);
    CHECK(memcmp(string_bytes(result), "kept", 4) == 0);
  }
  state->destroy();
}

TEST_CASE("bytecode verifier rejects bad instructions and jump targets") {
  State* state = vm_state();
  {
    const u8 bytes[] = {255};
    Value bad = code(*state, bytes, sizeof(bytes));
    CHECK_FALSE(code_verify(bad));
  }
  {
    const u8 bytes[] = {(u8)Op::Jump, 1, 0, 0, 0, (u8)Op::Return};
    Value bad = code(*state, bytes, sizeof(bytes));
    CHECK_FALSE(code_verify(bad));
  }
  state->destroy();
}

TEST_CASE("compiled calls use locals and return through VM frames") {
  State* state = vm_state();
  {
    const u8 bytes[] = {(u8)Op::GetLocal, 0, 0, (u8)Op::Return};
    CodeSpec spec;
    spec.nfixed = 1;
    spec.nlocals = 1;
    spec.maxStack = 1;
    Scope roots(*state);
    Slot fn = roots.push(function(*state, code(*state, bytes, sizeof(bytes), nil_v(), spec)));
    Slot arg = roots.push(int_v(42));
    Value result = vm_call(*state, fn.get(), arg.idx, 1);
    CHECK(result.tag == Tag::Int);
    CHECK(result.i == 42);
    CHECK(state->frames.len == 0);
  }
  state->destroy();
}

TEST_CASE("CALL enters compiled functions without a C apply") {
  State* state = vm_state();
  {
    const u8 innerBytes[] = {(u8)Op::GetLocal, 0, 0, (u8)Op::Return};
    CodeSpec innerSpec;
    innerSpec.nfixed = 1;
    innerSpec.nlocals = 1;
    innerSpec.maxStack = 1;
    Scope roots(*state);
    Slot inner = roots.push(
        function(*state, code(*state, innerBytes, sizeof(innerBytes), nil_v(), innerSpec)));
    Slot constants = roots.push(make_array(*state, 1));
    array_push(*state, constants.get(), inner.get());
    const u8 outerBytes[] = {(u8)Op::Const, 0, 0, (u8)Op::Int8,  42,
                             (u8)Op::Call,  1, 0, (u8)Op::Return};
    CodeSpec outerSpec;
    outerSpec.maxStack = 3;
    Slot outer =
        roots.push(code(*state, outerBytes, sizeof(outerBytes), constants.get(), outerSpec));
    Value result = vm_execute_code(*state, outer.get());
    CHECK(result.tag == Tag::Int);
    CHECK(result.i == 42);
    CHECK(state->frames.len == 0);
  }
  state->destroy();
}

TEST_CASE("TAILCALL reuses the frame under a depth limit of one") {
  State* state = vm_state(1);
  {
    const u8 innerBytes[] = {(u8)Op::GetLocal, 0, 0, (u8)Op::Return};
    CodeSpec innerSpec;
    innerSpec.nfixed = 1;
    innerSpec.nlocals = 1;
    innerSpec.maxStack = 1;
    Scope roots(*state);
    Slot inner = roots.push(
        function(*state, code(*state, innerBytes, sizeof(innerBytes), nil_v(), innerSpec)));
    Slot constants = roots.push(make_array(*state, 1));
    array_push(*state, constants.get(), inner.get());
    const u8 outerBytes[] = {(u8)Op::Const, 0, 0, (u8)Op::GetLocal, 0, 0, (u8)Op::TailCall, 1, 0};
    CodeSpec outerSpec;
    outerSpec.nfixed = 1;
    outerSpec.nlocals = 1;
    outerSpec.maxStack = 2;
    Slot outerCode =
        roots.push(code(*state, outerBytes, sizeof(outerBytes), constants.get(), outerSpec));
    Slot outer = roots.push(function(*state, outerCode.get()));
    Slot arg = roots.push(int_v(77));
    Value result = vm_call(*state, outer.get(), arg.idx, 1);
    CHECK(result.tag == Tag::Int);
    CHECK(result.i == 77);
    CHECK(state->frames.len == 0);
  }
  state->destroy();
}

TEST_CASE("rest arguments are packed without leaving the VM") {
  State* state = vm_state();
  {
    const u8 bytes[] = {(u8)Op::GetLocal, 1, 0, (u8)Op::Return};
    CodeSpec spec;
    spec.nfixed = 1;
    spec.hasRest = 1;
    spec.nlocals = 2;
    spec.maxStack = 1;
    Scope roots(*state);
    Slot fn = roots.push(function(*state, code(*state, bytes, sizeof(bytes), nil_v(), spec)));
    u32 args = state->stack.len;
    state->push(int_v(1));
    state->push(int_v(2));
    state->push(int_v(3));
    Value result = vm_call(*state, fn.get(), args, 3);
    REQUIRE(result.tag == Tag::Pair);
    CHECK(as_pair(result)->car.i == 2);
    REQUIRE(as_pair(result)->cdr.tag == Tag::Pair);
    CHECK(as_pair(as_pair(result)->cdr)->car.i == 3);
    CHECK(as_pair(as_pair(result)->cdr)->cdr.tag == Tag::Null);
  }
  state->destroy();
}

TEST_CASE("flat closures retain boxed captures across collection") {
  State* state = vm_state();
  {
    Scope roots(*state);
    const u8 innerBytes[] = {(u8)Op::GetUpval, 0, 0, (u8)Op::Return};
    CodeSpec innerSpec;
    innerSpec.nupvals = 1;
    innerSpec.maxStack = 1;
    Slot innerCode = roots.push(code(*state, innerBytes, sizeof(innerBytes), nil_v(), innerSpec));

    Slot descriptor = roots.push(make_array(*state, 2));
    array_push(*state, descriptor.get(), innerCode.get());
    array_push(*state, descriptor.get(), int_v(0));
    Slot constants = roots.push(make_array(*state, 1));
    array_push(*state, constants.get(), descriptor.get());
    const u8 outerBytes[] = {
        (u8)Op::GetLocal, 0, 0, (u8)Op::MakeBox, (u8)Op::SetLocal, 0, 0, (u8)Op::Pop,
        (u8)Op::Closure,  0, 0, (u8)Op::Return,
    };
    CodeSpec outerSpec;
    outerSpec.nfixed = 1;
    outerSpec.nlocals = 1;
    outerSpec.maxStack = 1;
    Slot outerCode =
        roots.push(code(*state, outerBytes, sizeof(outerBytes), constants.get(), outerSpec));
    Slot outer = roots.push(function(*state, outerCode.get()));
    Slot arg = roots.push(int_v(55));
    Slot closure = roots.push(vm_call(*state, outer.get(), arg.idx, 1));
    REQUIRE(closure.get().tag == Tag::Function);
    state->heap.collect();
    Value result = vm_call(*state, closure.get(), state->stack.len, 0);
    CHECK(result.tag == Tag::Int);
    CHECK(result.i == 55);
  }
  state->destroy();
}

static Value native_reenter(State& state, u32 base, u32 argc) {
  if (argc != 2) return raise_error(state, "native_reenter: expected callback and value");
  return vm_call(state, state.stack[base], base + 1, 1);
}

static Value native_fail(State& state, u32, u32 argc) {
  if (argc != 0) return raise_error(state, "native_fail: expected no arguments");
  return raise_error(state, "vm test failure");
}

TEST_CASE("native callbacks re-enter only to their frame floor") {
  State* state = vm_state();
  {
    const u8 callbackBytes[] = {(u8)Op::GetLocal, 0, 0, (u8)Op::Return};
    CodeSpec callbackSpec;
    callbackSpec.nfixed = 1;
    callbackSpec.nlocals = 1;
    callbackSpec.maxStack = 1;
    Scope roots(*state);
    Slot callback = roots.push(function(
        *state, code(*state, callbackBytes, sizeof(callbackBytes), nil_v(), callbackSpec)));
    Slot native = roots.push(make_native(*state, "native-reenter", native_reenter));
    Slot constants = roots.push(make_array(*state, 2));
    array_push(*state, constants.get(), native.get());
    array_push(*state, constants.get(), callback.get());
    const u8 outerBytes[] = {(u8)Op::Const, 0, 0, (u8)Op::Const, 1, 0, (u8)Op::Int8, 9,
                             (u8)Op::Call,  2, 0, (u8)Op::Return};
    CodeSpec outerSpec;
    outerSpec.maxStack = 4;
    Slot outer =
        roots.push(code(*state, outerBytes, sizeof(outerBytes), constants.get(), outerSpec));
    Value result = vm_execute_code(*state, outer.get());
    CHECK(result.tag == Tag::Int);
    CHECK(result.i == 9);
    CHECK(state->frames.len == 0);
  }
  state->destroy();
}

TEST_CASE("unwinds discard compiled frames to the execution floor") {
  State* state = vm_state();
  {
    Scope roots(*state);
    Slot native = roots.push(make_native(*state, "native-fail", native_fail));
    Slot constants = roots.push(make_array(*state, 1));
    array_push(*state, constants.get(), native.get());
    const u8 bytes[] = {(u8)Op::Const, 0, 0, (u8)Op::Call, 0, 0, (u8)Op::Return};
    CodeSpec spec;
    spec.maxStack = 1;
    Slot codeValue = roots.push(code(*state, bytes, sizeof(bytes), constants.get(), spec));
    u32 savedNs = state->currentNs;
    Value result = vm_execute_code(*state, codeValue.get());
    CHECK(result.tag == Tag::Unwind);
    CHECK(state->unwindKind == UnwindKind::Condition);
    CHECK(state->frames.len == 0);
    CHECK(state->currentNs == savedNs);
    state_cancel_unwind(*state);
  }
  state->destroy();
}

TEST_CASE("cached global cells observe later redefinition") {
  State* state = vm_state();
  {
    Scope roots(*state);
    u32 name = state->intern.intern("vm-global", 9);
    ns_define(*state, name, int_v(1), false, nil_v());
    Slot constants = roots.push(make_array(*state, 1));
    array_push(*state, constants.get(), symbol_v(name));
    const u8 bytes[] = {(u8)Op::GetGlobal, 0, 0, (u8)Op::Return};
    CodeSpec spec;
    spec.maxStack = 1;
    Slot codeValue = roots.push(code(*state, bytes, sizeof(bytes), constants.get(), spec));
    Slot fn = roots.push(function(*state, codeValue.get()));
    Value first = vm_call(*state, fn.get(), state->stack.len, 0);
    CHECK(first.tag == Tag::Int);
    CHECK(first.i == 1);
    ns_define(*state, name, int_v(2), false, nil_v());
    Value second = vm_call(*state, fn.get(), state->stack.len, 0);
    CHECK(second.tag == Tag::Int);
    CHECK(second.i == 2);
    CHECK(as_code(codeValue.get())->consts[0].tag == Tag::Array);
  }
  state->destroy();
}
