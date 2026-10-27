#include "internal.h"
#include <string.h>

/* GCC and Clang dispatch through a label table; other compilers, or builds with
 * OTIUM_VM_SWITCH, use a switch. Handlers are written once for both. */
#if defined(__GNUC__) && !defined(OTIUM_VM_SWITCH)
#define VM_THREADED 1
#else
#define VM_THREADED 0
#endif

#if defined(__GNUC__)
#define VM_LIKELY(x) __builtin_expect(!!(x), 1)
#define VM_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define VM_LIKELY(x) (x)
#define VM_UNLIKELY(x) (x)
#endif

#define VM_APPLY (((ot_value)B_APPLY << 8) | TAG_BUILTIN)

static void vm_height(ot_state* rt, size_t sp) { rt->sp = rt->stack_roots.count = sp; }

/* Make stack[from, to) valid before a frame takes it without writing it. */
static inline void vm_fresh(ot_state* rt, ot_value* stack, size_t from, size_t to) {
  size_t stale = rt->stack_stale;
  if (to <= stale) return;
  for (size_t i = from > stale ? from : stale; i < to; i++) stack[i] = V_UNBOUND;
  rt->stack_stale = to;
}

/* A frame's top covers its registers and everything live below its entry. */
static size_t vm_top(const oti_frame* frame) {
  size_t top = frame->base + 1 + frame->fn->locals;
  return top > frame->floor ? top : frame->floor;
}

static bool vm_primitive(ot_value value) {
  return (value & 255) == TAG_BUILTIN || oti_kind(value, K_FOREIGN_FUNCTION) ||
         oti_kind(value, K_PARAM) || oti_kind(value, K_INSTANCE) || oti_kind(value, K_RECORD) ||
         oti_collection(value);
}

static ot_value vm_call_primitive(ot_state* rt, size_t call, size_t argc) {
  ot_value value = rt->stack[call];
  if (oti_kind(value, K_RECORD)) return oti_record_call(rt, value, call + 1, argc);
  if (oti_kind(value, K_INSTANCE)) return oti_instance_call(rt, value, call + 1, argc);
  if (oti_kind(value, K_PARAM)) return oti_param_call(rt, value, argc);
  if (oti_collection(value)) return oti_collection_call(rt, call, call + 1, argc);
  if (oti_kind(value, K_FOREIGN_FUNCTION)) return oti_foreign_call(rt, call, call + 1, argc);
  return oti_builtin(rt, (unsigned)(value >> 8), call + 1, argc);
}

/* Enter the closure in stack[call] with arguments above it. A tail call
 * replaces the current frame and keeps its floor. */
bool oti_enter(ot_state* rt, size_t call, size_t argc, bool tail) {
  ot_value closure = rt->stack[call];
  if (oti_kind(closure, K_MACRO)) {
    closure = object_fields(closure)[0];
    rt->stack[call] = closure;
  }
  if (oti_kind(closure, K_GENERIC) || oti_kind(closure, K_NEXT)) {
    if (!oti_generic_prepare(rt, call, &argc)) return false;
    closure = rt->stack[call];
  }
  if (!oti_kind(closure, K_CLOSURE)) return oti_coded_error(rt, "E0302", "value is not callable");
  uint32_t id;
  memcpy(&id, object_bytes(closure), sizeof(id));
  oti_function* fn = rt->functions[id];
  if (argc < fn->arity || (!fn->rest && argc > fn->arity + fn->optional))
    return oti_coded_error(rt, "E0301", "wrong number of arguments");
  size_t base = tail ? rt->frames[rt->frame_count - 1].base : call;
  size_t floor = tail ? rt->frames[rt->frame_count - 1].floor : rt->sp;
  if (fn->locals >= rt->stack_limit || base >= rt->stack_limit - fn->locals - 1)
    return oti_error(rt, "VM stack limit exceeded");
  size_t top = base + 1 + fn->locals;
  if (top > rt->stack_capacity && !oti_stack(rt, top)) return false;
  if (!tail && rt->frame_count == rt->frame_capacity &&
      !oti_grow(rt, (void**)&rt->frames, &rt->frame_capacity, rt->frame_count + 1,
                sizeof(oti_frame)))
    return false;
  ot_value rest = OT_EMPTY;
  if (fn->rest && argc > fn->arity) {
    oti_roots root;
    oti_roots_push(&rt->heap, &root, &rest, 1);
    for (size_t i = argc; i > fn->arity; i--) {
      ot_value pair = oti_alloc(rt, K_PAIR, 2, 0);
      if (!pair) {
        oti_roots_pop(&rt->heap, &root);
        return false;
      }
      object_fields(pair)[0] = rt->stack[call + i];
      object_fields(pair)[1] = rest;
      rest = pair;
    }
    oti_roots_pop(&rt->heap, &root);
    closure = rt->stack[call];
  }
  size_t supplied = fn->rest && argc > fn->arity ? fn->arity : argc;
  if (base != call)
    memmove(rt->stack + base + 1, rt->stack + call + 1, supplied * sizeof(ot_value));
  rt->stack[base] = closure;
  for (size_t i = supplied; i < fn->locals; i++) rt->stack[base + 1 + i] = V_UNBOUND;
  if (fn->rest) rt->stack[base + 1 + fn->arity] = rest;
  vm_height(rt, top > floor ? top : floor);
  if (!tail) rt->frame_count++;
  rt->frames[rt->frame_count - 1] = (oti_frame){fn, fn->code, base, floor};
  if (rt->frame_count > rt->max_frames) rt->max_frames = rt->frame_count;
  return true;
}

