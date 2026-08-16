// eval.c - top-level expansion/compilation, application, and control helpers.
#include "eval.h"
#include "builtins.h"
#include "compile.h"
#include "form.h"
#include "ns.h"
#include "reader.h"
#include "vm.h"

// ---------------------------------------------------------------- helpers

static Value quit_condition(State* vm) {
  OT_SCOPE(vm);
  Ref c = ref_push(vm, make_table(vm));
  table_put(vm, ref_get(vm, c), keyword_v(vm->syms.kwType), symbol_v(vm->syms.quit_));
  return ref_get(vm, c);
}

Value start_quit(State* vm) {
  vm->unwindCondition = quit_condition(vm);
  vm->unwindKind = UnwindKind_Quit;
  return unwind_v();
}

Value make_native(State* vm, const char* name, NativeFn fn) {
  Obj* o = heap_alloc(&vm->heap, ObjType_Function, sizeof(FunctionData));
  Value fv = obj_v(Tag_Function, o);
  FunctionData* fd = fn_data(fv);
  fd->name = intern_id(&vm->intern, name, (u32)strlen(name));
  fd->code = nil_v();
  fd->nsName = symbol_v(vm->syms.otiumCore_);
  fd->native = fn;
  fd->docstring = nil_v();
  fd->nupvals = 0;
  return fv;
}

static Value param_read(State* vm, Value p) {
  for (u32 i = vm->paramBindings.len; i-- > 0;) {
    if (vm->paramBindings.data[i].param.obj == p.obj) return vm->paramBindings.data[i].value;
  }
  return param_data(p)->defaultVal;
}

// ---------------------------------------------------------------- apply

Value apply(State* vm, Value callee, u32 base, u32 argc) {
  switch (callee.tag) {
    // Tag_Macro shares FunctionData; direct apply is the expander's
    // privileged call path. Bytecode call position rejects macros.
    case Tag_Macro:
    case Tag_Function: {
      if (fn_data(callee)->native) return fn_data(callee)->native(vm, base, argc);
      if (fn_data(callee)->code.tag == Tag_Code) return vm_call(vm, callee, base, argc);
      return raise_error(vm, "function has no implementation");
    }
    case Tag_Table: {
      if (argc < 1 || argc > 2) return raise_error(vm, "table call: 1 or 2 arguments");
      Value v = table_get(vm, callee, vm->stack.data[base]);
      if (is_nil(v) && argc == 2) v = vm->stack.data[base + 1];
      return v;
    }
    case Tag_Array: {
      if (argc < 1 || argc > 2) return raise_error(vm, "array call: 1 or 2 arguments");
      Value k = vm->stack.data[base];
      Value v = (k.tag == Tag_Int) ? array_get(callee, k.i) : nil_v();
      if (is_nil(v) && argc == 2) v = vm->stack.data[base + 1];
      return v;
    }
    case Tag_Keyword: {
      if (argc < 1 || argc > 2) return raise_error(vm, "keyword call: 1 or 2 arguments");
      Value coll = vm->stack.data[base];
      Value v = (coll.tag == Tag_Table) ? table_get(vm, coll, callee) : nil_v();
      if (is_nil(v) && argc == 2) v = vm->stack.data[base + 1];
      return v;
    }
    case Tag_Param: {
      if (argc != 0) return raise_error(vm, "params take no arguments");
      return param_read(vm, callee);
    }
    default: return raise_error(vm, "value is not callable");
  }
}

// ---------------------------------------------------------------- require

static Value unwrap_quote(State* vm, Value v) {
  if (pairp(v) && sym_is(car_(v), vm->syms.quote_) && pairp(cdr_(v))) return car_(cdr_(v));
  return v;
}

