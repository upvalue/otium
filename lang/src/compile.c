#include "compile.h"
#include "code.h"
#include "eval.h"
#include "form.h"
#include "heap.h"
#include "ns.h"
#include "state.h"

typedef struct Binding {
  u32 name;
  u32 slot;
  bool captured;
} Binding;

typedef struct Capture {
  u32 name;
  bool local;
  u32 index;
} Capture;

typedef struct LambdaInfo LambdaInfo;

OT_VEC_TYPE(Binding, VecBinding);
OT_VEC_TYPE(Capture, VecCapture);
OT_VEC_TYPE(LambdaInfo*, VecLambdaInfoPtr);

struct LambdaInfo {
  LambdaInfo* parent;
  VecBinding bindings;
  VecU32 active;
  VecCapture captures;
  VecLambdaInfoPtr children;
  u32 nfixed;
  bool hasRest;
  u32 initialCount;
  u32 userDepth;
};

static void lambda_info_init(LambdaInfo* li, LambdaInfo* parent, u32 userDepth) {
  memset(li, 0, sizeof(*li));
  li->parent = parent;
  li->userDepth = userDepth;
}

static void lambda_info_free(LambdaInfo* li);

// Mirrors the C++ destructor: delete every child, then release this node's
// vec storage (the members' own destructors in C++).
static void lambda_info_deinit(LambdaInfo* li) {
  for (u32 i = 0; i < li->children.len; i++) lambda_info_free(li->children.data[i]);
  vec_deinit(&li->bindings);
  vec_deinit(&li->active);
  vec_deinit(&li->captures);
  vec_deinit(&li->children);
}

static void lambda_info_free(LambdaInfo* li) {
  lambda_info_deinit(li);
  ot_free(li);
}

static i32 find_active(LambdaInfo* li, u32 name) {
  for (u32 i = li->active.len; i-- > 0;) {
    u32 binding = li->active.data[i];
    if (li->bindings.data[binding].name == name) return (i32)binding;
  }
  return -1;
}

static i32 find_capture(LambdaInfo* li, u32 name) {
  for (u32 i = 0; i < li->captures.len; i++)
    if (li->captures.data[i].name == name) return (i32)i;
  return -1;
}

static u32 add_binding(LambdaInfo* li, u32 name) {
  u32 index = li->bindings.len;
  vec_push(&li->bindings, ((Binding){name, index, false}));
  vec_push(&li->active, index);
  return index;
}

static i32 capture_name(LambdaInfo* li, u32 name) {
  i32 existing = find_capture(li, name);
  if (existing >= 0) return existing;
  if (!li->parent) return -1;
  i32 local = find_active(li->parent, name);
  Capture capture = {name, false, 0};
  if (local >= 0) {
    li->parent->bindings.data[(u32)local].captured = true;
    capture.local = true;
    capture.index = li->parent->bindings.data[(u32)local].slot;
  } else {
    i32 parentCapture = capture_name(li->parent, name);
    if (parentCapture < 0) return -1;
    capture.index = (u32)parentCapture;
  }
  vec_push(&li->captures, capture);
  return (i32)(li->captures.len - 1);
}

static void analyze_expr(State* vm, LambdaInfo* li, Value form);
static Value defined_name(Value form);
static void analyze_body(State* vm, LambdaInfo* li, Value forms);

static void analyze_quasiquote(State* vm, LambdaInfo* li, Value form, u32 depth) {
  if (!pairp(form)) return;
  Value head = car_(form);
  if (sym_is(head, vm->syms.unquote_)) {
    Value args = cdr_(form);
    if (pairp(args)) {
      if (depth == 1) analyze_expr(vm, li, car_(args));
      else analyze_quasiquote(vm, li, car_(args), depth - 1);
    }
    return;
  }
  if (sym_is(head, vm->syms.quasiquote_)) {
    Value args = cdr_(form);
    if (pairp(args)) analyze_quasiquote(vm, li, car_(args), depth + 1);
    return;
  }
  if (pairp(head) && sym_is(car_(head), vm->syms.unquoteSplicing_) && depth == 1) {
    Value spliceArgs = cdr_(head);
    if (pairp(spliceArgs)) analyze_expr(vm, li, car_(spliceArgs));
  } else {
    analyze_quasiquote(vm, li, head, depth);
  }
  analyze_quasiquote(vm, li, cdr_(form), depth);
}

static bool is_define_head(State* vm, u32 name) {
  return name == vm->syms.define_ || name == vm->syms.def_ || name == vm->syms.definePriv_;
}

static LambdaInfo* add_compiler_thunk(LambdaInfo* parent) {
  LambdaInfo* child = (LambdaInfo*)ot_alloc(sizeof(LambdaInfo));
  lambda_info_init(child, parent, parent->userDepth);
  vec_push(&parent->children, child);
  return child;
}

static void analyze_thunk_expr(State* vm, LambdaInfo* parent, Value form) {
  LambdaInfo* child = add_compiler_thunk(parent);
  if (child->userDepth > 0 && pairp(form) && car_(form).tag == Tag_Symbol) {
    u32 head = car_(form).id;
    if (is_define_head(vm, head)) {
      Value name = defined_name(form);
      if (name.tag == Tag_Symbol) add_binding(child, name.id);
    }
  }
  child->initialCount = child->bindings.len;
  analyze_expr(vm, child, form);
}

static void analyze_thunk_body(State* vm, LambdaInfo* parent, Value forms) {
  LambdaInfo* child = add_compiler_thunk(parent);
  analyze_body(vm, child, forms);
}

static void analyze_binding_control(State* vm, LambdaInfo* li, Value args, bool thunkBindings) {
  if (!pairp(args)) return;
  Value bindings = strip_array_literal_head(car_(args), vm->syms.array_);
  for (Value cursor = bindings; pairp(cursor); cursor = cdr_(cursor)) {
    Value binding = car_(cursor);
    if (pairp(binding)) {
      if (thunkBindings) analyze_thunk_expr(vm, li, car_(binding));
      else analyze_expr(vm, li, car_(binding));
    }
    if (pairp(binding) && pairp(cdr_(binding))) {
      if (thunkBindings) analyze_thunk_expr(vm, li, car_(cdr_(binding)));
      else analyze_expr(vm, li, car_(cdr_(binding)));
    }
  }
  analyze_thunk_body(vm, li, cdr_(args));
}

static void analyze_one_arg_lambda(State* vm, LambdaInfo* parent, Value param, Value body) {
  LambdaInfo* child = (LambdaInfo*)ot_alloc(sizeof(LambdaInfo));
  lambda_info_init(child, parent, parent->userDepth + 1);
  vec_push(&parent->children, child);
  if (param.tag == Tag_Symbol) {
    add_binding(child, param.id);
    child->nfixed = 1;
  }
  analyze_body(vm, child, body);
}