bool oti_leave(ot_state* rt, ot_value result) {
  oti_frame frame = rt->frames[--rt->frame_count];
  /* The returning frame already owns this slot; no allocation is needed. */
  rt->stack[frame.base] = result;
  vm_height(rt, frame.floor > frame.base ? frame.floor : frame.base + 1);
  return true;
}

bool oti_apply(ot_state* rt, size_t call, size_t argc, bool tail) {
  while (rt->stack[call] == VM_APPLY) {
    if (argc < 2) return oti_error(rt, "apply needs a callable and a final argument sequence");
    ot_value list = rt->stack[call + argc];
    bool array = oti_kind(list, K_ARRAY);
    ot_value cursor = array || list == OT_NIL ? OT_EMPTY : list, slow = cursor;
    size_t length = array ? oti_as_array(list)->length : 0, prefix = argc - 2;
    while (oti_kind(cursor, K_PAIR)) {
      if (length >= rt->stack_limit) return oti_error(rt, "VM stack limit exceeded");
      length++;
      cursor = object_fields(cursor)[1];
      if (!(length & 1)) slow = object_fields(slow)[1];
      if (cursor != OT_EMPTY && cursor == slow)
        return oti_error(rt, "apply needs an acyclic proper list");
    }
    if (cursor != OT_EMPTY) return oti_error(rt, "apply needs a proper list");
    if (length > rt->stack_limit || prefix > rt->stack_limit - length ||
        call >= rt->stack_limit - prefix - length)
      return oti_error(rt, "VM stack limit exceeded");
    size_t count = prefix + length;
    if (call + 1 + count > rt->stack_capacity && !oti_stack(rt, call + 1 + count)) return false;
    memmove(rt->stack + call, rt->stack + call + 1, (prefix + 1) * sizeof(ot_value));
    if (array && length) {
      memcpy(rt->stack + call + 1 + prefix, object_fields(object_fields(list)[0]),
             length * sizeof(ot_value));
    } else
      for (size_t i = 0; i < length; i++) {
        rt->stack[call + 1 + prefix + i] = object_fields(list)[0];
        list = object_fields(list)[1];
      }
    argc = count;
    /* Spread arguments may pass the caller's top; the stack never shrinks
     * below a live frame. */
    if (call + 1 + argc > rt->sp) vm_height(rt, call + 1 + argc);
  }
  ot_value target = rt->stack[call];
  if (vm_primitive(target)) {
    ot_value answer = vm_call_primitive(rt, call, argc);
    if (!answer || oti_failed(rt)) return false;
    if (tail) return oti_leave(rt, answer);
    rt->stack[call] = answer;
    return true;
  }
  return oti_enter(rt, call, argc, tail);
}

bool oti_execute(ot_state* rt, uint32_t function, ot_value* result) {
  ot_value closure = oti_alloc(rt, K_CLOSURE, 0, sizeof(function));
  if (!closure) return false;
  memcpy(object_bytes(closure), &function, sizeof(function));
  *result = oti_call_list(rt, closure, OT_EMPTY);
  return *result != 0;
}

bool oti_invoke(ot_state* rt, size_t argc, ot_value* result) {
  ot_value callable = rt->stack[0];
  if (callable == VM_APPLY) {
    if (!oti_apply(rt, 0, argc, false)) return false;
  } else if (vm_primitive(callable)) {
    ot_value answer = vm_call_primitive(rt, 0, argc);
    if (!answer) return false;
    rt->stack[0] = answer;
    vm_height(rt, 1);
    *result = answer;
    return true;
  } else if (!oti_enter(rt, 0, argc, false)) return false;
  if (!oti_drive(rt, 0)) return false;
  *result = rt->stack[0];
  return true;
}

/* Call a builtin with operands copied above the frame, as the slow path of an
 * inline primitive. The caller reloads its register pointer afterwards. */
static ot_value vm_builtin(ot_state* rt, unsigned id, ot_value a, ot_value b, size_t count) {
  size_t first = rt->sp;
  if (!oti_stack(rt, first + 2)) return 0;
  rt->stack[first] = a;
  rt->stack[first + 1] = b;
  vm_height(rt, first + count);
  ot_value result = oti_builtin(rt, id, first, count);
  vm_height(rt, first);
  return result && !oti_failed(rt) ? result : 0;
}

static bool vm_float(ot_value v, double* out) {
  if (!oti_kind(v, K_FLOAT)) return false;
  memcpy(out, object_bytes(v), sizeof(*out));
  return true;
}

static bool vm_install_try(ot_state* rt, size_t target, size_t slot) {
  if (!oti_grow(rt, (void**)&rt->tries, &rt->try_capacity, rt->try_count + 1,
                sizeof(oti_try_frame)))
    return false;
  rt->tries[rt->try_count++] = (oti_try_frame){rt->frame_count,
                                               rt->sp,
                                               target,
                                               rt->param_count,
                                               rt->handler_count,
                                               rt->handler_top,
                                               rt->restart_scope_count,
                                               rt->cleanup_count,
                                               slot};
  return true;
}

static bool vm_unbound_global(ot_state* rt, uint32_t index) {
  return oti_coded_errorf(rt, "E0201", "unbound global: %s", rt->global_info[index].name);
}

/* True, with an error, when index is the cell of a builtin the compiler turns
 * into instructions. Those cells are immutable, so compiled code never needs
 * to check them. */