static Value require_load(State* vm, u32 nsName) {
  u32 len;
  const char* nm = intern_name(&vm->intern, nsName, &len);
  NativeModule* native = find_native_module(vm, nsName);
  if (!native && !is_nil(ns_lookup(vm, nsName))) return nil_v();
  if (native && native->initialized && !is_nil(ns_lookup(vm, nsName))) return nil_v();
  for (u32 i = 0; i < vm->loadingNs.len; i++)
    if (vm->loadingNs.data[i] == nsName)
      return raise_error(vm, "circular require: %.*s", (int)len, nm);
  char cname[256];
  snprintf(cname, sizeof cname, "%.*s", (int)len, nm);
  vec_push(&vm->loadingNs, nsName);
  u32 savedNs = vm->currentNs;

  bool nativeHit = native != nullptr;
  if (native && !native->initialized) {
    // Mark first: an init callback may register another module and reallocate
    // nativeModules, invalidating the pointer above.
    NativeModuleInit init = native->init;
    native->initialized = true;
    ns_switch(vm, nsName);
    init(vm);
  }

  Buf src = {0};
  bool sourceHit = vm->loadFn && vm->loadFn(vm->loadUd, cname, &src);
  Value r = nil_v();
  if (sourceHit) {
    ns_switch(vm, nsName);
    r = eval_source(vm, src.data, src.len, cname);
  } else if (!nativeHit) {
    r = vm->loadFn ? raise_error(vm, "namespace not found on load path: %s", cname)
                   : raise_error(vm, "namespace not found: %.*s", (int)len, nm);
  }
  buf_deinit(&src);
  vm->currentNs = savedNs;
  vec_pop(&vm->loadingNs);
  if (r.tag == Tag_Unwind) return r;
  return nil_v();
}

static Value require_spec(State* vm, Value spec) {
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
  OT_SCOPE(vm);
  Ref optS = ref_push(vm, opts);
  Value loaded = require_load(vm, target);
  if (loaded.tag == Tag_Unwind) return loaded;
  Ref curS = ref_push(vm, ns_get_or_create(vm, vm->currentNs));
  Ref names = ref_push(vm, nil_v());
  Ref sym = ref_push(vm, nil_v());
  Ref var = ref_push(vm, nil_v());
  while (pairp(ref_get(vm, optS))) {
    Value opt = car_(ref_get(vm, optS));
    if (opt.tag == Tag_Keyword && opt.id == vm->syms.kwAs) {
      ref_set(vm, optS, cdr_(ref_get(vm, optS)));
      if (!pairp(ref_get(vm, optS))) return raise_error(vm, "require: :as needs a name");
      Value alias = unwrap_quote(vm, car_(ref_get(vm, optS)));
      table_put(vm, ns_field(vm, ref_get(vm, curS), vm->syms.kwAliases), alias,
                symbol_v(target));
    } else if (opt.tag == Tag_Keyword && opt.id == vm->syms.kwRefer) {
      ref_set(vm, optS, cdr_(ref_get(vm, optS)));
      if (!pairp(ref_get(vm, optS))) return raise_error(vm, "require: :refer needs a list");
      ref_set(vm, names,
              strip_array_literal_head(unwrap_quote(vm, car_(ref_get(vm, optS))),
                                       vm->syms.array_));
      // The target namespace is re-looked-up each iteration rather than hoisted:
      // table_put allocates once table storage is GC-owned.
      while (pairp(ref_get(vm, names))) {
        ref_set(vm, sym, car_(ref_get(vm, names)));
        ref_set(vm, var, table_get(vm, ns_field(vm, ns_lookup(vm, target), vm->syms.kwVars),
                                   ref_get(vm, sym)));
        if (is_nil(ref_get(vm, var)) || var_private(ref_get(vm, var)))
          return raise_error_sym(vm, "cannot refer %.*s", ref_get(vm, sym).id);
        table_put(vm, ns_field(vm, ref_get(vm, curS), vm->syms.kwRefers), ref_get(vm, sym),
                  ref_get(vm, var));
        ref_set(vm, names, cdr_(ref_get(vm, names)));
      }
    }  // :reload and unknown options tolerated / ignored in stage 0
    ref_set(vm, optS, cdr_(ref_get(vm, optS)));
  }
  return nil_v();
}

static Value control_apply0(State* vm, Value fn) { return apply(vm, fn, vm->stack.len, 0); }