static bool parse_params(State* vm, LambdaInfo* li, Value params) {
  if (params.tag == Tag_Symbol) {
    li->hasRest = true;
    add_binding(li, params.id);
    return true;
  }
  params = strip_array_literal_head(params, vm->syms.array_);
  while (pairp(params)) {
    Value param = car_(params);
    if (sym_is(param, vm->syms.amp_)) {
      params = cdr_(params);
      if (!pairp(params) || car_(params).tag != Tag_Symbol) return false;
      li->hasRest = true;
      add_binding(li, car_(params).id);
      return cdr_(params).tag == Tag_Null;
    }
    if (param.tag != Tag_Symbol) return false;
    add_binding(li, param.id);
    li->nfixed++;
    params = cdr_(params);
  }
  if (params.tag == Tag_Symbol) {
    li->hasRest = true;
    add_binding(li, params.id);
    return true;
  }
  return params.tag == Tag_Null;
}

static Value defined_name(Value form) {
  Value args = cdr_(form);
  if (!pairp(args)) return nil_v();
  Value target = car_(args);
  return pairp(target) ? car_(target) : target;
}

// The name a body form hoists to a local slot, or nil when it is not one of
// the defining forms. Both passes walk bodies through this so they cannot
// disagree about which forms allocate a slot.
static Value body_define_name(State* vm, Value form) {
  if (!pairp(form) || car_(form).tag != Tag_Symbol) return nil_v();
  u32 head = car_(form).id;
  if (!is_define_head(vm, head)) return nil_v();
  Value name = defined_name(form);
  return name.tag == Tag_Symbol ? name : nil_v();
}

static void collect_body_defines(State* vm, LambdaInfo* li, Value forms) {
  if (li->userDepth == 0) return;
  for (Value cursor = forms; pairp(cursor); cursor = cdr_(cursor)) {
    Value name = body_define_name(vm, car_(cursor));
    if (is_nil(name) || find_active(li, name.id) >= 0) continue;
    add_binding(li, name.id);
  }
}

static void analyze_body(State* vm, LambdaInfo* li, Value forms) {
  collect_body_defines(vm, li, forms);
  li->initialCount = li->bindings.len;
  for (Value cursor = forms; pairp(cursor); cursor = cdr_(cursor))
    analyze_expr(vm, li, car_(cursor));
}

static void analyze_lambda(State* vm, LambdaInfo* parent, Value params, Value body) {
  LambdaInfo* child = (LambdaInfo*)ot_alloc(sizeof(LambdaInfo));
  lambda_info_init(child, parent, parent->userDepth + 1);
  vec_push(&parent->children, child);
  if (!parse_params(vm, child, params)) return;
  analyze_body(vm, child, body);
}

static void analyze_expr(State* vm, LambdaInfo* li, Value form) {
  if (form.tag == Tag_Symbol) {
    if (!sym_qualified(vm, form.id) && find_active(li, form.id) < 0)
      (void)capture_name(li, form.id);
    return;
  }
  if (!pairp(form)) return;
  Value head = car_(form);
  Value args = cdr_(form);
  if (head.tag == Tag_Symbol) {
    u32 name = head.id;
    if (name == vm->syms.quote_) return;
    if (name == vm->syms.quasiquote_) {
      if (pairp(args)) analyze_quasiquote(vm, li, car_(args), 1);
      return;
    }
    if (name == vm->syms.lambda_ || name == vm->syms.fn_) {
      if (pairp(args)) analyze_lambda(vm, li, car_(args), cdr_(args));
      return;
    }
    if (is_define_head(vm, name)) {
      if (!pairp(args)) return;
      Value target = car_(args);
      Value rest = cdr_(args);
      if (pairp(target)) analyze_lambda(vm, li, cdr_(target), rest);
      else {
        rest = skip_docstring(rest, nullptr);
        if (pairp(rest)) analyze_expr(vm, li, car_(rest));
      }
      return;
    }
    if (name == vm->syms.defmacro_) {
      if (pairp(args) && pairp(cdr_(args)))
        analyze_lambda(vm, li, car_(cdr_(args)), cdr_(cdr_(args)));
      return;
    }
    if (name == vm->syms.setBang_) {
      if (pairp(args) && car_(args).tag == Tag_Symbol && !sym_qualified(vm, car_(args).id) &&
          find_active(li, car_(args).id) < 0)
        (void)capture_name(li, car_(args).id);
      if (pairp(args) && pairp(cdr_(args))) analyze_expr(vm, li, car_(cdr_(args)));
      return;
    }
    if (name == vm->syms.let_) {
      if (!pairp(args)) return;
      u32 activeBase = li->active.len;
      Value bindings = car_(args);
      bindings = strip_array_literal_head(bindings, vm->syms.array_);
      while (pairp(bindings)) {
        Value binding = car_(bindings);
        if (pairp(binding) && pairp(cdr_(binding))) {
          analyze_expr(vm, li, car_(cdr_(binding)));
          if (car_(binding).tag == Tag_Symbol) add_binding(li, car_(binding).id);
        }
        bindings = cdr_(bindings);
      }
      collect_body_defines(vm, li, cdr_(args));
      for (Value body = cdr_(args); pairp(body); body = cdr_(body))
        analyze_expr(vm, li, car_(body));
      li->active.len = activeBase;
      return;
    }
    if (name == vm->syms.ns_ || name == vm->syms.inNs_ || name == vm->syms.require_) return;
    if (name == vm->syms.handlerBind_) {
      analyze_binding_control(vm, li, args, false);
      return;
    }
    if (name == vm->syms.restartCase_) {
      if (!pairp(args)) return;
      analyze_thunk_expr(vm, li, car_(args));
      for (Value cursor = cdr_(args); pairp(cursor); cursor = cdr_(cursor)) {
        Value clause = car_(cursor);
        if (!pairp(clause)) continue;
        Value rest = cdr_(clause);
        rest = skip_docstring(rest, nullptr);
        if (pairp(rest)) analyze_lambda(vm, li, car_(rest), cdr_(rest));
      }
      return;
    }
    if (name == vm->syms.try_) {
      Value cursor = args;
      while (pairp(cursor)) {
        Value part = car_(cursor);
        if (pairp(part) && sym_is(car_(part), vm->syms.catch_)) break;
        analyze_thunk_expr(vm, li, part);
        cursor = cdr_(cursor);
      }
      while (pairp(cursor)) {
        Value clause = car_(cursor);
        if (pairp(clause) && pairp(cdr_(clause))) {
          Value spec = car_(cdr_(clause));
          if (pairp(spec)) {
            analyze_thunk_expr(vm, li, car_(spec));
            if (pairp(cdr_(spec)))
              analyze_one_arg_lambda(vm, li, car_(cdr_(spec)), cdr_(cdr_(clause)));
          }
        }
        cursor = cdr_(cursor);
      }
      return;
    }
    if (name == vm->syms.unwindProtect_ || name == vm->syms.defer_) {
      for (Value cursor = args; pairp(cursor); cursor = cdr_(cursor))
        analyze_thunk_expr(vm, li, car_(cursor));
      return;
    }
    if (name == vm->syms.withParams_) {
      analyze_binding_control(vm, li, args, true);
      return;
    }
    if (name == vm->syms.defparam_) {
      if (!pairp(args)) return;
      Value rest = cdr_(args);
      rest = skip_docstring(rest, nullptr);
      if (pairp(rest)) analyze_expr(vm, li, car_(rest));
      return;
    }
  }
  for (Value cursor = form; pairp(cursor); cursor = cdr_(cursor))
    analyze_expr(vm, li, car_(cursor));
}

