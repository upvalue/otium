#include "compile.hpp"
#include "code.hpp"
#include "eval.hpp"
#include "form.hpp"
#include "heap.hpp"
#include "ns.hpp"
#include "state.hpp"

namespace ot {

struct Binding {
  u32 name;
  u32 slot;
  bool captured;
};

struct Capture {
  u32 name;
  bool local;
  u32 index;
};

struct LambdaInfo {
  LambdaInfo* parent;
  Vec<Binding> bindings;
  Vec<u32> active;
  Vec<Capture> captures;
  Vec<LambdaInfo*> children;
  u32 nfixed;
  bool hasRest;
  u32 initialCount;
  u32 userDepth;

  explicit LambdaInfo(LambdaInfo* parent_, u32 userDepth_)
      : parent(parent_), nfixed(0), hasRest(false), initialCount(0), userDepth(userDepth_) {}
  ~LambdaInfo() {
    for (u32 i = 0; i < children.len; i++) delete children[i];
  }
};

static i32 find_active(LambdaInfo& lambda, u32 name) {
  for (u32 i = lambda.active.len; i-- > 0;) {
    u32 binding = lambda.active[i];
    if (lambda.bindings[binding].name == name) return (i32)binding;
  }
  return -1;
}

static i32 find_capture(LambdaInfo& lambda, u32 name) {
  for (u32 i = 0; i < lambda.captures.len; i++)
    if (lambda.captures[i].name == name) return (i32)i;
  return -1;
}

static u32 add_binding(LambdaInfo& lambda, u32 name) {
  u32 index = lambda.bindings.len;
  lambda.bindings.push(Binding{name, index, false});
  lambda.active.push(index);
  return index;
}

static i32 capture_name(LambdaInfo& lambda, u32 name) {
  i32 existing = find_capture(lambda, name);
  if (existing >= 0) return existing;
  if (!lambda.parent) return -1;
  i32 local = find_active(*lambda.parent, name);
  Capture capture{name, false, 0};
  if (local >= 0) {
    lambda.parent->bindings[(u32)local].captured = true;
    capture.local = true;
    capture.index = lambda.parent->bindings[(u32)local].slot;
  } else {
    i32 parentCapture = capture_name(*lambda.parent, name);
    if (parentCapture < 0) return -1;
    capture.index = (u32)parentCapture;
  }
  lambda.captures.push(capture);
  return (i32)(lambda.captures.len - 1);
}

static void analyze_expr(State&, LambdaInfo&, Value);
static Value defined_name(Value);
static void analyze_body(State&, LambdaInfo&, Value);

static void analyze_quasiquote(State& state, LambdaInfo& lambda, Value form, u32 depth) {
  if (!pairp(form)) return;
  Value head = car_(form);
  if (sym_is(head, state.syms.unquote_)) {
    Value args = cdr_(form);
    if (pairp(args)) {
      if (depth == 1) analyze_expr(state, lambda, car_(args));
      else analyze_quasiquote(state, lambda, car_(args), depth - 1);
    }
    return;
  }
  if (sym_is(head, state.syms.quasiquote_)) {
    Value args = cdr_(form);
    if (pairp(args)) analyze_quasiquote(state, lambda, car_(args), depth + 1);
    return;
  }
  if (pairp(head) && sym_is(car_(head), state.syms.unquoteSplicing_) && depth == 1) {
    Value spliceArgs = cdr_(head);
    if (pairp(spliceArgs)) analyze_expr(state, lambda, car_(spliceArgs));
  } else {
    analyze_quasiquote(state, lambda, head, depth);
  }
  analyze_quasiquote(state, lambda, cdr_(form), depth);
}

static bool is_define_head(State& state, u32 name) {
  return name == state.syms.define_ || name == state.syms.def_ || name == state.syms.definePriv_;
}

static LambdaInfo& add_compiler_thunk(LambdaInfo& parent) {
  LambdaInfo* child = new LambdaInfo(&parent, parent.userDepth);
  parent.children.push(child);
  return *child;
}

static void analyze_thunk_expr(State& state, LambdaInfo& parent, Value form) {
  LambdaInfo& child = add_compiler_thunk(parent);
  if (child.userDepth > 0 && pairp(form) && car_(form).tag == Tag::Symbol) {
    u32 head = car_(form).id;
    if (is_define_head(state, head)) {
      Value name = defined_name(form);
      if (name.tag == Tag::Symbol) add_binding(child, name.id);
    }
  }
  child.initialCount = child.bindings.len;
  analyze_expr(state, child, form);
}

static void analyze_thunk_body(State& state, LambdaInfo& parent, Value forms) {
  LambdaInfo& child = add_compiler_thunk(parent);
  analyze_body(state, child, forms);
}

static void analyze_binding_control(State& state, LambdaInfo& lambda, Value args,
                                    bool thunkBindings) {
  if (!pairp(args)) return;
  Value bindings = strip_array_literal_head(car_(args), state.syms.array_);
  for (Value cursor = bindings; pairp(cursor); cursor = cdr_(cursor)) {
    Value binding = car_(cursor);
    if (pairp(binding)) {
      if (thunkBindings) analyze_thunk_expr(state, lambda, car_(binding));
      else analyze_expr(state, lambda, car_(binding));
    }
    if (pairp(binding) && pairp(cdr_(binding))) {
      if (thunkBindings) analyze_thunk_expr(state, lambda, car_(cdr_(binding)));
      else analyze_expr(state, lambda, car_(cdr_(binding)));
    }
  }
  analyze_thunk_body(state, lambda, cdr_(args));
}

static void analyze_one_arg_lambda(State& state, LambdaInfo& parent, Value param, Value body) {
  LambdaInfo* child = new LambdaInfo(&parent, parent.userDepth + 1);
  parent.children.push(child);
  if (param.tag == Tag::Symbol) {
    add_binding(*child, param.id);
    child->nfixed = 1;
  }
  analyze_body(state, *child, body);
}

static bool parse_params(State& state, LambdaInfo& lambda, Value params) {
  if (params.tag == Tag::Symbol) {
    lambda.hasRest = true;
    add_binding(lambda, params.id);
    return true;
  }
  params = strip_array_literal_head(params, state.syms.array_);
  while (pairp(params)) {
    Value param = car_(params);
    if (sym_is(param, state.syms.amp_)) {
      params = cdr_(params);
      if (!pairp(params) || car_(params).tag != Tag::Symbol) return false;
      lambda.hasRest = true;
      add_binding(lambda, car_(params).id);
      return cdr_(params).tag == Tag::Null;
    }
    if (param.tag != Tag::Symbol) return false;
    add_binding(lambda, param.id);
    lambda.nfixed++;
    params = cdr_(params);
  }
  if (params.tag == Tag::Symbol) {
    lambda.hasRest = true;
    add_binding(lambda, params.id);
    return true;
  }
  return params.tag == Tag::Null;
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
static Value body_define_name(State& state, Value form) {
  if (!pairp(form) || car_(form).tag != Tag::Symbol) return nil_v();
  u32 head = car_(form).id;
  if (!is_define_head(state, head)) return nil_v();
  Value name = defined_name(form);
  return name.tag == Tag::Symbol ? name : nil_v();
}

static void collect_body_defines(State& state, LambdaInfo& lambda, Value forms) {
  if (lambda.userDepth == 0) return;
  for (Value cursor = forms; pairp(cursor); cursor = cdr_(cursor)) {
    Value name = body_define_name(state, car_(cursor));
    if (is_nil(name) || find_active(lambda, name.id) >= 0) continue;
    add_binding(lambda, name.id);
  }
}

static void analyze_body(State& state, LambdaInfo& lambda, Value forms) {
  collect_body_defines(state, lambda, forms);
  lambda.initialCount = lambda.bindings.len;
  for (Value cursor = forms; pairp(cursor); cursor = cdr_(cursor))
    analyze_expr(state, lambda, car_(cursor));
}

static void analyze_lambda(State& state, LambdaInfo& parent, Value params, Value body) {
  LambdaInfo* child = new LambdaInfo(&parent, parent.userDepth + 1);
  parent.children.push(child);
  if (!parse_params(state, *child, params)) return;
  analyze_body(state, *child, body);
}

static void analyze_expr(State& state, LambdaInfo& lambda, Value form) {
  if (form.tag == Tag::Symbol) {
    if (!sym_qualified(state, form.id) && find_active(lambda, form.id) < 0)
      (void)capture_name(lambda, form.id);
    return;
  }
  if (!pairp(form)) return;
  Value head = car_(form);
  Value args = cdr_(form);
  if (head.tag == Tag::Symbol) {
    u32 name = head.id;
    if (name == state.syms.quote_) return;
    if (name == state.syms.quasiquote_) {
      if (pairp(args)) analyze_quasiquote(state, lambda, car_(args), 1);
      return;
    }
    if (name == state.syms.lambda_ || name == state.syms.fn_) {
      if (pairp(args)) analyze_lambda(state, lambda, car_(args), cdr_(args));
      return;
    }
    if (is_define_head(state, name)) {
      if (!pairp(args)) return;
      Value target = car_(args);
      Value rest = cdr_(args);
      if (pairp(target)) analyze_lambda(state, lambda, cdr_(target), rest);
      else {
        rest = skip_docstring(rest);
        if (pairp(rest)) analyze_expr(state, lambda, car_(rest));
      }
      return;
    }
    if (name == state.syms.defmacro_) {
      if (pairp(args) && pairp(cdr_(args)))
        analyze_lambda(state, lambda, car_(cdr_(args)), cdr_(cdr_(args)));
      return;
    }
    if (name == state.syms.setBang_) {
      if (pairp(args) && car_(args).tag == Tag::Symbol && !sym_qualified(state, car_(args).id) &&
          find_active(lambda, car_(args).id) < 0)
        (void)capture_name(lambda, car_(args).id);
      if (pairp(args) && pairp(cdr_(args))) analyze_expr(state, lambda, car_(cdr_(args)));
      return;
    }
    if (name == state.syms.let_) {
      if (!pairp(args)) return;
      u32 activeBase = lambda.active.len;
      Value bindings = car_(args);
      bindings = strip_array_literal_head(bindings, state.syms.array_);
      while (pairp(bindings)) {
        Value binding = car_(bindings);
        if (pairp(binding) && pairp(cdr_(binding))) {
          analyze_expr(state, lambda, car_(cdr_(binding)));
          if (car_(binding).tag == Tag::Symbol) add_binding(lambda, car_(binding).id);
        }
        bindings = cdr_(bindings);
      }
      collect_body_defines(state, lambda, cdr_(args));
      for (Value body = cdr_(args); pairp(body); body = cdr_(body))
        analyze_expr(state, lambda, car_(body));
      lambda.active.len = activeBase;
      return;
    }
    if (name == state.syms.ns_ || name == state.syms.inNs_ || name == state.syms.require_) return;
    if (name == state.syms.handlerBind_) {
      analyze_binding_control(state, lambda, args, false);
      return;
    }
    if (name == state.syms.restartCase_) {
      if (!pairp(args)) return;
      analyze_thunk_expr(state, lambda, car_(args));
      for (Value cursor = cdr_(args); pairp(cursor); cursor = cdr_(cursor)) {
        Value clause = car_(cursor);
        if (!pairp(clause)) continue;
        Value rest = cdr_(clause);
        rest = skip_docstring(rest);
        if (pairp(rest)) analyze_lambda(state, lambda, car_(rest), cdr_(rest));
      }
      return;
    }
    if (name == state.syms.try_) {
      Value cursor = args;
      while (pairp(cursor)) {
        Value part = car_(cursor);
        if (pairp(part) && sym_is(car_(part), state.syms.catch_)) break;
        analyze_thunk_expr(state, lambda, part);
        cursor = cdr_(cursor);
      }
      while (pairp(cursor)) {
        Value clause = car_(cursor);
        if (pairp(clause) && pairp(cdr_(clause))) {
          Value spec = car_(cdr_(clause));
          if (pairp(spec)) {
            analyze_thunk_expr(state, lambda, car_(spec));
            if (pairp(cdr_(spec)))
              analyze_one_arg_lambda(state, lambda, car_(cdr_(spec)), cdr_(cdr_(clause)));
          }
        }
        cursor = cdr_(cursor);
      }
      return;
    }
    if (name == state.syms.unwindProtect_ || name == state.syms.defer_) {
      for (Value cursor = args; pairp(cursor); cursor = cdr_(cursor))
        analyze_thunk_expr(state, lambda, car_(cursor));
      return;
    }
    if (name == state.syms.withParams_) {
      analyze_binding_control(state, lambda, args, true);
      return;
    }
    if (name == state.syms.defparam_) {
      if (!pairp(args)) return;
      Value rest = cdr_(args);
      rest = skip_docstring(rest);
      if (pairp(rest)) analyze_expr(state, lambda, car_(rest));
      return;
    }
  }
  for (Value cursor = form; pairp(cursor); cursor = cdr_(cursor))
    analyze_expr(state, lambda, car_(cursor));
}

enum class ResolvedKind : u8 { Local, Upval, Global };
struct Resolved {
  ResolvedKind kind;
  u32 index;
  bool boxed;
};

struct Compiler {
  State& state;
  LambdaInfo& info;
  Buf bytes;
  Slot constants;
  Vec<u32> active;
  u32 bindingCursor;
  u32 childCursor;
  u32 depth;
  u32 maxDepth;
  bool failed;

  Compiler(State& state_, LambdaInfo& info_, Slot constants_)
      : state(state_), info(info_), constants(constants_), bindingCursor(info_.initialCount),
        childCursor(0), depth(0), maxDepth(0), failed(false) {
    for (u32 i = 0; i < info.initialCount; i++) active.push(i);
  }
};

static void compiler_error(Compiler& compiler, const char* message) {
  if (!compiler.failed) (void)raise_error(compiler.state, "compile: %s", message);
  compiler.failed = true;
}

static void push_depth(Compiler& compiler) {
  compiler.depth++;
  if (compiler.depth > compiler.maxDepth) compiler.maxDepth = compiler.depth;
}

static void pop_depth(Compiler& compiler, u32 count = 1) {
  if (count > compiler.depth) {
    compiler_error(compiler, "internal stack accounting underflow");
    compiler.depth = 0;
  } else {
    compiler.depth -= count;
  }
}

static void emit_op(Compiler& compiler, Op op) { compiler.bytes.push((char)(u8)op); }
static void emit_u16(Compiler& compiler, u32 value) {
  if (value > UINT16_MAX) {
    compiler_error(compiler, "operand exceeds 16 bits");
    value = 0;
  }
  compiler.bytes.push((char)(value & 0xff));
  compiler.bytes.push((char)((value >> 8) & 0xff));
}
static void emit_i32(Compiler& compiler, i32 value) {
  u32 bits = (u32)value;
  for (u32 i = 0; i < 4; i++) compiler.bytes.push((char)((bits >> (i * 8)) & 0xff));
}
static u32 emit_jump(Compiler& compiler, Op op) {
  emit_op(compiler, op);
  u32 operand = compiler.bytes.len;
  emit_i32(compiler, 0);
  return operand;
}
static void patch_jump(Compiler& compiler, u32 operand, u32 target) {
  i64 relative = (i64)target - (i64)(operand + 4);
  if (relative < INT32_MIN || relative > INT32_MAX) {
    compiler_error(compiler, "jump exceeds 32 bits");
    return;
  }
  u32 bits = (u32)(i32)relative;
  for (u32 i = 0; i < 4; i++) compiler.bytes.data[operand + i] = (char)(bits >> (i * 8));
}

static u32 add_constant(Compiler& compiler, Value value) {
  ArrayData* constants = as_array(compiler.constants.get());
  for (u32 i = 0; i < constants->len; i++)
    if (val_eq(constants->items[i], value)) return i;
  if (constants->len >= UINT16_MAX) {
    compiler_error(compiler, "too many constants");
    return 0;
  }
  u32 index = constants->len;
  array_push(compiler.state, compiler.constants.get(), value);
  return index;
}

static Resolved resolve(Compiler& compiler, u32 name) {
  if (!sym_qualified(compiler.state, name)) {
    for (u32 i = compiler.active.len; i-- > 0;) {
      Binding& binding = compiler.info.bindings[compiler.active[i]];
      if (binding.name == name)
        return Resolved{ResolvedKind::Local, binding.slot, binding.captured};
    }
    i32 capture = find_capture(compiler.info, name);
    if (capture >= 0) return Resolved{ResolvedKind::Upval, (u32)capture, true};
  }
  Value symbol = symbol_v(name);
  Value var = ns_resolve_var(compiler.state, symbol);
  return Resolved{ResolvedKind::Global, add_constant(compiler, is_nil(var) ? symbol : var), false};
}

static bool emit_expr(Compiler&, Value, bool);

static void emit_load(Compiler& compiler, Resolved resolved) {
  switch (resolved.kind) {
    case ResolvedKind::Local:
      emit_op(compiler, resolved.boxed ? Op::GetBoxed : Op::GetLocal);
      emit_u16(compiler, resolved.index);
      break;
    case ResolvedKind::Upval:
      emit_op(compiler, Op::GetUpval);
      emit_u16(compiler, resolved.index);
      break;
    case ResolvedKind::Global:
      emit_op(compiler, Op::GetGlobal);
      emit_u16(compiler, resolved.index);
      break;
  }
  push_depth(compiler);
}

static void emit_store(Compiler& compiler, Resolved resolved) {
  switch (resolved.kind) {
    case ResolvedKind::Local:
      emit_op(compiler, resolved.boxed ? Op::SetBoxed : Op::SetLocal);
      emit_u16(compiler, resolved.index);
      break;
    case ResolvedKind::Upval:
      emit_op(compiler, Op::SetUpval);
      emit_u16(compiler, resolved.index);
      break;
    case ResolvedKind::Global:
      emit_op(compiler, Op::SetGlobal);
      emit_u16(compiler, resolved.index);
      break;
  }
}

static bool emit_body(Compiler& compiler, Value forms, bool tail) {
  Scope roots(compiler.state);
  Slot cursor = roots.push(forms);
  if (!pairp(cursor.get())) {
    emit_op(compiler, Op::Nil);
    push_depth(compiler);
    return true;
  }
  while (pairp(cdr_(cursor.get()))) {
    if (!emit_expr(compiler, car_(cursor.get()), false)) return false;
    emit_op(compiler, Op::Pop);
    pop_depth(compiler);
    cursor.set(cdr_(cursor.get()));
  }
  return emit_expr(compiler, car_(cursor.get()), tail);
}

static Value compile_lambda(State&, LambdaInfo&, Value, u32);

// The parameter list is not passed down: parse_params already recorded the
// arity and the formals' slots on `child` during analysis.
static bool emit_lambda(Compiler& compiler, Value body, u32 name) {
  if (compiler.childCursor >= compiler.info.children.len) {
    compiler_error(compiler, "lambda analysis mismatch");
    return true;
  }
  LambdaInfo& child = *compiler.info.children[compiler.childCursor++];
  Scope roots(compiler.state);
  Slot bodyRoot = roots.push(body);
  Slot nested = roots.push(compile_lambda(compiler.state, child, bodyRoot.get(), name));
  if (nested.get().tag == Tag::Unwind) {
    compiler.failed = true;
    return true;
  }
  Slot descriptor = roots.push(make_array(compiler.state, child.captures.len + 1));
  array_push(compiler.state, descriptor.get(), nested.get());
  for (u32 i = 0; i < child.captures.len; i++) {
    Capture capture = child.captures[i];
    i64 encoded = capture.local ? (i64)capture.index : -(i64)capture.index - 1;
    array_push(compiler.state, descriptor.get(), int_v(encoded));
  }
  u32 constant = add_constant(compiler, descriptor.get());
  emit_op(compiler, Op::Closure);
  emit_u16(compiler, constant);
  push_depth(compiler);
  return true;
}

static bool emit_if(Compiler& compiler, Value args, bool tail) {
  if (!pairp(args) || !pairp(cdr_(args))) {
    compiler_error(compiler, "bad if");
    return true;
  }
  Scope roots(compiler.state);
  Slot argsRoot = roots.push(args);
  if (!emit_expr(compiler, car_(argsRoot.get()), false)) return false;
  u32 branchDepth = compiler.depth - 1;
  u32 falseJump = emit_jump(compiler, Op::JumpFalse);
  pop_depth(compiler);
  bool thenFalls = emit_expr(compiler, car_(cdr_(argsRoot.get())), tail);
  u32 thenDepth = compiler.depth;
  u32 endJump = 0;
  if (thenFalls) endJump = emit_jump(compiler, Op::Jump);
  u32 elseStart = compiler.bytes.len;
  patch_jump(compiler, falseJump, elseStart);
  compiler.depth = branchDepth;
  Value elseForms = cdr_(cdr_(argsRoot.get()));
  bool elseFalls;
  if (pairp(elseForms)) elseFalls = emit_expr(compiler, car_(elseForms), tail);
  else {
    emit_op(compiler, Op::Nil);
    push_depth(compiler);
    elseFalls = true;
  }
  u32 elseDepth = compiler.depth;
  u32 end = compiler.bytes.len;
  if (thenFalls) patch_jump(compiler, endJump, end);
  if (thenFalls && elseFalls && thenDepth != elseDepth)
    compiler_error(compiler, "if branches have different stack depths");
  compiler.depth = thenFalls ? thenDepth : elseDepth;
  return thenFalls || elseFalls;
}

static bool active_has(Compiler& compiler, u32 name) {
  for (u32 i = compiler.active.len; i-- > 0;)
    if (compiler.info.bindings[compiler.active[i]].name == name) return true;
  return false;
}

// Consume the binding the analysis pass recorded next and store the value on
// top of the stack into its slot. `name` is the binding this emit site expects;
// the two passes walk the same forms in the same order, so a mismatch is a
// compiler bug and must fail loudly rather than miscompile silently.
static bool bind_next_slot(Compiler& compiler, u32 name) {
  if (compiler.bindingCursor >= compiler.info.bindings.len) {
    compiler_error(compiler, "binding was not analyzed");
    return false;
  }
  Binding& metadata = compiler.info.bindings[compiler.bindingCursor];
  if (metadata.name != name) {
    compiler_error(compiler, "analysis and emit passes disagree on binding order");
    return false;
  }
  if (metadata.captured) emit_op(compiler, Op::MakeBox);
  emit_op(compiler, Op::SetLocal);
  emit_u16(compiler, metadata.slot);
  emit_op(compiler, Op::Pop);
  pop_depth(compiler);
  compiler.active.push(compiler.bindingCursor++);
  return true;
}

static bool emit_let(Compiler& compiler, Value args, bool tail) {
  if (!pairp(args)) {
    compiler_error(compiler, "bad let");
    return true;
  }
  Scope roots(compiler.state);
  Slot argsRoot = roots.push(args);
  Slot bindings = roots.push(car_(argsRoot.get()));
  bindings.set(strip_array_literal_head(bindings.get(), compiler.state.syms.array_));
  u32 activeBase = compiler.active.len;
  Slot bindingRoot = roots.push();
  while (pairp(bindings.get())) {
    bindingRoot.set(car_(bindings.get()));
    if (!pairp(bindingRoot.get()) || !pairp(cdr_(bindingRoot.get())) ||
        car_(bindingRoot.get()).tag != Tag::Symbol) {
      compiler_error(compiler, "bad let binding");
      break;
    }
    if (!emit_expr(compiler, car_(cdr_(bindingRoot.get())), false)) break;
    if (!bind_next_slot(compiler, car_(bindingRoot.get()).id)) break;
    bindings.set(cdr_(bindings.get()));
  }
  // Mirror the analyzer's collect_body_defines for this let body: allocate a
  // nil-initialized (boxed if captured) slot per hoisted define, in the same
  // order, so bindingCursor stays in lockstep.
  if (compiler.info.userDepth > 0) {
    for (Value body = cdr_(argsRoot.get()); pairp(body); body = cdr_(body)) {
      Value name = body_define_name(compiler.state, car_(body));
      if (is_nil(name) || active_has(compiler, name.id)) continue;
      emit_op(compiler, Op::Nil);
      push_depth(compiler);
      if (!bind_next_slot(compiler, name.id)) break;
    }
  }
  bool falls = emit_body(compiler, cdr_(argsRoot.get()), tail);
  compiler.active.len = activeBase;
  return falls;
}

static void emit_def_global(Compiler& compiler, Value name, bool isPrivate, Value doc) {
  Scope roots(compiler.state);
  Slot docRoot = roots.push(doc);
  Slot descriptor = roots.push(make_array(compiler.state, 3));
  array_push(compiler.state, descriptor.get(), name);
  array_push(compiler.state, descriptor.get(), bool_v(isPrivate));
  array_push(compiler.state, descriptor.get(), docRoot.get());
  emit_op(compiler, Op::DefGlobal);
  emit_u16(compiler, add_constant(compiler, descriptor.get()));
}

static bool emit_define(Compiler& compiler, Value form, bool isPrivate) {
  Scope roots(compiler.state);
  Slot formRoot = roots.push(form);
  Value args = cdr_(formRoot.get());
  if (!pairp(args)) {
    compiler_error(compiler, "bad define");
    return true;
  }
  Value target = car_(args);
  Value name = pairp(target) ? car_(target) : target;
  if (name.tag != Tag::Symbol) {
    compiler_error(compiler, "define name must be a symbol");
    return true;
  }
  if (pairp(target)) {
    if (!emit_lambda(compiler, cdr_(args), name.id)) return false;
  } else {
    Value rest = cdr_(args);
    rest = skip_docstring(rest);
    if (!pairp(rest)) {
      compiler_error(compiler, "define is missing a value");
      return true;
    }
    if (!emit_expr(compiler, car_(rest), false)) return false;
  }

  if (compiler.info.userDepth > 0) {
    Resolved local = resolve(compiler, name.id);
    if (local.kind == ResolvedKind::Global) {
      compiler_error(compiler, "internal define was not hoisted");
      return true;
    }
    emit_store(compiler, local);
  } else {
    args = cdr_(formRoot.get());
    Value doc = nil_v();
    skip_docstring(cdr_(args), &doc);
    emit_def_global(compiler, name, isPrivate, doc);
  }
  return true;
}

static bool emit_defmacro(Compiler& compiler, Value form) {
  Scope roots(compiler.state);
  Slot formRoot = roots.push(form);
  Value args = cdr_(formRoot.get());
  if (!pairp(args) || car_(args).tag != Tag::Symbol || !pairp(cdr_(args))) {
    compiler_error(compiler, "bad defmacro");
    return true;
  }
  Value name = car_(args);
  Value rest = cdr_(args);
  if (!emit_lambda(compiler, cdr_(rest), name.id)) return false;
  emit_op(compiler, Op::ToMacro);

  args = cdr_(formRoot.get());
  rest = cdr_(args);
  Value doc = nil_v();
  skip_docstring(cdr_(rest), &doc);
  emit_def_global(compiler, name, false, doc);
  return true;
}

static bool finish_control_call(Compiler& compiler, u32 argc, bool tail) {
  emit_op(compiler, tail ? Op::TailCall : Op::Call);
  emit_u16(compiler, argc);
  pop_depth(compiler, argc);
  return !tail;
}

static bool emit_call(Compiler& compiler, Value form, bool tail) {
  Scope roots(compiler.state);
  Slot cursor = roots.push(form);
  if (!emit_expr(compiler, car_(cursor.get()), false)) return false;
  cursor.set(cdr_(cursor.get()));
  u32 argc = 0;
  while (pairp(cursor.get())) {
    if (!emit_expr(compiler, car_(cursor.get()), false)) return false;
    argc++;
    cursor.set(cdr_(cursor.get()));
  }
  if (cursor.get().tag != Tag::Null) {
    compiler_error(compiler, "dotted call");
    return true;
  }
  return finish_control_call(compiler, argc, tail);
}

static bool emit_while(Compiler& compiler, Value args) {
  if (!pairp(args)) {
    compiler_error(compiler, "bad while");
    return true;
  }
  Scope roots(compiler.state);
  Slot argsRoot = roots.push(args);
  u32 start = compiler.bytes.len;
  if (!emit_expr(compiler, car_(argsRoot.get()), false)) return false;
  u32 exit = emit_jump(compiler, Op::JumpFalse);
  pop_depth(compiler);
  Slot body = roots.push(cdr_(argsRoot.get()));
  while (pairp(body.get())) {
    if (!emit_expr(compiler, car_(body.get()), false)) return false;
    emit_op(compiler, Op::Pop);
    pop_depth(compiler);
    body.set(cdr_(body.get()));
  }
  emit_op(compiler, Op::Loop);
  u32 operand = compiler.bytes.len;
  emit_i32(compiler, (i32)((i64)start - (i64)(operand + 4)));
  patch_jump(compiler, exit, compiler.bytes.len);
  emit_op(compiler, Op::Nil);
  push_depth(compiler);
  return true;
}

static bool emit_short_circuit(Compiler& compiler, Value args, bool isAnd, bool tail) {
  if (!pairp(args)) {
    emit_op(compiler, isAnd ? Op::True : Op::False);
    push_depth(compiler);
    return true;
  }
  Scope roots(compiler.state);
  Slot cursor = roots.push(args);
  Vec<u32> exits;
  while (pairp(cdr_(cursor.get()))) {
    if (!emit_expr(compiler, car_(cursor.get()), false)) return false;
    exits.push(emit_jump(compiler, isAnd ? Op::JumpFalsePeek : Op::JumpTruePeek));
    emit_op(compiler, Op::Pop);
    pop_depth(compiler);
    cursor.set(cdr_(cursor.get()));
  }
  bool falls = emit_expr(compiler, car_(cursor.get()), tail);
  u32 end = compiler.bytes.len;
  for (u32 i = 0; i < exits.len; i++) patch_jump(compiler, exits[i], end);
  return exits.len ? true : falls;
}

static bool emit_cond(Compiler& compiler, Value clauses, bool tail) {
  Scope roots(compiler.state);
  Slot cursor = roots.push(clauses);
  Vec<u32> exits;
  const u32 baseDepth = compiler.depth;
  bool anyFalls = false;
  bool hasElse = false;
  Slot clauseRoot = roots.push();

  while (pairp(cursor.get())) {
    clauseRoot.set(car_(cursor.get()));
    if (!pairp(clauseRoot.get())) {
      compiler_error(compiler, "bad cond clause");
      return true;
    }
    Value test = car_(clauseRoot.get());
    if (sym_is(test, compiler.state.syms.else_)) {
      if (!pairp(cdr_(clauseRoot.get()))) {
        compiler_error(compiler, "cond else needs a body");
        return true;
      }
      hasElse = true;
      bool falls = emit_body(compiler, cdr_(clauseRoot.get()), tail);
      anyFalls = anyFalls || falls;
      break;
    }

    if (!emit_expr(compiler, test, false)) return false;
    if (!pairp(cdr_(clauseRoot.get()))) {
      // The clause's own test value is the result, so this exit reaches the end
      // of the cond carrying a value: the form falls through even if every
      // clause body below ends in a tail call.
      u32 next = emit_jump(compiler, Op::JumpFalsePeek);
      exits.push(emit_jump(compiler, Op::Jump));
      anyFalls = true;
      patch_jump(compiler, next, compiler.bytes.len);
      emit_op(compiler, Op::Pop);
      pop_depth(compiler);
    } else {
      u32 next = emit_jump(compiler, Op::JumpFalse);
      pop_depth(compiler);
      bool falls = emit_body(compiler, cdr_(clauseRoot.get()), tail);
      if (falls) {
        exits.push(emit_jump(compiler, Op::Jump));
        anyFalls = true;
      }
      patch_jump(compiler, next, compiler.bytes.len);
    }
    compiler.depth = baseDepth;
    cursor.set(cdr_(cursor.get()));
  }

  if (!hasElse) {
    emit_op(compiler, Op::Nil);
    push_depth(compiler);
    anyFalls = true;
  }
  u32 end = compiler.bytes.len;
  for (u32 i = 0; i < exits.len; i++) patch_jump(compiler, exits[i], end);
  compiler.depth = anyFalls ? baseDepth + 1 : baseDepth;
  return anyFalls;
}

static void emit_quoted_symbol(Compiler& compiler, u32 name) {
  emit_op(compiler, Op::Const);
  emit_u16(compiler, add_constant(compiler, symbol_v(name)));
  push_depth(compiler);
}

static bool emit_quasiquote(Compiler& compiler, Value form, u32 depth) {
  Scope roots(compiler.state);
  Slot formRoot = roots.push(form);
  if (!pairp(formRoot.get())) {
    emit_op(compiler, Op::Const);
    emit_u16(compiler, add_constant(compiler, formRoot.get()));
    push_depth(compiler);
    return true;
  }

  Value head = car_(formRoot.get());
  if (sym_is(head, compiler.state.syms.unquote_)) {
    Value args = cdr_(formRoot.get());
    if (!pairp(args)) {
      compiler_error(compiler, "bad unquote");
      return true;
    }
    if (depth == 1) return emit_expr(compiler, car_(args), false);
    emit_quoted_symbol(compiler, compiler.state.syms.unquote_);
    if (!emit_quasiquote(compiler, car_(args), depth - 1)) return false;
    emit_op(compiler, Op::List);
    emit_u16(compiler, 2);
    pop_depth(compiler);
    return true;
  }
  if (sym_is(head, compiler.state.syms.quasiquote_)) {
    Value args = cdr_(formRoot.get());
    if (!pairp(args)) {
      compiler_error(compiler, "bad nested quasiquote");
      return true;
    }
    emit_quoted_symbol(compiler, compiler.state.syms.quasiquote_);
    if (!emit_quasiquote(compiler, car_(args), depth + 1)) return false;
    emit_op(compiler, Op::List);
    emit_u16(compiler, 2);
    pop_depth(compiler);
    return true;
  }

  if (pairp(head) && sym_is(car_(head), compiler.state.syms.unquoteSplicing_) && depth == 1) {
    Value spliceArgs = cdr_(head);
    if (!pairp(spliceArgs)) {
      compiler_error(compiler, "bad unquote-splicing");
      return true;
    }
    Slot spliceRoot = roots.push(spliceArgs);
    if (!emit_expr(compiler, car_(spliceRoot.get()), false)) return false;
    if (!emit_quasiquote(compiler, cdr_(formRoot.get()), depth)) return false;
    emit_op(compiler, Op::Append2);
    pop_depth(compiler);
    return true;
  }

  if (!emit_quasiquote(compiler, car_(formRoot.get()), depth)) return false;
  if (!emit_quasiquote(compiler, cdr_(formRoot.get()), depth)) return false;
  emit_op(compiler, Op::Cons);
  pop_depth(compiler);
  return true;
}

static void emit_constant(Compiler& compiler, Value value) {
  emit_op(compiler, Op::Const);
  emit_u16(compiler, add_constant(compiler, value));
  push_depth(compiler);
}

static void emit_native(Compiler& compiler, const char* name, NativeFn native) {
  Scope roots(compiler.state);
  Slot function = roots.push(make_native(compiler.state, name, native));
  emit_constant(compiler, function.get());
}

static bool emit_thunk_expr(Compiler& compiler, Value form) {
  Scope roots(compiler.state);
  Slot formRoot = roots.push(form);
  Slot body = roots.push(make_pair(compiler.state, formRoot.get(), null_v()));
  return emit_lambda(compiler, body.get(), 0);
}

static bool emit_thunk_body(Compiler& compiler, Value forms) {
  return emit_lambda(compiler, forms, 0);
}

static bool emit_binding_control(Compiler& compiler, Value args, bool tail, const char* badForm,
                                 const char* badBinding, const char* nativeName, NativeFn native,
                                 bool thunkBindings) {
  if (!pairp(args)) {
    compiler_error(compiler, badForm);
    return true;
  }
  Scope roots(compiler.state);
  Slot argsRoot = roots.push(args);
  emit_native(compiler, nativeName, native);
  Slot bindings = roots.push(car_(argsRoot.get()));
  bindings.set(strip_array_literal_head(bindings.get(), compiler.state.syms.array_));
  u32 argc = 0;
  Slot bindingRoot = roots.push();
  while (pairp(bindings.get())) {
    bindingRoot.set(car_(bindings.get()));
    if (!pairp(bindingRoot.get()) || !pairp(cdr_(bindingRoot.get()))) {
      compiler_error(compiler, badBinding);
      return true;
    }
    if (thunkBindings) {
      if (!emit_thunk_expr(compiler, car_(bindingRoot.get())) ||
          !emit_thunk_expr(compiler, car_(cdr_(bindingRoot.get()))))
        return false;
    } else if (!emit_expr(compiler, car_(bindingRoot.get()), false) ||
               !emit_expr(compiler, car_(cdr_(bindingRoot.get())), false)) {
      return false;
    }
    argc += 2;
    bindings.set(cdr_(bindings.get()));
  }
  if (!emit_thunk_body(compiler, cdr_(argsRoot.get()))) return false;
  return finish_control_call(compiler, argc + 1, tail);
}

static bool emit_handler_bind(Compiler& compiler, Value args, bool tail) {
  return emit_binding_control(compiler, args, tail, "bad handler-bind", "bad handler-bind binding",
                              "%handler-bind", vm_control_handler_bind, false);
}

static bool emit_restart_case(Compiler& compiler, Value args, bool tail) {
  if (!pairp(args)) {
    compiler_error(compiler, "bad restart-case");
    return true;
  }
  Scope roots(compiler.state);
  Slot argsRoot = roots.push(args);
  emit_native(compiler, "%restart-case", vm_control_restart_case);
  if (!emit_thunk_expr(compiler, car_(argsRoot.get()))) return false;
  u32 argc = 1;
  Slot clauses = roots.push(cdr_(argsRoot.get()));
  while (pairp(clauses.get())) {
    Value clause = car_(clauses.get());
    if (!pairp(clause) || car_(clause).tag != Tag::Symbol) {
      compiler_error(compiler, "bad restart-case clause");
      return true;
    }
    Value doc = nil_v();
    Value rest = skip_docstring(cdr_(clause), &doc);
    if (!pairp(rest)) {
      compiler_error(compiler, "restart-case clause needs parameters");
      return true;
    }
    emit_constant(compiler, car_(clause));
    emit_constant(compiler, doc);
    if (!emit_lambda(compiler, cdr_(rest), car_(clause).id)) return false;
    argc += 3;
    clauses.set(cdr_(clauses.get()));
  }
  return finish_control_call(compiler, argc, tail);
}

static bool emit_try(Compiler& compiler, Value args, bool tail) {
  Scope roots(compiler.state);
  Slot cursor = roots.push(args);
  u32 bodyCount = 0;
  for (Value scan = cursor.get(); pairp(scan); scan = cdr_(scan)) {
    Value part = car_(scan);
    if (pairp(part) && sym_is(car_(part), compiler.state.syms.catch_)) break;
    bodyCount++;
  }

  emit_native(compiler, "%try", vm_control_try);
  emit_constant(compiler, int_v(bodyCount));
  u32 argc = 1;
  for (u32 i = 0; i < bodyCount; i++) {
    if (!emit_thunk_expr(compiler, car_(cursor.get()))) return false;
    argc++;
    cursor.set(cdr_(cursor.get()));
  }
  Slot clauseRoot = roots.push();
  while (pairp(cursor.get())) {
    clauseRoot.set(car_(cursor.get()));
    if (!pairp(clauseRoot.get()) || !sym_is(car_(clauseRoot.get()), compiler.state.syms.catch_) ||
        !pairp(cdr_(clauseRoot.get()))) {
      compiler_error(compiler, "bad catch clause");
      return true;
    }
    Value spec = car_(cdr_(clauseRoot.get()));
    if (!pairp(spec) || !pairp(cdr_(spec)) || car_(cdr_(spec)).tag != Tag::Symbol) {
      compiler_error(compiler, "bad catch specification");
      return true;
    }
    if (!emit_thunk_expr(compiler, car_(spec))) return false;
    if (!emit_lambda(compiler, cdr_(cdr_(clauseRoot.get())), 0)) return false;
    argc += 2;
    cursor.set(cdr_(cursor.get()));
  }
  return finish_control_call(compiler, argc, tail);
}

static bool emit_unwind_protect(Compiler& compiler, Value args, bool tail) {
  if (!pairp(args)) {
    compiler_error(compiler, "bad unwind-protect");
    return true;
  }
  Scope roots(compiler.state);
  Slot cursor = roots.push(args);
  emit_native(compiler, "%unwind-protect", vm_control_unwind_protect);
  u32 argc = 0;
  while (pairp(cursor.get())) {
    if (!emit_thunk_expr(compiler, car_(cursor.get()))) return false;
    argc++;
    cursor.set(cdr_(cursor.get()));
  }
  return finish_control_call(compiler, argc, tail);
}

static bool emit_with_params(Compiler& compiler, Value args, bool tail) {
  return emit_binding_control(compiler, args, tail, "bad with-params", "bad with-params binding",
                              "%with-params", vm_control_with_params, true);
}

static bool emit_defparam(Compiler& compiler, Value args, bool tail) {
  if (compiler.info.userDepth > 0 || compiler.active.len > compiler.info.initialCount) {
    compiler_error(compiler, "defparam only allowed at top level");
    return true;
  }
  if (!pairp(args) || car_(args).tag != Tag::Symbol) {
    compiler_error(compiler, "bad defparam");
    return true;
  }
  Scope roots(compiler.state);
  Slot argsRoot = roots.push(args);
  Value name = car_(argsRoot.get());
  Slot rest = roots.push(cdr_(argsRoot.get()));
  Value doc = nil_v();
  rest.set(skip_docstring(rest.get(), &doc));
  Slot docRoot = roots.push(doc);
  if (!pairp(rest.get())) {
    compiler_error(compiler, "defparam missing default");
    return true;
  }
  emit_native(compiler, "%defparam", vm_control_defparam);
  emit_constant(compiler, name);
  emit_constant(compiler, docRoot.get());
  if (!emit_expr(compiler, car_(rest.get()), false)) return false;
  return finish_control_call(compiler, 3, tail);
}

static bool emit_data_control(Compiler& compiler, Value args, bool tail, const char* helperName,
                              NativeFn native, bool requireArg) {
  Scope roots(compiler.state);
  Slot cursor = roots.push(args);
  if (requireArg && !pairp(cursor.get())) {
    compiler_error(compiler, "missing control form argument");
    return true;
  }
  emit_native(compiler, helperName, native);
  u32 argc = 0;
  while (pairp(cursor.get())) {
    emit_constant(compiler, car_(cursor.get()));
    argc++;
    cursor.set(cdr_(cursor.get()));
  }
  return finish_control_call(compiler, argc, tail);
}

static bool emit_expr(Compiler& compiler, Value form, bool tail) {
  Scope roots(compiler.state);
  Slot formRoot = roots.push(form);
  form = formRoot.get();
  if (form.tag == Tag::Symbol) {
    emit_load(compiler, resolve(compiler, form.id));
    return true;
  }
  if (!pairp(form)) {
    switch (form.tag) {
      case Tag::Nil: emit_op(compiler, Op::Nil); break;
      case Tag::True: emit_op(compiler, Op::True); break;
      case Tag::False: emit_op(compiler, Op::False); break;
      case Tag::Null: emit_op(compiler, Op::Null); break;
      case Tag::Int:
        if (form.i >= INT8_MIN && form.i <= INT8_MAX) {
          emit_op(compiler, Op::Int8);
          compiler.bytes.push((char)(i8)form.i);
        } else {
          emit_op(compiler, Op::Const);
          emit_u16(compiler, add_constant(compiler, form));
        }
        break;
      default:
        emit_op(compiler, Op::Const);
        emit_u16(compiler, add_constant(compiler, form));
        break;
    }
    push_depth(compiler);
    return true;
  }

  Value head = car_(formRoot.get());
  Value args = cdr_(formRoot.get());
  if (head.tag == Tag::Symbol) {
    u32 name = head.id;
    if (name == compiler.state.syms.quote_) {
      if (!pairp(args)) compiler_error(compiler, "bad quote");
      Value quoted = pairp(args) ? car_(args) : nil_v();
      emit_op(compiler, Op::Const);
      emit_u16(compiler, add_constant(compiler, quoted));
      push_depth(compiler);
      return true;
    }
    if (name == compiler.state.syms.quasiquote_) {
      if (!pairp(args)) {
        compiler_error(compiler, "bad quasiquote");
        return true;
      }
      return emit_quasiquote(compiler, car_(args), 1);
    }
    if (name == compiler.state.syms.unquote_ || name == compiler.state.syms.unquoteSplicing_) {
      compiler_error(compiler, "unquote outside quasiquote");
      return true;
    }
    if (name == compiler.state.syms.if_) return emit_if(compiler, args, tail);
    if (name == compiler.state.syms.begin_ || name == compiler.state.syms.do_)
      return emit_body(compiler, args, tail);
    if (name == compiler.state.syms.lambda_ || name == compiler.state.syms.fn_) {
      if (!pairp(args)) compiler_error(compiler, "bad lambda");
      return pairp(args) ? emit_lambda(compiler, cdr_(args), 0) : true;
    }
    if (name == compiler.state.syms.let_) return emit_let(compiler, args, tail);
    if (is_define_head(compiler.state, name))
      return emit_define(compiler, formRoot.get(), name == compiler.state.syms.definePriv_);
    if (name == compiler.state.syms.defmacro_) return emit_defmacro(compiler, formRoot.get());
    if (name == compiler.state.syms.setBang_) {
      if (!pairp(args) || car_(args).tag != Tag::Symbol || !pairp(cdr_(args))) {
        compiler_error(compiler, "bad set!");
        return true;
      }
      Value target = car_(args);
      if (!emit_expr(compiler, car_(cdr_(args)), false)) return false;
      emit_store(compiler, resolve(compiler, target.id));
      return true;
    }
    if (name == compiler.state.syms.while_) return emit_while(compiler, args);
    if (name == compiler.state.syms.and_) return emit_short_circuit(compiler, args, true, tail);
    if (name == compiler.state.syms.or_) return emit_short_circuit(compiler, args, false, tail);
    if (name == compiler.state.syms.cond_) return emit_cond(compiler, args, tail);
    if (name == compiler.state.syms.handlerBind_) return emit_handler_bind(compiler, args, tail);
    if (name == compiler.state.syms.restartCase_) return emit_restart_case(compiler, args, tail);
    if (name == compiler.state.syms.try_) return emit_try(compiler, args, tail);
    if (name == compiler.state.syms.unwindProtect_ || name == compiler.state.syms.defer_)
      return emit_unwind_protect(compiler, args, tail);
    if (name == compiler.state.syms.withParams_) return emit_with_params(compiler, args, tail);
    if (name == compiler.state.syms.defparam_) return emit_defparam(compiler, args, tail);
    if (name == compiler.state.syms.ns_)
      return emit_data_control(compiler, args, tail, "%ns", vm_control_ns, true);
    if (name == compiler.state.syms.inNs_)
      return emit_data_control(compiler, args, tail, "%in-ns", vm_control_in_ns, true);
    if (name == compiler.state.syms.require_)
      return emit_data_control(compiler, args, tail, "%require", vm_control_require, false);
  }
  return emit_call(compiler, formRoot.get(), tail);
}

static Value compile_lambda(State& state, LambdaInfo& info, Value body, u32 name) {
  Scope roots(state);
  Slot bodyRoot = roots.push(body);
  bodyRoot.set(skip_docstring(bodyRoot.get()));
  Slot constants = roots.push(make_array(state, 8));
  Compiler compiler(state, info, constants);

  for (u32 i = 0; i < info.initialCount; i++) {
    Binding& binding = info.bindings[i];
    if (!binding.captured) continue;
    emit_op(compiler, Op::GetLocal);
    emit_u16(compiler, binding.slot);
    push_depth(compiler);
    emit_op(compiler, Op::MakeBox);
    emit_op(compiler, Op::SetLocal);
    emit_u16(compiler, binding.slot);
    emit_op(compiler, Op::Pop);
    pop_depth(compiler);
  }

  bool falls = emit_body(compiler, bodyRoot.get(), true);
  if (falls) emit_op(compiler, Op::Return);
  if (compiler.failed) return unwind_v();
  CodeSpec spec;
  spec.nfixed = info.nfixed;
  spec.hasRest = info.hasRest;
  spec.nupvals = info.captures.len;
  spec.nlocals = info.bindings.len;
  spec.maxStack = compiler.maxDepth;
  spec.name = name;
  return make_code(state, (const u8*)compiler.bytes.data, compiler.bytes.len, constants.get(),
                   spec);
}

Value compile_form(State& state, Value expanded) {
  Scope roots(state);
  Slot formRoot = roots.push(expanded);
  LambdaInfo top(nullptr, 0);
  Value bodyPair = make_pair(state, formRoot.get(), null_v());
  Slot body = roots.push(bodyPair);
  analyze_body(state, top, body.get());
  return compile_lambda(state, top, body.get(), 0);
}

}  // namespace ot
