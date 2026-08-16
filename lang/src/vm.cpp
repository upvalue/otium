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

// Boxed locals and upvalues are single-element Arrays. Returns the cell, or
// null if the value is not a well-formed box.
static Value* box_cell(Value box) {
  if (box.tag != Tag::Array || as_array(box)->len != 1) return nullptr;
  return &as_array(box)->items[0];
}

// Resolve a global-reference constant to its var cell, caching the resolution
// in the pool. The cell is mutated in place on redefinition rather than
// replaced, so previously compiled code keeps seeing the current value.
// Returns Unwind when the symbol does not resolve, nil when the constant is
// not a var at all.
//
// `fn` and the CodeData behind it are GC-heap pointers, so this relies on
// ns_resolve_var being allocation-free (it is: ns_lookup/table_get never touch
// the GC heap, and the raiseErr=false path cannot signal). If that ever
// changes, this write-back has to re-derive from the frame instead.
static Value global_cell(State& state, FunctionData* fn, u16 index) {
  Value var = as_code(fn->code)->consts[index];
  if (var.tag == Tag::Symbol) {
    var = ns_resolve_var(state, var);
    if (is_nil(var)) return unwind_v();
    as_code(fn->code)->consts[index] = var;
  }
  if (var.tag != Tag::Array || as_array(var)->len != VAR_SLOTS) return nil_v();
  return var;
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
#define VM_FRAME() state.frames[state.frames.len - 1]
#define VM_FN() as_function(VM_FRAME().fn)
#define VM_CONSTS() as_code(VM_FN()->code)->consts
// Read this instruction's u16 operand and step past it.
#define VM_U16() (ip += 2, code_read_u16(ip - 2))

// Every bytecode invariant the verifier is supposed to guarantee. Reaching one
// means the compiler emitted bad code: raise a real condition rather than
// unwinding blank, so the failure is catchable and reportable instead of
// surfacing as a bare nil.
#define VM_FAULT(...)                                                                              \
  do {                                                                                             \
    VM_SAVE_IP();                                                                                  \
    (void)raise_error(state, __VA_ARGS__);                                                         \
    return unwind_to(state, floor);                                                                \
  } while (0)
#define VM_NEED(cond, ...)                                                                         \
  do {                                                                                             \
    if (!(cond)) VM_FAULT(__VA_ARGS__);                                                            \
  } while (0)
// Fault naming the unresolved symbol still sitting in the constant pool.
#define VM_FAULT_SYM(index, fmt)                                                                   \
  do {                                                                                             \
    VM_SAVE_IP();                                                                                  \
    raise_error_sym(state, fmt, VM_CONSTS()[index].id);                                            \
    return unwind_to(state, floor);                                                                \
  } while (0)
#define VM_NEED_OPERANDS(n) VM_NEED(state.stack.len - stackBase >= (u32)(n), "vm: operand underflow")
#define VM_NEED_LOCAL(index)                                                                       \
  VM_NEED((index) < VM_FRAME().stackBase - VM_FRAME().base, "vm: local index out of range")
#define VM_POLL_INTERRUPT()                                                                        \
  do {                                                                                             \
    if (state.interruptFlag) {                                                                     \
      state.interruptFlag = false;                                                                 \
      start_quit(state);                                                                           \
      return unwind_to(state, floor);                                                              \
    }                                                                                              \
  } while (0)
// Pop the current frame and hand `result` back: return it when the frame we
// popped is the one this vm_execute was entered for, otherwise resume the
// caller with it on the stack. Deliberately a brace block, not do/while --
// VM_DISPATCH() expands to `continue` in the switch build.
#define VM_LEAVE_FRAME(result)                                                                     \
  {                                                                                                \
    Value leaving = (result);                                                                      \
    CallFrame frame = state.frames.pop();                                                          \
    if (frame.restoreNs) state.currentNs = frame.savedNs;                                          \
    state.popTo(frame.callBase);                                                                   \
    if (state.frames.len == floor) return leaving;                                                 \
    state.push(leaving);                                                                           \
    VM_LOAD_FRAME();                                                                               \
    VM_DISPATCH();                                                                                 \
  }

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
    VM_LEAVE_FRAME(state.stack.len > stackBase ? state.stack[state.stack.len - 1] : nil_v());
  }
  VM_OP(Const) {
    state.push(VM_CONSTS()[VM_U16()]);
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
    VM_NEED_OPERANDS(1);
    state.stack.pop();
    VM_DISPATCH();
  }
  VM_OP(PopNKeep1) {
    u16 count = VM_U16();
    VM_NEED_OPERANDS(count + 1u);
    Value top = state.stack[state.stack.len - 1];
    state.stack.len -= count;
    state.stack[state.stack.len - 1] = top;
    VM_DISPATCH();
  }
  VM_OP(GetLocal) {
    u16 index = VM_U16();
    VM_NEED_LOCAL(index);
    state.push(state.stack[VM_FRAME().base + index]);
    VM_DISPATCH();
  }
  VM_OP(SetLocal) {
    u16 index = VM_U16();
    VM_NEED_LOCAL(index);
    VM_NEED_OPERANDS(1);
    state.stack[VM_FRAME().base + index] = state.stack[state.stack.len - 1];
    VM_DISPATCH();
  }
  VM_OP(GetBoxed) {
    u16 index = VM_U16();
    VM_NEED_LOCAL(index);
    Value* cell = box_cell(state.stack[VM_FRAME().base + index]);
    VM_NEED(cell, "vm: local is not a box");
    state.push(*cell);
    VM_DISPATCH();
  }
  VM_OP(SetBoxed) {
    u16 index = VM_U16();
    VM_NEED_LOCAL(index);
    VM_NEED_OPERANDS(1);
    Value* cell = box_cell(state.stack[VM_FRAME().base + index]);
    VM_NEED(cell, "vm: local is not a box");
    *cell = state.stack[state.stack.len - 1];
    VM_DISPATCH();
  }
  VM_OP(MakeBox) {
    VM_NEED_OPERANDS(1);
    Slot value{&state, state.stack.len - 1};
    Value box = make_array(state, 1);
    array_push(state, box, value.get());
    value.set(box);
    VM_DISPATCH();
  }
  VM_OP(GetUpval) {
    u16 index = VM_U16();
    VM_NEED(index < VM_FN()->nupvals, "vm: upvalue index out of range");
    Value* cell = box_cell(function_upvals(VM_FN())[index]);
    VM_NEED(cell, "vm: upvalue is not a box");
    state.push(*cell);
    VM_DISPATCH();
  }
  VM_OP(SetUpval) {
    u16 index = VM_U16();
    VM_NEED(index < VM_FN()->nupvals, "vm: upvalue index out of range");
    VM_NEED_OPERANDS(1);
    Value* cell = box_cell(function_upvals(VM_FN())[index]);
    VM_NEED(cell, "vm: upvalue is not a box");
    *cell = state.stack[state.stack.len - 1];
    VM_DISPATCH();
  }
  VM_OP(Closure) {
    u16 index = VM_U16();
    Value descriptor = VM_CONSTS()[index];
    VM_NEED(descriptor.tag == Tag::Array && as_array(descriptor)->len != 0,
            "vm: bad closure descriptor");
    Value nested = as_array(descriptor)->items[0];
    VM_NEED(nested.tag == Tag::Code && as_array(descriptor)->len - 1 == as_code(nested)->nupvals,
            "vm: bad closure descriptor");
    u32 captureCount = as_code(nested)->nupvals;
    Slot captures{&state, state.push(make_array(state, captureCount))};
    for (u32 i = 0; i < captureCount; i++) {
      // Re-derive through the frame: make_array above may have moved headers.
      FunctionData* parent = VM_FN();
      Value capture = as_array(as_code(parent->code)->consts[index])->items[i + 1];
      VM_NEED(capture.tag == Tag::Int, "vm: bad capture descriptor");
      Value box;
      if (capture.i >= 0) {
        VM_NEED((u64)capture.i < VM_FRAME().stackBase - VM_FRAME().base,
                "vm: capture index out of range");
        box = state.stack[VM_FRAME().base + (u32)capture.i];
      } else {
        u64 upvalue = (u64)(-(capture.i + 1));
        VM_NEED(upvalue < parent->nupvals, "vm: capture index out of range");
        box = function_upvals(parent)[upvalue];
      }
      VM_NEED(box_cell(box), "vm: capture is not a box");
      array_push(state, captures.get(), box);
    }
    FunctionData* parent = VM_FN();
    nested = as_array(as_code(parent->code)->consts[index])->items[0];
    Value closure = make_compiled_function(state, nested, captures.get(), parent->nsName,
                                           as_code(nested)->name, false);
    if (closure.tag == Tag::Unwind) return unwind_to(state, floor);
    captures.set(closure);
    VM_DISPATCH();
  }
  VM_OP(ToMacro) {
    VM_NEED_OPERANDS(1);
    VM_NEED(state.stack[state.stack.len - 1].tag == Tag::Function, "vm: to-macro needs a function");
    state.stack[state.stack.len - 1].obj->type = ObjType::Macro;
    state.stack[state.stack.len - 1].tag = Tag::Macro;
    VM_DISPATCH();
  }
  VM_OP(Call) {
    u16 argc = VM_U16();
    VM_NEED_OPERANDS(argc + 1u);
    u32 callBase = state.stack.len - argc - 1;
    Value callee = state.stack[callBase];
    VM_SAVE_IP();
    VM_NEED(callee.tag != Tag::Macro, "macro used as function");
    if (compiled_function(callee)) {
      Value entered = enter_frame(state, callee, callBase, argc, false);
      if (entered.tag == Tag::Unwind) return unwind_to(state, floor);
      VM_LOAD_FRAME();
      VM_DISPATCH();
    }
    Value result = apply(state, callee, callBase + 1, argc);
    if (result.tag == Tag::Unwind) return unwind_to(state, floor);
    VM_POLL_INTERRUPT();
    state.popTo(callBase);
    state.push(result);
    VM_LOAD_FRAME();
    VM_DISPATCH();
  }
  VM_OP(TailCall) {
    u16 argc = VM_U16();
    VM_NEED_OPERANDS(argc + 1u);
    u32 source = state.stack.len - argc - 1;
    Value callee = state.stack[source];
    VM_SAVE_IP();
    VM_NEED(callee.tag != Tag::Macro, "macro used as function");
    if (compiled_function(callee)) {
      u32 destination = VM_FRAME().callBase;
      memmove(&state.stack[destination], &state.stack[source], (size_t)(argc + 1) * sizeof(Value));
      state.stack.len = destination + argc + 1;
      Value entered = enter_frame(state, state.stack[destination], destination, argc, true);
      if (entered.tag == Tag::Unwind) return unwind_to(state, floor);
      VM_POLL_INTERRUPT();
      VM_LOAD_FRAME();
      VM_DISPATCH();
    }
    Value result = apply(state, callee, source + 1, argc);
    if (result.tag == Tag::Unwind) return unwind_to(state, floor);
    VM_POLL_INTERRUPT();
    VM_LEAVE_FRAME(result);
  }
  VM_OP(Return) {
    VM_NEED_OPERANDS(1);
    VM_LEAVE_FRAME(state.stack[state.stack.len - 1]);
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
    VM_NEED_OPERANDS(1);
    if (is_falsy(state.stack.pop())) ip += offset;
    VM_DISPATCH();
  }
  VM_OP(JumpFalsePeek) {
    i32 offset = code_read_i32(ip);
    ip += 4;
    VM_NEED_OPERANDS(1);
    if (is_falsy(state.stack[state.stack.len - 1])) ip += offset;
    VM_DISPATCH();
  }
  VM_OP(JumpTruePeek) {
    i32 offset = code_read_i32(ip);
    ip += 4;
    VM_NEED_OPERANDS(1);
    if (is_truthy(state.stack[state.stack.len - 1])) ip += offset;
    VM_DISPATCH();
  }
  VM_OP(Loop) {
    i32 offset = code_read_i32(ip);
    ip += 4;
    VM_POLL_INTERRUPT();
    ip += offset;
    VM_DISPATCH();
  }
  VM_OP(Cons) {
    VM_NEED_OPERANDS(2);
    Slot car{&state, state.stack.len - 2};
    Slot cdr{&state, state.stack.len - 1};
    Value pair = make_pair(state, car, cdr);
    state.stack.len--;
    state.stack[state.stack.len - 1] = pair;
    VM_DISPATCH();
  }
  VM_OP(List) {
    u16 count = VM_U16();
    VM_NEED_OPERANDS(count);
    u32 first = state.stack.len - count;
    Slot result{&state, state.push(null_v())};
    for (u32 i = count; i-- > 0;) result.set(make_pair(state, Slot{&state, first + i}, result));
    Value list = result.get();
    state.popTo(first);
    state.push(list);
    VM_DISPATCH();
  }
  VM_OP(Append2) {
    VM_NEED_OPERANDS(2);
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
    u16 index = VM_U16();
    Value var = global_cell(state, VM_FN(), index);
    if (var.tag == Tag::Unwind) VM_FAULT_SYM(index, "unresolved symbol: %.*s");
    VM_NEED(!is_nil(var), "vm: global constant is not a var");
    state.push(var_value(var));
    VM_DISPATCH();
  }
  VM_OP(SetGlobal) {
    u16 index = VM_U16();
    VM_NEED_OPERANDS(1);
    Value var = global_cell(state, VM_FN(), index);
    if (var.tag == Tag::Unwind) VM_FAULT_SYM(index, "set!: unbound %.*s");
    VM_NEED(!is_nil(var), "vm: global constant is not a var");
    var_set(var, state.stack[state.stack.len - 1]);
    VM_DISPATCH();
  }
  VM_OP(DefGlobal) {
    u16 index = VM_U16();
    VM_NEED_OPERANDS(1);
    Value descriptor = VM_CONSTS()[index];
    Value name = descriptor;
    Value doc = nil_v();
    bool isPrivate = false;
    if (descriptor.tag == Tag::Array) {
      ArrayData* data = as_array(descriptor);
      VM_NEED(data->len == 3, "vm: bad define descriptor");
      name = data->items[0];
      isPrivate = is_truthy(data->items[1]);
      doc = data->items[2];
    }
    VM_NEED(name.tag == Tag::Symbol, "vm: define name is not a symbol");
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

#undef VM_FAULT_SYM
#undef VM_LEAVE_FRAME
#undef VM_POLL_INTERRUPT
#undef VM_NEED_LOCAL
#undef VM_NEED_OPERANDS
#undef VM_NEED
#undef VM_FAULT
#undef VM_U16
#undef VM_CONSTS
#undef VM_FN
#undef VM_FRAME
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