typedef enum ResolvedKind : u8 {
  ResolvedKind_Local,
  ResolvedKind_Upval,
  ResolvedKind_Global
} ResolvedKind;
typedef struct Resolved {
  ResolvedKind kind;
  u32 index;
  bool boxed;
} Resolved;

typedef struct Compiler {
  State* vm;
  LambdaInfo* info;
  Buf bytes;
  Slot constants;
  VecU32 active;
  u32 bindingCursor;
  u32 childCursor;
  u32 depth;
  u32 maxDepth;
  bool failed;
} Compiler;

static void compiler_init(Compiler* c, State* vm, LambdaInfo* info, Slot constants) {
  c->vm = vm;
  c->info = info;
  c->bytes = (Buf){0};
  c->constants = constants;
  c->active = (VecU32){0};
  c->bindingCursor = info->initialCount;
  c->childCursor = 0;
  c->depth = 0;
  c->maxDepth = 0;
  c->failed = false;
  for (u32 i = 0; i < info->initialCount; i++) vec_push(&c->active, i);
}

// Releases the members whose C++ destructors ran when the Compiler left scope.
static void compiler_deinit(Compiler* c) {
  buf_deinit(&c->bytes);
  vec_deinit(&c->active);
}

static void compiler_error(Compiler* c, const char* message) {
  if (!c->failed) (void)raise_error(c->vm, "compile: %s", message);
  c->failed = true;
}

static void push_depth(Compiler* c) {
  c->depth++;
  if (c->depth > c->maxDepth) c->maxDepth = c->depth;
}

static void pop_depth(Compiler* c, u32 count) {
  if (count > c->depth) {
    compiler_error(c, "internal stack accounting underflow");
    c->depth = 0;
  } else {
    c->depth -= count;
  }
}

static void emit_op(Compiler* c, Op op) { vec_push(&c->bytes, (char)(u8)op); }
static void emit_u16(Compiler* c, u32 value) {
  if (value > UINT16_MAX) {
    compiler_error(c, "operand exceeds 16 bits");
    value = 0;
  }
  vec_push(&c->bytes, (char)(value & 0xff));
  vec_push(&c->bytes, (char)((value >> 8) & 0xff));
}
static void emit_i32(Compiler* c, i32 value) {
  u32 bits = (u32)value;
  for (u32 i = 0; i < 4; i++) vec_push(&c->bytes, (char)((bits >> (i * 8)) & 0xff));
}
static u32 emit_jump(Compiler* c, Op op) {
  emit_op(c, op);
  u32 operand = c->bytes.len;
  emit_i32(c, 0);
  return operand;
}
static void patch_jump(Compiler* c, u32 operand, u32 target) {
  i64 relative = (i64)target - (i64)(operand + 4);
  if (relative < INT32_MIN || relative > INT32_MAX) {
    compiler_error(c, "jump exceeds 32 bits");
    return;
  }
  u32 bits = (u32)(i32)relative;
  for (u32 i = 0; i < 4; i++) c->bytes.data[operand + i] = (char)(bits >> (i * 8));
}

static u32 add_constant(Compiler* c, Value value) {
  ArrayData* constants = as_array(slot_get(c->constants));
  for (u32 i = 0; i < constants->len; i++)
    if (val_eq(constants->items[i], value)) return i;
  if (constants->len >= UINT16_MAX) {
    compiler_error(c, "too many constants");
    return 0;
  }
  u32 index = constants->len;
  array_push(c->vm, slot_get(c->constants), value);
  return index;
}

static Resolved resolve(Compiler* c, u32 name) {
  if (!sym_qualified(c->vm, name)) {
    for (u32 i = c->active.len; i-- > 0;) {
      Binding* binding = &c->info->bindings.data[c->active.data[i]];
      if (binding->name == name)
        return (Resolved){ResolvedKind_Local, binding->slot, binding->captured};
    }
    i32 capture = find_capture(c->info, name);
    if (capture >= 0) return (Resolved){ResolvedKind_Upval, (u32)capture, true};
  }
  Value symbol = symbol_v(name);
  Value var = ns_resolve_var(c->vm, symbol);
  return (Resolved){ResolvedKind_Global, add_constant(c, is_nil(var) ? symbol : var), false};
}

static bool emit_expr(Compiler* c, Value form, bool tail);

static void emit_load(Compiler* c, Resolved resolved) {
  switch (resolved.kind) {
    case ResolvedKind_Local:
      emit_op(c, resolved.boxed ? Op_GetBoxed : Op_GetLocal);
      emit_u16(c, resolved.index);
      break;
    case ResolvedKind_Upval:
      emit_op(c, Op_GetUpval);
      emit_u16(c, resolved.index);
      break;
    case ResolvedKind_Global:
      emit_op(c, Op_GetGlobal);
      emit_u16(c, resolved.index);
      break;
  }
  push_depth(c);
}

static void emit_store(Compiler* c, Resolved resolved) {
  switch (resolved.kind) {
    case ResolvedKind_Local:
      emit_op(c, resolved.boxed ? Op_SetBoxed : Op_SetLocal);
      emit_u16(c, resolved.index);
      break;
    case ResolvedKind_Upval:
      emit_op(c, Op_SetUpval);
      emit_u16(c, resolved.index);
      break;
    case ResolvedKind_Global:
      emit_op(c, Op_SetGlobal);
      emit_u16(c, resolved.index);
      break;
  }
}

static bool emit_body(Compiler* c, Value forms, bool tail) {
  State* vm = c->vm;
  u32 sc = scope_begin(vm);
  Slot cursor = scope_push(vm, forms);
  if (!pairp(slot_get(cursor))) {
    emit_op(c, Op_Nil);
    push_depth(c);
    scope_pop_to(vm, sc);
    return true;
  }
  while (pairp(cdr_(slot_get(cursor)))) {
    if (!emit_expr(c, car_(slot_get(cursor)), false)) {
      scope_pop_to(vm, sc);
      return false;
    }
    emit_op(c, Op_Pop);
    pop_depth(c, 1);
    slot_set(cursor, cdr_(slot_get(cursor)));
  }
  bool falls = emit_expr(c, car_(slot_get(cursor)), tail);
  scope_pop_to(vm, sc);
  return falls;
}

static Value compile_lambda(State* vm, LambdaInfo* info, Value body, u32 name);

// The parameter list is not passed down: parse_params already recorded the
// arity and the formals' slots on `child` during analysis.
static bool emit_lambda(Compiler* c, Value body, u32 name) {
  if (c->childCursor >= c->info->children.len) {
    compiler_error(c, "lambda analysis mismatch");
    return true;
  }
  LambdaInfo* child = c->info->children.data[c->childCursor++];
  State* vm = c->vm;
  u32 sc = scope_begin(vm);
  Slot bodyRoot = scope_push(vm, body);
  Slot nested = scope_push(vm, compile_lambda(vm, child, slot_get(bodyRoot), name));
  if (slot_get(nested).tag == Tag_Unwind) {
    c->failed = true;
    scope_pop_to(vm, sc);
    return true;
  }
  Slot descriptor = scope_push(vm, make_array(vm, child->captures.len + 1));
  array_push(vm, slot_get(descriptor), slot_get(nested));
  for (u32 i = 0; i < child->captures.len; i++) {
    Capture capture = child->captures.data[i];
    i64 encoded = capture.local ? (i64)capture.index : -(i64)capture.index - 1;
    array_push(vm, slot_get(descriptor), int_v(encoded));
  }
  u32 constant = add_constant(c, slot_get(descriptor));
  emit_op(c, Op_Closure);
  emit_u16(c, constant);
  push_depth(c);
  scope_pop_to(vm, sc);
  return true;
}

