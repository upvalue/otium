#include "vm.h"
#include "code.h"
#include "eval.h"
#include "heap.h"
#include "ns.h"
#include "state.h"

static bool compiled_function(Value value) {
  return (value.tag == Tag_Function || value.tag == Tag_Macro) &&
         as_function(value)->code.tag == Tag_Code;
}

Value make_compiled_function(State* vm, Value code, Value captures, Value nsName, u32 name,
                             bool macro) {
  OT_SCOPE(vm);
  Ref codeRoot = ref_push(vm, code);
  Ref capturesRoot = ref_push(vm, captures);
  Ref nsRoot = ref_push(vm, nsName);
  if (ref_get(vm, codeRoot).tag != Tag_Code)
    return raise_error(vm, "compiled function needs code");
  if (ref_get(vm, capturesRoot).tag != Tag_Array)
    return raise_error(vm, "compiled function captures must be an array");
  u32 count = as_array(ref_get(vm, capturesRoot))->len;
  if (count != as_code(ref_get(vm, codeRoot))->nupvals)
    return raise_error(vm, "compiled function capture count mismatch");
  if (count > (UINT32_MAX - (u32)sizeof(FunctionData)) / (u32)sizeof(Value))
    ot_fatal("compiled function: capture size overflow");
  u32 size = (u32)sizeof(FunctionData) + count * (u32)sizeof(Value);
  ObjType objectType = macro ? ObjType_Macro : ObjType_Function;
  Tag tag = macro ? Tag_Macro : Tag_Function;
  Obj* object = heap_alloc(&vm->heap, objectType, size);
  Value fnValue = obj_v(tag, object);
  FunctionData* fn = as_function(fnValue);
  fn->name = name;
  fn->code = ref_get(vm, codeRoot);
  fn->nsName = ref_get(vm, nsRoot);
  fn->native = nullptr;
  fn->docstring = nil_v();
  fn->nupvals = count;
  // Nothing allocates between here and the last write, so both interior
  // pointers stay valid for the copy.
  ArrayData* source = as_array(ref_get(vm, capturesRoot));
  for (u32 i = 0; i < count; i++) function_upvals(fn)[i] = source->items[i];
  return fnValue;
}

static Value enter_frame(State* vm, Value fnValue, u32 callBase, u32 argc, bool tail,
                         bool restoreNs) {
  FunctionData* fn = as_function(fnValue);
  CodeData* code = as_code(fn->code);
  u32 nfixed = code->nfixed;
  bool hasRest = code->hasRest != 0;
  u32 nlocals = code->nlocals;
  u32 maxStack = code->maxStack;
  u32 formalCount = nfixed + (hasRest ? 1u : 0u);
  if ((!hasRest && argc != nfixed) || (hasRest && argc < nfixed))
    return raise_error(vm, "call: wrong number of arguments (%u)", argc);
  if (nlocals < formalCount) return raise_error(vm, "vm: code has too few local slots");
  u64 required = (u64)callBase + 1u + nlocals + maxStack;
  if (required > vm->cfg.stackSlots) return raise_error(vm, "vm stack limit exceeded");
  if (!tail && vm->frames.len >= vm->cfg.maxDepth)
    return raise_error(vm, "recursion depth exceeded");

  if (tail) {
    CallFrame* frame = &vm->frames.data[vm->frames.len - 1];
    frame->fn = fnValue;
    frame->ip = 0;
    frame->callBase = callBase;
    frame->base = callBase + 1;
    frame->stackBase = frame->base + nlocals;
  } else {
    vec_push(&vm->frames, ((CallFrame){fnValue, 0, callBase, callBase + 1, callBase + 1 + nlocals,
                                       vm->currentNs, restoreNs}));
  }

  CallFrame* frame = &vm->frames.data[vm->frames.len - 1];
  if (hasRest) {
    u32 extra = argc - nfixed;
    Value list = list_from_stack(vm, frame->base + nfixed, extra);
    state_pop_to(vm, frame->base + nfixed);
    state_push(vm, list);
  } else {
    state_pop_to(vm, frame->base + argc);
  }
  while (vm->stack.len < frame->stackBase) state_push(vm, nil_v());
  vm->currentNs = as_function(frame->fn)->nsName.id;
  return nil_v();
}