static Value control_apply1(State* vm, Value fn, Value arg) {
  OT_SCOPE(vm);
  Ref fnRoot = ref_push(vm, fn);
  Ref argRoot = ref_push(vm, arg);
  return apply(vm, ref_get(vm, fnRoot), argRoot.i, 1);
}

Value vm_control_handler_bind(State* vm, u32 base, u32 argc) {
  if (argc == 0 || (argc - 1) % 2 != 0) return raise_error(vm, "handler-bind: bad compiled form");
  u32 handlerBase = vm->handlers.len;
  for (u32 i = 0; i + 1 < argc; i += 2)
    vec_push(&vm->handlers,
             ((HandlerBinding){vm->stack.data[base + i], vm->stack.data[base + i + 1]}));
  Value result = control_apply0(vm, vm->stack.data[base + argc - 1]);
  vm->handlers.len = handlerBase;
  return result;
}

Value vm_control_restart_case(State* vm, u32 base, u32 argc) {
  if (argc == 0 || (argc - 1) % 3 != 0) return raise_error(vm, "restart-case: bad compiled form");
  u32 restartBase = vm->restarts.len;
  u64 firstId = vm->restartIdCounter + 1;
  u32 count = (argc - 1) / 3;
  for (u32 i = 0; i < count; i++) {
    u32 arg = base + 1 + i * 3;
    if (vm->stack.data[arg].tag != Tag_Symbol) {
      vm->restarts.len = restartBase;
      return raise_error(vm, "restart-case: bad name");
    }
    Obj* object = heap_alloc(&vm->heap, ObjType_Restart, sizeof(RestartData));
    Value restart = obj_v(Tag_Restart, object);
    RestartData* data = as_restart(restart);
    data->name = vm->stack.data[arg].id;
    data->description = vm->stack.data[arg + 1];
    data->restartId = ++vm->restartIdCounter;
    vec_push(&vm->restarts, ((RestartRec){restart}));
  }

  Value result = control_apply0(vm, vm->stack.data[base]);
  vm->restarts.len = restartBase;
  if (result.tag != Tag_Unwind || vm->unwindKind != UnwindKind_Restart ||
      vm->unwindRestartId < firstId || vm->unwindRestartId >= firstId + count)
    return result;

  u32 selected = (u32)(vm->unwindRestartId - firstId);
  OT_SCOPE(vm);
  Ref args = ref_push(vm, vm->unwindRestartArgs);
  vm->unwindKind = UnwindKind_None;
  vm->unwindCondition = nil_v();
  vm->unwindRestartArgs = nil_v();
  u32 argBase = vm->stack.len;
  u32 argCount = 0;
  // state_push does not allocate, so walking the list with a raw cursor is
  // safe; re-read through `args` if that ever stops holding.
  for (Value cursor = ref_get(vm, args); cursor.tag == Tag_Pair; cursor = as_pair(cursor)->cdr) {
    state_push(vm, as_pair(cursor)->car);
    argCount++;
  }
  return apply(vm, vm->stack.data[base + 1 + selected * 3 + 2], argBase, argCount);
}

Value vm_control_try(State* vm, u32 base, u32 argc) {
  if (argc == 0 || vm->stack.data[base].tag != Tag_Int || vm->stack.data[base].i < 0)
    return raise_error(vm, "try: bad compiled form");
  u64 bodyCount64 = (u64)vm->stack.data[base].i;
  if (bodyCount64 > argc - 1 || (argc - 1 - bodyCount64) % 2 != 0)
    return raise_error(vm, "try: bad compiled form");
  u32 bodyCount = (u32)bodyCount64;
  Value result = nil_v();
  for (u32 i = 0; i < bodyCount; i++) {
    result = control_apply0(vm, vm->stack.data[base + 1 + i]);
    if (result.tag == Tag_Unwind) break;
  }
  if (result.tag != Tag_Unwind || vm->unwindKind != UnwindKind_Condition) return result;

  OT_SCOPE(vm);
  Ref condition = ref_push(vm, vm->unwindCondition);
  Ref predicate = ref_push(vm, nil_v());
  vm->unwindKind = UnwindKind_None;
  u32 catches = base + 1 + bodyCount;
  for (u32 arg = catches; arg < base + argc; arg += 2) {
    ref_set(vm, predicate, control_apply0(vm, vm->stack.data[arg]));
    if (ref_get(vm, predicate).tag == Tag_Unwind) return unwind_v();
    Value matches = control_apply1(vm, ref_get(vm, predicate), ref_get(vm, condition));
    if (matches.tag == Tag_Unwind) return matches;
    if (is_truthy(matches))
      return control_apply1(vm, vm->stack.data[arg + 1], ref_get(vm, condition));
  }
  vm->unwindKind = UnwindKind_Condition;
  vm->unwindCondition = ref_get(vm, condition);
  return unwind_v();
}

