#include "compile.hpp"
#include "code.hpp"
#include "heap.hpp"
#include "ns.hpp"
#include "state.hpp"

namespace ot {

static bool pairp(Value value) { return value.tag == Tag::Pair; }
static Value car(Value value) { return as_pair(value)->car; }
static Value cdr(Value value) { return as_pair(value)->cdr; }
static bool sym_is(Value value, u32 name) { return value.tag == Tag::Symbol && value.id == name; }

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

static i32 capture_name(State& state, LambdaInfo& lambda, u32 name) {
  (void)state;
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
    i32 parentCapture = capture_name(state, *lambda.parent, name);
    if (parentCapture < 0) return -1;
    capture.index = (u32)parentCapture;
  }
  lambda.captures.push(capture);
  return (i32)(lambda.captures.len - 1);
}

static void analyze_expr(State&, LambdaInfo&, Value);

static void analyze_quasiquote(State& state, LambdaInfo& lambda, Value form, u32 depth) {
  if (!pairp(form)) return;
  Value head = car(form);
  if (sym_is(head, state.syms.unquote_)) {
    Value args = cdr(form);
    if (pairp(args)) {
      if (depth == 1)
        analyze_expr(state, lambda, car(args));
      else
        analyze_quasiquote(state, lambda, car(args), depth - 1);
    }
    return;
  }
  if (sym_is(head, state.syms.quasiquote_)) {
    Value args = cdr(form);
    if (pairp(args)) analyze_quasiquote(state, lambda, car(args), depth + 1);
    return;
  }
  if (pairp(head) && sym_is(car(head), state.syms.unquoteSplicing_) && depth == 1) {
    Value spliceArgs = cdr(head);
    if (pairp(spliceArgs)) analyze_expr(state, lambda, car(spliceArgs));
  } else {
    analyze_quasiquote(state, lambda, head, depth);
  }
  analyze_quasiquote(state, lambda, cdr(form), depth);
}

static bool parse_params(State& state, LambdaInfo& lambda, Value params) {
  if (params.tag == Tag::Symbol) {
    lambda.hasRest = true;
    add_binding(lambda, params.id);
    return true;
  }
  if (pairp(params) && sym_is(car(params), state.syms.array_)) params = cdr(params);
  while (pairp(params)) {
    Value param = car(params);
    if (sym_is(param, state.syms.amp_)) {
      params = cdr(params);
      if (!pairp(params) || car(params).tag != Tag::Symbol) return false;
      lambda.hasRest = true;
      add_binding(lambda, car(params).id);
      return cdr(params).tag == Tag::Null;
    }
    if (param.tag != Tag::Symbol) return false;
    add_binding(lambda, param.id);
    lambda.nfixed++;
    params = cdr(params);
  }
  if (params.tag == Tag::Symbol) {
    lambda.hasRest = true;
    add_binding(lambda, params.id);
    return true;
  }
  return params.tag == Tag::Null;
}

static Value defined_name(Value form) {
  Value args = cdr(form);
  if (!pairp(args)) return nil_v();
  Value target = car(args);
  return pairp(target) ? car(target) : target;
}

static void collect_body_defines(State& state, LambdaInfo& lambda, Value forms) {
  if (lambda.userDepth == 0) return;
  for (Value cursor = forms; pairp(cursor); cursor = cdr(cursor)) {
    Value form = car(cursor);
    if (!pairp(form) || car(form).tag != Tag::Symbol) continue;
    u32 head = car(form).id;
    if (head != state.syms.define_ && head != state.syms.def_ && head != state.syms.definePriv_)
      continue;
    Value name = defined_name(form);
    if (name.tag != Tag::Symbol || find_active(lambda, name.id) >= 0) continue;
    add_binding(lambda, name.id);
  }
}

