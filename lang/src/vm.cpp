#include "vm.hpp"
#include "code.hpp"
#include "eval.hpp"
#include "heap.hpp"
#include "state.hpp"

namespace ot {

Value vm_execute_code(State& state, Value value) {
  if (value.tag != Tag::Code) return raise_error(state, "vm: expected code");
  Buf verifyError;
  if (!code_verify(value, &verifyError)) {
    verifyError.push('\0');
    return raise_error(state, "vm: %s", verifyError.data);
  }

  Scope roots(state);
  Slot codeRoot = roots.push(value);
  u32 base = state.stack.len;
  const u8* bytes = as_code(codeRoot.get())->bytes;
  const u8* ip = bytes;
  const u8* end = bytes + as_code(codeRoot.get())->len;

#ifdef OT_COMPUTED_GOTO
  static void* labels[] = {
#define OT_LABEL(name, text, operand) &&op_##name,
      OT_OPCODE_LIST(OT_LABEL)
#undef OT_LABEL
  };
  static_assert(sizeof(labels) / sizeof(labels[0]) == (u32)Op::Count);
#define VM_DISPATCH()                                                                              \
  do {                                                                                             \
    if (ip >= end) return raise_error(state, "vm: fell off bytecode");                             \
    u8 next = *ip++;                                                                               \
    if (next >= (u8)Op::Count) return raise_error(state, "vm: invalid opcode");                    \
    goto* labels[next];                                                                            \
  } while (0)
#define VM_OP(name) op_##name:
  VM_DISPATCH();
#else
#define VM_DISPATCH() continue
#define VM_OP(name) case Op::name:
  for (;;) {
    if (ip >= end) return raise_error(state, "vm: fell off bytecode");
    Op instruction = (Op)*ip++;
    switch (instruction) {
#endif

  VM_OP(Halt) {
    Value result = state.stack.len > base ? state.stack[state.stack.len - 1] : nil_v();
    return result;
  }
  VM_OP(Const) {
    u16 index = code_read_u16(ip);
    ip += 2;
    state.push(as_code(codeRoot.get())->consts[index]);
    VM_DISPATCH();
  }
  VM_OP(Nil) {
    state.push(nil_v());
    VM_DISPATCH();
  }
  VM_OP(True) {
    state.push(bool_v(true));
    VM_DISPATCH();
  }
  VM_OP(False) {
    state.push(bool_v(false));
    VM_DISPATCH();
  }
  VM_OP(Null) {
    state.push(null_v());
    VM_DISPATCH();
  }
  VM_OP(Int8) {
    state.push(int_v((i8)*ip++));
    VM_DISPATCH();
  }
  VM_OP(Pop) {
    if (state.stack.len <= base) return raise_error(state, "vm: stack underflow");
    state.stack.pop();
    VM_DISPATCH();
  }
  VM_OP(PopNKeep1) {
    u16 count = code_read_u16(ip);
    ip += 2;
    if (state.stack.len <= base || count > state.stack.len - base - 1)
      return raise_error(state, "vm: stack underflow");
    Value top = state.stack[state.stack.len - 1];
    state.stack.len -= count;
    state.stack[state.stack.len - 1] = top;
    VM_DISPATCH();
  }
  VM_OP(Jump) {
    i32 offset = code_read_i32(ip);
    ip += 4;
    ip += offset;
    VM_DISPATCH();
  }
  VM_OP(JumpFalse) {
    i32 offset = code_read_i32(ip);
    ip += 4;
    if (state.stack.len <= base) return raise_error(state, "vm: stack underflow");
    Value test = state.stack.pop();
    if (is_falsy(test)) ip += offset;
    VM_DISPATCH();
  }
  VM_OP(JumpFalsePeek) {
    i32 offset = code_read_i32(ip);
    ip += 4;
    if (state.stack.len <= base) return raise_error(state, "vm: stack underflow");
    if (is_falsy(state.stack[state.stack.len - 1])) ip += offset;
    VM_DISPATCH();
  }
  VM_OP(JumpTruePeek) {
    i32 offset = code_read_i32(ip);
    ip += 4;
    if (state.stack.len <= base) return raise_error(state, "vm: stack underflow");
    if (is_truthy(state.stack[state.stack.len - 1])) ip += offset;
    VM_DISPATCH();
  }
  VM_OP(Loop) {
    i32 offset = code_read_i32(ip);
    ip += 4;
    if (state.interruptFlag) {
      state.interruptFlag = false;
      return start_quit(state);
    }
    ip += offset;
    VM_DISPATCH();
  }
  VM_OP(Cons) {
    if (state.stack.len - base < 2) return raise_error(state, "vm: stack underflow");
    Slot car{&state, state.stack.len - 2};
    Slot cdr{&state, state.stack.len - 1};
    Value pair = make_pair(state, car, cdr);
    state.stack.len--;
    state.stack[state.stack.len - 1] = pair;
    VM_DISPATCH();
  }
  VM_OP(List) {
    u16 count = code_read_u16(ip);
    ip += 2;
    if (count > state.stack.len - base) return raise_error(state, "vm: stack underflow");
    u32 first = state.stack.len - count;
    Slot result{&state, state.push(null_v())};
    for (u32 i = count; i-- > 0;) result.set(make_pair(state, Slot{&state, first + i}, result));
    Value list = result.get();
    state.popTo(first);
    state.push(list);
    VM_DISPATCH();
  }
  VM_OP(Return) {
    if (state.stack.len <= base) return raise_error(state, "vm: return stack underflow");
    return state.stack[state.stack.len - 1];
  }

  // These opcodes are part of the stable inventory but require real call
  // frames. Reaching one in the skeleton is a checked error.
  VM_OP(GetLocal) { return raise_error(state, "vm: get-local before frame support"); }
  VM_OP(SetLocal) { return raise_error(state, "vm: set-local before frame support"); }
  VM_OP(GetBoxed) { return raise_error(state, "vm: get-boxed before frame support"); }
  VM_OP(SetBoxed) { return raise_error(state, "vm: set-boxed before frame support"); }
  VM_OP(MakeBox) { return raise_error(state, "vm: make-box before frame support"); }
  VM_OP(GetUpval) { return raise_error(state, "vm: get-upval before frame support"); }
  VM_OP(SetUpval) { return raise_error(state, "vm: set-upval before frame support"); }
  VM_OP(GetGlobal) { return raise_error(state, "vm: get-global before frame support"); }
  VM_OP(SetGlobal) { return raise_error(state, "vm: set-global before frame support"); }
  VM_OP(DefGlobal) { return raise_error(state, "vm: def-global before frame support"); }
  VM_OP(Closure) { return raise_error(state, "vm: closure before frame support"); }
  VM_OP(Call) { return raise_error(state, "vm: call before frame support"); }
  VM_OP(TailCall) { return raise_error(state, "vm: tailcall before frame support"); }

#ifndef OT_COMPUTED_GOTO
  default: return raise_error(state, "vm: invalid opcode");
}
}
#endif

#undef VM_OP
#undef VM_DISPATCH
}

}  // namespace ot