static bool vm_inlined_builtin(ot_state* rt, uint32_t index) {
  ot_value old = rt->globals[index];
  if ((old & 255) != TAG_BUILTIN) return false;
  unsigned id = (unsigned)(old >> 8);
  switch (id) {
    case B_ADD:
    case B_SUB:
    case B_MUL:
    case B_LT:
    case B_LE:
    case B_GT:
    case B_GE:
    case B_NUMEQ:
    case B_EQ:
    case B_CONS:
    case B_CAR:
    case B_CDR:
    case B_NOT:
    case B_NULL:
    case B_PAIR: break;
    default: return false;
  }
  if (rt->builtin_cells[id] != index) return false;
  oti_errorf(rt, "cannot redefine builtin: %s", rt->global_info[index].name);
  return true;
}

static bool vm_closure(ot_state* rt, ot_value* registers, uint32_t id, ot_value* out) {
  oti_function* child = rt->functions[id];
  ot_value v = oti_alloc(rt, K_CLOSURE, (uint32_t)child->capture_count, sizeof(uint32_t));
  if (!v) return false;
  memcpy(object_bytes(v), &id, sizeof(id));
  for (size_t i = 0; i < child->capture_count; i++) {
    oti_capture capture = child->captures[i];
    object_fields(v)[i] =
        capture.local ? registers[capture.index] : object_fields(registers[0])[capture.index];
  }
  *out = v;
  return true;
}

/* order is -1, 0 or 1; 3 means unordered (NaN) and satisfies nothing. */
static bool vm_compare(int order, unsigned id) {
  if (order == 3) return false;
  switch (id) {
    case B_LT: return order < 0;
    case B_LE: return order <= 0;
    case B_GT: return order > 0;
    case B_GE: return order >= 0;
    default: return order == 0;
  }
}

/* Builtins behind opcodes with a builtin slow path. */
static unsigned vm_builtin_id(unsigned op) {
  switch (op) {
    case OP_ADD:
    case OP_ADDI: return B_ADD;
    case OP_SUB: return B_SUB;
    case OP_MUL: return B_MUL;
    case OP_LT:
    case OP_BR_LT:
    case OP_BR_LTI: return B_LT;
    case OP_LE:
    case OP_BR_LE:
    case OP_BR_LEI: return B_LE;
    case OP_GT:
    case OP_BR_GT:
    case OP_BR_GTI: return B_GT;
    case OP_GE:
    case OP_BR_GE:
    case OP_BR_GEI: return B_GE;
    case OP_NUMEQ:
    case OP_BR_NUMEQ:
    case OP_BR_NUMEQI: return B_NUMEQ;
    case OP_CAR: return B_CAR;
    default: return B_CDR;
  }
}