static void analyze_body(State& state, LambdaInfo& lambda, Value forms) {
  collect_body_defines(state, lambda, forms);
  lambda.initialCount = lambda.bindings.len;
  for (Value cursor = forms; pairp(cursor); cursor = cdr(cursor))
    analyze_expr(state, lambda, car(cursor));
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
      (void)capture_name(state, lambda, form.id);
    return;
  }
  if (!pairp(form)) return;
  Value head = car(form);
  Value args = cdr(form);
  if (head.tag == Tag::Symbol) {
    u32 name = head.id;
    if (name == state.syms.quote_) return;
    if (name == state.syms.quasiquote_) {
      if (pairp(args)) analyze_quasiquote(state, lambda, car(args), 1);
      return;
    }
    if (name == state.syms.lambda_ || name == state.syms.fn_) {
      if (pairp(args)) analyze_lambda(state, lambda, car(args), cdr(args));
      return;
    }
    if (name == state.syms.define_ || name == state.syms.def_ ||
        name == state.syms.definePriv_) {
      if (!pairp(args)) return;
      Value target = car(args);
      Value rest = cdr(args);
      if (pairp(target))
        analyze_lambda(state, lambda, cdr(target), rest);
      else {
        if (pairp(rest) && car(rest).tag == Tag::String && pairp(cdr(rest))) rest = cdr(rest);
        if (pairp(rest)) analyze_expr(state, lambda, car(rest));
      }
      return;
    }
    if (name == state.syms.defmacro_) {
      if (pairp(args) && pairp(cdr(args)))
        analyze_lambda(state, lambda, car(cdr(args)), cdr(cdr(args)));
      return;
    }
    if (name == state.syms.setBang_) {
      if (pairp(args) && car(args).tag == Tag::Symbol &&
          !sym_qualified(state, car(args).id) && find_active(lambda, car(args).id) < 0)
        (void)capture_name(state, lambda, car(args).id);
      if (pairp(args) && pairp(cdr(args))) analyze_expr(state, lambda, car(cdr(args)));
      return;
    }
    if (name == state.syms.let_) {
      if (!pairp(args)) return;
      u32 activeBase = lambda.active.len;
      Value bindings = car(args);
      if (pairp(bindings) && sym_is(car(bindings), state.syms.array_)) bindings = cdr(bindings);
      while (pairp(bindings)) {
        Value binding = car(bindings);
        if (pairp(binding) && pairp(cdr(binding))) {
          analyze_expr(state, lambda, car(cdr(binding)));
          if (car(binding).tag == Tag::Symbol) add_binding(lambda, car(binding).id);
        }
        bindings = cdr(bindings);
      }
      for (Value body = cdr(args); pairp(body); body = cdr(body))
        analyze_expr(state, lambda, car(body));
      lambda.active.len = activeBase;
      return;
    }
  }
  for (Value cursor = form; pairp(cursor); cursor = cdr(cursor))
    analyze_expr(state, lambda, car(cursor));
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
  while (pairp(cdr(cursor.get()))) {
    if (!emit_expr(compiler, car(cursor.get()), false)) return false;
    emit_op(compiler, Op::Pop);
    pop_depth(compiler);
    cursor.set(cdr(cursor.get()));
  }
  return emit_expr(compiler, car(cursor.get()), tail);
}

static Value compile_lambda(State&, LambdaInfo&, Value, Value, u32);