static bool emit_if(Compiler* c, Value args, bool tail) {
  if (!pairp(args) || !pairp(cdr_(args))) {
    compiler_error(c, "bad if");
    return true;
  }
  State* vm = c->vm;
  u32 sc = scope_begin(vm);
  Slot argsRoot = scope_push(vm, args);
  if (!emit_expr(c, car_(slot_get(argsRoot)), false)) {
    scope_pop_to(vm, sc);
    return false;
  }
  u32 branchDepth = c->depth - 1;
  u32 falseJump = emit_jump(c, Op_JumpFalse);
  pop_depth(c, 1);
  bool thenFalls = emit_expr(c, car_(cdr_(slot_get(argsRoot))), tail);
  u32 thenDepth = c->depth;
  u32 endJump = 0;
  if (thenFalls) endJump = emit_jump(c, Op_Jump);
  u32 elseStart = c->bytes.len;
  patch_jump(c, falseJump, elseStart);
  c->depth = branchDepth;
  Value elseForms = cdr_(cdr_(slot_get(argsRoot)));
  bool elseFalls;
  if (pairp(elseForms)) elseFalls = emit_expr(c, car_(elseForms), tail);
  else {
    emit_op(c, Op_Nil);
    push_depth(c);
    elseFalls = true;
  }
  u32 elseDepth = c->depth;
  u32 end = c->bytes.len;
  if (thenFalls) patch_jump(c, endJump, end);
  if (thenFalls && elseFalls && thenDepth != elseDepth)
    compiler_error(c, "if branches have different stack depths");
  c->depth = thenFalls ? thenDepth : elseDepth;
  scope_pop_to(vm, sc);
  return thenFalls || elseFalls;
}

static bool active_has(Compiler* c, u32 name) {
  for (u32 i = c->active.len; i-- > 0;)
    if (c->info->bindings.data[c->active.data[i]].name == name) return true;
  return false;
}

// Consume the binding the analysis pass recorded next and store the value on
// top of the stack into its slot. `name` is the binding this emit site expects;
// the two passes walk the same forms in the same order, so a mismatch is a
// compiler bug and must fail loudly rather than miscompile silently.
static bool bind_next_slot(Compiler* c, u32 name) {
  if (c->bindingCursor >= c->info->bindings.len) {
    compiler_error(c, "binding was not analyzed");
    return false;
  }
  Binding* metadata = &c->info->bindings.data[c->bindingCursor];
  if (metadata->name != name) {
    compiler_error(c, "analysis and emit passes disagree on binding order");
    return false;
  }
  if (metadata->captured) emit_op(c, Op_MakeBox);
  emit_op(c, Op_SetLocal);
  emit_u16(c, metadata->slot);
  emit_op(c, Op_Pop);
  pop_depth(c, 1);
  vec_push(&c->active, c->bindingCursor++);
  return true;
}

static bool emit_let(Compiler* c, Value args, bool tail) {
  if (!pairp(args)) {
    compiler_error(c, "bad let");
    return true;
  }
  State* vm = c->vm;
  u32 sc = scope_begin(vm);
  Slot argsRoot = scope_push(vm, args);
  Slot bindings = scope_push(vm, car_(slot_get(argsRoot)));
  slot_set(bindings, strip_array_literal_head(slot_get(bindings), vm->syms.array_));
  u32 activeBase = c->active.len;
  Slot bindingRoot = scope_push(vm, nil_v());
  while (pairp(slot_get(bindings))) {
    slot_set(bindingRoot, car_(slot_get(bindings)));
    if (!pairp(slot_get(bindingRoot)) || !pairp(cdr_(slot_get(bindingRoot))) ||
        car_(slot_get(bindingRoot)).tag != Tag_Symbol) {
      compiler_error(c, "bad let binding");
      break;
    }
    if (!emit_expr(c, car_(cdr_(slot_get(bindingRoot))), false)) break;
    if (!bind_next_slot(c, car_(slot_get(bindingRoot)).id)) break;
    slot_set(bindings, cdr_(slot_get(bindings)));
  }
  // Mirror the analyzer's collect_body_defines for this let body: allocate a
  // nil-initialized (boxed if captured) slot per hoisted define, in the same
  // order, so bindingCursor stays in lockstep.
  if (c->info->userDepth > 0) {
    for (Value body = cdr_(slot_get(argsRoot)); pairp(body); body = cdr_(body)) {
      Value name = body_define_name(vm, car_(body));
      if (is_nil(name) || active_has(c, name.id)) continue;
      emit_op(c, Op_Nil);
      push_depth(c);
      if (!bind_next_slot(c, name.id)) break;
    }
  }
  bool falls = emit_body(c, cdr_(slot_get(argsRoot)), tail);
  c->active.len = activeBase;
  scope_pop_to(vm, sc);
  return falls;
}

static void emit_def_global(Compiler* c, Value name, bool isPrivate, Value doc) {
  State* vm = c->vm;
  u32 sc = scope_begin(vm);
  Slot docRoot = scope_push(vm, doc);
  Slot descriptor = scope_push(vm, make_array(vm, 3));
  array_push(vm, slot_get(descriptor), name);
  array_push(vm, slot_get(descriptor), bool_v(isPrivate));
  array_push(vm, slot_get(descriptor), slot_get(docRoot));
  emit_op(c, Op_DefGlobal);
  emit_u16(c, add_constant(c, slot_get(descriptor)));
  scope_pop_to(vm, sc);
}

static bool emit_define(Compiler* c, Value form, bool isPrivate) {
  State* vm = c->vm;
  u32 sc = scope_begin(vm);
  Slot formRoot = scope_push(vm, form);
  Value args = cdr_(slot_get(formRoot));
  if (!pairp(args)) {
    compiler_error(c, "bad define");
    scope_pop_to(vm, sc);
    return true;
  }
  Value target = car_(args);
  Value name = pairp(target) ? car_(target) : target;
  if (name.tag != Tag_Symbol) {
    compiler_error(c, "define name must be a symbol");
    scope_pop_to(vm, sc);
    return true;
  }
  if (pairp(target)) {
    if (!emit_lambda(c, cdr_(args), name.id)) {
      scope_pop_to(vm, sc);
      return false;
    }
  } else {
    Value rest = cdr_(args);
    rest = skip_docstring(rest, nullptr);
    if (!pairp(rest)) {
      compiler_error(c, "define is missing a value");
      scope_pop_to(vm, sc);
      return true;
    }
    if (!emit_expr(c, car_(rest), false)) {
      scope_pop_to(vm, sc);
      return false;
    }
  }

  if (c->info->userDepth > 0) {
    Resolved local = resolve(c, name.id);
    if (local.kind == ResolvedKind_Global) {
      compiler_error(c, "internal define was not hoisted");
      scope_pop_to(vm, sc);
      return true;
    }
    emit_store(c, local);
  } else {
    args = cdr_(slot_get(formRoot));
    Value doc = nil_v();
    skip_docstring(cdr_(args), &doc);
    emit_def_global(c, name, isPrivate, doc);
  }
  scope_pop_to(vm, sc);
  return true;
}