static bool dispatch(ot_state* rt, size_t stop_frames) {
  /* A failed transfer (such as a restart applied to bad arguments) leaves its
   * error pending for the unwinding loop. */
  if (rt->frame_count <= stop_frames || oti_failed(rt)) return !oti_failed(rt);
  oti_frame* frame;
  oti_function* fn;
  const oti_instruction *code, *pc;
  ot_value* R;
  size_t base;
  /* The running instruction. Handlers read its fields where they need them,
   * so dispatch decodes only the opcode. */
  const oti_instruction* in;
#define ins (*in)
  ot_value v;
  size_t argc;

#define LOAD_FRAME()                                                                               \
  do {                                                                                             \
    frame = &rt->frames[rt->frame_count - 1];                                                      \
    fn = frame->fn;                                                                                \
    code = fn->code;                                                                               \
    pc = frame->pc;                                                                                \
    base = frame->base;                                                                            \
    R = rt->stack + base;                                                                          \
  } while (0)
#define SAVE_IP() (frame->pc = pc)
  /* Resume the top frame after a call out to C, which may have moved the stack. */
#define RELOAD()                                                                                   \
  do {                                                                                             \
    LOAD_FRAME();                                                                                  \
    vm_height(rt, vm_top(frame));                                                                  \
  } while (0)
#define FAIL()                                                                                     \
  do {                                                                                             \
    SAVE_IP();                                                                                     \
    return false;                                                                                  \
  } while (0)
#define SAFEPOINT()                                                                                \
  do {                                                                                             \
    if (VM_UNLIKELY(rt->interrupt_flag && *rt->interrupt_flag)) {                                  \
      SAVE_IP();                                                                                   \
      if (!ot_poll_interrupt(rt)) return false;                                                    \
    }                                                                                              \
  } while (0)
#define TARGET(word) (code + oti_instruction_bc(word))
  /* Loops are made of backward jumps, so those check for interrupts. */
#define JUMP_TO(target)                                                                            \
  do {                                                                                             \
    const oti_instruction* target_ = (target);                                                     \
    if (target_ < pc) {                                                                            \
      pc = target_;                                                                                \
      SAFEPOINT();                                                                                 \
    } else pc = target_;                                                                           \
  } while (0)

#if VM_THREADED
  static const void* const labels[OP_COUNT] = {
      [OP_MOVE] = &&L_OP_MOVE,
      [OP_IMM] = &&L_OP_IMM,
      [OP_CONST] = &&L_OP_CONST,
      [OP_GLOBAL] = &&L_OP_GLOBAL,
      [OP_SET_GLOBAL] = &&L_OP_SET_GLOBAL,
      [OP_DEFINE_GLOBAL] = &&L_OP_DEFINE_GLOBAL,
      [OP_UPVALUE] = &&L_OP_UPVALUE,
      [OP_UPVALUE_BOX] = &&L_OP_UPVALUE_BOX,
      [OP_SET_UPVALUE] = &&L_OP_SET_UPVALUE,
      [OP_BOX] = &&L_OP_BOX,
      [OP_UNBOX] = &&L_OP_UNBOX,
      [OP_SET_BOX] = &&L_OP_SET_BOX,
      [OP_CHECK] = &&L_OP_CHECK,
      [OP_CLEAR] = &&L_OP_CLEAR,
      [OP_CLOSURE] = &&L_OP_CLOSURE,
      [OP_JUMP] = &&L_OP_JUMP,
      [OP_JFALSE] = &&L_OP_JFALSE,
      [OP_JTRUE] = &&L_OP_JTRUE,
      [OP_JBOUND] = &&L_OP_JBOUND,
      [OP_CALL] = &&L_OP_CALL,
      [OP_TAIL_CALL] = &&L_OP_TAIL_CALL,
      [OP_CALL_GLOBAL] = &&L_OP_CALL_GLOBAL,
      [OP_TAIL_CALL_GLOBAL] = &&L_OP_TAIL_CALL_GLOBAL,
      [OP_RETURN] = &&L_OP_RETURN,
      [OP_RETURN_IMM] = &&L_OP_RETURN_IMM,
      [OP_TRY] = &&L_OP_TRY,
      [OP_END_TRY] = &&L_OP_END_TRY,
      [OP_RAISE] = &&L_OP_RAISE,
      [OP_CLEANUP] = &&L_OP_CLEANUP,
      [OP_RUN_CLEANUP] = &&L_OP_RUN_CLEANUP,
      [OP_RESTARTS] = &&L_OP_RESTARTS,
      [OP_RESULT] = &&L_OP_RESULT,
      [OP_END_RESTARTS] = &&L_OP_END_RESTARTS,
      [OP_BIND_HANDLERS] = &&L_OP_BIND_HANDLERS,
      [OP_UNBIND_HANDLERS] = &&L_OP_UNBIND_HANDLERS,
      [OP_MAKE_PARAM] = &&L_OP_MAKE_PARAM,
      [OP_BIND_PARAM] = &&L_OP_BIND_PARAM,
      [OP_UNBIND_PARAMS] = &&L_OP_UNBIND_PARAMS,
      [OP_ADD] = &&L_OP_ADD,
      [OP_SUB] = &&L_OP_SUB,
      [OP_MUL] = &&L_OP_MUL,
      [OP_ADDI] = &&L_OP_ADDI,
      [OP_LT] = &&L_OP_LT,
      [OP_LE] = &&L_OP_LE,
      [OP_GT] = &&L_OP_GT,
      [OP_GE] = &&L_OP_GE,
      [OP_NUMEQ] = &&L_OP_NUMEQ,
      [OP_EQ] = &&L_OP_EQ,
      [OP_CONS] = &&L_OP_CONS,
      [OP_CAR] = &&L_OP_CAR,
      [OP_CDR] = &&L_OP_CDR,
      [OP_NOT] = &&L_OP_NOT,
      [OP_NULLP] = &&L_OP_NULLP,
      [OP_PAIRP] = &&L_OP_PAIRP,
      [OP_BR_LT] = &&L_OP_BR_LT,
      [OP_BR_LE] = &&L_OP_BR_LE,
      [OP_BR_GT] = &&L_OP_BR_GT,
      [OP_BR_GE] = &&L_OP_BR_GE,
      [OP_BR_NUMEQ] = &&L_OP_BR_NUMEQ,
      [OP_BR_EQ] = &&L_OP_BR_EQ,
      [OP_BR_LTI] = &&L_OP_BR_LTI,
      [OP_BR_LEI] = &&L_OP_BR_LEI,
      [OP_BR_GTI] = &&L_OP_BR_GTI,
      [OP_BR_GEI] = &&L_OP_BR_GEI,
      [OP_BR_NUMEQI] = &&L_OP_BR_NUMEQI,
      [OP_BR_NULL] = &&L_OP_BR_NULL,
      [OP_BR_PAIR] = &&L_OP_BR_PAIR,
      [OP_BR_EQK] = &&L_OP_BR_EQK,
  };
#define CASE(op) L_##op:
#define NEXT()                                                                                     \
  do {                                                                                             \
    in = pc++;                                                                                     \
    goto* labels[in->op];                                                                          \
  } while (0)
#else
#define CASE(op) case op:
#define NEXT() goto next
#endif

  RELOAD();
#if VM_THREADED
  NEXT();
#else
next:
  in = pc++;
  switch (in->op) {
#endif

  CASE(OP_MOVE) {
    R[ins.a] = R[ins.b];
    NEXT();
  }
  CASE(OP_IMM) {
    R[ins.a] = (ot_value)(intptr_t)(int32_t)oti_instruction_bc(ins);
    NEXT();
  }
  CASE(OP_CONST) {
    R[ins.a] = rt->constants[oti_instruction_bc(ins)];
    NEXT();
  }
  /* v = G[bc]. The eval builtin is read as an evaluator for this namespace. */
#define LOAD_GLOBAL()                                                                              \
  do {                                                                                             \
    uint32_t index_ = oti_instruction_bc(ins);                                                     \
    v = rt->globals[index_];                                                                       \
    if (VM_UNLIKELY(v == (((ot_value)B_EVAL << 8) | TAG_BUILTIN))) {                               \
      SAVE_IP();                                                                                   \
      v = oti_evaluator(rt, fn->namespace);                                                        \
      if (!v) return false;                                                                        \
      RELOAD();                                                                                    \
    }                                                                                              \
    if (VM_UNLIKELY(v == V_UNBOUND)) {                                                             \
      vm_unbound_global(rt, index_);                                                               \
      FAIL();                                                                                      \
    }                                                                                              \
  } while (0)
  CASE(OP_GLOBAL) {
    LOAD_GLOBAL();
    R[ins.a] = v;
    NEXT();
  }
  CASE(OP_SET_GLOBAL) {
    uint32_t index = oti_instruction_bc(ins);
    if (rt->globals[index] == V_UNBOUND) {
      oti_coded_error(rt, "E0201", "cannot set an unbound global");
      FAIL();
    }
    if (VM_UNLIKELY(vm_inlined_builtin(rt, index))) FAIL();
    rt->global_info[index].macro = oti_kind(R[ins.a], K_MACRO);
    rt->globals[index] = R[ins.a];
    NEXT();
  }
  CASE(OP_DEFINE_GLOBAL) {
    uint32_t index = oti_instruction_bc(ins);
    if (VM_UNLIKELY(vm_inlined_builtin(rt, index))) FAIL();
    rt->global_info[index].defined = 1;
    rt->global_info[index].macro = oti_kind(R[ins.a], K_MACRO);
    rt->globals[index] = R[ins.a];
    NEXT();
  }
  CASE(OP_UPVALUE) {
    R[ins.a] = object_fields(R[0])[ins.b];
    NEXT();
  }
  CASE(OP_UPVALUE_BOX) {
    v = object_fields(object_fields(R[0])[ins.b])[0];
    if (VM_UNLIKELY(v == V_UNBOUND)) {
      oti_error(rt, "uninitialized captured binding");
      FAIL();
    }
    R[ins.a] = v;
    NEXT();
  }
  CASE(OP_SET_UPVALUE) {
    object_fields(object_fields(R[0])[ins.b])[0] = R[ins.a];
    NEXT();
  }
  CASE(OP_BOX) {
    v = oti_alloc(rt, K_BOX, 1, 0);
    if (!v) FAIL();
    object_fields(v)[0] = R[ins.a];
    R[ins.a] = v;
    NEXT();
  }
  CASE(OP_UNBOX) {
    v = object_fields(R[ins.b])[0];
    if (VM_UNLIKELY(v == V_UNBOUND)) {
      oti_error(rt, "uninitialized local");
      FAIL();
    }
    R[ins.a] = v;
    NEXT();
  }
  CASE(OP_SET_BOX) {
    object_fields(R[ins.a])[0] = R[ins.b];
    NEXT();
  }
  CASE(OP_CHECK) {
    if (VM_UNLIKELY(R[ins.a] == V_UNBOUND)) {
      oti_error(rt, "uninitialized local");
      FAIL();
    }
    NEXT();
  }
  CASE(OP_CLEAR) {
    for (unsigned i = 0; i < ins.b; i++) R[ins.a + i] = V_UNBOUND;
    NEXT();
  }
  CASE(OP_CLOSURE) {
    if (!vm_closure(rt, R, oti_instruction_bc(ins), &v)) FAIL();
    R[ins.a] = v;
    NEXT();
  }
  CASE(OP_JUMP) {
    JUMP_TO(TARGET(ins));
    NEXT();
  }
  CASE(OP_JFALSE) {
    if (!oti_truth(R[ins.a])) JUMP_TO(TARGET(ins));
    NEXT();
  }
  CASE(OP_JTRUE) {
    if (oti_truth(R[ins.a])) JUMP_TO(TARGET(ins));
    NEXT();
  }
  CASE(OP_JBOUND) {
    if (R[ins.a] != V_UNBOUND) pc = TARGET(ins);
    NEXT();
  }
  CASE(OP_CALL_GLOBAL) {
    LOAD_GLOBAL();
    R[ins.a] = v;
    argc = ins.x;
    goto call;
  }
  CASE(OP_CALL) {
    v = R[ins.a];
    argc = ins.b;
  call:
    if (VM_LIKELY(oti_kind(v, K_CLOSURE))) {
      uint32_t id;
      memcpy(&id, object_bytes(v), sizeof(id));
      oti_function* callee = rt->functions[id];
      /* Runtime fields are read before any store: a stack store could alias them. */
      size_t callee_base = base + ins.a, top = callee_base + 1 + callee->locals, old = rt->sp;
      size_t count = rt->frame_count;
      if (VM_LIKELY(argc == callee->arity && !callee->variadic && top <= rt->stack_room &&
                    count < rt->frame_capacity)) {
        ot_value* stack = rt->stack;
        SAVE_IP();
        frame = rt->frames + count++;
        rt->frame_count = count;
        if (count > rt->max_frames) rt->max_frames = count;
        *frame = (oti_frame){callee, callee->code, callee_base, old};
        if (top > old) {
          vm_fresh(rt, stack, old, top);
          vm_height(rt, top);
        }
        fn = callee;
        code = pc = callee->code;
        base = callee_base;
        R = stack + base;
        SAFEPOINT();
        NEXT();
      }
    } else if ((v & 255) == TAG_BUILTIN && v != VM_APPLY) {
      SAVE_IP();
      v = oti_builtin(rt, (unsigned)(v >> 8), base + ins.a + 1, argc);
      if (!v || oti_failed(rt)) return false;
      RELOAD();
      R[ins.a] = v;
      SAFEPOINT();
      NEXT();
    }
    SAVE_IP();
    if (!oti_apply(rt, base + ins.a, argc, false)) return false;
    RELOAD();
    SAFEPOINT();
    NEXT();
  }
  CASE(OP_TAIL_CALL_GLOBAL) {
    LOAD_GLOBAL();
    R[ins.a] = v;
    argc = ins.x;
    goto tail_call;
  }
  CASE(OP_TAIL_CALL) {
    v = R[ins.a];
    argc = ins.b;
  tail_call:
    if (VM_LIKELY(oti_kind(v, K_CLOSURE))) {
      uint32_t id;
      memcpy(&id, object_bytes(v), sizeof(id));
      oti_function* callee = rt->functions[id];
      size_t top = base + 1 + callee->locals, old = rt->sp, floor = frame->floor;
      if (VM_LIKELY(argc == callee->arity && !callee->variadic && top <= rt->stack_room)) {
        ot_value* stack = rt->stack;
        memmove(R, R + ins.a, ((size_t)argc + 1) * sizeof(ot_value));
        if (top > old) vm_fresh(rt, stack, old, top);
        vm_height(rt, top > floor ? top : floor);
        frame->fn = callee;
        fn = callee;
        code = pc = callee->code;
        SAFEPOINT();
        NEXT();
      }
    } else if ((v & 255) == TAG_BUILTIN && v != VM_APPLY) {
      SAVE_IP();
      v = oti_builtin(rt, (unsigned)(v >> 8), base + ins.a + 1, argc);
      if (!v || oti_failed(rt)) return false;
      RELOAD();
      goto return_value;
    }
    SAVE_IP();
    if (!oti_apply(rt, base + ins.a, argc, true)) return false;
    if (rt->frame_count <= stop_frames) return true;
    RELOAD();
    SAFEPOINT();
    NEXT();
  }
  CASE(OP_RETURN_IMM) {
    v = (ot_value)(intptr_t)(int32_t)oti_instruction_bc(ins);
    goto return_value;
  }
  CASE(OP_RETURN) {
    v = R[ins.a];
  return_value:;
    size_t floor = frame->floor, count = rt->frame_count - 1;
    ot_value* stack = rt->stack;
    R[0] = v;
    rt->frame_count = count;
    vm_height(rt, floor > base ? floor : base + 1);
    if (count <= stop_frames) return true;
    frame--;
    fn = frame->fn;
    code = fn->code;
    pc = frame->pc;
    base = frame->base;
    R = stack + base;
    NEXT();
  }
  CASE(OP_TRY) {
    if (!vm_install_try(rt, oti_instruction_bc(ins), base + ins.a)) FAIL();
    NEXT();
  }
  CASE(OP_END_TRY) {
    if (!rt->try_count) {
      oti_error(rt, "invalid try stack");
      FAIL();
    }
    rt->try_count--;
    NEXT();
  }
  CASE(OP_RAISE) {
    rt->condition_values[0] = R[ins.a];
    rt->raised = true;
    oti_error(rt, "uncaught condition");
    rt->signaled = true;
    FAIL();
  }
  CASE(OP_CLEANUP) {
    /* The cleanup state records the frame top, so the closure goes just above it. */
    size_t top = rt->sp;
    SAVE_IP();
    if (!oti_stack(rt, top + 1)) return false;
    rt->stack[top] = rt->stack[base + ins.a];
    vm_height(rt, top + 1);
    bool ok = oti_install_cleanup(rt, ins.x != 0);
    vm_height(rt, top);
    if (!ok) return false;
    RELOAD();
    NEXT();
  }
  CASE(OP_RUN_CLEANUP) {
    SAVE_IP();
    if (!oti_run_cleanup(rt, false)) return false;
    RELOAD();
    NEXT();
  }
  CASE(OP_RESTARTS) {
    /* Restart triples are copied above the frame top, where a transfer leaves
     * its result for OP_RESULT. */
    size_t top = rt->sp, count = ins.b;
    pc++;
    SAVE_IP();
    if (!oti_stack(rt, top + 3 * count)) return false;
    memcpy(rt->stack + top, rt->stack + base + ins.a, 3 * count * sizeof(ot_value));
    vm_height(rt, top + 3 * count);
    bool ok = oti_install_restarts(rt, top, count, oti_instruction_bc(pc[-1]));
    vm_height(rt, top);
    if (!ok) return false;
    RELOAD();
    NEXT();
  }
  CASE(OP_RESULT) {
    R[ins.a] = rt->stack[vm_top(frame)];
    NEXT();
  }
  CASE(OP_END_RESTARTS) {
    if (!rt->restart_scope_count) {
      oti_error(rt, "invalid restart scope");
      FAIL();
    }
    oti_restore_restarts(rt, rt->restart_scope_count - 1);
    NEXT();
  }
  CASE(OP_BIND_HANDLERS) {
    if (!oti_install_handlers(rt, base + ins.a, ins.b)) FAIL();
    NEXT();
  }
  CASE(OP_UNBIND_HANDLERS) {
    if (ins.b > rt->handler_count) {
      oti_error(rt, "invalid handler stack");
      FAIL();
    }
    oti_remove_handlers(rt, ins.b);
    NEXT();
  }
  CASE(OP_MAKE_PARAM) {
    v = oti_alloc(rt, K_PARAM, 2, 0);
    if (!v) FAIL();
    object_fields(v)[0] = R[ins.b];
    object_fields(v)[1] = R[ins.c];
    R[ins.a] = v;
    NEXT();
  }
  CASE(OP_BIND_PARAM) {
    if (!oti_bind_param(rt, R[ins.a], R[ins.b])) FAIL();
    NEXT();
  }
  CASE(OP_UNBIND_PARAMS) {
    if (ins.b > rt->param_count) {
      oti_error(rt, "invalid parameter stack");
      FAIL();
    }
    oti_restore_params(rt, rt->param_count - ins.b);
    NEXT();
  }

  /* Inline primitives fall back to their builtin, which also reports errors. */
#define SLOW(id, x, y, n)                                                                          \
  do {                                                                                             \
    SAVE_IP();                                                                                     \
    ot_value slow_ = vm_builtin(rt, id, x, y, n);                                                  \
    if (!slow_) return false;                                                                      \
    RELOAD();                                                                                      \
    v = slow_;                                                                                     \
  } while (0)
#define ARITHMETIC(op, int_op, float_op)                                                           \
  CASE(op) {                                                                                       \
    ot_value x = R[ins.b], y = op == OP_ADDI ? ot_int((int16_t)ins.c) : R[ins.c];                  \
    double dx, dy;                                                                                 \
    if (VM_LIKELY(ot_is_int(x) && ot_is_int(y) && int_op(x, y, &v))) {                             \
      R[ins.a] = v;                                                                                \
      NEXT();                                                                                      \
    }                                                                                              \
    if (vm_float(x, &dx) && vm_float(y, &dy)) {                                                    \
      v = oti_float(rt, (op == OP_ADD || op == OP_ADDI ? 0.0 + dx : dx) float_op dy);              \
      if (!v) FAIL();                                                                              \
    } else SLOW(vm_builtin_id(op), x, y, 2);                                                       \
    R[ins.a] = v;                                                                                  \
    NEXT();                                                                                        \
  }
  /* Builtin addition folds from 0.0, so (+ -0.0 -0.0) is 0.0; match it. */
  ARITHMETIC(OP_ADD, value_int_add, +)
  ARITHMETIC(OP_SUB, value_int_sub, -)
  ARITHMETIC(OP_MUL, value_int_mul, *)
  ARITHMETIC(OP_ADDI, value_int_add, +)

  /* Order two numbers as -1, 0 or 1, 3 when unordered, or 2 when the builtin
   * must decide. */
#define ORDER(x, y, out)                                                                           \
  do {                                                                                             \
    double dx_, dy_;                                                                               \
    if (VM_LIKELY(ot_is_int(x) && ot_is_int(y))) {                                                 \
      intptr_t ix_ = ot_to_int(x), iy_ = ot_to_int(y);                                             \
      out = ix_ < iy_ ? -1 : ix_ > iy_;                                                            \
    } else if (vm_float(x, &dx_) && vm_float(y, &dy_))                                             \
      out = dx_ < dy_ ? -1 : dx_ > dy_ ? 1 : dx_ == dy_ ? 0 : 3;                                   \
    else out = 2;                                                                                  \
  } while (0)
#define COMPARE(op)                                                                                \
  CASE(op) {                                                                                       \
    ot_value x = R[ins.b], y = R[ins.c];                                                           \
    int order;                                                                                     \
    ORDER(x, y, order);                                                                            \
    if (VM_LIKELY(order != 2)) v = vm_compare(order, vm_builtin_id(op)) ? OT_TRUE : OT_FALSE;      \
    else SLOW(vm_builtin_id(op), x, y, 2);                                                         \
    R[ins.a] = v;                                                                                  \
    NEXT();                                                                                        \
  }
  COMPARE(OP_LT)
  COMPARE(OP_LE)
  COMPARE(OP_GT)
  COMPARE(OP_GE)
  COMPARE(OP_NUMEQ)

#define BRANCH_IF(test)                                                                            \
  do {                                                                                             \
    if ((test) == (ins.x != 0)) JUMP_TO(TARGET(*pc));                                              \
    else pc++;                                                                                     \
    NEXT();                                                                                        \
  } while (0)
#define BRANCH_COMPARE(op, immediate)                                                              \
  CASE(op) {                                                                                       \
    ot_value x = R[ins.a], y = immediate ? ot_int((int16_t)ins.c) : R[ins.b];                      \
    int order;                                                                                     \
    ORDER(x, y, order);                                                                            \
    if (VM_LIKELY(order != 2)) BRANCH_IF(vm_compare(order, vm_builtin_id(op)));                    \
    SLOW(vm_builtin_id(op), x, y, 2);                                                              \
    BRANCH_IF(oti_truth(v));                                                                       \
  }
  BRANCH_COMPARE(OP_BR_LT, false)
  BRANCH_COMPARE(OP_BR_LE, false)
  BRANCH_COMPARE(OP_BR_GT, false)
  BRANCH_COMPARE(OP_BR_GE, false)
  BRANCH_COMPARE(OP_BR_NUMEQ, false)
  BRANCH_COMPARE(OP_BR_LTI, true)
  BRANCH_COMPARE(OP_BR_LEI, true)
  BRANCH_COMPARE(OP_BR_GTI, true)
  BRANCH_COMPARE(OP_BR_GEI, true)
  BRANCH_COMPARE(OP_BR_NUMEQI, true)
  CASE(OP_BR_EQ) { BRANCH_IF(R[ins.a] == R[ins.b]); }
  CASE(OP_BR_NULL) { BRANCH_IF(R[ins.a] == OT_EMPTY); }
  CASE(OP_BR_PAIR) { BRANCH_IF(oti_kind(R[ins.a], K_PAIR)); }
  CASE(OP_BR_EQK) { BRANCH_IF(R[ins.a] == rt->constants[oti_instruction_bc(ins)]); }

  CASE(OP_EQ) {
    R[ins.a] = R[ins.b] == R[ins.c] ? OT_TRUE : OT_FALSE;
    NEXT();
  }
  CASE(OP_CONS) {
    v = oti_alloc(rt, K_PAIR, 2, 0);
    if (!v) FAIL();
    object_fields(v)[0] = R[ins.b];
    object_fields(v)[1] = R[ins.c];
    R[ins.a] = v;
    NEXT();
  }
  CASE(OP_CAR)
  CASE(OP_CDR) {
    ot_value x = R[ins.b];
    if (VM_LIKELY(oti_kind(x, K_PAIR))) v = object_fields(x)[ins.op == OP_CDR];
    else SLOW(vm_builtin_id(ins.op), x, OT_NIL, 1);
    R[ins.a] = v;
    NEXT();
  }
  CASE(OP_NOT) {
    R[ins.a] = oti_truth(R[ins.b]) ? OT_FALSE : OT_TRUE;
    NEXT();
  }
  CASE(OP_NULLP) {
    R[ins.a] = R[ins.b] == OT_EMPTY ? OT_TRUE : OT_FALSE;
    NEXT();
  }
  CASE(OP_PAIRP) {
    R[ins.a] = oti_kind(R[ins.b], K_PAIR) ? OT_TRUE : OT_FALSE;
    NEXT();
  }

#if !VM_THREADED
  default: oti_error(rt, "invalid bytecode"); FAIL();
}
#endif

#undef LOAD_FRAME
#undef SAVE_IP
#undef RELOAD
#undef FAIL
#undef SAFEPOINT
#undef TARGET
#undef LOAD_GLOBAL
#undef JUMP_TO
#undef CASE
#undef NEXT
#undef SLOW
#undef ARITHMETIC
#undef ORDER
#undef COMPARE
#undef BRANCH_IF
#undef BRANCH_COMPARE
#undef ins
}

