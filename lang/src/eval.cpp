// eval.cpp - top-level expansion/compilation, application, and control helpers.
#include "eval.hpp"
#include "compile.hpp"
#include "ns.hpp"
#include "reader.hpp"
#include "vm.hpp"

namespace ot {

// ---------------------------------------------------------------- helpers

static Value car_(Value v) { return as_pair(v)->car; }
static Value cdr_(Value v) { return as_pair(v)->cdr; }
static bool pairp(Value v) { return v.tag == Tag::Pair; }
static bool sym_is(Value v, u32 id) { return v.tag == Tag::Symbol && v.id == id; }
static Value strip_array_literal_head(Value forms, u32 arrayId) {
  return pairp(forms) && sym_is(car_(forms), arrayId) ? cdr_(forms) : forms;
}

static Value quit_condition(State& vm) {
  Scope s(vm);
  Slot c = s.push(make_table(vm));
  table_put(vm, c.get(), keyword_v(vm.syms.kwType), symbol_v(vm.syms.quit_));
  return c.get();
}

Value start_quit(State& vm) {
  vm.unwindCondition = quit_condition(vm);
  vm.unwindKind = UnwindKind::Quit;
  return unwind_v();
}

Value make_native(State& vm, const char* name, NativeFn fn) {
  Obj* o = vm.heap.alloc(ObjType::Function, sizeof(FunctionData));
  Value fv = obj_v(Tag::Function, o);
  FunctionData* fd = fn_data(fv);
  fd->name = vm.intern.intern(name, (u32)strlen(name));
  fd->code = nil_v();
  fd->nsName = symbol_v(vm.syms.otiumCore_);
  fd->native = fn;
  fd->docstring = nil_v();
  fd->nupvals = 0;
  return fv;
}

static Value param_read(State& vm, Value p) {
  for (u32 i = vm.paramBindings.len; i-- > 0;) {
    if (vm.paramBindings[i].param.obj == p.obj) return vm.paramBindings[i].value;
  }
  return param_data(p)->defaultVal;
}

// ---------------------------------------------------------------- apply

Value apply(State& vm, Value callee, u32 base, u32 argc) {
  switch (callee.tag) {
    // Tag::Macro shares FunctionData; direct apply is the expander's
    // privileged call path. Bytecode call position rejects macros.
    case Tag::Macro:
    case Tag::Function: {
      if (fn_data(callee)->native) return fn_data(callee)->native(vm, base, argc);
      if (fn_data(callee)->code.tag == Tag::Code) return vm_call(vm, callee, base, argc);
      return raise_error(vm, "function has no implementation");
    }
    case Tag::Table: {
      if (argc < 1 || argc > 2) return raise_error(vm, "table call: 1 or 2 arguments");
      Value v = table_get(vm, callee, vm.stack[base]);
      if (is_nil(v) && argc == 2) v = vm.stack[base + 1];
      return v;
    }
    case Tag::Array: {
      if (argc < 1 || argc > 2) return raise_error(vm, "array call: 1 or 2 arguments");
      Value k = vm.stack[base];
      Value v = (k.tag == Tag::Int) ? array_get(callee, k.i) : nil_v();
      if (is_nil(v) && argc == 2) v = vm.stack[base + 1];
      return v;
    }
    case Tag::Keyword: {
      if (argc < 1 || argc > 2) return raise_error(vm, "keyword call: 1 or 2 arguments");
      Value coll = vm.stack[base];
      Value v = (coll.tag == Tag::Table) ? table_get(vm, coll, callee) : nil_v();
      if (is_nil(v) && argc == 2) v = vm.stack[base + 1];
      return v;
    }
    case Tag::Param: {
      if (argc != 0) return raise_error(vm, "params take no arguments");
      return param_read(vm, callee);
    }
    default: return raise_error(vm, "value is not callable");
  }
}

// ---------------------------------------------------------------- require

static u32 name_id_of(State& vm, Value v) {  // symbol/keyword id; string interned; 0 if none
  if (v.tag == Tag::Symbol || v.tag == Tag::Keyword) return v.id;
  if (v.tag == Tag::String) {
    StringData* s = as_string(v);
    return vm.intern.intern(string_bytes(s), s->len);
  }
  return 0;
}

static Value unwrap_quote(State& vm, Value v) {
  if (pairp(v) && sym_is(car_(v), vm.syms.quote_) && pairp(cdr_(v))) return car_(cdr_(v));
  return v;
}

static Value require_load(State& vm, u32 nsName) {
  u32 len;
  const char* nm = vm.intern.name(nsName, &len);
  NativeModule* native = find_native_module(vm, nsName);
  if (!native && !is_nil(ns_lookup(vm, nsName))) return nil_v();
  if (native && native->initialized && !is_nil(ns_lookup(vm, nsName))) return nil_v();
  for (u32 i = 0; i < vm.loadingNs.len; i++)
    if (vm.loadingNs[i] == nsName) return raise_error(vm, "circular require: %.*s", (int)len, nm);
  char cname[256];
  snprintf(cname, sizeof cname, "%.*s", (int)len, nm);
  vm.loadingNs.push(nsName);
  u32 savedNs = vm.currentNs;

  bool nativeHit = native != nullptr;
  if (native && !native->initialized) {
    // Mark first: an init callback may register another module and reallocate
    // nativeModules, invalidating the pointer above.
    NativeModuleInit init = native->init;
    native->initialized = true;
    ns_switch(vm, nsName);
    init(vm);
  }

  Buf src;
  bool sourceHit = vm.loadFn && vm.loadFn(vm.loadUd, cname, &src);
  Value r = nil_v();
  if (sourceHit) {
    ns_switch(vm, nsName);
    r = eval_source(vm, src.data, src.len, cname);
  } else if (!nativeHit) {
    r = vm.loadFn ? raise_error(vm, "namespace not found on load path: %s", cname)
                  : raise_error(vm, "namespace not found: %.*s", (int)len, nm);
  }
  vm.currentNs = savedNs;
  vm.loadingNs.pop();
  if (r.tag == Tag::Unwind) return r;
  return nil_v();
}

static Value require_spec(State& vm, Value spec) {
  spec = unwrap_quote(vm, spec);
  Value nameV = spec;
  Value opts = null_v();
  if (pairp(spec)) {
    nameV = unwrap_quote(vm, car_(spec));
    opts = cdr_(spec);
  }
  u32 target = name_id_of(vm, nameV);
  if (!target) return raise_error(vm, "require: bad namespace name");
  // require_load evaluates a whole file: root the opts cursor across it.
  Scope sc(vm);
  Slot optS = sc.push(opts);
  OT_TRY(require_load(vm, target));
  Slot curS = sc.push(ns_get_or_create(vm, vm.currentNs));
  while (pairp(optS.get())) {
    Value opt = car_(optS.get());
    if (opt.tag == Tag::Keyword && opt.id == vm.syms.kwAs) {
      optS.set(cdr_(optS.get()));
      if (!pairp(optS.get())) return raise_error(vm, "require: :as needs a name");
      Value alias = unwrap_quote(vm, car_(optS.get()));
      table_put(vm, ns_field(vm, curS.get(), vm.syms.kwAliases), alias, symbol_v(target));
    } else if (opt.tag == Tag::Keyword && opt.id == vm.syms.kwRefer) {
      optS.set(cdr_(optS.get()));
      if (!pairp(optS.get())) return raise_error(vm, "require: :refer needs a list");
      Value names = unwrap_quote(vm, car_(optS.get()));
      names = strip_array_literal_head(names, vm.syms.array_);
      Value tgt = ns_lookup(vm, target);
      // table_put/table_get never touch the GC heap, so this walk is safe
      // (raise_error below allocates, but only on the return-out path)
      for (Value n = names; pairp(n); n = cdr_(n)) {
        Value sym = car_(n);
        Value var = table_get(vm, ns_field(vm, tgt, vm.syms.kwVars), sym);
        if (is_nil(var) || var_private(var)) {
          return raise_error_sym(vm, "cannot refer %.*s", sym.id);
        }
        table_put(vm, ns_field(vm, curS.get(), vm.syms.kwRefers), sym, var);
        tgt = ns_lookup(vm, target);
      }
    }  // :reload and unknown options tolerated / ignored in stage 0
    optS.set(cdr_(optS.get()));
  }
  return nil_v();
}

static Value control_apply0(State& vm, Value fn) { return apply(vm, fn, vm.stack.len, 0); }

static Value control_apply1(State& vm, Value fn, Value arg) {
  Scope roots(vm);
  Slot argRoot = roots.push(arg);
  return apply(vm, fn, argRoot.idx, 1);
}

Value vm_control_handler_bind(State& vm, u32 base, u32 argc) {
  if (argc == 0 || (argc - 1) % 2 != 0) return raise_error(vm, "handler-bind: bad compiled form");
  u32 handlerBase = vm.handlers.len;
  for (u32 i = 0; i + 1 < argc; i += 2)
    vm.handlers.push(HandlerBinding{vm.stack[base + i], vm.stack[base + i + 1]});
  Value result = control_apply0(vm, vm.stack[base + argc - 1]);
  vm.handlers.len = handlerBase;
  return result;
}

Value vm_control_restart_case(State& vm, u32 base, u32 argc) {
  if (argc == 0 || (argc - 1) % 3 != 0) return raise_error(vm, "restart-case: bad compiled form");
  u32 restartBase = vm.restarts.len;
  u64 firstId = vm.restartIdCounter + 1;
  u32 count = (argc - 1) / 3;
  for (u32 i = 0; i < count; i++) {
    u32 arg = base + 1 + i * 3;
    if (vm.stack[arg].tag != Tag::Symbol) {
      vm.restarts.len = restartBase;
      return raise_error(vm, "restart-case: bad name");
    }
    Obj* object = vm.heap.alloc(ObjType::Restart, sizeof(RestartData));
    Value restart = obj_v(Tag::Restart, object);
    RestartData* data = as_restart(restart);
    data->name = vm.stack[arg].id;
    data->description = vm.stack[arg + 1];
    data->restartId = ++vm.restartIdCounter;
    vm.restarts.push(RestartRec{restart});
  }

  Value result = control_apply0(vm, vm.stack[base]);
  vm.restarts.len = restartBase;
  if (result.tag != Tag::Unwind || vm.unwindKind != UnwindKind::Restart ||
      vm.unwindRestartId < firstId || vm.unwindRestartId >= firstId + count)
    return result;

  u32 selected = (u32)(vm.unwindRestartId - firstId);
  Scope roots(vm);
  Slot args = roots.push(vm.unwindRestartArgs);
  vm.unwindKind = UnwindKind::None;
  vm.unwindCondition = nil_v();
  vm.unwindRestartArgs = nil_v();
  u32 argBase = vm.stack.len;
  u32 argCount = 0;
  for (Value cursor = args.get(); cursor.tag == Tag::Pair; cursor = as_pair(cursor)->cdr) {
    vm.push(as_pair(cursor)->car);
    argCount++;
  }
  Value handler = vm.stack[base + 1 + selected * 3 + 2];
  return apply(vm, handler, argBase, argCount);
}

Value vm_control_try(State& vm, u32 base, u32 argc) {
  if (argc == 0 || vm.stack[base].tag != Tag::Int || vm.stack[base].i < 0)
    return raise_error(vm, "try: bad compiled form");
  u64 bodyCount64 = (u64)vm.stack[base].i;
  if (bodyCount64 > argc - 1 || (argc - 1 - bodyCount64) % 2 != 0)
    return raise_error(vm, "try: bad compiled form");
  u32 bodyCount = (u32)bodyCount64;
  Value result = nil_v();
  for (u32 i = 0; i < bodyCount; i++) {
    result = control_apply0(vm, vm.stack[base + 1 + i]);
    if (result.tag == Tag::Unwind) break;
  }
  if (result.tag != Tag::Unwind || vm.unwindKind != UnwindKind::Condition) return result;

  Scope roots(vm);
  Slot condition = roots.push(vm.unwindCondition);
  vm.unwindKind = UnwindKind::None;
  u32 catches = base + 1 + bodyCount;
  for (u32 arg = catches; arg < base + argc; arg += 2) {
    Value predicate = control_apply0(vm, vm.stack[arg]);
    if (predicate.tag == Tag::Unwind) return predicate;
    Value matches = control_apply1(vm, predicate, condition.get());
    if (matches.tag == Tag::Unwind) return matches;
    if (is_truthy(matches)) return control_apply1(vm, vm.stack[arg + 1], condition.get());
  }
  vm.unwindKind = UnwindKind::Condition;
  vm.unwindCondition = condition.get();
  return unwind_v();
}

Value vm_control_unwind_protect(State& vm, u32 base, u32 argc) {
  if (argc == 0) return raise_error(vm, "unwind-protect: bad compiled form");
  Value result = control_apply0(vm, vm.stack[base]);
  UnwindKind kind = vm.unwindKind;
  Scope roots(vm);
  Slot condition = roots.push(vm.unwindCondition);
  Slot restartArgs = roots.push(vm.unwindRestartArgs);
  u64 restartId = vm.unwindRestartId;
  if (result.tag == Tag::Unwind) vm.unwindKind = UnwindKind::None;
  for (u32 i = 1; i < argc; i++) {
    Value cleanup = control_apply0(vm, vm.stack[base + i]);
    if (cleanup.tag == Tag::Unwind) {
      result = cleanup;
      kind = vm.unwindKind;
      condition.set(vm.unwindCondition);
      restartArgs.set(vm.unwindRestartArgs);
      restartId = vm.unwindRestartId;
      vm.unwindKind = UnwindKind::None;
    }
  }
  if (result.tag == Tag::Unwind) {
    vm.unwindKind = kind;
    vm.unwindCondition = condition.get();
    vm.unwindRestartArgs = restartArgs.get();
    vm.unwindRestartId = restartId;
  }
  return result;
}

Value vm_control_with_params(State& vm, u32 base, u32 argc) {
  if (argc == 0 || (argc - 1) % 2 != 0) return raise_error(vm, "with-params: bad compiled form");
  u32 paramBase = vm.paramBindings.len;
  Scope roots(vm);
  for (u32 i = 0; i + 1 < argc; i += 2) {
    Value param = control_apply0(vm, vm.stack[base + i]);
    if (param.tag == Tag::Unwind) {
      vm.paramBindings.len = paramBase;
      return param;
    }
    if (param.tag != Tag::Param) {
      vm.paramBindings.len = paramBase;
      return raise_error(vm, "with-params: not a param");
    }
    Slot paramRoot = roots.push(param);
    Value value = control_apply0(vm, vm.stack[base + i + 1]);
    if (value.tag == Tag::Unwind) {
      vm.paramBindings.len = paramBase;
      return value;
    }
    Slot valueRoot = roots.push(value);
    vm.paramBindings.push(ParamBinding{paramRoot.get(), valueRoot.get()});
  }
  Value result = control_apply0(vm, vm.stack[base + argc - 1]);
  vm.paramBindings.len = paramBase;
  return result;
}

Value vm_control_defparam(State& vm, u32 base, u32 argc) {
  if (argc != 3 || vm.stack[base].tag != Tag::Symbol)
    return raise_error(vm, "defparam: bad compiled form");
  Obj* object = vm.heap.alloc(ObjType::Param, sizeof(ParamData));
  Value param = obj_v(Tag::Param, object);
  ParamData* data = as_param(param);
  data->name = vm.stack[base].id;
  data->defaultVal = vm.stack[base + 2];
  Scope roots(vm);
  Slot paramRoot = roots.push(param);
  return ns_define(vm, data->name, paramRoot.get(), false, vm.stack[base + 1]);
}

Value vm_control_ns(State& vm, u32 base, u32 argc) {
  if (argc == 0 || vm.stack[base].tag != Tag::Symbol)
    return raise_error(vm, "ns: bad compiled form");
  ns_switch(vm, vm.stack[base].id);
  Scope roots(vm);
  Slot specs = roots.push();
  for (u32 i = 1; i < argc; i++) {
    Value clause = vm.stack[base + i];
    if (!pairp(clause) || car_(clause).tag != Tag::Keyword || car_(clause).id != vm.syms.kwRequire)
      continue;
    specs.set(cdr_(clause));
    while (pairp(specs.get())) {
      Value result = require_spec(vm, car_(specs.get()));
      if (result.tag == Tag::Unwind) return result;
      specs.set(cdr_(specs.get()));
    }
  }
  return nil_v();
}

Value vm_control_in_ns(State& vm, u32 base, u32 argc) {
  if (argc != 1) return raise_error(vm, "in-ns: one argument");
  u32 name = name_id_of(vm, unwrap_quote(vm, vm.stack[base]));
  if (!name) return raise_error(vm, "in-ns: bad namespace name");
  ns_switch(vm, name);
  return nil_v();
}

Value vm_control_require(State& vm, u32 base, u32 argc) {
  for (u32 i = 0; i < argc; i++) {
    Value result = require_spec(vm, vm.stack[base + i]);
    if (result.tag == Tag::Unwind) return result;
  }
  return nil_v();
}

// Compile and execute one expanded top-level form.
Value eval_form(State& vm, Value form) {
  // Route through the expansion hook *expander* in otium.core.
  Value core = ns_lookup(vm, vm.syms.otiumCore_);
  if (!is_nil(core)) {
    Value var = table_get(vm, ns_field(vm, core, vm.syms.kwVars), symbol_v(vm.syms.expander_));
    if (!is_nil(var) && var_value(var).tag == Tag::Function) {
      Value expanded;
      {
        Scope s(vm);
        Slot b = s.push(form);
        u32 savedExpandNs = vm.expandNs;
        vm.expandNs = vm.currentNs;
        expanded = apply(vm, var_value(var), b.idx, 1);
        vm.expandNs = savedExpandNs;
      }
      if (expanded.tag == Tag::Unwind) return expanded;
      form = expanded;
    }
  }
  Scope roots(vm);
  Slot formRoot = roots.push(form);
  Slot code = roots.push(compile_form(vm, formRoot.get()));
  if (code.get().tag == Tag::Unwind) return code.get();
  return vm_execute_code(vm, code.get());
}

Value eval_source(State& vm, const char* src, u32 len, const char* name,
                  const EvalSourcePolicy& policy) {
  EvalSourceState localState;
  EvalSourceState& state = policy.state ? *policy.state : localState;
  state = {};

  Reader reader(vm, src, len, name);
  Value last = nil_v();
  for (;;) {
    Value form = reader.next();
    if (form.tag == Tag::Unwind) {
      state.readError = true;
      state.incomplete = reader.incomplete();
      return form;
    }
    if (reader.atEof()) {
      state.consumed = len;
      return last;
    }
    last = policy.eval ? policy.eval(vm, form, policy.data) : eval_form(vm, form);
    if (last.tag == Tag::Unwind) return last;
    state.consumed = reader.position();
    if (policy.afterEval) policy.afterEval(vm, last, state.consumed, policy.data);
  }
}

Value eval_source(State& vm, const char* src, u32 len, const char* name) {
  return eval_source(vm, src, len, name, EvalSourcePolicy{});
}

}  // namespace ot