static bool emit_defmacro(Compiler* c, Value form) {
  State* vm = c->vm;
  u32 sc = scope_begin(vm);
  Slot formRoot = scope_push(vm, form);
  Value args = cdr_(slot_get(formRoot));
  if (!pairp(args) || car_(args).tag != Tag_Symbol || !pairp(cdr_(args))) {
    compiler_error(c, "bad defmacro");
    scope_pop_to(vm, sc);
    return true;
  }
  Value name = car_(args);
  Value rest = cdr_(args);
  if (!emit_lambda(c, cdr_(rest), name.id)) {
    scope_pop_to(vm, sc);
    return false;
  }
  emit_op(c, Op_ToMacro);

  args = cdr_(slot_get(formRoot));
  rest = cdr_(args);
  Value doc = nil_v();
  skip_docstring(cdr_(rest), &doc);
  emit_def_global(c, name, false, doc);
  scope_pop_to(vm, sc);
  return true;
}

static bool finish_control_call(Compiler* c, u32 argc, bool tail) {
  emit_op(c, tail ? Op_TailCall : Op_Call);
  emit_u16(c, argc);
  pop_depth(c, argc);
  return !tail;
}

static bool emit_call(Compiler* c, Value form, bool tail) {
  State* vm = c->vm;
  u32 sc = scope_begin(vm);
  Slot cursor = scope_push(vm, form);
  if (!emit_expr(c, car_(slot_get(cursor)), false)) {
    scope_pop_to(vm, sc);
    return false;
  }
  slot_set(cursor, cdr_(slot_get(cursor)));
  u32 argc = 0;
  while (pairp(slot_get(cursor))) {
    if (!emit_expr(c, car_(slot_get(cursor)), false)) {
      scope_pop_to(vm, sc);
      return false;
    }
    argc++;
    slot_set(cursor, cdr_(slot_get(cursor)));
  }
  if (slot_get(cursor).tag != Tag_Null) {
    compiler_error(c, "dotted call");
    scope_pop_to(vm, sc);
    return true;
  }
  scope_pop_to(vm, sc);
  return finish_control_call(c, argc, tail);
}

static bool emit_while(Compiler* c, Value args) {
  if (!pairp(args)) {
    compiler_error(c, "bad while");
    return true;
  }
  State* vm = c->vm;
  u32 sc = scope_begin(vm);
  Slot argsRoot = scope_push(vm, args);
  u32 start = c->bytes.len;
  if (!emit_expr(c, car_(slot_get(argsRoot)), false)) {
    scope_pop_to(vm, sc);
    return false;
  }
  u32 exit = emit_jump(c, Op_JumpFalse);
  pop_depth(c, 1);
  Slot body = scope_push(vm, cdr_(slot_get(argsRoot)));
  while (pairp(slot_get(body))) {
    if (!emit_expr(c, car_(slot_get(body)), false)) {
      scope_pop_to(vm, sc);
      return false;
    }
    emit_op(c, Op_Pop);
    pop_depth(c, 1);
    slot_set(body, cdr_(slot_get(body)));
  }
  emit_op(c, Op_Loop);
  u32 operand = c->bytes.len;
  emit_i32(c, (i32)((i64)start - (i64)(operand + 4)));
  patch_jump(c, exit, c->bytes.len);
  emit_op(c, Op_Nil);
  push_depth(c);
  scope_pop_to(vm, sc);
  return true;
}

static bool emit_short_circuit(Compiler* c, Value args, bool isAnd, bool tail) {
  if (!pairp(args)) {
    emit_op(c, isAnd ? Op_True : Op_False);
    push_depth(c);
    return true;
  }
  State* vm = c->vm;
  u32 sc = scope_begin(vm);
  Slot cursor = scope_push(vm, args);
  VecU32 exits = {0};
  while (pairp(cdr_(slot_get(cursor)))) {
    if (!emit_expr(c, car_(slot_get(cursor)), false)) {
      vec_deinit(&exits);
      scope_pop_to(vm, sc);
      return false;
    }
    vec_push(&exits, emit_jump(c, isAnd ? Op_JumpFalsePeek : Op_JumpTruePeek));
    emit_op(c, Op_Pop);
    pop_depth(c, 1);
    slot_set(cursor, cdr_(slot_get(cursor)));
  }
  bool falls = emit_expr(c, car_(slot_get(cursor)), tail);
  u32 end = c->bytes.len;
  for (u32 i = 0; i < exits.len; i++) patch_jump(c, exits.data[i], end);
  bool result = exits.len ? true : falls;
  vec_deinit(&exits);
  scope_pop_to(vm, sc);
  return result;
}

static bool emit_cond(Compiler* c, Value clauses, bool tail) {
  State* vm = c->vm;
  u32 sc = scope_begin(vm);
  Slot cursor = scope_push(vm, clauses);
  VecU32 exits = {0};
  const u32 baseDepth = c->depth;
  bool anyFalls = false;
  bool hasElse = false;
  Slot clauseRoot = scope_push(vm, nil_v());

  while (pairp(slot_get(cursor))) {
    slot_set(clauseRoot, car_(slot_get(cursor)));
    if (!pairp(slot_get(clauseRoot))) {
      compiler_error(c, "bad cond clause");
      vec_deinit(&exits);
      scope_pop_to(vm, sc);
      return true;
    }
    Value test = car_(slot_get(clauseRoot));
    if (sym_is(test, vm->syms.else_)) {
      if (!pairp(cdr_(slot_get(clauseRoot)))) {
        compiler_error(c, "cond else needs a body");
        vec_deinit(&exits);
        scope_pop_to(vm, sc);
        return true;
      }
      hasElse = true;
      bool falls = emit_body(c, cdr_(slot_get(clauseRoot)), tail);
      anyFalls = anyFalls || falls;
      break;
    }

    if (!emit_expr(c, test, false)) {
      vec_deinit(&exits);
      scope_pop_to(vm, sc);
      return false;
    }
    if (!pairp(cdr_(slot_get(clauseRoot)))) {
      // The clause's own test value is the result, so this exit reaches the end
      // of the cond carrying a value: the form falls through even if every
      // clause body below ends in a tail call.
      u32 next = emit_jump(c, Op_JumpFalsePeek);
      vec_push(&exits, emit_jump(c, Op_Jump));
      anyFalls = true;
      patch_jump(c, next, c->bytes.len);
      emit_op(c, Op_Pop);
      pop_depth(c, 1);
    } else {
      u32 next = emit_jump(c, Op_JumpFalse);
      pop_depth(c, 1);
      bool falls = emit_body(c, cdr_(slot_get(clauseRoot)), tail);
      if (falls) {
        vec_push(&exits, emit_jump(c, Op_Jump));
        anyFalls = true;
      }
      patch_jump(c, next, c->bytes.len);
    }
    c->depth = baseDepth;
    slot_set(cursor, cdr_(slot_get(cursor)));
  }

  if (!hasElse) {
    emit_op(c, Op_Nil);
    push_depth(c);
    anyFalls = true;
  }
  u32 end = c->bytes.len;
  for (u32 i = 0; i < exits.len; i++) patch_jump(c, exits.data[i], end);
  c->depth = anyFalls ? baseDepth + 1 : baseDepth;
  vec_deinit(&exits);
  scope_pop_to(vm, sc);
  return anyFalls;
}