Value vm_control_unwind_protect(State* vm, u32 base, u32 argc) {
  if (argc == 0) return raise_error(vm, "unwind-protect: bad compiled form");
  Value result = control_apply0(vm, vm->stack.data[base]);
  UnwindKind kind = vm->unwindKind;
  OT_SCOPE(vm);
  Ref condition = ref_push(vm, vm->unwindCondition);
  Ref restartArgs = ref_push(vm, vm->unwindRestartArgs);
  u64 restartId = vm->unwindRestartId;
  if (result.tag == Tag_Unwind) vm->unwindKind = UnwindKind_None;
  for (u32 i = 1; i < argc; i++) {
    Value cleanup = control_apply0(vm, vm->stack.data[base + i]);
    if (cleanup.tag == Tag_Unwind) {
      result = cleanup;
      kind = vm->unwindKind;
      ref_set(vm, condition, vm->unwindCondition);
      ref_set(vm, restartArgs, vm->unwindRestartArgs);
      restartId = vm->unwindRestartId;
      vm->unwindKind = UnwindKind_None;
    }
  }
  if (result.tag == Tag_Unwind) {
    vm->unwindKind = kind;
    vm->unwindCondition = ref_get(vm, condition);
    vm->unwindRestartArgs = ref_get(vm, restartArgs);
    vm->unwindRestartId = restartId;
  }
  return result;
}

Value vm_control_with_params(State* vm, u32 base, u32 argc) {
  if (argc == 0 || (argc - 1) % 2 != 0) return raise_error(vm, "with-params: bad compiled form");
  u32 paramBase = vm->paramBindings.len;
  OT_SCOPE(vm);
  for (u32 i = 0; i + 1 < argc; i += 2) {
    Ref paramRoot = ref_push(vm, control_apply0(vm, vm->stack.data[base + i]));
    if (ref_get(vm, paramRoot).tag == Tag_Unwind) {
      vm->paramBindings.len = paramBase;
      return unwind_v();
    }
    if (ref_get(vm, paramRoot).tag != Tag_Param) {
      vm->paramBindings.len = paramBase;
      return raise_error(vm, "with-params: not a param");
    }
    Ref valueRoot = ref_push(vm, control_apply0(vm, vm->stack.data[base + i + 1]));
    if (ref_get(vm, valueRoot).tag == Tag_Unwind) {
      vm->paramBindings.len = paramBase;
      return unwind_v();
    }
    vec_push(&vm->paramBindings,
             ((ParamBinding){ref_get(vm, paramRoot), ref_get(vm, valueRoot)}));
  }
  Value result = control_apply0(vm, vm->stack.data[base + argc - 1]);
  vm->paramBindings.len = paramBase;
  return result;
}

Value vm_control_defparam(State* vm, u32 base, u32 argc) {
  if (argc != 3 || vm->stack.data[base].tag != Tag_Symbol)
    return raise_error(vm, "defparam: bad compiled form");
  Obj* object = heap_alloc(&vm->heap, ObjType_Param, sizeof(ParamData));
  Value param = obj_v(Tag_Param, object);
  ParamData* data = as_param(param);
  u32 paramName = vm->stack.data[base].id;
  data->name = paramName;
  data->defaultVal = vm->stack.data[base + 2];
  OT_SCOPE(vm);
  Ref paramRoot = ref_push(vm, param);
  Ref doc = ref_push(vm, vm->stack.data[base + 1]);
  // `data` is not read past here: ns_define allocates and would strand it.
  return ns_define(vm, paramName, ref_get(vm, paramRoot), false, ref_get(vm, doc));
}