static bool emit_lambda(Compiler& compiler, Value params, Value body, u32 name) {
  if (compiler.childCursor >= compiler.info.children.len) {
    compiler_error(compiler, "lambda analysis mismatch");
    return true;
  }
  LambdaInfo& child = *compiler.info.children[compiler.childCursor++];
  Scope roots(compiler.state);
  Slot paramsRoot = roots.push(params);
  Slot bodyRoot = roots.push(body);
  Slot nested = roots.push(compile_lambda(compiler.state, child, paramsRoot.get(), bodyRoot.get(), name));
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
  if (!pairp(args) || !pairp(cdr(args))) {
    compiler_error(compiler, "bad if");
    return true;
  }
  Scope roots(compiler.state);
  Slot argsRoot = roots.push(args);
  if (!emit_expr(compiler, car(argsRoot.get()), false)) return false;
  u32 branchDepth = compiler.depth - 1;
  u32 falseJump = emit_jump(compiler, Op::JumpFalse);
  pop_depth(compiler);
  bool thenFalls = emit_expr(compiler, car(cdr(argsRoot.get())), tail);
  u32 thenDepth = compiler.depth;
  u32 endJump = 0;
  if (thenFalls) endJump = emit_jump(compiler, Op::Jump);
  u32 elseStart = compiler.bytes.len;
  patch_jump(compiler, falseJump, elseStart);
  compiler.depth = branchDepth;
  Value elseForms = cdr(cdr(argsRoot.get()));
  bool elseFalls;
  if (pairp(elseForms))
    elseFalls = emit_expr(compiler, car(elseForms), tail);
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

static bool emit_let(Compiler& compiler, Value args, bool tail) {
  if (!pairp(args)) {
    compiler_error(compiler, "bad let");
    return true;
  }
  Scope roots(compiler.state);
  Slot argsRoot = roots.push(args);
  Slot bindings = roots.push(car(argsRoot.get()));
  if (pairp(bindings.get()) && sym_is(car(bindings.get()), compiler.state.syms.array_))
    bindings.set(cdr(bindings.get()));
  u32 activeBase = compiler.active.len;
  while (pairp(bindings.get())) {
    Value binding = car(bindings.get());
    if (!pairp(binding) || !pairp(cdr(binding)) || car(binding).tag != Tag::Symbol ||
        compiler.bindingCursor >= compiler.info.bindings.len) {
      compiler_error(compiler, "bad let binding");
      break;
    }
    if (!emit_expr(compiler, car(cdr(binding)), false)) break;
    u32 bindingIndex = compiler.bindingCursor++;
    Binding& metadata = compiler.info.bindings[bindingIndex];
    if (metadata.captured) emit_op(compiler, Op::MakeBox);
    emit_op(compiler, Op::SetLocal);
    emit_u16(compiler, metadata.slot);
    emit_op(compiler, Op::Pop);
    pop_depth(compiler);
    compiler.active.push(bindingIndex);
    bindings.set(cdr(bindings.get()));
  }
  bool falls = emit_body(compiler, cdr(argsRoot.get()), tail);
  compiler.active.len = activeBase;
  return falls;
}

static bool emit_define(Compiler& compiler, Value form, bool isPrivate) {
  Scope roots(compiler.state);
  Slot formRoot = roots.push(form);
  Value args = cdr(formRoot.get());
  if (!pairp(args)) {
    compiler_error(compiler, "bad define");
    return true;
  }
  Value target = car(args);
  Value name = pairp(target) ? car(target) : target;
  if (name.tag != Tag::Symbol) {
    compiler_error(compiler, "define name must be a symbol");
    return true;
  }
  if (pairp(target)) {
    if (!emit_lambda(compiler, cdr(target), cdr(args), name.id)) return false;
  } else {
    Value rest = cdr(args);
    if (pairp(rest) && car(rest).tag == Tag::String && pairp(cdr(rest))) rest = cdr(rest);
    if (!pairp(rest)) {
      compiler_error(compiler, "define is missing a value");
      return true;
    }
    if (!emit_expr(compiler, car(rest), false)) return false;
  }

  if (compiler.info.userDepth > 0) {
    Resolved local = resolve(compiler, name.id);
    if (local.kind == ResolvedKind::Global) {
      compiler_error(compiler, "internal define was not hoisted");
      return true;
    }
    emit_store(compiler, local);
  } else {
    args = cdr(formRoot.get());
    Value rest = cdr(args);
    Value doc = nil_v();
    if (!pairp(target) && pairp(rest) && car(rest).tag == Tag::String && pairp(cdr(rest)))
      doc = car(rest);
    Slot docRoot = roots.push(doc);
    Slot descriptor = roots.push(make_array(compiler.state, 3));
    array_push(compiler.state, descriptor.get(), name);
    array_push(compiler.state, descriptor.get(), bool_v(isPrivate));
    array_push(compiler.state, descriptor.get(), docRoot.get());
    emit_op(compiler, Op::DefGlobal);
    emit_u16(compiler, add_constant(compiler, descriptor.get()));
  }
  return true;
}

static bool emit_defmacro(Compiler& compiler, Value form) {
  Scope roots(compiler.state);
  Slot formRoot = roots.push(form);
  Value args = cdr(formRoot.get());
  if (!pairp(args) || car(args).tag != Tag::Symbol || !pairp(cdr(args))) {
    compiler_error(compiler, "bad defmacro");
    return true;
  }
  Value name = car(args);
  Value rest = cdr(args);
  if (!emit_lambda(compiler, car(rest), cdr(rest), name.id)) return false;
  emit_op(compiler, Op::ToMacro);

  Slot descriptor = roots.push(make_array(compiler.state, 3));
  array_push(compiler.state, descriptor.get(), name);
  array_push(compiler.state, descriptor.get(), bool_v(false));
  array_push(compiler.state, descriptor.get(), nil_v());
  emit_op(compiler, Op::DefGlobal);
  emit_u16(compiler, add_constant(compiler, descriptor.get()));
  return true;
}

static bool emit_call(Compiler& compiler, Value form, bool tail) {
  Scope roots(compiler.state);
  Slot cursor = roots.push(form);
  if (!emit_expr(compiler, car(cursor.get()), false)) return false;
  cursor.set(cdr(cursor.get()));
  u32 argc = 0;
  while (pairp(cursor.get())) {
    if (!emit_expr(compiler, car(cursor.get()), false)) return false;
    argc++;
    cursor.set(cdr(cursor.get()));
  }
  if (cursor.get().tag != Tag::Null) {
    compiler_error(compiler, "dotted call");
    return true;
  }
  emit_op(compiler, tail ? Op::TailCall : Op::Call);
  emit_u16(compiler, argc);
  pop_depth(compiler, argc);
  return !tail;
}

static bool emit_while(Compiler& compiler, Value args) {
  if (!pairp(args)) {
    compiler_error(compiler, "bad while");
    return true;
  }
  Scope roots(compiler.state);
  Slot argsRoot = roots.push(args);
  u32 start = compiler.bytes.len;
  if (!emit_expr(compiler, car(argsRoot.get()), false)) return false;
  u32 exit = emit_jump(compiler, Op::JumpFalse);
  pop_depth(compiler);
  Slot body = roots.push(cdr(argsRoot.get()));
  while (pairp(body.get())) {
    if (!emit_expr(compiler, car(body.get()), false)) return false;
    emit_op(compiler, Op::Pop);
    pop_depth(compiler);
    body.set(cdr(body.get()));
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
  while (pairp(cdr(cursor.get()))) {
    if (!emit_expr(compiler, car(cursor.get()), false)) return false;
    exits.push(emit_jump(compiler, isAnd ? Op::JumpFalsePeek : Op::JumpTruePeek));
    emit_op(compiler, Op::Pop);
    pop_depth(compiler);
    cursor.set(cdr(cursor.get()));
  }
  bool falls = emit_expr(compiler, car(cursor.get()), tail);
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

  while (pairp(cursor.get())) {
    Value clause = car(cursor.get());
    if (!pairp(clause)) {
      compiler_error(compiler, "bad cond clause");
      return true;
    }
    Value test = car(clause);
    Value body = cdr(clause);
    if (sym_is(test, compiler.state.syms.else_)) {
      if (!pairp(body)) {
        compiler_error(compiler, "cond else needs a body");
        return true;
      }
      hasElse = true;
      bool falls = emit_body(compiler, body, tail);
      anyFalls = anyFalls || falls;
      break;
    }

    if (!emit_expr(compiler, test, false)) return false;
    if (!pairp(body)) {
      u32 next = emit_jump(compiler, Op::JumpFalsePeek);
      exits.push(emit_jump(compiler, Op::Jump));
      patch_jump(compiler, next, compiler.bytes.len);
      emit_op(compiler, Op::Pop);
      pop_depth(compiler);
    } else {
      u32 next = emit_jump(compiler, Op::JumpFalse);
      pop_depth(compiler);
      bool falls = emit_body(compiler, body, tail);
      if (falls) {
        exits.push(emit_jump(compiler, Op::Jump));
        anyFalls = true;
      }
      patch_jump(compiler, next, compiler.bytes.len);
    }
    compiler.depth = baseDepth;
    cursor.set(cdr(cursor.get()));
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

  Value head = car(formRoot.get());
  if (sym_is(head, compiler.state.syms.unquote_)) {
    Value args = cdr(formRoot.get());
    if (!pairp(args)) {
      compiler_error(compiler, "bad unquote");
      return true;
    }
    if (depth == 1) return emit_expr(compiler, car(args), false);
    emit_quoted_symbol(compiler, compiler.state.syms.unquote_);
    if (!emit_quasiquote(compiler, car(args), depth - 1)) return false;
    emit_op(compiler, Op::List);
    emit_u16(compiler, 2);
    pop_depth(compiler);
    return true;
  }
  if (sym_is(head, compiler.state.syms.quasiquote_)) {
    Value args = cdr(formRoot.get());
    if (!pairp(args)) {
      compiler_error(compiler, "bad nested quasiquote");
      return true;
    }
    emit_quoted_symbol(compiler, compiler.state.syms.quasiquote_);
    if (!emit_quasiquote(compiler, car(args), depth + 1)) return false;
    emit_op(compiler, Op::List);
    emit_u16(compiler, 2);
    pop_depth(compiler);
    return true;
  }

  if (pairp(head) && sym_is(car(head), compiler.state.syms.unquoteSplicing_) && depth == 1) {
    Value spliceArgs = cdr(head);
    if (!pairp(spliceArgs)) {
      compiler_error(compiler, "bad unquote-splicing");
      return true;
    }
    Slot spliceRoot = roots.push(spliceArgs);
    if (!emit_expr(compiler, car(spliceRoot.get()), false)) return false;
    if (!emit_quasiquote(compiler, cdr(formRoot.get()), depth)) return false;
    emit_op(compiler, Op::Append2);
    pop_depth(compiler);
    return true;
  }

  if (!emit_quasiquote(compiler, car(formRoot.get()), depth)) return false;
  if (!emit_quasiquote(compiler, cdr(formRoot.get()), depth)) return false;
  emit_op(compiler, Op::Cons);
  pop_depth(compiler);
  return true;
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

  Value head = car(formRoot.get());
  Value args = cdr(formRoot.get());
  if (head.tag == Tag::Symbol) {
    u32 name = head.id;
    if (name == compiler.state.syms.quote_) {
      if (!pairp(args)) compiler_error(compiler, "bad quote");
      Value quoted = pairp(args) ? car(args) : nil_v();
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
      return emit_quasiquote(compiler, car(args), 1);
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
      return pairp(args) ? emit_lambda(compiler, car(args), cdr(args), 0) : true;
    }
    if (name == compiler.state.syms.let_) return emit_let(compiler, args, tail);
    if (name == compiler.state.syms.define_ || name == compiler.state.syms.def_ ||
        name == compiler.state.syms.definePriv_)
      return emit_define(compiler, formRoot.get(), name == compiler.state.syms.definePriv_);
    if (name == compiler.state.syms.defmacro_) return emit_defmacro(compiler, formRoot.get());
    if (name == compiler.state.syms.setBang_) {
      if (!pairp(args) || car(args).tag != Tag::Symbol || !pairp(cdr(args))) {
        compiler_error(compiler, "bad set!");
        return true;
      }
      Value target = car(args);
      if (!emit_expr(compiler, car(cdr(args)), false)) return false;
      emit_store(compiler, resolve(compiler, target.id));
      return true;
    }
    if (name == compiler.state.syms.while_) return emit_while(compiler, args);
    if (name == compiler.state.syms.and_)
      return emit_short_circuit(compiler, args, true, tail);
    if (name == compiler.state.syms.or_)
      return emit_short_circuit(compiler, args, false, tail);
    if (name == compiler.state.syms.cond_) return emit_cond(compiler, args, tail);
  }
  return emit_call(compiler, formRoot.get(), tail);
}

static Value compile_lambda(State& state, LambdaInfo& info, Value params, Value body, u32 name) {
  (void)params;
  Scope roots(state);
  Slot bodyRoot = roots.push(body);
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
  return make_code(state, (const u8*)compiler.bytes.data, compiler.bytes.len, constants.get(), spec);
}

Value compile_form(State& state, Value expanded) {
  Scope roots(state);
  Slot formRoot = roots.push(expanded);
  LambdaInfo top(nullptr, 0);
  Value bodyPair = make_pair(state, formRoot.get(), null_v());
  Slot body = roots.push(bodyPair);
  analyze_body(state, top, body.get());
  return compile_lambda(state, top, null_v(), body.get(), 0);
}

}  // namespace ot
