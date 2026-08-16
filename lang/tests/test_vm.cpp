#include "doctest.h"
#include "../src/code.hpp"
#include "../src/heap.hpp"
#include "../src/state.hpp"
#include "../src/vm.hpp"
#include <cstring>

using namespace ot;

static State* vm_state() {
  StateConfig cfg{256 * 1024, 4096, 1000};
  return State::create(cfg);
}

static Value code(State& state, const u8* bytes, u32 len, Value constants = nil_v()) {
  Scope roots(state);
  Slot pool = roots.push(constants);
  if (pool.get().tag != Tag::Array) pool.set(make_array(state, 0));
  return make_code(state, bytes, len, pool.get(), CodeSpec{});
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
    CHECK(std::string(printed.data, printed.len) == "\"6ZF\"");
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