Value vm_control_ns(State* vm, u32 base, u32 argc) {
  if (argc == 0 || vm->stack.data[base].tag != Tag_Symbol)
    return raise_error(vm, "ns: bad compiled form");
  ns_switch(vm, vm->stack.data[base].id);
  OT_SCOPE(vm);
  Ref specs = ref_push(vm, nil_v());
  for (u32 i = 1; i < argc; i++) {
    Value clause = vm->stack.data[base + i];
    if (!pairp(clause) || car_(clause).tag != Tag_Keyword || car_(clause).id != vm->syms.kwRequire)
      continue;
    ref_set(vm, specs, cdr_(clause));
    while (pairp(ref_get(vm, specs))) {
      Value result = require_spec(vm, car_(ref_get(vm, specs)));
      if (result.tag == Tag_Unwind) return result;
      ref_set(vm, specs, cdr_(ref_get(vm, specs)));
    }
  }
  return nil_v();
}

Value vm_control_in_ns(State* vm, u32 base, u32 argc) {
  if (argc != 1) return raise_error(vm, "in-ns: one argument");
  u32 name = name_id_of(vm, unwrap_quote(vm, vm->stack.data[base]));
  if (!name) return raise_error(vm, "in-ns: bad namespace name");
  ns_switch(vm, name);
  return nil_v();
}

Value vm_control_require(State* vm, u32 base, u32 argc) {
  for (u32 i = 0; i < argc; i++) {
    Value result = require_spec(vm, vm->stack.data[base + i]);
    if (result.tag == Tag_Unwind) return result;
  }
  return nil_v();
}

// Compile and execute one expanded top-level form.
Value eval_form(State* vm, Value form) {
  // Route through the expansion hook *expander* in otium.core.
  Value core = ns_lookup(vm, vm->syms.otiumCore_);
  if (!is_nil(core)) {
    Value var = table_get(vm, ns_field(vm, core, vm->syms.kwVars), symbol_v(vm->syms.expander_));
    if (!is_nil(var) && var_value(var).tag == Tag_Function) {
      Value expanded;
      {
        OT_SCOPE(vm);
        Ref expander = ref_push(vm, var_value(var));
        Ref b = ref_push(vm, form);
        u32 savedExpandNs = vm->expandNs;
        vm->expandNs = vm->currentNs;
        expanded = apply(vm, ref_get(vm, expander), b.i, 1);
        vm->expandNs = savedExpandNs;
      }
      if (expanded.tag == Tag_Unwind) return expanded;
      form = expanded;
    }
  }
  OT_SCOPE(vm);
  Ref formRoot = ref_push(vm, form);
  Ref code = ref_push(vm, compile_form(vm, ref_get(vm, formRoot)));
  if (ref_get(vm, code).tag == Tag_Unwind) return unwind_v();
  return vm_execute_code(vm, ref_get(vm, code));
}

Value eval_source_policy(State* vm, const char* src, u32 len, const char* name,
                         const EvalSourcePolicy* policy) {
  EvalSourceState localState;
  EvalSourceState* state = policy->state ? policy->state : &localState;
  *state = (EvalSourceState){0};

  Reader reader;
  reader_init(&reader, vm, src, len, name);
  Value last = nil_v();
  for (;;) {
    Value form = reader_next(&reader);
    if (form.tag == Tag_Unwind) {
      state->readError = true;
      state->incomplete = reader_incomplete(&reader);
      return form;
    }
    if (reader_at_eof(&reader)) {
      state->consumed = len;
      return last;
    }
    last = policy->eval ? policy->eval(vm, form, policy->data) : eval_form(vm, form);
    if (last.tag == Tag_Unwind) return last;
    state->consumed = reader_position(&reader);
    if (policy->afterEval) policy->afterEval(vm, last, state->consumed, policy->data);
  }
}

Value eval_source(State* vm, const char* src, u32 len, const char* name) {
  EvalSourcePolicy policy = {0};
  return eval_source_policy(vm, src, len, name, &policy);
}
