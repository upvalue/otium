// eval.cpp — stage-0 evaluator: trampoline with proper tail calls, special
// forms, application, conditions/restarts (spec 8), dynamic params (spec 9).
#include "eval.hpp"
#include "ns.hpp"
#include "reader.hpp"
#include "vm.hpp"

namespace ot {

// ---------------------------------------------------------------- helpers

static Value car_(Value v) { return as_pair(v)->car; }
static Value cdr_(Value v) { return as_pair(v)->cdr; }
static bool pairp(Value v) { return v.tag == Tag::Pair; }
static bool sym_is(Value v, u32 id) { return v.tag == Tag::Symbol && v.id == id; }
static bool has_leading_docstring(Value forms) {
  return pairp(forms) && car_(forms).tag == Tag::String && pairp(cdr_(forms));
}
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

// Overflow errors unwind directly (no handler walk) so that reporting the
// overflow cannot itself overflow.
static Value raise_overflow(State& vm, const char* msg) {
  Scope s(vm);
  Slot c = s.push(make_table(vm));
  table_put(vm, c.get(), keyword_v(vm.syms.kwType), symbol_v(vm.syms.error_));
  Value msgStr = make_string(vm, msg, (u32)strlen(msg));
  table_put(vm, c.get(), keyword_v(vm.syms.kwMessage), msgStr);
  vm.unwindKind = UnwindKind::Condition;
  vm.unwindCondition = c.get();
  return unwind_v();
}

// one-element array "box" for a lexical binding
static Value make_box(State& vm, Value v) {
  Scope s(vm);
  Slot vS = s.push(v);
  Slot b = s.push(make_array(vm, 1));
  array_push(vm, b.get(), vS.get());  // alloc-free
  return b.get();
}
static Value box_get(Value b) { return as_array(b)->items[0]; }
static void box_set(Value b, Value v) { as_array(b)->items[0] = v; }

static Value env_lookup_box(State& vm, Value env, Value sym) {
  while (pairp(env)) {
    Value b = table_get(vm, car_(env), sym);
    if (!is_nil(b)) return b;
    env = cdr_(env);
  }
  return nil_v();
}

static void frame_bind(State& vm, Value frame, Value sym, Value v) {
  Scope s(vm);
  Slot f = s.push(frame);
  Value box = make_box(vm, v);
  table_put(vm, f.get(), sym, box);  // alloc-free; sym is an immediate
}

static Value list_from_stack(State& vm, u32 base, u32 n) {
  Scope s(vm);
  Slot acc = s.push(null_v());
  for (u32 j = n; j-- > 0;) acc.set(make_pair(vm, Slot{&vm, base + j}, acc));
  return acc.get();
}

static Value lookup_symbol(State& vm, Value sym, Value env) {
  if (!sym_qualified(vm, sym.id)) {
    Value box = env_lookup_box(vm, env, sym);
    if (!is_nil(box)) return box_get(box);
  }
  return ns_resolve(vm, sym);
}

static Value make_closure(State& vm, u32 name, Value params, Value body, Value env, bool macro) {
  Value doc = nil_v();
  if (has_leading_docstring(body)) {
    doc = car_(body);
    body = cdr_(body);
  }
  Scope s(vm);
  Slot pS = s.push(params);
  Slot bS = s.push(body);
  Slot eS = s.push(env);
  Slot dS = s.push(doc);
  Obj* o = vm.heap.alloc(macro ? ObjType::Macro : ObjType::Function, sizeof(FunctionData));
  Value fv = obj_v(macro ? Tag::Macro : Tag::Function, o);
  FunctionData* fd = fn_data(fv);
  fd->name = name;
  fd->params = pS.get();
  fd->body = bS.get();
  fd->env = eS.get();
  fd->code = nil_v();
  fd->nsName = symbol_v(vm.currentNs);
  fd->native = nullptr;
  fd->docstring = dS.get();
  fd->nupvals = 0;
  return fv;
}

Value make_native(State& vm, const char* name, NativeFn fn) {
  Obj* o = vm.heap.alloc(ObjType::Function, sizeof(FunctionData));
  Value fv = obj_v(Tag::Function, o);
  FunctionData* fd = fn_data(fv);
  fd->name = vm.intern.intern(name, (u32)strlen(name));
  fd->params = nil_v();
  fd->body = nil_v();
  fd->env = nil_v();
  fd->code = nil_v();
  fd->nsName = symbol_v(vm.syms.otiumCore_);
  fd->native = fn;
  fd->docstring = nil_v();
  fd->nupvals = 0;
  return fv;
}

// Bind a parameter-list form against a list of argument values into `frame`.
// Handles fixed params, `. rest` (improper tail), `& rest`, bare symbol, and
// the [a b] array-literal spelling (leading `array` symbol skipped).
static Value bind_param_list(State& vm, Value frame, Value ps, Value argsList) {
  // frame_bind allocates, so the ps/args cursors live in rooted slots and
  // every read goes through them (GC rewrites the slots, not our locals).
  Scope s(vm);
  Slot f = s.push(frame);
  Slot pS = s.push(ps);
  Slot aS = s.push(argsList);
  if (pS.get().tag == Tag::Symbol) {
    frame_bind(vm, f.get(), pS.get(), aS.get());
    return nil_v();
  }
  pS.set(strip_array_literal_head(pS.get(), vm.syms.array_));
  while (pairp(pS.get())) {
    Value p = car_(pS.get());  // param names are symbols: immediate, safe
    if (sym_is(p, vm.syms.amp_)) {
      pS.set(cdr_(pS.get()));
      if (!pairp(pS.get())) return raise_error(vm, "malformed rest parameter");
      frame_bind(vm, f.get(), car_(pS.get()), aS.get());
      return nil_v();
    }
    if (!pairp(aS.get())) return raise_error(vm, "too few arguments");
    frame_bind(vm, f.get(), p, car_(aS.get()));
    aS.set(cdr_(aS.get()));
    pS.set(cdr_(pS.get()));
  }
  if (pS.get().tag == Tag::Symbol) {  // dotted rest
    frame_bind(vm, f.get(), pS.get(), aS.get());
    return nil_v();
  }
  if (pairp(aS.get())) return raise_error(vm, "too many arguments");
  return nil_v();
}

// Build the callee's env from stack args. Returns env or Unwind.
// callee must be a Function/Macro Value; it is rooted here so fd-> reads stay
// valid across the allocations below.
static Value bind_params_stack(State& vm, Value callee, u32 base, u32 argc) {
  Scope s(vm);
  Slot c = s.push(callee);
  Slot args = s.push(list_from_stack(vm, base, argc));
  Slot frame = s.push(make_table(vm));
  Slot env = s.push(fn_data(c.get())->env);
  OT_TRY(bind_param_list(vm, frame.get(), fn_data(c.get())->params, args.get()));
  return make_pair(vm, frame, env);
}

static Value param_read(State& vm, Value p) {
  for (u32 i = vm.paramBindings.len; i-- > 0;) {
    if (vm.paramBindings[i].param.obj == p.obj) return vm.paramBindings[i].value;
  }
  return param_data(p)->defaultVal;
}

// ---------------------------------------------------------------- apply

static Value eval_body(State& vm, Value body, Value env) {
  // eval_in can collect; keep the cursor and env in rooted slots.
  Scope s(vm);
  Slot b = s.push(body);
  Slot e = s.push(env);
  Value r = nil_v();
  while (pairp(b.get())) {
    r = eval_in(vm, car_(b.get()), e.get());
    if (r.tag == Tag::Unwind) break;
    b.set(cdr_(b.get()));
  }
  return r;
}

Value apply(State& vm, Value callee, u32 base, u32 argc) {
  switch (callee.tag) {
    // Tag::Macro shares FunctionData; direct apply is the expander's
    // privileged call path (call position still rejects macros in eval_tr).
    case Tag::Macro:
    case Tag::Function: {
      if (fn_data(callee)->native) return fn_data(callee)->native(vm, base, argc);
      if (fn_data(callee)->code.tag == Tag::Code) return vm_call(vm, callee, base, argc);
      Scope s(vm);
      Slot c = s.push(callee);  // binding below allocates
      Value env = bind_params_stack(vm, c.get(), base, argc);
      OT_TRY(env);
      u32 savedNs = vm.currentNs;
      FunctionData* fd = fn_data(c.get());
      vm.currentNs = fd->nsName.id;
      Value r = eval_body(vm, fd->body, env);
      vm.currentNs = savedNs;
      return r;
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

// ---------------------------------------------------------------- quasiquote

static Value list_append2(State& vm, Value a, Value b) {  // copies a
  if (!pairp(a)) return b;
  Scope s(vm);
  Slot aS = s.push(a);
  // count then build backwards (alloc-free walk)
  u32 n = 0;
  for (Value c = a; pairp(c); c = cdr_(c)) n++;
  Slot out = s.push(b);
  for (u32 i = n; i-- > 0;) {
    // Re-walk from the rooted head each round: a raw cursor would go stale
    // across make_pair. O(n^2), fine for the short lists qq builds.
    Value c = aS.get();
    for (u32 j = 0; j < i; j++) c = cdr_(c);
    out.set(make_pair(vm, car_(c), out.get()));
  }
  return out.get();
}

static Value qq(State& vm, Value t, int depth, Value env) {
  if (!pairp(t)) return t;
  Value h = car_(t);
  if (sym_is(h, vm.syms.unquote_)) {
    if (depth == 1) return eval_in(vm, car_(cdr_(t)), env);
    Scope s(vm);
    Slot r = s.push(qq(vm, car_(cdr_(t)), depth - 1, env));
    OT_TRY(r.get());
    r.set(make_pair(vm, r.get(), null_v()));
    return make_pair(vm, symbol_v(vm.syms.unquote_), r.get());
  }
  if (sym_is(h, vm.syms.quasiquote_)) {
    Scope s(vm);
    Slot r = s.push(qq(vm, car_(cdr_(t)), depth + 1, env));
    OT_TRY(r.get());
    r.set(make_pair(vm, r.get(), null_v()));
    return make_pair(vm, symbol_v(vm.syms.quasiquote_), r.get());
  }
  // element position: root t and env, since recursing/evaluating allocates
  Scope s(vm);
  Slot tS = s.push(t);
  Slot eS = s.push(env);
  if (pairp(h) && sym_is(car_(h), vm.syms.unquoteSplicing_) && depth == 1) {
    Value lst = eval_in(vm, car_(cdr_(h)), env);
    OT_TRY(lst);
    if (!pairp(lst) && lst.tag != Tag::Null) return raise_error(vm, "unquote-splicing: not a list");
    Slot r = s.push(lst);
    Value rest = qq(vm, cdr_(tS.get()), depth, eS.get());
    OT_TRY(rest);
    return list_append2(vm, r.get(), rest);
  }
  Value eh = qq(vm, h, depth, env);
  OT_TRY(eh);
  Slot r = s.push(eh);
  Value et = qq(vm, cdr_(tS.get()), depth, eS.get());
  OT_TRY(et);
  return make_pair(vm, r.get(), et);
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
  if (!is_nil(ns_lookup(vm, nsName))) return nil_v();
  u32 len;
  const char* nm = vm.intern.name(nsName, &len);
  if (!vm.loadFn) return raise_error(vm, "namespace not found: %.*s", (int)len, nm);
  for (u32 i = 0; i < vm.loadingNs.len; i++)
    if (vm.loadingNs[i] == nsName) return raise_error(vm, "circular require: %.*s", (int)len, nm);
  Buf src;
  char cname[256];
  snprintf(cname, sizeof cname, "%.*s", (int)len, nm);
  if (!vm.loadFn(vm.loadUd, cname, &src))
    return raise_error(vm, "namespace not found on load path: %s", cname);
  vm.loadingNs.push(nsName);
  u32 savedNs = vm.currentNs;
  ns_switch(vm, nsName);
  Value r = eval_source(vm, src.data, src.len, cname);
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

// ---------------------------------------------------------------- trampoline

// Restore paramBindings/handler/restart depth never happens here: each special
// form manages its own stacks with single exit points.
// GC DISCIPLINE: any eval_in / make_* / heap.alloc call can collect and move
// every heap object; C++ locals holding heap Values are stale afterwards.
// The current form, env, and the special forms' list cursor live in three
// rooted slots (rootBase..rootBase+2) that the collector rewrites in place.
// After any allocating call, re-read through the slots (EVAL_OR_RET refreshes
// form/env automatically; ARGS always reads the cursor slot).
static Value eval_tr(State& vm, Value form, Value env, bool topLevel) {
  u32 rootBase = vm.stack.len;
  vm.push(form);
  vm.push(env);
  vm.push(nil_v());  // rootBase+2: the active special form's list cursor
  bool enteredClosure = false;
  u32 restoreNs = vm.currentNs;

#define ARGS (vm.stack[rootBase + 2])
#define RET(x)                                                                                     \
  do {                                                                                             \
    Value _r = (x);                                                                                \
    vm.popTo(rootBase);                                                                            \
    if (topLevel && enteredClosure) vm.currentNs = restoreNs;                                      \
    return _r;                                                                                     \
  } while (0)
#define EVAL_OR_RET(dst, f)                                                                        \
  Value dst = eval_in(vm, (f), env);                                                               \
  if ((dst).tag == Tag::Unwind) RET(dst);                                                          \
  form = vm.stack[rootBase];                                                                       \
  env = vm.stack[rootBase + 1];

  for (;;) {
    vm.stack[rootBase] = form;
    vm.stack[rootBase + 1] = env;
    vm.stack[rootBase + 2] = form.tag == Tag::Pair ? cdr_(form) : nil_v();

    if (vm.interruptFlag) {
      vm.interruptFlag = false;
      RET(start_quit(vm));
    }

    if (form.tag == Tag::Symbol) RET(lookup_symbol(vm, form, env));
    if (form.tag != Tag::Pair) RET(form);  // self-evaluating (incl. ())

    Value head = car_(form);
    Value args = cdr_(form);
    Syms& S = vm.syms;

    if (head.tag == Tag::Symbol) {
      u32 h = head.id;

      if (h == S.quote_) {
        if (!pairp(args)) RET(raise_error(vm, "quote: one argument"));
        RET(car_(args));
      }

      if (h == S.if_) {
        if (!pairp(args) || !pairp(cdr_(args))) RET(raise_error(vm, "if: bad form"));
        EVAL_OR_RET(t, car_(args));
        if (is_truthy(t)) {
          form = car_(cdr_(ARGS));
          continue;
        }
        Value elseTail = cdr_(cdr_(ARGS));
        if (!pairp(elseTail)) RET(nil_v());
        form = car_(elseTail);
        continue;
      }

      if (h == S.define_ || h == S.def_ || h == S.definePriv_) {
        bool priv = (h == S.definePriv_);
        if (!pairp(args)) RET(raise_error(vm, "define: bad form"));
        Value target = car_(args);
        Value name, valueV;
        bool hasDoc;
        if (pairp(target)) {  // (define (name . params) [doc] body...)
          name = car_(target);
          Value body = cdr_(args);
          hasDoc = has_leading_docstring(body);
          if (hasDoc) body = cdr_(body);
          valueV = make_closure(vm, name.id, cdr_(target), body, env, false);
          env = vm.stack[rootBase + 1];  // make_closure may have collected
        } else {
          name = target;
          Value rest = cdr_(args);
          hasDoc = has_leading_docstring(rest);
          if (hasDoc) rest = cdr_(rest);
          if (!pairp(rest)) RET(raise_error(vm, "define: missing value"));
          EVAL_OR_RET(v, car_(rest));
          valueV = v;
          if (valueV.tag == Tag::Function) {  // name an anonymous closure
            FunctionData* fd = fn_data(valueV);
            if (!fd->native && fd->name == 0) fd->name = name.id;
          }
        }
        if (name.tag != Tag::Symbol) RET(raise_error(vm, "define: name must be a symbol"));
        // re-derive the docstring from the rooted form: the reads above
        // predate allocations
        Value doc = hasDoc ? car_(cdr_(ARGS)) : nil_v();
        if (!pairp(env)) {  // top level: namespace var
          Scope s(vm);
          Slot vS = s.push(valueV);
          Slot dS = s.push(doc);
          RET(ns_define(vm, name.id, vS.get(), priv, dS.get()));
        }
        frame_bind(vm, car_(env), name, valueV);  // local binding
        RET(valueV);
      }

      if (h == S.setBang_) {
        if (!pairp(args) || !pairp(cdr_(args))) RET(raise_error(vm, "set!: bad form"));
        Value name = car_(args);
        EVAL_OR_RET(v, car_(cdr_(args)));
        if (name.tag != Tag::Symbol) RET(raise_error(vm, "set!: name must be a symbol"));
        if (!sym_qualified(vm, name.id)) {
          Value box = env_lookup_box(vm, env, name);
          if (!is_nil(box)) {
            box_set(box, v);
            RET(v);
          }
        }
        Value var = ns_resolve_var(vm, name);
        if (is_nil(var)) {
          RET(raise_error_sym(vm, "set!: unbound %.*s", name.id));
        }
        var_set(var, v);
        RET(v);
      }

      if (h == S.lambda_ || h == S.fn_) {
        if (!pairp(args)) RET(raise_error(vm, "lambda: bad form"));
        RET(make_closure(vm, 0, car_(args), cdr_(args), env, false));
      }

      if (h == S.defmacro_) {
        if (!pairp(args) || !pairp(cdr_(args))) RET(raise_error(vm, "defmacro: bad form"));
        Value name = car_(args);
        Scope s(vm);
        Slot m = s.push(make_closure(vm, name.id, car_(cdr_(args)), cdr_(cdr_(args)), env, true));
        RET(ns_define(vm, name.id, m.get(), false, nil_v()));
      }

      if (h == S.begin_ || h == S.do_) {
        if (!pairp(args)) RET(nil_v());
        while (pairp(cdr_(ARGS))) {
          EVAL_OR_RET(r, car_(ARGS));
          (void)r;
          ARGS = cdr_(ARGS);
        }
        form = car_(ARGS);
        continue;
      }

      if (h == S.let_) {
        if (!pairp(args)) RET(raise_error(vm, "let: bad form"));
        Scope s(vm);
        Slot envS = s.push(make_table(vm));
        envS.set(make_pair(vm, envS.get(), vm.stack[rootBase + 1]));
        Slot bS = s.push();  // bindings cursor
        { bS.set(strip_array_literal_head(car_(ARGS), S.array_)); }
        while (pairp(bS.get())) {
          Value pair = car_(bS.get());
          if (!pairp(pair) || !pairp(cdr_(pair))) RET(raise_error(vm, "let: bad binding"));
          Value bv = eval_in(vm, car_(cdr_(pair)), envS.get());  // sequential
          if (bv.tag == Tag::Unwind) RET(bv);
          pair = car_(bS.get());  // re-read: the eval may have collected
          frame_bind(vm, car_(envS.get()), car_(pair), bv);
          bS.set(cdr_(bS.get()));
        }
        bS.set(cdr_(ARGS));  // reuse the cursor for the body walk
        if (!pairp(bS.get())) RET(nil_v());
        vm.stack[rootBase + 1] = envS.get();  // keep env2 rooted past the scope pop
        while (pairp(cdr_(bS.get()))) {
          Value r = eval_in(vm, car_(bS.get()), vm.stack[rootBase + 1]);
          if (r.tag == Tag::Unwind) RET(r);
          bS.set(cdr_(bS.get()));
        }
        form = car_(bS.get());
        env = vm.stack[rootBase + 1];
        continue;
      }

      if (h == S.while_) {
        Scope s(vm);
        Slot bS = s.push();  // body cursor
        for (;;) {
          if (vm.interruptFlag) {
            vm.interruptFlag = false;
            RET(start_quit(vm));
          }
          if (!pairp(ARGS)) RET(raise_error(vm, "while: bad form"));
          EVAL_OR_RET(t, car_(ARGS));
          if (is_falsy(t)) RET(nil_v());
          bS.set(cdr_(ARGS));
          while (pairp(bS.get())) {
            EVAL_OR_RET(r, car_(bS.get()));
            (void)r;
            bS.set(cdr_(bS.get()));
          }
        }
      }

      if (h == S.and_) {
        if (!pairp(args)) RET(bool_v(true));
        while (pairp(cdr_(ARGS))) {
          EVAL_OR_RET(r, car_(ARGS));
          if (is_falsy(r)) RET(r);
          ARGS = cdr_(ARGS);
        }
        form = car_(ARGS);
        continue;
      }

      if (h == S.or_) {
        if (!pairp(args)) RET(bool_v(false));
        while (pairp(cdr_(ARGS))) {
          EVAL_OR_RET(r, car_(ARGS));
          if (is_truthy(r)) RET(r);
          ARGS = cdr_(ARGS);
        }
        form = car_(ARGS);
        continue;
      }

      if (h == S.cond_) {
        bool chosen = false;
        Scope s(vm);
        Slot bS = s.push();  // clause body cursor
        while (pairp(ARGS)) {
          Value clause = car_(ARGS);
          if (!pairp(clause)) RET(raise_error(vm, "cond: bad clause"));
          Value test = car_(clause);
          Value t;
          if (sym_is(test, S.else_)) {
            if (!pairp(cdr_(clause))) RET(raise_error(vm, "cond: else needs a body"));
            t = bool_v(true);
          } else {
            t = eval_in(vm, test, env);
            if (t.tag == Tag::Unwind) RET(t);
            env = vm.stack[rootBase + 1];
          }
          if (is_truthy(t)) {
            bS.set(cdr_(car_(ARGS)));
            if (!pairp(bS.get())) RET(t);  // one-element clause
            while (pairp(cdr_(bS.get()))) {
              EVAL_OR_RET(r, car_(bS.get()));
              (void)r;
              bS.set(cdr_(bS.get()));
            }
            form = car_(bS.get());
            chosen = true;
            break;
          }
          ARGS = cdr_(ARGS);
        }
        if (chosen) continue;
        RET(nil_v());
      }

      if (h == S.quasiquote_) {
        if (!pairp(args)) RET(raise_error(vm, "quasiquote: one argument"));
        RET(qq(vm, car_(args), 1, env));
      }
      if (h == S.unquote_ || h == S.unquoteSplicing_)
        RET(raise_error(vm, "unquote outside quasiquote"));

      if (h == S.ns_) {
        if (!pairp(args) || car_(args).tag != Tag::Symbol) RET(raise_error(vm, "ns: bad form"));
        ns_switch(vm, car_(args).id);
        ARGS = cdr_(ARGS);  // clause cursor (require_spec allocates)
        Scope s(vm);
        Slot spS = s.push();  // spec cursor
        while (pairp(ARGS)) {
          Value clause = car_(ARGS);
          if (pairp(clause) && car_(clause).tag == Tag::Keyword && car_(clause).id == S.kwRequire) {
            spS.set(cdr_(clause));
            while (pairp(spS.get())) {
              Value r = require_spec(vm, car_(spS.get()));
              if (r.tag == Tag::Unwind) RET(r);
              spS.set(cdr_(spS.get()));
            }
          }
          ARGS = cdr_(ARGS);
        }
        RET(nil_v());
      }

      if (h == S.inNs_) {
        if (!pairp(args)) RET(raise_error(vm, "in-ns: one argument"));
        Value n = unwrap_quote(vm, car_(args));
        u32 id = name_id_of(vm, n);
        if (!id) RET(raise_error(vm, "in-ns: bad namespace name"));
        ns_switch(vm, id);
        RET(nil_v());
      }

      if (h == S.require_) {
        while (pairp(ARGS)) {
          Value r = require_spec(vm, car_(ARGS));
          if (r.tag == Tag::Unwind) RET(r);
          ARGS = cdr_(ARGS);
        }
        RET(nil_v());
      }

      if (h == S.handlerBind_) {
        if (!pairp(args)) RET(raise_error(vm, "handler-bind: bad form"));
        Scope s(vm);
        u32 hbase = vm.handlers.len;
        Slot cS = s.push();  // clause cursor
        { cS.set(strip_array_literal_head(car_(ARGS), S.array_)); }
        while (pairp(cS.get())) {
          Value cl = car_(cS.get());
          if (!pairp(cl) || !pairp(cdr_(cl))) {
            vm.handlers.len = hbase;
            RET(raise_error(vm, "handler-bind: bad binding"));
          }
          Value pr = eval_in(vm, car_(cl), env);
          if (pr.tag == Tag::Unwind) {
            vm.handlers.len = hbase;
            RET(pr);
          }
          env = vm.stack[rootBase + 1];
          // pi roots pr across the handler eval below; once pushed into
          // vm.handlers both are traced by the root walker (state.cpp).
          Slot pi = s.push(pr);
          cl = car_(cS.get());  // re-read after the eval
          Value hd = eval_in(vm, car_(cdr_(cl)), env);
          if (hd.tag == Tag::Unwind) {
            vm.handlers.len = hbase;
            RET(hd);
          }
          env = vm.stack[rootBase + 1];
          Slot hi = s.push(hd);
          vm.handlers.push({pi.get(), hi.get()});
          cS.set(cdr_(cS.get()));
        }
        Value r = eval_body(vm, cdr_(ARGS), env);
        vm.handlers.len = hbase;
        RET(r);
      }

      if (h == S.restartCase_) {
        if (!pairp(args)) RET(raise_error(vm, "restart-case: bad form"));
        Scope s(vm);
        u32 rbase = vm.restarts.len;
        u64 firstId = vm.restartIdCounter + 1;
        u32 count = 0;
        Slot cS = s.push(cdr_(ARGS));  // clause cursor (the alloc below moves pairs)
        while (pairp(cS.get())) {
          Value cl = car_(cS.get());
          if (!pairp(cl) || car_(cl).tag != Tag::Symbol) {
            vm.restarts.len = rbase;
            RET(raise_error(vm, "restart-case: bad clause"));
          }
          Value desc = nil_v();
          Value rest2 = cdr_(cl);
          if (has_leading_docstring(rest2)) desc = car_(rest2);
          Slot dr = s.push(desc);
          Obj* o = vm.heap.alloc(ObjType::Restart, sizeof(RestartData));
          Value rv = obj_v(Tag::Restart, o);
          RestartData* rd = restart_data(rv);
          rd->name = car_(car_(cS.get())).id;  // re-read: the alloc collected
          rd->description = dr.get();
          rd->restartId = ++vm.restartIdCounter;
          dr.set(rv);  // keep rooted (one slot per clause, popped at scope end)
          vm.restarts.push({rv});
          count++;
          cS.set(cdr_(cS.get()));
        }
        Value r = eval_in(vm, car_(ARGS), env);
        vm.restarts.len = rbase;
        if (r.tag == Tag::Unwind && vm.unwindKind == UnwindKind::Restart &&
            vm.unwindRestartId >= firstId && vm.unwindRestartId < firstId + count) {
          u32 k = (u32)(vm.unwindRestartId - firstId);
          Value cl = cdr_(ARGS);
          for (u32 i = 0; i < k; i++) cl = cdr_(cl);
          cl = car_(cl);
          Value rest2 = cdr_(cl);
          if (has_leading_docstring(rest2)) rest2 = cdr_(rest2);  // skip description
          cS.set(rest2);  // keep the clause tail rooted across binding
          Value clArgs = vm.unwindRestartArgs;
          vm.unwindKind = UnwindKind::None;
          vm.unwindCondition = nil_v();
          vm.unwindRestartArgs = nil_v();
          Slot ar = s.push(clArgs);
          Slot fr = s.push(make_table(vm));
          Value be = bind_param_list(vm, fr.get(), car_(cS.get()), ar.get());
          if (be.tag == Tag::Unwind) RET(be);
          env = vm.stack[rootBase + 1];
          Slot e2 = s.push(make_pair(vm, fr.get(), env));
          r = eval_body(vm, cdr_(cS.get()), e2.get());
        }
        RET(r);
      }

      if (h == S.try_) {
        Scope s(vm);
        Slot bS = s.push(args);  // body cursor
        Slot catchS = s.push(null_v());
        {
          Value b = args;  // split body / trailing catch clauses
          while (pairp(b) && !(pairp(car_(b)) && sym_is(car_(car_(b)), S.catch_))) b = cdr_(b);
          catchS.set(b);
        }
        Value r = nil_v();
        while (pairp(bS.get()) && !val_eq(bS.get(), catchS.get())) {
          r = eval_in(vm, car_(bS.get()), env);
          if (r.tag == Tag::Unwind) break;
          env = vm.stack[rootBase + 1];
          bS.set(cdr_(bS.get()));
        }
        if (r.tag == Tag::Unwind && vm.unwindKind == UnwindKind::Condition && pairp(catchS.get())) {
          Slot cr = s.push(vm.unwindCondition);
          vm.unwindKind = UnwindKind::None;
          bS.set(catchS.get());  // reuse as the catch-clause cursor
          while (pairp(bS.get())) {
            Value cl = car_(bS.get());  // (catch (pred var) forms...)
            if (!pairp(cdr_(cl)) || !pairp(car_(cdr_(cl))))
              RET(raise_error(vm, "try: bad catch clause"));
            Value pf = eval_in(vm, car_(car_(cdr_(cl))), env);
            if (pf.tag == Tag::Unwind) RET(pf);
            env = vm.stack[rootBase + 1];
            Value t;
            {
              Scope s2(vm);
              Slot pr = s2.push(pf);
              Slot ab = s2.push(cr.get());
              t = apply(vm, pr.get(), ab.idx, 1);
            }
            if (t.tag == Tag::Unwind) RET(t);
            env = vm.stack[rootBase + 1];
            if (is_truthy(t)) {
              Slot fr = s.push(make_table(vm));
              cl = car_(bS.get());  // re-read after the allocations above
              frame_bind(vm, fr.get(), car_(cdr_(car_(cdr_(cl)))), cr.get());
              env = vm.stack[rootBase + 1];
              Slot e2 = s.push(make_pair(vm, fr.get(), env));
              RET(eval_body(vm, cdr_(cdr_(car_(bS.get()))), e2.get()));
            }
            bS.set(cdr_(bS.get()));
          }
          // no clause matched: keep unwinding
          vm.unwindKind = UnwindKind::Condition;
          vm.unwindCondition = cr.get();
        }
        RET(r);
      }

      if (h == S.unwindProtect_ || h == S.defer_) {
        if (!pairp(args)) RET(raise_error(vm, "unwind-protect: bad form"));
        Scope s(vm);
        Value r = eval_in(vm, car_(args), env);
        env = vm.stack[rootBase + 1];
        UnwindKind k = vm.unwindKind;
        Slot condR = s.push(vm.unwindCondition);
        Slot argsR = s.push(vm.unwindRestartArgs);
        u64 rid = vm.unwindRestartId;
        if (r.tag == Tag::Unwind) vm.unwindKind = UnwindKind::None;
        Slot cS = s.push(cdr_(ARGS));  // cleanup cursor
        while (pairp(cS.get())) {
          Value cr = eval_in(vm, car_(cS.get()), env);
          if (cr.tag == Tag::Unwind) {  // cleanup's unwind replaces in-flight state
            r = cr;
            k = vm.unwindKind;
            condR.set(vm.unwindCondition);
            argsR.set(vm.unwindRestartArgs);
            rid = vm.unwindRestartId;
            vm.unwindKind = UnwindKind::None;
          }
          env = vm.stack[rootBase + 1];
          cS.set(cdr_(cS.get()));
        }
        if (r.tag == Tag::Unwind) {
          vm.unwindKind = k;
          vm.unwindCondition = condR.get();
          vm.unwindRestartArgs = argsR.get();
          vm.unwindRestartId = rid;
        }
        RET(r);
      }

      if (h == S.defparam_) {
        if (pairp(env)) RET(raise_error(vm, "defparam: only allowed at top level"));
        if (!pairp(args) || car_(args).tag != Tag::Symbol)
          RET(raise_error(vm, "defparam: bad form"));
        Value name = car_(args);
        Value rest = cdr_(args);
        bool hasDoc = has_leading_docstring(rest);
        if (hasDoc) rest = cdr_(rest);
        if (!pairp(rest)) RET(raise_error(vm, "defparam: missing default"));
        EVAL_OR_RET(d, car_(rest));
        Value doc = hasDoc ? car_(cdr_(ARGS)) : nil_v();  // re-read post-eval
        Scope s(vm);
        Slot dr = s.push(d);
        Slot docS = s.push(doc);
        Obj* o = vm.heap.alloc(ObjType::Param, sizeof(ParamData));
        Value pv = obj_v(Tag::Param, o);
        ParamData* pd = param_data(pv);
        pd->name = name.id;
        pd->defaultVal = dr.get();
        Slot pvS = s.push(pv);
        RET(ns_define(vm, name.id, pvS.get(), false, docS.get()));
      }

      if (h == S.withParams_) {
        if (!pairp(args)) RET(raise_error(vm, "with-params: bad form"));
        Scope s(vm);
        u32 pbase = vm.paramBindings.len;
        Slot bS = s.push();  // bindings cursor
        { bS.set(strip_array_literal_head(car_(ARGS), S.array_)); }
        Value r = nil_v();
        while (pairp(bS.get())) {
          Value pair = car_(bS.get());
          if (!pairp(pair) || !pairp(cdr_(pair))) {
            r = raise_error(vm, "with-params: bad binding");
            goto wp_done;
          }
          r = eval_in(vm, car_(pair), env);
          if (r.tag == Tag::Unwind) goto wp_done;
          if (r.tag != Tag::Param) {
            r = raise_error(vm, "with-params: not a param");
            goto wp_done;
          }
          env = vm.stack[rootBase + 1];
          {
            Slot pi = s.push(r);
            pair = car_(bS.get());                         // re-read after the eval
            Value v = eval_in(vm, car_(cdr_(pair)), env);  // sees earlier bindings
            if (v.tag == Tag::Unwind) {
              r = v;
              goto wp_done;
            }
            env = vm.stack[rootBase + 1];
            Slot vi = s.push(v);
            vm.paramBindings.push({pi.get(), vi.get()});
          }
          bS.set(cdr_(bS.get()));
        }
        r = eval_body(vm, cdr_(ARGS), env);
      wp_done:
        vm.paramBindings.len = pbase;  // removed on every exit path
        RET(r);
      }
    }

    // ---- application (3.2 step 2 / 3.3) ----
    // Evaluating the head or any argument can collect, moving the call
    // form's own pairs — so the unevaluated-args cursor lives in a rooted
    // slot (below abase, keeping args contiguous at abase+1 for apply).
    Scope s(vm);
    Slot cursor = s.push(args);
    u32 abase = vm.stack.len;
    {
      Value hv = eval_in(vm, head, env);
      if (hv.tag == Tag::Unwind) RET(hv);
      vm.push(hv);  // raw pushes keep callee+args contiguous at abase
    }
    u32 argc = 0;
    while (pairp(cursor.get())) {
      Value av = eval_in(vm, car_(cursor.get()), vm.stack[rootBase + 1]);
      if (av.tag == Tag::Unwind) RET(av);
      vm.push(av);
      cursor.set(cdr_(cursor.get()));
      argc++;
    }
    env = vm.stack[rootBase + 1];
    if (cursor.get().tag != Tag::Null) RET(raise_error(vm, "dotted call form"));
    Value callee = vm.stack[abase];
    if (callee.tag == Tag::Function && !fn_data(callee)->native &&
        fn_data(callee)->code.tag != Tag::Code) {
      // tail-call: rebind (form, env) and continue — constant stack
      Value env2 = bind_params_stack(vm, callee, abase + 1, argc);
      if (env2.tag == Tag::Unwind) RET(env2);
      callee = vm.stack[abase];  // re-read: binding may have collected
      FunctionData* fd = fn_data(callee);
      if (!enteredClosure) {
        restoreNs = vm.currentNs;
        enteredClosure = true;
      }
      vm.currentNs = fd->nsName.id;
      if (!pairp(fd->body)) RET(nil_v());
      env = env2;
      vm.stack[rootBase + 1] = env;
      cursor.set(fd->body);  // reuse the rooted slot for the body walk
      while (pairp(cdr_(cursor.get()))) {
        Value r = eval_in(vm, car_(cursor.get()), env);
        if (r.tag == Tag::Unwind) RET(r);
        env = vm.stack[rootBase + 1];  // re-read after possible collection
        cursor.set(cdr_(cursor.get()));
      }
      form = car_(cursor.get());
      continue;
    }
    if (callee.tag == Tag::Macro) RET(raise_error(vm, "macro used as function"));
    RET(apply(vm, callee, abase + 1, argc));
  }
#undef RET
#undef EVAL_OR_RET
}

Value eval_in(State& vm, Value form, Value env) {
  if (vm.depth >= vm.cfg.maxDepth) return raise_overflow(vm, "recursion depth exceeded");
  if (vm.stack.len + 16 >= vm.cfg.stackSlots) return raise_overflow(vm, "value stack overflow");
  vm.depth++;
  u32 savedNs = vm.currentNs;
  Value r = eval_tr(vm, form, env, false);
  vm.currentNs = savedNs;  // ns switches never leak out of nested evaluation
  vm.depth--;
  return r;
}

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
  return eval_tr(vm, form, null_v(), true);
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