static void emit_quoted_symbol(Compiler* c, u32 name) {
  emit_op(c, Op_Const);
  emit_u16(c, add_constant(c, symbol_v(name)));
  push_depth(c);
}

static bool emit_quasiquote(Compiler* c, Value form, u32 depth) {
  State* vm = c->vm;
  u32 sc = scope_begin(vm);
  Slot formRoot = scope_push(vm, form);
  if (!pairp(slot_get(formRoot))) {
    emit_op(c, Op_Const);
    emit_u16(c, add_constant(c, slot_get(formRoot)));
    push_depth(c);
    scope_pop_to(vm, sc);
    return true;
  }

  Value head = car_(slot_get(formRoot));
  if (sym_is(head, vm->syms.unquote_)) {
    Value args = cdr_(slot_get(formRoot));
    if (!pairp(args)) {
      compiler_error(c, "bad unquote");
      scope_pop_to(vm, sc);
      return true;
    }
    if (depth == 1) {
      bool result = emit_expr(c, car_(args), false);
      scope_pop_to(vm, sc);
      return result;
    }
    emit_quoted_symbol(c, vm->syms.unquote_);
    if (!emit_quasiquote(c, car_(args), depth - 1)) {
      scope_pop_to(vm, sc);
      return false;
    }
    emit_op(c, Op_List);
    emit_u16(c, 2);
    pop_depth(c, 1);
    scope_pop_to(vm, sc);
    return true;
  }
  if (sym_is(head, vm->syms.quasiquote_)) {
    Value args = cdr_(slot_get(formRoot));
    if (!pairp(args)) {
      compiler_error(c, "bad nested quasiquote");
      scope_pop_to(vm, sc);
      return true;
    }
    emit_quoted_symbol(c, vm->syms.quasiquote_);
    if (!emit_quasiquote(c, car_(args), depth + 1)) {
      scope_pop_to(vm, sc);
      return false;
    }
    emit_op(c, Op_List);
    emit_u16(c, 2);
    pop_depth(c, 1);
    scope_pop_to(vm, sc);
    return true;
  }

  if (pairp(head) && sym_is(car_(head), vm->syms.unquoteSplicing_) && depth == 1) {
    Value spliceArgs = cdr_(head);
    if (!pairp(spliceArgs)) {
      compiler_error(c, "bad unquote-splicing");
      scope_pop_to(vm, sc);
      return true;
    }
    Slot spliceRoot = scope_push(vm, spliceArgs);
    if (!emit_expr(c, car_(slot_get(spliceRoot)), false)) {
      scope_pop_to(vm, sc);
      return false;
    }
    if (!emit_quasiquote(c, cdr_(slot_get(formRoot)), depth)) {
      scope_pop_to(vm, sc);
      return false;
    }
    emit_op(c, Op_Append2);
    pop_depth(c, 1);
    scope_pop_to(vm, sc);
    return true;
  }

  if (!emit_quasiquote(c, car_(slot_get(formRoot)), depth)) {
    scope_pop_to(vm, sc);
    return false;
  }
  if (!emit_quasiquote(c, cdr_(slot_get(formRoot)), depth)) {
    scope_pop_to(vm, sc);
    return false;
  }
  emit_op(c, Op_Cons);
  pop_depth(c, 1);
  scope_pop_to(vm, sc);
  return true;
}

static void emit_constant(Compiler* c, Value value) {
  emit_op(c, Op_Const);
  emit_u16(c, add_constant(c, value));
  push_depth(c);
}

static void emit_native(Compiler* c, const char* name, NativeFn native) {
  State* vm = c->vm;
  u32 sc = scope_begin(vm);
  Slot function = scope_push(vm, make_native(vm, name, native));
  emit_constant(c, slot_get(function));
  scope_pop_to(vm, sc);
}

static bool emit_thunk_expr(Compiler* c, Value form) {
  State* vm = c->vm;
  u32 sc = scope_begin(vm);
  Slot formRoot = scope_push(vm, form);
  Slot body = scope_push(vm, make_pair(vm, slot_get(formRoot), null_v()));
  bool result = emit_lambda(c, slot_get(body), 0);
  scope_pop_to(vm, sc);
  return result;
}

static bool emit_thunk_body(Compiler* c, Value forms) { return emit_lambda(c, forms, 0); }

static bool emit_binding_control(Compiler* c, Value args, bool tail, const char* badForm,
                                 const char* badBinding, const char* nativeName, NativeFn native,
                                 bool thunkBindings) {
  if (!pairp(args)) {
    compiler_error(c, badForm);
    return true;
  }
  State* vm = c->vm;
  u32 sc = scope_begin(vm);
  Slot argsRoot = scope_push(vm, args);
  emit_native(c, nativeName, native);
  Slot bindings = scope_push(vm, car_(slot_get(argsRoot)));
  slot_set(bindings, strip_array_literal_head(slot_get(bindings), vm->syms.array_));
  u32 argc = 0;
  Slot bindingRoot = scope_push(vm, nil_v());
  while (pairp(slot_get(bindings))) {
    slot_set(bindingRoot, car_(slot_get(bindings)));
    if (!pairp(slot_get(bindingRoot)) || !pairp(cdr_(slot_get(bindingRoot)))) {
      compiler_error(c, badBinding);
      scope_pop_to(vm, sc);
      return true;
    }
    if (thunkBindings) {
      if (!emit_thunk_expr(c, car_(slot_get(bindingRoot))) ||
          !emit_thunk_expr(c, car_(cdr_(slot_get(bindingRoot))))) {
        scope_pop_to(vm, sc);
        return false;
      }
    } else if (!emit_expr(c, car_(slot_get(bindingRoot)), false) ||
               !emit_expr(c, car_(cdr_(slot_get(bindingRoot))), false)) {
      scope_pop_to(vm, sc);
      return false;
    }
    argc += 2;
    slot_set(bindings, cdr_(slot_get(bindings)));
  }
  if (!emit_thunk_body(c, cdr_(slot_get(argsRoot)))) {
    scope_pop_to(vm, sc);
    return false;
  }
  scope_pop_to(vm, sc);
  return finish_control_call(c, argc + 1, tail);
}

static bool emit_handler_bind(Compiler* c, Value args, bool tail) {
  return emit_binding_control(c, args, tail, "bad handler-bind", "bad handler-bind binding",
                              "%handler-bind", vm_control_handler_bind, false);
}