static void load_frame(State* vm, const u8** bytes, const u8** ip, const u8** end, u32* stackBase) {
  CallFrame* frame = &vm->frames.data[vm->frames.len - 1];
  CodeData* code = as_code(as_function(frame->fn)->code);
  *bytes = code->bytes;
  *ip = code->bytes + frame->ip;
  *end = code->bytes + code->len;
  *stackBase = frame->stackBase;
}

// Boxed locals and upvalues are single-element Arrays. Returns the cell, or
// null if the value is not a well-formed box.
static Value* box_cell(Value box) {
  if (box.tag != Tag_Array || as_array(box)->len != 1) return nullptr;
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
static Value global_cell(State* vm, FunctionData* fn, u16 index) {
  Value var = as_code(fn->code)->consts[index];
  if (var.tag == Tag_Symbol) {
    var = ns_resolve_var(vm, var);
    if (is_nil(var)) return unwind_v();
    as_code(fn->code)->consts[index] = var;
  }
  if (var.tag != Tag_Array || as_array(var)->len != VAR_SLOTS) return nil_v();
  return var;
}

static Value unwind_to(State* vm, u32 floor) {
  while (vm->frames.len > floor) {
    CallFrame frame = vec_pop(&vm->frames);
    if (frame.restoreNs) vm->currentNs = frame.savedNs;
    state_pop_to(vm, frame.callBase);
  }
  return unwind_v();
}

Value vm_execute(State* vm, u32 floor) {
  if (vm->frames.len <= floor) return raise_error(vm, "vm: no frame to execute");
  const u8* bytes = nullptr;
  const u8* ip = nullptr;
  const u8* end = nullptr;
  u32 stackBase = 0;
  load_frame(vm, &bytes, &ip, &end, &stackBase);

#define VM_SAVE_IP()                                                                               \
  do {                                                                                             \
    vm->frames.data[vm->frames.len - 1].ip = (u32)(ip - bytes);                                    \
  } while (0)
#define VM_LOAD_FRAME() load_frame(vm, &bytes, &ip, &end, &stackBase)
#define VM_FRAME() vm->frames.data[vm->frames.len - 1]
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
    (void)raise_error(vm, __VA_ARGS__);                                                            \
    return unwind_to(vm, floor);                                                                   \
  } while (0)
#define VM_NEED(cond, ...)                                                                         \
  do {                                                                                             \
    if (!(cond)) VM_FAULT(__VA_ARGS__);                                                            \
  } while (0)
#define VM_PROPAGATE(value)                                                                        \
  do {                                                                                             \
    if ((value).tag == Tag_Unwind) return unwind_to(vm, floor);                                    \
  } while (0)
// Fault naming the unresolved symbol still sitting in the constant pool.
#define VM_FAULT_SYM(index, fmt)                                                                   \
  do {                                                                                             \
    VM_SAVE_IP();                                                                                  \
    raise_error_sym(vm, fmt, VM_CONSTS()[index].id);                                               \
    return unwind_to(vm, floor);                                                                   \
  } while (0)
#define VM_NEED_OPERANDS(n) VM_NEED(vm->stack.len - stackBase >= (u32)(n), "vm: operand underflow")
#define VM_NEED_LOCAL(index)                                                                       \
  VM_NEED((index) < VM_FRAME().stackBase - VM_FRAME().base, "vm: local index out of range")
#define VM_POLL_INTERRUPT()                                                                        \
  do {                                                                                             \
    if (vm->interruptFlag) {                                                                       \
      vm->interruptFlag = false;                                                                   \
      start_quit(vm);                                                                              \
      return unwind_to(vm, floor);                                                                 \
    }                                                                                              \
  } while (0)
// Pop the current frame and hand `result` back: return it when the frame we
// popped is the one this vm_execute was entered for, otherwise resume the
// caller with it on the stack. Deliberately a brace block, not do/while --
// VM_DISPATCH() expands to `continue` in the switch build.
#define VM_LEAVE_FRAME(result)                                                                     \
  {                                                                                                \
    Value leaving = (result);                                                                      \
    CallFrame frame = vec_pop(&vm->frames);                                                        \
    if (frame.restoreNs) vm->currentNs = frame.savedNs;                                            \
    state_pop_to(vm, frame.callBase);                                                              \
    if (vm->frames.len == floor) return leaving;                                                   \
    state_push(vm, leaving);                                                                       \
    VM_LOAD_FRAME();                                                                               \
    VM_DISPATCH();                                                                                 \
  }

#ifdef OT_COMPUTED_GOTO
  static void* labels[] = {
#define OT_LABEL(name, text, operand) &&op_##name,
      OT_OPCODE_LIST(OT_LABEL)
#undef OT_LABEL
  };
  static_assert(sizeof(labels) / sizeof(labels[0]) == (u32)Op_Count);
#define VM_DISPATCH()                                                                              \
  do {                                                                                             \
    if (ip >= end) return unwind_to(vm, floor);                                                    \
    u8 next = *ip++;                                                                               \
    if (next >= (u8)Op_Count) return unwind_to(vm, floor);                                         \
    goto* labels[next];                                                                            \
  } while (0)
#define VM_OP(name) op_##name:
  VM_DISPATCH();
#else
#define VM_DISPATCH() continue
#define VM_OP(name) case Op_##name:
  for (;;) {
    if (ip >= end) return unwind_to(vm, floor);
    Op instruction = (Op)*ip++;
    switch (instruction) {
#endif

  VM_OP(Const) {
    state_push(vm, VM_CONSTS()[VM_U16()]);
    VM_DISPATCH();
  }
  VM_OP(Nil) {
    state_push(vm, nil_v());
    VM_DISPATCH();
  }
  VM_OP(True) {
    state_push(vm, bool_v(true));
    VM_DISPATCH();
  }
  VM_OP(False) {
    state_push(vm, bool_v(false));
    VM_DISPATCH();
  }
  VM_OP(Null) {
    state_push(vm, null_v());
    VM_DISPATCH();
  }
  VM_OP(Int8) {
    state_push(vm, int_v((i8)*ip++));
    VM_DISPATCH();
  }
  VM_OP(Pop) {
    VM_NEED_OPERANDS(1);
    (void)vec_pop(&vm->stack);
    VM_DISPATCH();
  }
  VM_OP(GetLocal) {
    u16 index = VM_U16();
    VM_NEED_LOCAL(index);
    state_push(vm, vm->stack.data[VM_FRAME().base + index]);
    VM_DISPATCH();
  }
  VM_OP(SetLocal) {
    u16 index = VM_U16();
    VM_NEED_LOCAL(index);
    VM_NEED_OPERANDS(1);
    vm->stack.data[VM_FRAME().base + index] = vm->stack.data[vm->stack.len - 1];
    VM_DISPATCH();
  }
  VM_OP(GetBoxed) {
    u16 index = VM_U16();
    VM_NEED_LOCAL(index);
    Value* cell = box_cell(vm->stack.data[VM_FRAME().base + index]);
    VM_NEED(cell, "vm: local is not a box");
    state_push(vm, *cell);
    VM_DISPATCH();
  }
  VM_OP(SetBoxed) {
    u16 index = VM_U16();
    VM_NEED_LOCAL(index);
    VM_NEED_OPERANDS(1);
    Value* cell = box_cell(vm->stack.data[VM_FRAME().base + index]);
    VM_NEED(cell, "vm: local is not a box");
    *cell = vm->stack.data[vm->stack.len - 1];
    VM_DISPATCH();
  }
  VM_OP(MakeBox) {
    VM_NEED_OPERANDS(1);
    Ref value = {vm->stack.len - 1};
    Ref box = ref_push(vm, make_array(vm, 1));
    array_push(vm, ref_get(vm, box), ref_get(vm, value));
    ref_set(vm, value, ref_get(vm, box));
    (void)vec_pop(&vm->stack);
    VM_DISPATCH();
  }
  VM_OP(GetUpval) {
    u16 index = VM_U16();
    VM_NEED(index < VM_FN()->nupvals, "vm: upvalue index out of range");
    Value* cell = box_cell(function_upvals(VM_FN())[index]);
    VM_NEED(cell, "vm: upvalue is not a box");
    state_push(vm, *cell);
    VM_DISPATCH();
  }
  VM_OP(SetUpval) {
    u16 index = VM_U16();
    VM_NEED(index < VM_FN()->nupvals, "vm: upvalue index out of range");
    VM_NEED_OPERANDS(1);
    Value* cell = box_cell(function_upvals(VM_FN())[index]);
    VM_NEED(cell, "vm: upvalue is not a box");
    *cell = vm->stack.data[vm->stack.len - 1];
    VM_DISPATCH();
  }
  VM_OP(Closure) {
    u16 index = VM_U16();
    Value descriptor = VM_CONSTS()[index];
    VM_NEED(descriptor.tag == Tag_Array && as_array(descriptor)->len != 0,
            "vm: bad closure descriptor");
    Value nested = as_array(descriptor)->items[0];
    VM_NEED(nested.tag == Tag_Code && as_array(descriptor)->len - 1 == as_code(nested)->nupvals,
            "vm: bad closure descriptor");
    u32 captureCount = as_code(nested)->nupvals;
    Ref captures = {state_push(vm, make_array(vm, captureCount))};
    for (u32 i = 0; i < captureCount; i++) {
      // Re-derive through the frame: make_array above may have moved headers.
      FunctionData* parent = VM_FN();
      Value capture = as_array(as_code(parent->code)->consts[index])->items[i + 1];
      VM_NEED(capture.tag == Tag_Int, "vm: bad capture descriptor");
      Value box;
      if (capture.i >= 0) {
        VM_NEED((u64)capture.i < VM_FRAME().stackBase - VM_FRAME().base,
                "vm: capture index out of range");
        box = vm->stack.data[VM_FRAME().base + (u32)capture.i];
      } else {
        u64 upvalue = (u64)(-(capture.i + 1));
        VM_NEED(upvalue < parent->nupvals, "vm: capture index out of range");
        box = function_upvals(parent)[upvalue];
      }
      VM_NEED(box_cell(box), "vm: capture is not a box");
      array_push(vm, ref_get(vm, captures), box);
    }
    FunctionData* parent = VM_FN();
    nested = as_array(as_code(parent->code)->consts[index])->items[0];
    Value closure = make_compiled_function(vm, nested, ref_get(vm, captures), parent->nsName,
                                           as_code(nested)->name, false);
    VM_PROPAGATE(closure);
    ref_set(vm, captures, closure);
    VM_DISPATCH();
  }
  VM_OP(ToMacro) {
    VM_NEED_OPERANDS(1);
    VM_NEED(vm->stack.data[vm->stack.len - 1].tag == Tag_Function, "vm: to-macro needs a function");
    vm->stack.data[vm->stack.len - 1].obj->type = ObjType_Macro;
    vm->stack.data[vm->stack.len - 1].tag = Tag_Macro;
    VM_DISPATCH();
  }
  VM_OP(Call) {
    u16 argc = VM_U16();
    VM_NEED_OPERANDS(argc + 1u);
    u32 callBase = vm->stack.len - argc - 1;
    Value callee = vm->stack.data[callBase];
    VM_SAVE_IP();
    VM_NEED(callee.tag != Tag_Macro, "macro used as function");
    if (compiled_function(callee)) {
      Value entered = enter_frame(vm, callee, callBase, argc, false, true);
      VM_PROPAGATE(entered);
      VM_LOAD_FRAME();
      VM_DISPATCH();
    }
    Value result = apply(vm, callee, callBase + 1, argc);
    VM_PROPAGATE(result);
    VM_POLL_INTERRUPT();
    state_pop_to(vm, callBase);
    state_push(vm, result);
    VM_LOAD_FRAME();
    VM_DISPATCH();
  }
  VM_OP(TailCall) {
    u16 argc = VM_U16();
    VM_NEED_OPERANDS(argc + 1u);
    u32 source = vm->stack.len - argc - 1;
    Value callee = vm->stack.data[source];
    VM_SAVE_IP();
    VM_NEED(callee.tag != Tag_Macro, "macro used as function");
    if (compiled_function(callee)) {
      u32 destination = VM_FRAME().callBase;
      memmove(&vm->stack.data[destination], &vm->stack.data[source],
              (size_t)(argc + 1) * sizeof(Value));
      vm->stack.len = destination + argc + 1;
      Value entered = enter_frame(vm, vm->stack.data[destination], destination, argc, true, true);
      VM_PROPAGATE(entered);
      VM_POLL_INTERRUPT();
      VM_LOAD_FRAME();
      VM_DISPATCH();
    }
    Value result = apply(vm, callee, source + 1, argc);
    VM_PROPAGATE(result);
    VM_POLL_INTERRUPT();
    VM_LEAVE_FRAME(result);
  }
  VM_OP(Return) {
    VM_NEED_OPERANDS(1);
    VM_LEAVE_FRAME(vm->stack.data[vm->stack.len - 1]);
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
    if (is_falsy(vec_pop(&vm->stack))) ip += offset;
    VM_DISPATCH();
  }
  VM_OP(JumpFalsePeek) {
    i32 offset = code_read_i32(ip);
    ip += 4;
    VM_NEED_OPERANDS(1);
    if (is_falsy(vm->stack.data[vm->stack.len - 1])) ip += offset;
    VM_DISPATCH();
  }
  VM_OP(JumpTruePeek) {
    i32 offset = code_read_i32(ip);
    ip += 4;
    VM_NEED_OPERANDS(1);
    if (is_truthy(vm->stack.data[vm->stack.len - 1])) ip += offset;
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
    Value pair = make_pair(vm, vm->stack.data[vm->stack.len - 2],
                           vm->stack.data[vm->stack.len - 1]);
    vm->stack.len--;
    vm->stack.data[vm->stack.len - 1] = pair;
    VM_DISPATCH();
  }
  VM_OP(List) {
    u16 count = VM_U16();
    VM_NEED_OPERANDS(count);
    u32 first = vm->stack.len - count;
    Value list = list_from_stack(vm, first, count);
    state_pop_to(vm, first);
    state_push(vm, list);
    VM_DISPATCH();
  }
  VM_OP(Append2) {
    VM_NEED_OPERANDS(2);
    u32 left = vm->stack.len - 2;
    u32 elements = vm->stack.len;
    Value cursor = vm->stack.data[left];
    while (cursor.tag == Tag_Pair) {
      state_push(vm, as_pair(cursor)->car);
      cursor = as_pair(cursor)->cdr;
    }
    if (cursor.tag != Tag_Null) VM_FAULT("unquote-splicing: expected proper list");
    // Folds onto the right-hand operand at left+1, which is the tail this
    // splice appends in front of -- not onto the empty list.
    Value value =
        list_from_stack_onto(vm, elements, vm->stack.len - elements, vm->stack.data[left + 1]);
    state_pop_to(vm, left);
    state_push(vm, value);
    VM_DISPATCH();
  }

  VM_OP(GetGlobal) {
    u16 index = VM_U16();
    Value var = global_cell(vm, VM_FN(), index);
    if (var.tag == Tag_Unwind) VM_FAULT_SYM(index, "unresolved symbol: %.*s");
    VM_NEED(!is_nil(var), "vm: global constant is not a var");
    state_push(vm, var_value(var));
    VM_DISPATCH();
  }
  VM_OP(SetGlobal) {
    u16 index = VM_U16();
    VM_NEED_OPERANDS(1);
    Value var = global_cell(vm, VM_FN(), index);
    if (var.tag == Tag_Unwind) VM_FAULT_SYM(index, "set!: unbound %.*s");
    VM_NEED(!is_nil(var), "vm: global constant is not a var");
    var_set(var, vm->stack.data[vm->stack.len - 1]);
    VM_DISPATCH();
  }
  VM_OP(DefGlobal) {
    u16 index = VM_U16();
    VM_NEED_OPERANDS(1);
    Value descriptor = VM_CONSTS()[index];
    Value name = descriptor;
    Value doc = nil_v();
    bool isPrivate = false;
    if (descriptor.tag == Tag_Array) {
      ArrayData* data = as_array(descriptor);
      VM_NEED(data->len == 3, "vm: bad define descriptor");
      name = data->items[0];
      isPrivate = is_truthy(data->items[1]);
      doc = data->items[2];
    }
    VM_NEED(name.tag == Tag_Symbol, "vm: define name is not a symbol");
    Value value = vm->stack.data[vm->stack.len - 1];
    if (value.tag == Tag_Function || value.tag == Tag_Macro) {
      FunctionData* definedFunction = as_function(value);
      if (definedFunction->name == 0) definedFunction->name = name.id;
      if (!is_nil(doc)) definedFunction->docstring = doc;
    }
    VM_SAVE_IP();
    Value defined = ns_define(vm, name.id, value, isPrivate, doc);
    VM_PROPAGATE(defined);
    VM_LOAD_FRAME();
    VM_DISPATCH();
  }

#ifndef OT_COMPUTED_GOTO
  default: return unwind_to(vm, floor);
}
}
#endif

#undef VM_FAULT_SYM
#undef VM_LEAVE_FRAME
#undef VM_POLL_INTERRUPT
#undef VM_NEED_LOCAL
#undef VM_NEED_OPERANDS
#undef VM_NEED
#undef VM_PROPAGATE
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

Value vm_call(State* vm, Value callee, u32 base, u32 argc) {
  if (!compiled_function(callee)) return apply(vm, callee, base, argc);
  u32 floor = vm->frames.len;
  u32 callBase = vm->stack.len;
  state_push(vm, callee);
  for (u32 i = 0; i < argc; i++) state_push(vm, vm->stack.data[base + i]);
  Value entered = enter_frame(vm, vm->stack.data[callBase], callBase, argc, false, true);
  if (entered.tag == Tag_Unwind) {
    state_pop_to(vm, callBase);
    return entered;
  }
  return vm_execute(vm, floor);
}

Value vm_execute_code(State* vm, Value code) {
  OT_SCOPE(vm);
  Ref codeRoot = ref_push(vm, code);
  if (ref_get(vm, codeRoot).tag != Tag_Code) return raise_error(vm, "vm: expected code");
  Buf verifyError = {0};
  if (!code_verify(ref_get(vm, codeRoot), &verifyError)) {
    vec_push(&verifyError, '\0');
    Value raised = raise_error(vm, "vm: %s", verifyError.data);
    buf_deinit(&verifyError);
    return raised;
  }
  buf_deinit(&verifyError);
  Ref captures = ref_push(vm, make_array(vm, 0));
  Ref fn = ref_push(vm, make_compiled_function(vm, ref_get(vm, codeRoot), ref_get(vm, captures),
                                                  symbol_v(vm->currentNs),
                                               as_code(ref_get(vm, codeRoot))->name, false));
  if (ref_get(vm, fn).tag == Tag_Unwind) return unwind_v();
  u32 floor = vm->frames.len;
  u32 callBase = vm->stack.len;
  state_push(vm, ref_get(vm, fn));
  Value entered = enter_frame(vm, vm->stack.data[callBase], callBase, 0, false, false);
  if (entered.tag == Tag_Unwind) {
    state_pop_to(vm, callBase);
    return entered;
  }
  return vm_execute(vm, floor);
}