bool oti_drive(ot_state* rt, size_t stop_frames) {
  while (!dispatch(rt, stop_frames)) {
    if (!rt->interrupted && !rt->exit_pending && !rt->restart_pending && !rt->signaled)
      oti_signal_error(rt);
    if (rt->restart_pending) {
      if (!oti_resume_restart(rt, stop_frames)) return false;
      continue;
    }
    size_t target_cleanups = !rt->interrupted && !rt->exit_pending && rt->try_count
                                 ? rt->tries[rt->try_count - 1].cleanups
                                 : 0;
    if (rt->cleanup_count > target_cleanups) {
      if (rt->cleanup_states[rt->cleanup_count - 1].state.frames <= stop_frames) return false;
      oti_run_cleanup(rt, true);
      continue;
    }
    if (rt->interrupted || rt->exit_pending || !rt->try_count ||
        rt->tries[rt->try_count - 1].frames <= stop_frames)
      return false;
    oti_try_frame handler = rt->tries[--rt->try_count];
    rt->frame_count = handler.frames;
    oti_restore_restarts(rt, handler.restarts);
    oti_restore_params(rt, handler.params);
    oti_restore_handlers(rt, handler.handlers, handler.handler_top);
    vm_height(rt, handler.sp);
    ot_value condition = oti_error_condition(rt);
    if (!condition) continue;
    oti_clear_error(rt);
    rt->raised = false;
    rt->signaled = false;
    rt->stack[handler.slot] = condition;
    rt->condition_values[0] = OT_NIL;
    oti_frame* frame = &rt->frames[rt->frame_count - 1];
    frame->pc = frame->fn->code + handler.target;
  }
  return true;
}

