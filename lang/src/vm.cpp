#include "vm.hpp"
#include "code.hpp"
#include "eval.hpp"
#include "heap.hpp"
#include "ns.hpp"
#include "state.hpp"

namespace ot {

static bool compiled_function(Value value) {
  return (value.tag == Tag::Function || value.tag == Tag::Macro) &&
         as_function(value)->code.tag == Tag::Code;
}

Value make_compiled_function(State& state, Value code, Value captures, Value nsName, u32 name,
                             bool macro) {
  Scope roots(state);
  Slot codeRoot = roots.push(code);
  Slot capturesRoot = roots.push(captures);
  Slot nsRoot = roots.push(nsName);
  if (codeRoot.get().tag != Tag::Code) return raise_error(state, "compiled function needs code");
  if (capturesRoot.get().tag != Tag::Array)
    return raise_error(state, "compiled function captures must be an array");
  u32 count = as_array(capturesRoot.get())->len;
  if (count != as_code(codeRoot.get())->nupvals)
    return raise_error(state, "compiled function capture count mismatch");
  if (count > (UINT32_MAX - (u32)sizeof(FunctionData)) / (u32)sizeof(Value))
    ot_fatal("compiled function: capture size overflow");
  u32 size = (u32)sizeof(FunctionData) + count * (u32)sizeof(Value);
  ObjType objectType = macro ? ObjType::Macro : ObjType::Function;
  Tag tag = macro ? Tag::Macro : Tag::Function;
  Obj* object = state.heap.alloc(objectType, size);
  Value fnValue = obj_v(tag, object);
  FunctionData* fn = as_function(fnValue);
  fn->name = name;
  fn->code = codeRoot.get();
  fn->nsName = nsRoot.get();
  fn->native = nullptr;
  fn->docstring = nil_v();
  fn->nupvals = count;
  ArrayData* source = as_array(capturesRoot.get());
  for (u32 i = 0; i < count; i++) function_upvals(fn)[i] = source->items[i];
  return fnValue;
}

static Value enter_frame(State& state, Value fnValue, u32 callBase, u32 argc, bool tail,
                         bool restoreNs = true) {
  FunctionData* fn = as_function(fnValue);
  CodeData* code = as_code(fn->code);
  u32 nfixed = code->nfixed;
  bool hasRest = code->hasRest != 0;
  u32 nlocals = code->nlocals;
  u32 maxStack = code->maxStack;
  u32 formalCount = nfixed + (hasRest ? 1u : 0u);
  if ((!hasRest && argc != nfixed) || (hasRest && argc < nfixed))
    return raise_error(state, "call: wrong number of arguments (%u)", argc);
  if (nlocals < formalCount) return raise_error(state, "vm: code has too few local slots");
  u64 required = (u64)callBase + 1u + nlocals + maxStack;
  if (required > state.cfg.stackSlots) return raise_error(state, "vm stack limit exceeded");
  if (!tail && state.frames.len >= state.cfg.maxDepth)
    return raise_error(state, "recursion depth exceeded");

  if (tail) {
    CallFrame& frame = state.frames[state.frames.len - 1];
    frame.fn = fnValue;
    frame.ip = 0;
    frame.callBase = callBase;
    frame.base = callBase + 1;
    frame.stackBase = frame.base + nlocals;
  } else {
    state.frames.push(CallFrame{fnValue, 0, callBase, callBase + 1, callBase + 1 + nlocals,
                                state.currentNs, restoreNs});
  }

  CallFrame& frame = state.frames[state.frames.len - 1];
  if (hasRest) {
    u32 extra = argc - nfixed;
    Slot rest{&state, state.push(null_v())};
    for (u32 i = extra; i-- > 0;) {
      Slot item{&state, frame.base + nfixed + i};
      rest.set(make_pair(state, item, rest));
    }
    Value list = rest.get();
    state.popTo(frame.base + nfixed);
    state.push(list);
  } else {
    state.popTo(frame.base + argc);
  }
  while (state.stack.len < frame.stackBase) state.push(nil_v());
  state.currentNs = as_function(frame.fn)->nsName.id;
  return nil_v();
}

static void load_frame(State& state, const u8** bytes, const u8** ip, const u8** end,
                       u32* stackBase) {
  CallFrame& frame = state.frames[state.frames.len - 1];
  CodeData* code = as_code(as_function(frame.fn)->code);
  *bytes = code->bytes;
  *ip = code->bytes + frame.ip;
  *end = code->bytes + code->len;
  *stackBase = frame.stackBase;
}

static Value unwind_to(State& state, u32 floor) {
  while (state.frames.len > floor) {
    CallFrame frame = state.frames.pop();
    if (frame.restoreNs) state.currentNs = frame.savedNs;
    state.popTo(frame.callBase);
  }
  return unwind_v();
}

Value vm_execute(State& state, u32 floor) {
  if (state.frames.len <= floor) return raise_error(state, "vm: no frame to execute");
  const u8* bytes = nullptr;
  const u8* ip = nullptr;
  const u8* end = nullptr;
  u32 stackBase = 0;
  load_frame(state, &bytes, &ip, &end, &stackBase);

#define VM_SAVE_IP()                                                                               \
  do {                                                                                             \
    state.frames[state.frames.len - 1].ip = (u32)(ip - bytes);                                     \
  } while (0)
#define VM_LOAD_FRAME() load_frame(state, &bytes, &ip, &end, &stackBase)

#ifdef OT_COMPUTED_GOTO
  static void* labels[] = {
#define OT_LABEL(name, text, operand) &&op_##name,
      OT_OPCODE_LIST(OT_LABEL)
#undef OT_LABEL
  };
  static_assert(sizeof(labels) / sizeof(labels[0]) == (u32)Op::Count);
#define VM_DISPATCH()                                                                              \
  do {                                                                                             \
    if (ip >= end) return unwind_to(state, floor);                                                 \
    u8 next = *ip++;                                                                               \
    if (next >= (u8)Op::Count) return unwind_to(state, floor);                                     \
    goto* labels[next];                                                                            \
  } while (0)
#define VM_OP(name) op_##name:
  VM_DISPATCH();
#else
#define VM_DISPATCH() continue
#define VM_OP(name) case Op::name:
  for (;;) {
    if (ip >= end) return unwind_to(state, floor);
    Op instruction = (Op)*ip++;
    switch (instruction) {
#endif

  VM_OP(Halt) {
    Value result = state.stack.len > stackBase ? state.stack[state.stack.len - 1] : nil_v();
    CallFrame frame = state.frames.pop();
    if (frame.restoreNs) state.currentNs = frame.savedNs;
    state.popTo(frame.callBase);
    if (state.frames.len == floor) return result;
    state.push(result);
    VM_LOAD_FRAME();
    VM_DISPATCH();
  }
  VM_OP(Const) {
    u16 index = code_read_u16(ip);
    ip += 2;
    FunctionData* fn = as_function(state.frames[state.frames.len - 1].fn);
    state.push(as_code(fn->code)->consts[index]);
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
    if (state.stack.len <= stackBase) return unwind_to(state, floor);
    state.stack.pop();
    VM_DISPATCH();
  }
  VM_OP(PopNKeep1) {
    u16 count = code_read_u16(ip);
    ip += 2;
    if (state.stack.len <= stackBase || count > state.stack.len - stackBase - 1)
      return unwind_to(state, floor);
    Value top = state.stack[state.stack.len - 1];
    state.stack.len -= count;
    state.stack[state.stack.len - 1] = top;
    VM_DISPATCH();
  }
  VM_OP(GetLocal) {
    u16 index = code_read_u16(ip);
    ip += 2;
    CallFrame& frame = state.frames[state.frames.len - 1];
    if (index >= frame.stackBase - frame.base) return unwind_to(state, floor);
    state.push(state.stack[frame.base + index]);
    VM_DISPATCH();
  }
  VM_OP(SetLocal) {
    u16 index = code_read_u16(ip);
    ip += 2;
    CallFrame& frame = state.frames[state.frames.len - 1];
    if (index >= frame.stackBase - frame.base || state.stack.len <= stackBase)
      return unwind_to(state, floor);
    state.stack[frame.base + index] = state.stack[state.stack.len - 1];
    VM_DISPATCH();
  }
  VM_OP(GetBoxed) {
    u16 index = code_read_u16(ip);
    ip += 2;
    CallFrame& frame = state.frames[state.frames.len - 1];
    if (index >= frame.stackBase - frame.base) return unwind_to(state, floor);
    Value box = state.stack[frame.base + index];
    if (box.tag != Tag::Array || as_array(box)->len != 1) return unwind_to(state, floor);
    state.push(as_array(box)->items[0]);
    VM_DISPATCH();
  }
  VM_OP(SetBoxed) {
    u16 index = code_read_u16(ip);
    ip += 2;
    CallFrame& frame = state.frames[state.frames.len - 1];
    if (index >= frame.stackBase - frame.base || state.stack.len <= stackBase)
      return unwind_to(state, floor);
    Value box = state.stack[frame.base + index];
    if (box.tag != Tag::Array || as_array(box)->len != 1) return unwind_to(state, floor);
    as_array(box)->items[0] = state.stack[state.stack.len - 1];
    VM_DISPATCH();
  }
  VM_OP(MakeBox) {
    if (state.stack.len <= stackBase) return unwind_to(state, floor);
    Slot value{&state, state.stack.len - 1};
    Value box = make_array(state, 1);
    array_push(state, box, value.get());
    value.set(box);
    VM_DISPATCH();
  }
  VM_OP(GetUpval) {
    u16 index = code_read_u16(ip);
    ip += 2;
    FunctionData* fn = as_function(state.frames[state.frames.len - 1].fn);
    if (index >= fn->nupvals) return unwind_to(state, floor);
    Value box = function_upvals(fn)[index];
    if (box.tag != Tag::Array || as_array(box)->len != 1) return unwind_to(state, floor);
    state.push(as_array(box)->items[0]);
    VM_DISPATCH();
  }
  VM_OP(SetUpval) {
    u16 index = code_read_u16(ip);
    ip += 2;
    FunctionData* fn = as_function(state.frames[state.frames.len - 1].fn);
    if (index >= fn->nupvals || state.stack.len <= stackBase) return unwind_to(state, floor);
    Value box = function_upvals(fn)[index];
    if (box.tag != Tag::Array || as_array(box)->len != 1) return unwind_to(state, floor);
    as_array(box)->items[0] = state.stack[state.stack.len - 1];
    VM_DISPATCH();
  }
  VM_OP(Closure) {
    u16 index = code_read_u16(ip);
    ip += 2;
    FunctionData* parent = as_function(state.frames[state.frames.len - 1].fn);
    Value descriptor = as_code(parent->code)->consts[index];
    if (descriptor.tag != Tag::Array || as_array(descriptor)->len == 0)
      return unwind_to(state, floor);
    Value nested = as_array(descriptor)->items[0];
    if (nested.tag != Tag::Code || as_array(descriptor)->len - 1 != as_code(nested)->nupvals)
      return unwind_to(state, floor);
    u32 captureCount = as_code(nested)->nupvals;
    Slot captures{&state, state.push(make_array(state, captureCount))};
    for (u32 i = 0; i < captureCount; i++) {
      parent = as_function(state.frames[state.frames.len - 1].fn);
      descriptor = as_code(parent->code)->consts[index];
      Value capture = as_array(descriptor)->items[i + 1];
      if (capture.tag != Tag::Int) return unwind_to(state, floor);
      Value box;
      if (capture.i >= 0) {
        CallFrame& frame = state.frames[state.frames.len - 1];
        if ((u64)capture.i >= frame.stackBase - frame.base) return unwind_to(state, floor);
        box = state.stack[frame.base + (u32)capture.i];
      } else {
        u64 upvalue = (u64)(-(capture.i + 1));
        if (upvalue >= parent->nupvals) return unwind_to(state, floor);
        box = function_upvals(parent)[upvalue];
      }
      if (box.tag != Tag::Array || as_array(box)->len != 1) return unwind_to(state, floor);
      array_push(state, captures.get(), box);
    }
    parent = as_function(state.frames[state.frames.len - 1].fn);
    descriptor = as_code(parent->code)->consts[index];
    nested = as_array(descriptor)->items[0];
    Value closure = make_compiled_function(state, nested, captures.get(), parent->nsName,
                                           as_code(nested)->name, false);
    if (closure.tag == Tag::Unwind) return unwind_to(state, floor);
    captures.set(closure);
    VM_DISPATCH();
  }
  VM_OP(ToMacro) {
    if (state.stack.len <= stackBase || state.stack[state.stack.len - 1].tag != Tag::Function)
      return unwind_to(state, floor);
    state.stack[state.stack.len - 1].obj->type = ObjType::Macro;
    state.stack[state.stack.len - 1].tag = Tag::Macro;
    VM_DISPATCH();
  }
  VM_OP(Call) {
    u16 argc = code_read_u16(ip);
    ip += 2;
    if (argc + 1u > state.stack.len - stackBase) return unwind_to(state, floor);
    u32 callBase = state.stack.len - argc - 1;
    Value callee = state.stack[callBase];
    VM_SAVE_IP();
    if (callee.tag == Tag::Macro) {
      (void)raise_error(state, "macro used as function");
      return unwind_to(state, floor);
    }
    if (compiled_function(callee)) {
      Value entered = enter_frame(state, callee, callBase, argc, false);
      if (entered.tag == Tag::Unwind) return unwind_to(state, floor);
      VM_LOAD_FRAME();
      VM_DISPATCH();
    }
    Value result = apply(state, callee, callBase + 1, argc);
    if (result.tag == Tag::Unwind) return unwind_to(state, floor);
    if (state.interruptFlag) {
      state.interruptFlag = false;
      start_quit(state);
      return unwind_to(state, floor);
    }
    state.popTo(callBase);
    state.push(result);
    VM_LOAD_FRAME();
    VM_DISPATCH();
  }
  VM_OP(TailCall) {
    u16 argc = code_read_u16(ip);
    ip += 2;
    if (argc + 1u > state.stack.len - stackBase) return unwind_to(state, floor);
    u32 source = state.stack.len - argc - 1;
    Value callee = state.stack[source];
    if (callee.tag == Tag::Macro) {
      VM_SAVE_IP();
      (void)raise_error(state, "macro used as function");
      return unwind_to(state, floor);
    }
    if (compiled_function(callee)) {
      u32 destination = state.frames[state.frames.len - 1].callBase;
      memmove(&state.stack[destination], &state.stack[source], (size_t)(argc + 1) * sizeof(Value));
      state.stack.len = destination + argc + 1;
      callee = state.stack[destination];
      Value entered = enter_frame(state, callee, destination, argc, true);
      if (entered.tag == Tag::Unwind) return unwind_to(state, floor);
      VM_LOAD_FRAME();
      VM_DISPATCH();
    }
    Value result = apply(state, callee, source + 1, argc);
    if (result.tag == Tag::Unwind) return unwind_to(state, floor);
    if (state.interruptFlag) {
      state.interruptFlag = false;
      start_quit(state);
      return unwind_to(state, floor);
    }
    CallFrame frame = state.frames.pop();
    if (frame.restoreNs) state.currentNs = frame.savedNs;
    state.popTo(frame.callBase);
    if (state.frames.len == floor) return result;
    state.push(result);
    VM_LOAD_FRAME();
    VM_DISPATCH();
  }
  VM_OP(Return) {
    if (state.stack.len <= stackBase) return unwind_to(state, floor);
    Value result = state.stack[state.stack.len - 1];
    CallFrame frame = state.frames.pop();
    if (frame.restoreNs) state.currentNs = frame.savedNs;
    state.popTo(frame.callBase);
    if (state.frames.len == floor) return result;
    state.push(result);
    VM_LOAD_FRAME();
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
    if (state.stack.len <= stackBase) return unwind_to(state, floor);
    Value test = state.stack.pop();
    if (is_falsy(test)) ip += offset;
    VM_DISPATCH();
  }
  VM_OP(JumpFalsePeek) {
    i32 offset = code_read_i32(ip);
    ip += 4;
    if (state.stack.len <= stackBase) return unwind_to(state, floor);
    if (is_falsy(state.stack[state.stack.len - 1])) ip += offset;
    VM_DISPATCH();
  }
  VM_OP(JumpTruePeek) {
    i32 offset = code_read_i32(ip);
    ip += 4;
    if (state.stack.len <= stackBase) return unwind_to(state, floor);
    if (is_truthy(state.stack[state.stack.len - 1])) ip += offset;
    VM_DISPATCH();
  }
  VM_OP(Loop) {
    i32 offset = code_read_i32(ip);
    ip += 4;
    if (state.interruptFlag) {
      state.interruptFlag = false;
      start_quit(state);
      return unwind_to(state, floor);
    }
    ip += offset;
    VM_DISPATCH();
  }
  VM_OP(Cons) {
    if (state.stack.len - stackBase < 2) return unwind_to(state, floor);
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
    if (count > state.stack.len - stackBase) return unwind_to(state, floor);
    u32 first = state.stack.len - count;
    Slot result{&state, state.push(null_v())};
    for (u32 i = count; i-- > 0;) result.set(make_pair(state, Slot{&state, first + i}, result));
    Value list = result.get();
    state.popTo(first);
    state.push(list);
    VM_DISPATCH();
  }
  VM_OP(Append2) {
    if (state.stack.len - stackBase < 2) return unwind_to(state, floor);
    u32 left = state.stack.len - 2;
    u32 elements = state.stack.len;
    Value cursor = state.stack[left];
    while (cursor.tag == Tag::Pair) {
      state.push(as_pair(cursor)->car);
      cursor = as_pair(cursor)->cdr;
    }
    if (cursor.tag != Tag::Null) {
      VM_SAVE_IP();
      (void)raise_error(state, "unquote-splicing: expected proper list");
      return unwind_to(state, floor);
    }
    Slot result{&state, left + 1};
    for (u32 i = state.stack.len; i-- > elements;)
      result.set(make_pair(state, Slot{&state, i}, result));
    Value value = result.get();
    state.popTo(left);
    state.push(value);
    VM_DISPATCH();
  }

  VM_OP(GetGlobal) {
    u16 index = code_read_u16(ip);
    ip += 2;
    FunctionData* fn = as_function(state.frames[state.frames.len - 1].fn);
    CodeData* code = as_code(fn->code);
    Value var = code->consts[index];
    if (var.tag == Tag::Symbol) {
      var = ns_resolve_var(state, var);
      if (is_nil(var)) {
        VM_SAVE_IP();
        raise_error_sym(state, "unresolved symbol: %.*s", code->consts[index].id);
        return unwind_to(state, floor);
      }
      code->consts[index] = var;
    }
    if (var.tag != Tag::Array || as_array(var)->len != VAR_SLOTS) return unwind_to(state, floor);
    state.push(var_value(var));
    VM_DISPATCH();
  }
  VM_OP(SetGlobal) {
    u16 index = code_read_u16(ip);
    ip += 2;
    if (state.stack.len <= stackBase) return unwind_to(state, floor);
    FunctionData* fn = as_function(state.frames[state.frames.len - 1].fn);
    CodeData* code = as_code(fn->code);
    Value var = code->consts[index];
    if (var.tag == Tag::Symbol) {
      var = ns_resolve_var(state, var);
      if (is_nil(var)) {
        VM_SAVE_IP();
        raise_error_sym(state, "set!: unbound %.*s", code->consts[index].id);
        return unwind_to(state, floor);
      }
      code->consts[index] = var;
    }
    if (var.tag != Tag::Array || as_array(var)->len != VAR_SLOTS) return unwind_to(state, floor);
    var_set(var, state.stack[state.stack.len - 1]);
    VM_DISPATCH();
  }
  VM_OP(DefGlobal) {
    u16 index = code_read_u16(ip);
    ip += 2;
    if (state.stack.len <= stackBase) return unwind_to(state, floor);
    FunctionData* fn = as_function(state.frames[state.frames.len - 1].fn);
    Value descriptor = as_code(fn->code)->consts[index];
    Value name = descriptor;
    Value doc = nil_v();
    bool isPrivate = false;
    if (descriptor.tag == Tag::Array) {
      ArrayData* data = as_array(descriptor);
      if (data->len != 3) return unwind_to(state, floor);
      name = data->items[0];
      isPrivate = is_truthy(data->items[1]);
      doc = data->items[2];
    }
    if (name.tag != Tag::Symbol) return unwind_to(state, floor);
    Value value = state.stack[state.stack.len - 1];
    if (value.tag == Tag::Function || value.tag == Tag::Macro) {
      FunctionData* definedFunction = as_function(value);
      if (definedFunction->name == 0) definedFunction->name = name.id;
      if (!is_nil(doc)) definedFunction->docstring = doc;
    }
    VM_SAVE_IP();
    Value defined = ns_define(state, name.id, value, isPrivate, doc);
    if (defined.tag == Tag::Unwind) return unwind_to(state, floor);
    VM_LOAD_FRAME();
    VM_DISPATCH();
  }

#ifndef OT_COMPUTED_GOTO
  default: return unwind_to(state, floor);
}
}
#endif

#undef VM_OP
#undef VM_DISPATCH
#undef VM_LOAD_FRAME
#undef VM_SAVE_IP
}

Value vm_call(State& state, Value callee, u32 base, u32 argc) {
  if (!compiled_function(callee)) return apply(state, callee, base, argc);
  u32 floor = state.frames.len;
  u32 callBase = state.stack.len;
  state.push(callee);
  for (u32 i = 0; i < argc; i++) state.push(state.stack[base + i]);
  Value entered = enter_frame(state, state.stack[callBase], callBase, argc, false);
  if (entered.tag == Tag::Unwind) {
    state.popTo(callBase);
    return entered;
  }
  return vm_execute(state, floor);
}

Value vm_execute_code(State& state, Value code) {
  Scope roots(state);
  Slot codeRoot = roots.push(code);
  if (codeRoot.get().tag != Tag::Code) return raise_error(state, "vm: expected code");
  Buf verifyError;
  if (!code_verify(codeRoot.get(), &verifyError)) {
    verifyError.push('\0');
    return raise_error(state, "vm: %s", verifyError.data);
  }
  Slot captures = roots.push(make_array(state, 0));
  Slot fn =
      roots.push(make_compiled_function(state, codeRoot.get(), captures.get(),
                                        symbol_v(state.currentNs), as_code(codeRoot.get())->name));
  if (fn.get().tag == Tag::Unwind) return fn.get();
  u32 floor = state.frames.len;
  u32 callBase = state.stack.len;
  state.push(fn.get());
  Value entered = enter_frame(state, state.stack[callBase], callBase, 0, false, false);
  if (entered.tag == Tag::Unwind) {
    state.popTo(callBase);
    return entered;
  }
  return vm_execute(state, floor);
}

}  // namespace ot