static bool emit_restart_case(Compiler* c, Value args, bool tail) {
  if (!pairp(args)) {
    compiler_error(c, "bad restart-case");
    return true;
  }
  State* vm = c->vm;
  u32 sc = scope_begin(vm);
  Slot argsRoot = scope_push(vm, args);
  emit_native(c, "%restart-case", vm_control_restart_case);
  if (!emit_thunk_expr(c, car_(slot_get(argsRoot)))) {
    scope_pop_to(vm, sc);
    return false;
  }
  u32 argc = 1;
  Slot clauses = scope_push(vm, cdr_(slot_get(argsRoot)));
  while (pairp(slot_get(clauses))) {
    Value clause = car_(slot_get(clauses));
    if (!pairp(clause) || car_(clause).tag != Tag_Symbol) {
      compiler_error(c, "bad restart-case clause");
      scope_pop_to(vm, sc);
      return true;
    }
    Value doc = nil_v();
    Value rest = skip_docstring(cdr_(clause), &doc);
    if (!pairp(rest)) {
      compiler_error(c, "restart-case clause needs parameters");
      scope_pop_to(vm, sc);
      return true;
    }
    emit_constant(c, car_(clause));
    emit_constant(c, doc);
    if (!emit_lambda(c, cdr_(rest), car_(clause).id)) {
      scope_pop_to(vm, sc);
      return false;
    }
    argc += 3;
    slot_set(clauses, cdr_(slot_get(clauses)));
  }
  scope_pop_to(vm, sc);
  return finish_control_call(c, argc, tail);
}

static bool emit_try(Compiler* c, Value args, bool tail) {
  State* vm = c->vm;
  u32 sc = scope_begin(vm);
  Slot cursor = scope_push(vm, args);
  u32 bodyCount = 0;
  for (Value scan = slot_get(cursor); pairp(scan); scan = cdr_(scan)) {
    Value part = car_(scan);
    if (pairp(part) && sym_is(car_(part), vm->syms.catch_)) break;
    bodyCount++;
  }

  emit_native(c, "%try", vm_control_try);
  emit_constant(c, int_v(bodyCount));
  u32 argc = 1;
  for (u32 i = 0; i < bodyCount; i++) {
    if (!emit_thunk_expr(c, car_(slot_get(cursor)))) {
      scope_pop_to(vm, sc);
      return false;
    }
    argc++;
    slot_set(cursor, cdr_(slot_get(cursor)));
  }
  Slot clauseRoot = scope_push(vm, nil_v());
  while (pairp(slot_get(cursor))) {
    slot_set(clauseRoot, car_(slot_get(cursor)));
    if (!pairp(slot_get(clauseRoot)) || !sym_is(car_(slot_get(clauseRoot)), vm->syms.catch_) ||
        !pairp(cdr_(slot_get(clauseRoot)))) {
      compiler_error(c, "bad catch clause");
      scope_pop_to(vm, sc);
      return true;
    }
    Value spec = car_(cdr_(slot_get(clauseRoot)));
    if (!pairp(spec) || !pairp(cdr_(spec)) || car_(cdr_(spec)).tag != Tag_Symbol) {
      compiler_error(c, "bad catch specification");
      scope_pop_to(vm, sc);
      return true;
    }
    if (!emit_thunk_expr(c, car_(spec))) {
      scope_pop_to(vm, sc);
      return false;
    }
    if (!emit_lambda(c, cdr_(cdr_(slot_get(clauseRoot))), 0)) {
      scope_pop_to(vm, sc);
      return false;
    }
    argc += 2;
    slot_set(cursor, cdr_(slot_get(cursor)));
  }
  scope_pop_to(vm, sc);
  return finish_control_call(c, argc, tail);
}

static bool emit_unwind_protect(Compiler* c, Value args, bool tail) {
  if (!pairp(args)) {
    compiler_error(c, "bad unwind-protect");
    return true;
  }
  State* vm = c->vm;
  u32 sc = scope_begin(vm);
  Slot cursor = scope_push(vm, args);
  emit_native(c, "%unwind-protect", vm_control_unwind_protect);
  u32 argc = 0;
  while (pairp(slot_get(cursor))) {
    if (!emit_thunk_expr(c, car_(slot_get(cursor)))) {
      scope_pop_to(vm, sc);
      return false;
    }
    argc++;
    slot_set(cursor, cdr_(slot_get(cursor)));
  }
  scope_pop_to(vm, sc);
  return finish_control_call(c, argc, tail);
}

static bool emit_with_params(Compiler* c, Value args, bool tail) {
  return emit_binding_control(c, args, tail, "bad with-params", "bad with-params binding",
                              "%with-params", vm_control_with_params, true);
}

static bool emit_defparam(Compiler* c, Value args, bool tail) {
  if (c->info->userDepth > 0 || c->active.len > c->info->initialCount) {
    compiler_error(c, "defparam only allowed at top level");
    return true;
  }
  if (!pairp(args) || car_(args).tag != Tag_Symbol) {
    compiler_error(c, "bad defparam");
    return true;
  }
  State* vm = c->vm;
  u32 sc = scope_begin(vm);
  Slot argsRoot = scope_push(vm, args);
  Value name = car_(slot_get(argsRoot));
  Slot rest = scope_push(vm, cdr_(slot_get(argsRoot)));
  Value doc = nil_v();
  slot_set(rest, skip_docstring(slot_get(rest), &doc));
  Slot docRoot = scope_push(vm, doc);
  if (!pairp(slot_get(rest))) {
    compiler_error(c, "defparam missing default");
    scope_pop_to(vm, sc);
    return true;
  }
  emit_native(c, "%defparam", vm_control_defparam);
  emit_constant(c, name);
  emit_constant(c, slot_get(docRoot));
  if (!emit_expr(c, car_(slot_get(rest)), false)) {
    scope_pop_to(vm, sc);
    return false;
  }
  scope_pop_to(vm, sc);
  return finish_control_call(c, 3, tail);
}

static bool emit_data_control(Compiler* c, Value args, bool tail, const char* helperName,
                              NativeFn native, bool requireArg) {
  State* vm = c->vm;
  u32 sc = scope_begin(vm);
  Slot cursor = scope_push(vm, args);
  if (requireArg && !pairp(slot_get(cursor))) {
    compiler_error(c, "missing control form argument");
    scope_pop_to(vm, sc);
    return true;
  }
  emit_native(c, helperName, native);
  u32 argc = 0;
  while (pairp(slot_get(cursor))) {
    emit_constant(c, car_(slot_get(cursor)));
    argc++;
    slot_set(cursor, cdr_(slot_get(cursor)));
  }
  scope_pop_to(vm, sc);
  return finish_control_call(c, argc, tail);
}