/* Call back into the VM without discarding the suspended caller's roots. */
ot_value oti_call_list(ot_state* rt, ot_value function, ot_value args) {
  if (!oti_host_entry(rt)) return 0;
  size_t base = rt->sp, frames = rt->frame_count, tries = rt->try_count, params = rt->param_count;
  size_t handlers = rt->handler_count, handler_top = rt->handler_top,
         restarts = rt->restart_scope_count, cleanups = rt->cleanup_count;
  if (rt->callback_depth >= 128) {
    oti_error(rt, "VM callback nesting limit exceeded");
    rt->signaled = true;
    return 0;
  }
  if (rt->stack_limit < 3 || base > rt->stack_limit - 3) {
    oti_error(rt, "VM stack limit exceeded");
    rt->signaled = true;
    return 0;
  }
  if (!oti_stack(rt, base + 3)) {
    rt->signaled = true;
    return 0;
  }
  rt->callback_depth++;
  rt->stack[base] = VM_APPLY;
  rt->stack[base + 1] = function;
  rt->stack[base + 2] = args;
  vm_height(rt, base + 3);
  bool applied = oti_apply(rt, base, 2, false);
  if (!applied && !oti_failed(rt)) oti_error(rt, "VM call failed");
  bool ok = oti_drive(rt, frames);
  ot_value result = ok ? rt->stack[base] : 0;
  rt->frame_count = frames;
  rt->try_count = tries;
  oti_restore_restarts(rt, restarts);
  oti_restore_cleanups(rt, cleanups);
  oti_restore_params(rt, params);
  oti_restore_handlers(rt, handlers, handler_top);
  rt->callback_depth--;
  vm_height(rt, base);
  return result;
}