static bool emit_expr(Compiler* c, Value form, bool tail) {
  State* vm = c->vm;
  u32 sc = scope_begin(vm);
  Slot formRoot = scope_push(vm, form);
  form = slot_get(formRoot);
  if (form.tag == Tag_Symbol) {
    emit_load(c, resolve(c, form.id));
    scope_pop_to(vm, sc);
    return true;
  }
  if (!pairp(form)) {
    switch (form.tag) {
      case Tag_Nil: emit_op(c, Op_Nil); break;
      case Tag_True: emit_op(c, Op_True); break;
      case Tag_False: emit_op(c, Op_False); break;
      case Tag_Null: emit_op(c, Op_Null); break;
      case Tag_Int:
        if (form.i >= INT8_MIN && form.i <= INT8_MAX) {
          emit_op(c, Op_Int8);
          vec_push(&c->bytes, (char)(i8)form.i);
        } else {
          emit_op(c, Op_Const);
          emit_u16(c, add_constant(c, form));
        }
        break;
      default:
        emit_op(c, Op_Const);
        emit_u16(c, add_constant(c, form));
        break;
    }
    push_depth(c);
    scope_pop_to(vm, sc);
    return true;
  }

  Value head = car_(slot_get(formRoot));
  Value args = cdr_(slot_get(formRoot));
  if (head.tag == Tag_Symbol) {
    u32 name = head.id;
    if (name == vm->syms.quote_) {
      if (!pairp(args)) compiler_error(c, "bad quote");
      Value quoted = pairp(args) ? car_(args) : nil_v();
      emit_op(c, Op_Const);
      emit_u16(c, add_constant(c, quoted));
      push_depth(c);
      scope_pop_to(vm, sc);
      return true;
    }
    if (name == vm->syms.quasiquote_) {
      if (!pairp(args)) {
        compiler_error(c, "bad quasiquote");
        scope_pop_to(vm, sc);
        return true;
      }
      bool r = emit_quasiquote(c, car_(args), 1);
      scope_pop_to(vm, sc);
      return r;
    }
    if (name == vm->syms.unquote_ || name == vm->syms.unquoteSplicing_) {
      compiler_error(c, "unquote outside quasiquote");
      scope_pop_to(vm, sc);
      return true;
    }
    if (name == vm->syms.if_) {
      bool r = emit_if(c, args, tail);
      scope_pop_to(vm, sc);
      return r;
    }
    if (name == vm->syms.begin_ || name == vm->syms.do_) {
      bool r = emit_body(c, args, tail);
      scope_pop_to(vm, sc);
      return r;
    }
    if (name == vm->syms.lambda_ || name == vm->syms.fn_) {
      if (!pairp(args)) compiler_error(c, "bad lambda");
      bool r = pairp(args) ? emit_lambda(c, cdr_(args), 0) : true;
      scope_pop_to(vm, sc);
      return r;
    }
    if (name == vm->syms.let_) {
      bool r = emit_let(c, args, tail);
      scope_pop_to(vm, sc);
      return r;
    }
    if (is_define_head(vm, name)) {
      bool r = emit_define(c, slot_get(formRoot), name == vm->syms.definePriv_);
      scope_pop_to(vm, sc);
      return r;
    }
    if (name == vm->syms.defmacro_) {
      bool r = emit_defmacro(c, slot_get(formRoot));
      scope_pop_to(vm, sc);
      return r;
    }
    if (name == vm->syms.setBang_) {
      if (!pairp(args) || car_(args).tag != Tag_Symbol || !pairp(cdr_(args))) {
        compiler_error(c, "bad set!");
        scope_pop_to(vm, sc);
        return true;
      }
      Value target = car_(args);
      if (!emit_expr(c, car_(cdr_(args)), false)) {
        scope_pop_to(vm, sc);
        return false;
      }
      emit_store(c, resolve(c, target.id));
      scope_pop_to(vm, sc);
      return true;
    }
    if (name == vm->syms.while_) {
      bool r = emit_while(c, args);
      scope_pop_to(vm, sc);
      return r;
    }
    if (name == vm->syms.and_) {
      bool r = emit_short_circuit(c, args, true, tail);
      scope_pop_to(vm, sc);
      return r;
    }
    if (name == vm->syms.or_) {
      bool r = emit_short_circuit(c, args, false, tail);
      scope_pop_to(vm, sc);
      return r;
    }
    if (name == vm->syms.cond_) {
      bool r = emit_cond(c, args, tail);
      scope_pop_to(vm, sc);
      return r;
    }
    if (name == vm->syms.handlerBind_) {
      bool r = emit_handler_bind(c, args, tail);
      scope_pop_to(vm, sc);
      return r;
    }
    if (name == vm->syms.restartCase_) {
      bool r = emit_restart_case(c, args, tail);
      scope_pop_to(vm, sc);
      return r;
    }
    if (name == vm->syms.try_) {
      bool r = emit_try(c, args, tail);
      scope_pop_to(vm, sc);
      return r;
    }
    if (name == vm->syms.unwindProtect_ || name == vm->syms.defer_) {
      bool r = emit_unwind_protect(c, args, tail);
      scope_pop_to(vm, sc);
      return r;
    }
    if (name == vm->syms.withParams_) {
      bool r = emit_with_params(c, args, tail);
      scope_pop_to(vm, sc);
      return r;
    }
    if (name == vm->syms.defparam_) {
      bool r = emit_defparam(c, args, tail);
      scope_pop_to(vm, sc);
      return r;
    }
    if (name == vm->syms.ns_) {
      bool r = emit_data_control(c, args, tail, "%ns", vm_control_ns, true);
      scope_pop_to(vm, sc);
      return r;
    }
    if (name == vm->syms.inNs_) {
      bool r = emit_data_control(c, args, tail, "%in-ns", vm_control_in_ns, true);
      scope_pop_to(vm, sc);
      return r;
    }
    if (name == vm->syms.require_) {
      bool r = emit_data_control(c, args, tail, "%require", vm_control_require, false);
      scope_pop_to(vm, sc);
      return r;
    }
  }
  bool r = emit_call(c, slot_get(formRoot), tail);
  scope_pop_to(vm, sc);
  return r;
}

static Value compile_lambda(State* vm, LambdaInfo* info, Value body, u32 name) {
  u32 sc = scope_begin(vm);
  Slot bodyRoot = scope_push(vm, body);
  slot_set(bodyRoot, skip_docstring(slot_get(bodyRoot), nullptr));
  Slot constants = scope_push(vm, make_array(vm, 8));
  Compiler compiler;
  compiler_init(&compiler, vm, info, constants);

  for (u32 i = 0; i < info->initialCount; i++) {
    Binding* binding = &info->bindings.data[i];
    if (!binding->captured) continue;
    emit_op(&compiler, Op_GetLocal);
    emit_u16(&compiler, binding->slot);
    push_depth(&compiler);
    emit_op(&compiler, Op_MakeBox);
    emit_op(&compiler, Op_SetLocal);
    emit_u16(&compiler, binding->slot);
    emit_op(&compiler, Op_Pop);
    pop_depth(&compiler, 1);
  }

  bool falls = emit_body(&compiler, slot_get(bodyRoot), true);
  if (falls) emit_op(&compiler, Op_Return);
  if (compiler.failed) {
    compiler_deinit(&compiler);
    return scope_exit(vm, sc, unwind_v());
  }
  CodeSpec spec = {
      .nfixed = info->nfixed,
      .hasRest = info->hasRest,
      .nupvals = info->captures.len,
      .nlocals = info->bindings.len,
      .maxStack = compiler.maxDepth,
      .name = name,
  };
  Value result =
      make_code(vm, (const u8*)compiler.bytes.data, compiler.bytes.len, slot_get(constants), &spec);
  compiler_deinit(&compiler);
  return scope_exit(vm, sc, result);
}

Value compile_form(State* vm, Value expanded) {
  u32 sc = scope_begin(vm);
  Slot formRoot = scope_push(vm, expanded);
  LambdaInfo top;
  lambda_info_init(&top, nullptr, 0);
  Value bodyPair = make_pair(vm, slot_get(formRoot), null_v());
  Slot body = scope_push(vm, bodyPair);
  analyze_body(vm, &top, slot_get(body));
  Value result = compile_lambda(vm, &top, slot_get(body), 0);
  lambda_info_deinit(&top);
  return scope_exit(vm, sc, result);
}
