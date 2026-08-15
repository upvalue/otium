// eval.cpp — stage-0 evaluator: trampoline with proper tail calls, special
// forms, application, conditions/restarts (spec 8), dynamic params (spec 9).
#include "eval.hpp"
#include "ns.hpp"
#include "reader.hpp"

namespace ot {

// ---------------------------------------------------------------- helpers

static Value car_(Value v) { return as_pair(v)->car; }
static Value cdr_(Value v) { return as_pair(v)->cdr; }
static bool pairp(Value v) { return v.tag == Tag::Pair; }
static bool sym_is(Value v, u32 id) { return v.tag == Tag::Symbol && v.id == id; }

static Value quit_condition(Vm& vm) {
  u32 r = vm.stack.len;
  Value c = make_table(vm);
  vm.push(c);
  table_put(vm, c, keyword_v(vm.syms.kwType), symbol_v(vm.syms.quit_));
  vm.popTo(r);
  return c;
}

static Value start_quit(Vm& vm) {
  vm.unwindCondition = quit_condition(vm);
  vm.unwindKind = UnwindKind::Quit;
  return unwind_v();
}

// Overflow errors unwind directly (no handler walk) so that reporting the
// overflow cannot itself overflow.
static Value raise_overflow(Vm& vm, const char* msg) {
  u32 r = vm.stack.len;
  Value c = make_table(vm);
  vm.push(c);
  table_put(vm, c, keyword_v(vm.syms.kwType), symbol_v(vm.syms.error_));
  table_put(vm, c, keyword_v(vm.syms.kwMessage), make_string(vm, msg, (u32)strlen(msg)));
  vm.popTo(r);
  vm.unwindKind = UnwindKind::Condition;
  vm.unwindCondition = c;
  return unwind_v();
}

// one-element array "box" for a lexical binding
static Value make_box(Vm& vm, Value v) {
  u32 r = vm.push(v);
  Value b = make_array(vm, 1);
  vm.push(b);
  array_push(vm, b, vm.stack[r]);
  vm.popTo(r);
  return b;
}
static Value box_get(Value b) { return as_array(b)->items[0]; }
static void box_set(Value b, Value v) { as_array(b)->items[0] = v; }

static Value env_lookup_box(Vm& vm, Value env, Value sym) {
  while (pairp(env)) {
    Value b = table_get(vm, car_(env), sym);
    if (!is_nil(b)) return b;
    env = cdr_(env);
  }
  return nil_v();
}

static void frame_bind(Vm& vm, Value frame, Value sym, Value v) {
  u32 r = vm.push(frame);
  Value box = make_box(vm, v);
  table_put(vm, vm.stack[r], sym, box);
  vm.popTo(r);
}

static Value list_from_stack(Vm& vm, u32 base, u32 n) {
  Value lst = null_v();
  u32 r = vm.push(lst);
  for (u32 j = n; j-- > 0;) {
    lst = make_pair(vm, vm.stack[base + j], vm.stack[r]);
    vm.stack[r] = lst;
  }
  lst = vm.stack[r];
  vm.popTo(r);
  return lst;
}

static Value lookup_symbol(Vm& vm, Value sym, Value env) {
  if (!sym_qualified(vm, sym.id)) {
    Value box = env_lookup_box(vm, env, sym);
    if (!is_nil(box)) return box_get(box);
  }
  return ns_resolve(vm, sym);
}

static Value make_closure(Vm& vm, u32 name, Value params, Value body, Value env, bool macro) {
  Value doc = nil_v();
  if (pairp(body) && car_(body).tag == Tag::String && pairp(cdr_(body))) {
    doc = car_(body);
    body = cdr_(body);
  }
  u32 r = vm.stack.len;
  vm.push(params);
  vm.push(body);
  vm.push(env);
  vm.push(doc);
  Obj* o = vm.heap.alloc(macro ? ObjType::Macro : ObjType::Function, sizeof(FunctionData));
  Value fv = obj_v(macro ? Tag::Macro : Tag::Function, o);
  FunctionData* fd = fn_data(fv);
  fd->name = name;
  fd->params = vm.stack[r];
  fd->body = vm.stack[r + 1];
  fd->env = vm.stack[r + 2];
  fd->nsName = symbol_v(vm.currentNs);
  fd->native = nullptr;
  fd->docstring = vm.stack[r + 3];
  vm.popTo(r);
  return fv;
}

Value make_native(Vm& vm, const char* name, NativeFn fn) {
  Obj* o = vm.heap.alloc(ObjType::Function, sizeof(FunctionData));
  Value fv = obj_v(Tag::Function, o);
  FunctionData* fd = fn_data(fv);
  fd->name = vm.intern.intern(name, (u32)strlen(name));
  fd->params = nil_v();
  fd->body = nil_v();
  fd->env = nil_v();
  fd->nsName = symbol_v(vm.syms.otiumCore_);
  fd->native = fn;
  fd->docstring = nil_v();
  return fv;
}

// Bind a parameter-list form against a list of argument values into `frame`.
// Handles fixed params, `. rest` (improper tail), `& rest`, bare symbol, and
// the [a b] array-literal spelling (leading `array` symbol skipped).
static Value bind_param_list(Vm& vm, Value frame, Value ps, Value argsList) {
  // frame_bind allocates, so the ps/args cursors live in rooted slots and
  // every read goes through them (GC rewrites the slots, not our locals).
  u32 r = vm.push(frame);
  u32 pS = vm.push(ps);
  u32 aS = vm.push(argsList);
  if (vm.stack[pS].tag == Tag::Symbol) {
    frame_bind(vm, vm.stack[r], vm.stack[pS], vm.stack[aS]);
    vm.popTo(r);
    return nil_v();
  }
  if (pairp(vm.stack[pS]) && sym_is(car_(vm.stack[pS]), vm.syms.array_))
    vm.stack[pS] = cdr_(vm.stack[pS]);
  while (pairp(vm.stack[pS])) {
    Value p = car_(vm.stack[pS]);  // param names are symbols: immediate, safe
    if (sym_is(p, vm.syms.amp_)) {
      vm.stack[pS] = cdr_(vm.stack[pS]);
      if (!pairp(vm.stack[pS])) {
        vm.popTo(r);
        return raise_error(vm, "malformed rest parameter");
      }
      frame_bind(vm, vm.stack[r], car_(vm.stack[pS]), vm.stack[aS]);
      vm.popTo(r);
      return nil_v();
    }
    if (!pairp(vm.stack[aS])) {
      vm.popTo(r);
      return raise_error(vm, "too few arguments");
    }
    frame_bind(vm, vm.stack[r], p, car_(vm.stack[aS]));
    vm.stack[aS] = cdr_(vm.stack[aS]);
    vm.stack[pS] = cdr_(vm.stack[pS]);
  }
  if (vm.stack[pS].tag == Tag::Symbol) {  // dotted rest
    frame_bind(vm, vm.stack[r], vm.stack[pS], vm.stack[aS]);
    vm.popTo(r);
    return nil_v();
  }
  if (pairp(vm.stack[aS])) {
    vm.popTo(r);
    return raise_error(vm, "too many arguments");
  }
  vm.popTo(r);
  return nil_v();
}

// Build the callee's env from stack args. Returns env or Unwind.
// callee must be a Function/Macro Value; it is rooted here so fd-> reads stay
// valid across the allocations below.
static Value bind_params_stack(Vm& vm, Value callee, u32 base, u32 argc) {
  u32 r = vm.push(callee);
  Value args = list_from_stack(vm, base, argc);
  vm.push(args);
  Value frame = make_table(vm);
  vm.push(frame);
  vm.push(fn_data(vm.stack[r])->env);
  Value err = bind_param_list(vm, vm.stack[r + 2], fn_data(vm.stack[r])->params, vm.stack[r + 1]);
  if (err.tag == Tag::Unwind) {
    vm.popTo(r);
    return err;
  }
  Value env = make_pair(vm, vm.stack[r + 2], vm.stack[r + 3]);
  vm.popTo(r);
  return env;
}

static Value param_read(Vm& vm, Value p) {
  for (u32 i = vm.paramBindings.len; i-- > 0;) {
    if (vm.paramBindings[i].param.obj == p.obj) return vm.paramBindings[i].value;
  }
  return param_data(p)->defaultVal;
}

// ---------------------------------------------------------------- apply

static Value eval_body(Vm& vm, Value body, Value env) {
  // eval_in can collect; keep the cursor and env in rooted slots.
  u32 b = vm.push(body);
  u32 e = vm.push(env);
  Value r = nil_v();
  while (pairp(vm.stack[b])) {
    r = eval_in(vm, car_(vm.stack[b]), vm.stack[e]);
    if (r.tag == Tag::Unwind) break;
    vm.stack[b] = cdr_(vm.stack[b]);
  }
  vm.popTo(b);
  return r;
}

Value apply(Vm& vm, Value callee, u32 base, u32 argc) {
  switch (callee.tag) {
    // Tag::Macro shares FunctionData; direct apply is the expander's
    // privileged call path (call position still rejects macros in eval_tr).
    case Tag::Macro:
    case Tag::Function: {
      if (fn_data(callee)->native) return fn_data(callee)->native(vm, base, argc);
      u32 c = vm.push(callee);  // binding below allocates
      Value env = bind_params_stack(vm, vm.stack[c], base, argc);
      if (env.tag == Tag::Unwind) {
        vm.popTo(c);
        return env;
      }
      u32 savedNs = vm.currentNs;
      FunctionData* fd = fn_data(vm.stack[c]);
      vm.currentNs = fd->nsName.id;
      Value r = eval_body(vm, fd->body, env);
      vm.currentNs = savedNs;
      vm.popTo(c);
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

static Value list_append2(Vm& vm, Value a, Value b) {  // copies a
  if (!pairp(a)) return b;
  u32 r = vm.push(a);
  vm.push(b);
  // count then build backwards
  u32 n = 0;
  for (Value c = a; pairp(c); c = cdr_(c)) n++;
  Value out = vm.stack[r + 1];
  u32 o = vm.push(out);
  for (u32 i = n; i-- > 0;) {
    Value c = vm.stack[r];
    for (u32 j = 0; j < i; j++) c = cdr_(c);
    out = make_pair(vm, car_(c), vm.stack[o]);
    vm.stack[o] = out;
  }
  out = vm.stack[o];
  vm.popTo(r);
  return out;
}

static Value qq(Vm& vm, Value t, int depth, Value env) {
  if (!pairp(t)) return t;
  Value h = car_(t);
  if (sym_is(h, vm.syms.unquote_)) {
    if (depth == 1) return eval_in(vm, car_(cdr_(t)), env);
    Value inner = qq(vm, car_(cdr_(t)), depth - 1, env);
    OT_TRY(inner);
    u32 r = vm.push(inner);
    Value l = make_pair(vm, vm.stack[r], null_v());
    vm.stack[r] = l;
    l = make_pair(vm, symbol_v(vm.syms.unquote_), vm.stack[r]);
    vm.popTo(r);
    return l;
  }
  if (sym_is(h, vm.syms.quasiquote_)) {
    Value inner = qq(vm, car_(cdr_(t)), depth + 1, env);
    OT_TRY(inner);
    u32 r = vm.push(inner);
    Value l = make_pair(vm, vm.stack[r], null_v());
    vm.stack[r] = l;
    l = make_pair(vm, symbol_v(vm.syms.quasiquote_), vm.stack[r]);
    vm.popTo(r);
    return l;
  }
  // element position: root t and env, since recursing/evaluating allocates
  u32 tS = vm.push(t);
  u32 eS = vm.push(env);
  if (pairp(h) && sym_is(car_(h), vm.syms.unquoteSplicing_) && depth == 1) {
    Value lst = eval_in(vm, car_(cdr_(h)), env);
    if (lst.tag == Tag::Unwind) {
      vm.popTo(tS);
      return lst;
    }
    if (!pairp(lst) && lst.tag != Tag::Null) {
      vm.popTo(tS);
      return raise_error(vm, "unquote-splicing: not a list");
    }
    u32 r = vm.push(lst);
    Value rest = qq(vm, cdr_(vm.stack[tS]), depth, vm.stack[eS]);
    if (rest.tag == Tag::Unwind) {
      vm.popTo(tS);
      return rest;
    }
    Value out = list_append2(vm, vm.stack[r], rest);
    vm.popTo(tS);
    return out;
  }
  Value eh = qq(vm, h, depth, env);
  if (eh.tag == Tag::Unwind) {
    vm.popTo(tS);
    return eh;
  }
  u32 r = vm.push(eh);
  Value et = qq(vm, cdr_(vm.stack[tS]), depth, vm.stack[eS]);
  if (et.tag == Tag::Unwind) {
    vm.popTo(tS);
    return et;
  }
  Value out = make_pair(vm, vm.stack[r], et);
  vm.popTo(tS);
  return out;
}

// ---------------------------------------------------------------- require

static u32 name_id_of(Vm& vm, Value v) {  // symbol/keyword id; string interned; 0 if none
  if (v.tag == Tag::Symbol || v.tag == Tag::Keyword) return v.id;
  if (v.tag == Tag::String) {
    StringData* s = as_string(v);
    return vm.intern.intern((const char*)(s + 1), s->len);
  }
  return 0;
}

static Value unwrap_quote(Vm& vm, Value v) {
  if (pairp(v) && sym_is(car_(v), vm.syms.quote_) && pairp(cdr_(v))) return car_(cdr_(v));
  return v;
}

static Value require_load(Vm& vm, u32 nsName) {
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
  Reader rd(vm, src.data, src.len, cname);
  Value r = nil_v();
  while (!rd.atEof()) {
    Value f = rd.next();
    if (f.tag == Tag::Unwind) {
      r = f;
      break;
    }
    if (rd.atEof()) break;
    r = eval_form(vm, f);
    if (r.tag == Tag::Unwind) break;
  }
  vm.currentNs = savedNs;
  vm.loadingNs.pop();
  if (r.tag == Tag::Unwind) return r;
  return nil_v();
}

static Value require_spec(Vm& vm, Value spec) {
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
  u32 optS = vm.push(opts);
  {
    Value r = require_load(vm, target);
    if (r.tag == Tag::Unwind) {
      vm.popTo(optS);
      return r;
    }
  }
  Value cur = ns_get_or_create(vm, vm.currentNs);
  u32 curRoot = vm.push(cur);
  while (pairp(vm.stack[optS])) {
    Value opt = car_(vm.stack[optS]);
    if (opt.tag == Tag::Keyword && opt.id == vm.syms.kwAs) {
      vm.stack[optS] = cdr_(vm.stack[optS]);
      if (!pairp(vm.stack[optS])) {
        vm.popTo(optS);
        return raise_error(vm, "require: :as needs a name");
      }
      Value alias = unwrap_quote(vm, car_(vm.stack[optS]));
      table_put(vm, ns_field(vm, vm.stack[curRoot], vm.syms.kwAliases), alias, symbol_v(target));
    } else if (opt.tag == Tag::Keyword && opt.id == vm.syms.kwRefer) {
      vm.stack[optS] = cdr_(vm.stack[optS]);
      if (!pairp(vm.stack[optS])) {
        vm.popTo(optS);
        return raise_error(vm, "require: :refer needs a list");
      }
      Value names = unwrap_quote(vm, car_(vm.stack[optS]));
      if (pairp(names) && sym_is(car_(names), vm.syms.array_)) names = cdr_(names);
      Value tgt = ns_lookup(vm, target);
      // table_put/table_get never touch the GC heap, so this walk is safe
      for (Value n = names; pairp(n); n = cdr_(n)) {
        Value sym = car_(n);
        Value var = table_get(vm, ns_field(vm, tgt, vm.syms.kwVars), sym);
        if (is_nil(var) || var_private(var)) {
          vm.popTo(optS);
          u32 l;
          const char* s = vm.intern.name(sym.id, &l);
          return raise_error(vm, "cannot refer %.*s", (int)l, s);
        }
        table_put(vm, ns_field(vm, vm.stack[curRoot], vm.syms.kwRefers), sym, var);
        tgt = ns_lookup(vm, target);
      }
    }  // :reload and unknown options tolerated / ignored in stage 0
    vm.stack[optS] = cdr_(vm.stack[optS]);
  }
  vm.popTo(optS);
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
static Value eval_tr(Vm& vm, Value form, Value env, bool topLevel) {
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
          hasDoc = pairp(body) && car_(body).tag == Tag::String && pairp(cdr_(body));
          if (hasDoc) body = cdr_(body);
          valueV = make_closure(vm, name.id, cdr_(target), body, env, false);
          env = vm.stack[rootBase + 1];  // make_closure may have collected
        } else {
          name = target;
          Value rest = cdr_(args);
          hasDoc = pairp(rest) && car_(rest).tag == Tag::String && pairp(cdr_(rest));
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
          u32 r = vm.push(valueV);
          vm.push(doc);
          Value res = ns_define(vm, name.id, vm.stack[r], priv, vm.stack[r + 1]);
          vm.popTo(r);
          RET(res);
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
          u32 l;
          const char* s = vm.intern.name(name.id, &l);
          RET(raise_error(vm, "set!: unbound %.*s", (int)l, s));
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
        Value m = make_closure(vm, name.id, car_(cdr_(args)), cdr_(cdr_(args)), env, true);
        u32 r = vm.push(m);
        Value res = ns_define(vm, name.id, vm.stack[r], false, nil_v());
        vm.popTo(r);
        RET(res);
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
        u32 er = vm.stack.len;
        vm.push(make_table(vm));
        Value env2 = make_pair(vm, vm.stack[er], vm.stack[rootBase + 1]);
        vm.stack[er] = env2;
        u32 bS = vm.push(nil_v());  // bindings cursor
        {
          Value b = car_(ARGS);
          if (pairp(b) && sym_is(car_(b), S.array_)) b = cdr_(b);
          vm.stack[bS] = b;
        }
        while (pairp(vm.stack[bS])) {
          Value pair = car_(vm.stack[bS]);
          if (!pairp(pair) || !pairp(cdr_(pair))) {
            vm.popTo(er);
            RET(raise_error(vm, "let: bad binding"));
          }
          Value bv = eval_in(vm, car_(cdr_(pair)), vm.stack[er]);  // sequential
          if (bv.tag == Tag::Unwind) {
            vm.popTo(er);
            RET(bv);
          }
          pair = car_(vm.stack[bS]);  // re-read: the eval may have collected
          frame_bind(vm, car_(vm.stack[er]), car_(pair), bv);
          vm.stack[bS] = cdr_(vm.stack[bS]);
        }
        vm.stack[bS] = cdr_(ARGS);  // reuse the cursor for the body walk
        if (!pairp(vm.stack[bS])) {
          vm.popTo(er);
          RET(nil_v());
        }
        vm.stack[rootBase + 1] = vm.stack[er];  // keep env2 rooted past the pop
        while (pairp(cdr_(vm.stack[bS]))) {
          Value r = eval_in(vm, car_(vm.stack[bS]), vm.stack[rootBase + 1]);
          if (r.tag == Tag::Unwind) {
            vm.popTo(er);
            RET(r);
          }
          vm.stack[bS] = cdr_(vm.stack[bS]);
        }
        form = car_(vm.stack[bS]);
        env = vm.stack[rootBase + 1];
        vm.popTo(er);
        continue;
      }

      if (h == S.while_) {
        u32 bS = vm.push(nil_v());  // body cursor
        for (;;) {
          if (vm.interruptFlag) {
            vm.interruptFlag = false;
            RET(start_quit(vm));
          }
          if (!pairp(ARGS)) RET(raise_error(vm, "while: bad form"));
          EVAL_OR_RET(t, car_(ARGS));
          if (is_falsy(t)) RET(nil_v());
          vm.stack[bS] = cdr_(ARGS);
          while (pairp(vm.stack[bS])) {
            EVAL_OR_RET(r, car_(vm.stack[bS]));
            (void)r;
            vm.stack[bS] = cdr_(vm.stack[bS]);
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
        u32 bS = vm.push(nil_v());  // clause body cursor
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
            vm.stack[bS] = cdr_(car_(ARGS));
            if (!pairp(vm.stack[bS])) RET(t);  // one-element clause
            while (pairp(cdr_(vm.stack[bS]))) {
              EVAL_OR_RET(r, car_(vm.stack[bS]));
              (void)r;
              vm.stack[bS] = cdr_(vm.stack[bS]);
            }
            form = car_(vm.stack[bS]);
            chosen = true;
            break;
          }
          ARGS = cdr_(ARGS);
        }
        vm.popTo(bS);
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
        ARGS = cdr_(args);           // clause cursor (require_spec allocates)
        u32 spS = vm.push(nil_v());  // spec cursor
        while (pairp(ARGS)) {
          Value clause = car_(ARGS);
          if (pairp(clause) && clause.tag == Tag::Pair && car_(clause).tag == Tag::Keyword &&
              car_(clause).id == S.kwRequire) {
            vm.stack[spS] = cdr_(clause);
            while (pairp(vm.stack[spS])) {
              Value r = require_spec(vm, car_(vm.stack[spS]));
              if (r.tag == Tag::Unwind) RET(r);
              vm.stack[spS] = cdr_(vm.stack[spS]);
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
        u32 sbase = vm.stack.len;
        u32 hbase = vm.handlers.len;
        u32 cS = vm.push(nil_v());  // clause cursor
        {
          Value clauses = car_(ARGS);
          if (pairp(clauses) && sym_is(car_(clauses), S.array_)) clauses = cdr_(clauses);
          vm.stack[cS] = clauses;
        }
        while (pairp(vm.stack[cS])) {
          Value cl = car_(vm.stack[cS]);
          if (!pairp(cl) || !pairp(cdr_(cl))) {
            vm.popTo(sbase);
            RET(raise_error(vm, "handler-bind: bad binding"));
          }
          Value pr = eval_in(vm, car_(cl), env);
          if (pr.tag == Tag::Unwind) {
            vm.handlers.len = hbase;
            vm.popTo(sbase);
            RET(pr);
          }
          env = vm.stack[rootBase + 1];
          u32 pi = vm.push(pr);
          cl = car_(vm.stack[cS]);  // re-read after the eval
          Value hd = eval_in(vm, car_(cdr_(cl)), env);
          if (hd.tag == Tag::Unwind) {
            vm.handlers.len = hbase;
            vm.popTo(sbase);
            RET(hd);
          }
          env = vm.stack[rootBase + 1];
          u32 hi = vm.push(hd);
          vm.handlers.push({vm.stack[pi], vm.stack[hi]});
          vm.stack[cS] = cdr_(vm.stack[cS]);
        }
        Value r = eval_body(vm, cdr_(ARGS), env);
        vm.handlers.len = hbase;
        vm.popTo(sbase);
        RET(r);
      }

      if (h == S.restartCase_) {
        if (!pairp(args)) RET(raise_error(vm, "restart-case: bad form"));
        u32 sbase = vm.stack.len;
        u32 rbase = vm.restarts.len;
        u64 firstId = vm.restartIdCounter + 1;
        u32 count = 0;
        u32 cS = vm.push(cdr_(ARGS));  // clause cursor (the alloc below moves pairs)
        while (pairp(vm.stack[cS])) {
          Value cl = car_(vm.stack[cS]);
          if (!pairp(cl) || car_(cl).tag != Tag::Symbol) {
            vm.restarts.len = rbase;
            vm.popTo(sbase);
            RET(raise_error(vm, "restart-case: bad clause"));
          }
          Value desc = nil_v();
          Value rest2 = cdr_(cl);
          if (pairp(rest2) && car_(rest2).tag == Tag::String && pairp(cdr_(rest2)))
            desc = car_(rest2);
          u32 dr = vm.push(desc);
          Obj* o = vm.heap.alloc(ObjType::Restart, sizeof(RestartData));
          Value rv = obj_v(Tag::Restart, o);
          RestartData* rd = restart_data(rv);
          rd->name = car_(car_(vm.stack[cS])).id;  // re-read: the alloc collected
          rd->description = vm.stack[dr];
          rd->restartId = ++vm.restartIdCounter;
          vm.stack[dr] = rv;  // keep rooted
          vm.restarts.push({rv});
          count++;
          vm.stack[cS] = cdr_(vm.stack[cS]);
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
          if (pairp(rest2) && car_(rest2).tag == Tag::String && pairp(cdr_(rest2)))
            rest2 = cdr_(rest2);  // skip description
          vm.stack[cS] = rest2;   // keep the clause tail rooted across binding
          Value clArgs = vm.unwindRestartArgs;
          vm.unwindKind = UnwindKind::None;
          vm.unwindCondition = nil_v();
          vm.unwindRestartArgs = nil_v();
          u32 ar = vm.push(clArgs);
          Value frame = make_table(vm);
          u32 fr = vm.push(frame);
          Value be = bind_param_list(vm, vm.stack[fr], car_(vm.stack[cS]), vm.stack[ar]);
          if (be.tag == Tag::Unwind) {
            vm.popTo(sbase);
            RET(be);
          }
          env = vm.stack[rootBase + 1];
          Value env2 = make_pair(vm, vm.stack[fr], env);
          vm.push(env2);
          r = eval_body(vm, cdr_(vm.stack[cS]), env2);
        }
        vm.popTo(sbase);
        RET(r);
      }

      if (h == S.try_) {
        u32 sbase = vm.stack.len;
        u32 bS = vm.push(args);  // body cursor
        u32 catchS = vm.push(null_v());
        {
          Value b = args;  // split body / trailing catch clauses
          while (pairp(b) && !(pairp(car_(b)) && sym_is(car_(car_(b)), S.catch_))) b = cdr_(b);
          vm.stack[catchS] = b;
        }
        Value r = nil_v();
        while (pairp(vm.stack[bS]) && !val_eq(vm.stack[bS], vm.stack[catchS])) {
          r = eval_in(vm, car_(vm.stack[bS]), env);
          if (r.tag == Tag::Unwind) break;
          env = vm.stack[rootBase + 1];
          vm.stack[bS] = cdr_(vm.stack[bS]);
        }
        if (r.tag == Tag::Unwind && vm.unwindKind == UnwindKind::Condition &&
            pairp(vm.stack[catchS])) {
          u32 cr = vm.push(vm.unwindCondition);
          vm.unwindKind = UnwindKind::None;
          vm.stack[bS] = vm.stack[catchS];  // reuse as the catch-clause cursor
          while (pairp(vm.stack[bS])) {
            Value cl = car_(vm.stack[bS]);  // (catch (pred var) forms...)
            if (!pairp(cdr_(cl)) || !pairp(car_(cdr_(cl)))) {
              vm.popTo(sbase);
              RET(raise_error(vm, "try: bad catch clause"));
            }
            Value pf = eval_in(vm, car_(car_(cdr_(cl))), env);
            if (pf.tag == Tag::Unwind) {
              vm.popTo(sbase);
              RET(pf);
            }
            env = vm.stack[rootBase + 1];
            u32 pr = vm.push(pf);
            u32 ab = vm.push(vm.stack[cr]);
            Value t = apply(vm, vm.stack[pr], ab, 1);
            vm.popTo(pr);
            if (t.tag == Tag::Unwind) {
              vm.popTo(sbase);
              RET(t);
            }
            env = vm.stack[rootBase + 1];
            if (is_truthy(t)) {
              Value frame = make_table(vm);
              u32 fr = vm.push(frame);
              cl = car_(vm.stack[bS]);  // re-read after the allocations above
              frame_bind(vm, vm.stack[fr], car_(cdr_(car_(cdr_(cl)))), vm.stack[cr]);
              env = vm.stack[rootBase + 1];
              Value env2 = make_pair(vm, vm.stack[fr], env);
              vm.push(env2);
              Value out = eval_body(vm, cdr_(cdr_(car_(vm.stack[bS]))), env2);
              vm.popTo(sbase);
              RET(out);
            }
            vm.stack[bS] = cdr_(vm.stack[bS]);
          }
          // no clause matched: keep unwinding
          vm.unwindKind = UnwindKind::Condition;
          vm.unwindCondition = vm.stack[cr];
        }
        vm.popTo(sbase);
        RET(r);
      }

      if (h == S.unwindProtect_ || h == S.defer_) {
        if (!pairp(args)) RET(raise_error(vm, "unwind-protect: bad form"));
        u32 sbase = vm.stack.len;
        Value r = eval_in(vm, car_(args), env);
        env = vm.stack[rootBase + 1];
        UnwindKind k = vm.unwindKind;
        u32 condR = vm.push(vm.unwindCondition);
        u32 argsR = vm.push(vm.unwindRestartArgs);
        u64 rid = vm.unwindRestartId;
        if (r.tag == Tag::Unwind) vm.unwindKind = UnwindKind::None;
        u32 cS = vm.push(cdr_(ARGS));  // cleanup cursor
        while (pairp(vm.stack[cS])) {
          Value cr = eval_in(vm, car_(vm.stack[cS]), env);
          if (cr.tag == Tag::Unwind) {  // cleanup's unwind replaces in-flight state
            r = cr;
            k = vm.unwindKind;
            vm.stack[condR] = vm.unwindCondition;
            vm.stack[argsR] = vm.unwindRestartArgs;
            rid = vm.unwindRestartId;
            vm.unwindKind = UnwindKind::None;
          }
          env = vm.stack[rootBase + 1];
          vm.stack[cS] = cdr_(vm.stack[cS]);
        }
        if (r.tag == Tag::Unwind) {
          vm.unwindKind = k;
          vm.unwindCondition = vm.stack[condR];
          vm.unwindRestartArgs = vm.stack[argsR];
          vm.unwindRestartId = rid;
        }
        vm.popTo(sbase);
        RET(r);
      }

      if (h == S.defparam_) {
        if (pairp(env)) RET(raise_error(vm, "defparam: only allowed at top level"));
        if (!pairp(args) || car_(args).tag != Tag::Symbol)
          RET(raise_error(vm, "defparam: bad form"));
        Value name = car_(args);
        Value rest = cdr_(args);
        bool hasDoc = pairp(rest) && car_(rest).tag == Tag::String && pairp(cdr_(rest));
        if (hasDoc) rest = cdr_(rest);
        if (!pairp(rest)) RET(raise_error(vm, "defparam: missing default"));
        EVAL_OR_RET(d, car_(rest));
        Value doc = hasDoc ? car_(cdr_(ARGS)) : nil_v();  // re-read post-eval
        u32 dr = vm.push(d);
        vm.push(doc);
        Obj* o = vm.heap.alloc(ObjType::Param, sizeof(ParamData));
        Value pv = obj_v(Tag::Param, o);
        ParamData* pd = param_data(pv);
        pd->name = name.id;
        pd->defaultVal = vm.stack[dr];
        vm.push(pv);
        Value res = ns_define(vm, name.id, pv, false, vm.stack[dr + 1]);
        vm.popTo(dr);
        RET(res);
      }

      if (h == S.withParams_) {
        if (!pairp(args)) RET(raise_error(vm, "with-params: bad form"));
        u32 sbase = vm.stack.len;
        u32 pbase = vm.paramBindings.len;
        u32 bS = vm.push(nil_v());  // bindings cursor
        {
          Value b = car_(ARGS);
          if (pairp(b) && sym_is(car_(b), S.array_)) b = cdr_(b);
          vm.stack[bS] = b;
        }
        Value r = nil_v();
        while (pairp(vm.stack[bS])) {
          Value pair = car_(vm.stack[bS]);
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
            u32 pi = vm.push(r);
            pair = car_(vm.stack[bS]);                     // re-read after the eval
            Value v = eval_in(vm, car_(cdr_(pair)), env);  // sees earlier bindings
            if (v.tag == Tag::Unwind) {
              r = v;
              goto wp_done;
            }
            env = vm.stack[rootBase + 1];
            u32 vi = vm.push(v);
            vm.paramBindings.push({vm.stack[pi], vm.stack[vi]});
          }
          vm.stack[bS] = cdr_(vm.stack[bS]);
        }
        r = eval_body(vm, cdr_(ARGS), env);
      wp_done:
        vm.paramBindings.len = pbase;  // removed on every exit path
        vm.popTo(sbase);
        RET(r);
      }
    }

    // ---- application (3.2 step 2 / 3.3) ----
    // Evaluating the head or any argument can collect, moving the call
    // form's own pairs — so the unevaluated-args cursor lives in a rooted
    // slot (below abase, keeping args contiguous at abase+1 for apply).
    u32 cursor = vm.push(args);
    u32 abase = vm.stack.len;
    {
      Value hv = eval_in(vm, head, env);
      if (hv.tag == Tag::Unwind) {
        vm.popTo(cursor);
        RET(hv);
      }
      vm.push(hv);
    }
    u32 argc = 0;
    while (pairp(vm.stack[cursor])) {
      Value av = eval_in(vm, car_(vm.stack[cursor]), vm.stack[rootBase + 1]);
      if (av.tag == Tag::Unwind) {
        vm.popTo(cursor);
        RET(av);
      }
      vm.push(av);
      vm.stack[cursor] = cdr_(vm.stack[cursor]);
      argc++;
    }
    env = vm.stack[rootBase + 1];
    if (vm.stack[cursor].tag != Tag::Null) {
      vm.popTo(cursor);
      RET(raise_error(vm, "dotted call form"));
    }
    Value callee = vm.stack[abase];
    if (callee.tag == Tag::Function && !fn_data(callee)->native) {
      // tail-call: rebind (form, env) and continue — constant stack
      Value env2 = bind_params_stack(vm, callee, abase + 1, argc);
      if (env2.tag == Tag::Unwind) {
        vm.popTo(cursor);
        RET(env2);
      }
      callee = vm.stack[abase];  // re-read: binding may have collected
      FunctionData* fd = fn_data(callee);
      if (!enteredClosure) {
        restoreNs = vm.currentNs;
        enteredClosure = true;
      }
      vm.currentNs = fd->nsName.id;
      if (!pairp(fd->body)) {
        vm.popTo(cursor);
        RET(nil_v());
      }
      env = env2;
      vm.stack[rootBase + 1] = env;
      vm.stack[cursor] = fd->body;  // reuse the rooted slot for the body walk
      while (pairp(cdr_(vm.stack[cursor]))) {
        Value r = eval_in(vm, car_(vm.stack[cursor]), env);
        if (r.tag == Tag::Unwind) {
          vm.popTo(cursor);
          RET(r);
        }
        env = vm.stack[rootBase + 1];  // re-read after possible collection
        vm.stack[cursor] = cdr_(vm.stack[cursor]);
      }
      form = car_(vm.stack[cursor]);
      vm.popTo(cursor);
      continue;
    }
    if (callee.tag == Tag::Macro) {
      vm.popTo(cursor);
      RET(raise_error(vm, "macro used as function"));
    }
    {
      Value r = apply(vm, callee, abase + 1, argc);
      vm.popTo(cursor);
      RET(r);
    }
  }
#undef RET
#undef EVAL_OR_RET
}

Value eval_in(Vm& vm, Value form, Value env) {
  if (vm.depth >= vm.cfg.maxDepth) return raise_overflow(vm, "recursion depth exceeded");
  if (vm.stack.len + 16 >= vm.cfg.stackSlots) return raise_overflow(vm, "value stack overflow");
  vm.depth++;
  u32 savedNs = vm.currentNs;
  Value r = eval_tr(vm, form, env, false);
  vm.currentNs = savedNs;  // ns switches never leak out of nested evaluation
  vm.depth--;
  return r;
}

Value eval_form(Vm& vm, Value form) {
  // Route through the expansion hook *expander* in otium.core.
  Value core = ns_lookup(vm, vm.syms.otiumCore_);
  if (!is_nil(core)) {
    Value var = table_get(vm, ns_field(vm, core, vm.syms.kwVars), symbol_v(vm.syms.expander_));
    if (!is_nil(var) && var_value(var).tag == Tag::Function) {
      u32 b = vm.push(form);
      u32 savedExpandNs = vm.expandNs;
      vm.expandNs = vm.currentNs;
      Value expanded = apply(vm, var_value(var), b, 1);
      vm.expandNs = savedExpandNs;
      vm.popTo(b);
      if (expanded.tag == Tag::Unwind) return expanded;
      form = expanded;
    }
  }
  return eval_tr(vm, form, null_v(), true);
}

// ---------------------------------------------------------------- natives

static Value build_condition(Vm& vm, u32 base, u32 argc) {
  if (argc == 0) return raise_error(vm, "signal: needs an argument");
  Value first = vm.stack[base];
  if (first.tag != Tag::String) {
    if (argc > 1) return raise_error(vm, "extra arguments after a non-string condition");
    return first;
  }
  u32 r = vm.stack.len;
  Value c = make_table(vm);
  vm.push(c);
  table_put(vm, c, keyword_v(vm.syms.kwType), symbol_v(vm.syms.error_));
  table_put(vm, vm.stack[r], keyword_v(vm.syms.kwMessage), vm.stack[base]);
  if (argc > 1) {
    Value data = make_array(vm, argc - 1);
    vm.push(data);
    for (u32 i = 1; i < argc; i++) array_push(vm, data, vm.stack[base + i]);
    table_put(vm, vm.stack[r], keyword_v(vm.syms.kwData), data);
  }
  c = vm.stack[r];
  vm.popTo(r);
  return c;
}

static Value nat_signal(Vm& vm, u32 base, u32 argc) {
  Value c = build_condition(vm, base, argc);
  OT_TRY(c);
  return signal_value(vm, c, false);
}

static Value nat_error(Vm& vm, u32 base, u32 argc) {
  Value c = build_condition(vm, base, argc);
  OT_TRY(c);
  return signal_value(vm, c, true);
}

static Value nat_invoke_restart(Vm& vm, u32 base, u32 argc) {
  if (argc < 1) return raise_error(vm, "invoke-restart: needs a restart");
  Value which = vm.stack[base];
  Value target = nil_v();
  if (which.tag == Tag::Restart) {
    for (u32 i = vm.restarts.len; i-- > 0;)
      if (vm.restarts[i].restart.obj == which.obj) {
        target = which;
        break;
      }
    if (is_nil(target)) return raise_error(vm, "invoke-restart: restart is no longer active");
  } else {
    u32 nid = name_id_of(vm, which);
    if (!nid) return raise_error(vm, "invoke-restart: bad restart name");
    for (u32 i = vm.restarts.len; i-- > 0;) {
      if (restart_data(vm.restarts[i].restart)->name == nid) {
        target = vm.restarts[i].restart;
        break;
      }
    }
    if (is_nil(target)) {
      u32 l;
      const char* s = vm.intern.name(nid, &l);
      return raise_error(vm, "no active restart named %.*s", (int)l, s);
    }
  }
  vm.unwindRestartArgs = list_from_stack(vm, base + 1, argc - 1);
  vm.unwindRestartId = restart_data(target)->restartId;
  vm.unwindCondition = nil_v();
  vm.unwindKind = UnwindKind::Restart;
  return unwind_v();
}

static Value nat_compute_restarts(Vm& vm, u32 base, u32 argc) {
  (void)base;
  (void)argc;
  u32 r = vm.stack.len;
  Value arr = make_array(vm, vm.restarts.len);
  vm.push(arr);
  for (u32 i = vm.restarts.len; i-- > 0;)  // innermost first
    array_push(vm, vm.stack[r], vm.restarts[i].restart);
  arr = vm.stack[r];
  vm.popTo(r);
  return arr;
}

static Value nat_find_restart(Vm& vm, u32 base, u32 argc) {
  if (argc < 1) return raise_error(vm, "find-restart: needs a name");
  u32 nid = name_id_of(vm, vm.stack[base]);
  if (!nid) return raise_error(vm, "find-restart: bad name");
  for (u32 i = vm.restarts.len; i-- > 0;)
    if (restart_data(vm.restarts[i].restart)->name == nid) return vm.restarts[i].restart;
  return nil_v();
}

static Value nat_restart_name(Vm& vm, u32 base, u32 argc) {
  if (argc != 1 || vm.stack[base].tag != Tag::Restart)
    return raise_error(vm, "restart-name: needs a restart");
  return symbol_v(restart_data(vm.stack[base])->name);
}

static Value nat_restart_description(Vm& vm, u32 base, u32 argc) {
  if (argc != 1 || vm.stack[base].tag != Tag::Restart)
    return raise_error(vm, "restart-description: needs a restart");
  return restart_data(vm.stack[base])->description;
}

static Value nat_define_condition(Vm& vm, u32 base, u32 argc) {
  if (argc != 2) return raise_error(vm, "define-condition: type and parent");
  u32 tid = name_id_of(vm, vm.stack[base]);
  if (!tid) return raise_error(vm, "define-condition: bad type");
  Value parent = vm.stack[base + 1];
  if (is_nil(parent)) {
    table_put(vm, vm.typeParents, symbol_v(tid), nil_v());  // delete = root
    return symbol_v(tid);
  }
  u32 pid = name_id_of(vm, parent);
  if (!pid) return raise_error(vm, "define-condition: bad parent");
  // cycle check: would tid become its own ancestor?
  u32 cur = pid;
  for (u32 guard = 0; guard < 1000; guard++) {
    if (cur == tid) return raise_error(vm, "define-condition: cycle");
    Value nxt = table_get(vm, vm.typeParents, symbol_v(cur));
    if (nxt.tag != Tag::Symbol) break;
    cur = nxt.id;
  }
  table_put(vm, vm.typeParents, symbol_v(tid), symbol_v(pid));
  return symbol_v(tid);
}

static Value nat_condition_of_type(Vm& vm, u32 base, u32 argc) {
  if (argc != 2) return raise_error(vm, "condition-of-type?: condition and type");
  Value c = vm.stack[base];
  u32 tid = name_id_of(vm, vm.stack[base + 1]);
  if (c.tag != Tag::Table) return bool_v(false);
  Value ct = table_get(vm, c, keyword_v(vm.syms.kwType));
  if (ct.tag != Tag::Symbol && ct.tag != Tag::Keyword) return bool_v(false);
  u32 cur = ct.id;
  for (u32 guard = 0; guard < 1000; guard++) {
    if (cur == tid) return bool_v(true);
    Value nxt = table_get(vm, vm.typeParents, symbol_v(cur));
    if (nxt.tag != Tag::Symbol) return bool_v(false);
    cur = nxt.id;
  }
  return bool_v(false);
}

static Value nat_gensym(Vm& vm, u32 base, u32 argc) {
  char buf[128];
  const char* prefix = "G";
  u32 plen = 1;
  if (argc >= 1) {
    u32 id = name_id_of(vm, vm.stack[base]);
    if (id) prefix = vm.intern.name(id, &plen);
  }
  int n = snprintf(buf, sizeof buf, "%.*s__%llu", (int)plen, prefix,
                   (unsigned long long)++vm.gensymCounter);
  return symbol_v(vm.intern.intern(buf, (u32)n));
}

// Stage-0 expander: expand macro calls in head position, recursively, and
// recurse into subforms (skipping quote). Identity for everything else.
static Value expand0(Vm& vm, Value form);

static Value expand0_list(Vm& vm, Value l) {
  if (!pairp(l)) return l;
  u32 lS = vm.push(l);  // expand0 allocates; keep the cursor rooted
  Value h = expand0(vm, car_(vm.stack[lS]));
  if (h.tag == Tag::Unwind) {
    vm.popTo(lS);
    return h;
  }
  u32 r = vm.push(h);
  Value t = expand0_list(vm, cdr_(vm.stack[lS]));
  if (t.tag == Tag::Unwind) {
    vm.popTo(lS);
    return t;
  }
  Value out = make_pair(vm, vm.stack[r], t);
  vm.popTo(lS);
  return out;
}

static Value expand0(Vm& vm, Value form) {
  for (u32 guard = 0; guard < 1000; guard++) {
    if (!pairp(form)) return form;
    Value h = car_(form);
    if (h.tag != Tag::Symbol) break;
    if (h.id == vm.syms.quote_) return form;
    Value var = ns_resolve_var(vm, h);
    if (is_nil(var) || var_value(var).tag != Tag::Macro) break;
    // call the macro on the unevaluated argument forms (vm.push doesn't
    // allocate on the GC heap, so walking the form while pushing is safe)
    u32 mslot = vm.push(var_value(var));
    u32 base = vm.stack.len;
    u32 argc = 0;
    for (Value a = cdr_(form); pairp(a); a = cdr_(a)) {
      vm.push(car_(a));
      argc++;
    }
    Value r = apply(vm, vm.stack[mslot], base, argc);
    vm.popTo(mslot);
    OT_TRY(r);
    form = r;  // re-expand the replacement
  }
  // recurse into subforms
  return expand0_list(vm, form);
}

static Value nat_expander(Vm& vm, u32 base, u32 argc) {
  if (argc != 1) return raise_error(vm, "*expander*: one argument");
  return expand0(vm, vm.stack[base]);
}

static Value nat_expander_lexical(Vm& vm, u32 base, u32 argc) {
  if (argc != 2) return raise_error(vm, "expander-lexical?: env-handle and symbol");
  Value env = vm.stack[base];
  Value sym = vm.stack[base + 1];
  while (pairp(env)) {
    if (!is_nil(table_get(vm, car_(env), sym))) return bool_v(true);
    env = cdr_(env);
  }
  return bool_v(false);
}

static Value nat_expander_macro_var(Vm& vm, u32 base, u32 argc) {
  if (argc != 1 || vm.stack[base].tag != Tag::Symbol) return nil_v();
  // Resolve in the namespace of the form being expanded, not in whatever
  // namespace the expander closure itself was defined in.
  u32 savedNs = vm.currentNs;
  if (vm.expandNs) vm.currentNs = vm.expandNs;
  Value var = ns_resolve_var(vm, vm.stack[base]);
  vm.currentNs = savedNs;
  if (is_nil(var)) return nil_v();
  Value v = var_value(var);
  return (v.tag == Tag::Macro) ? v : nil_v();
}

static Value nat_current_ns(Vm& vm, u32 base, u32 argc) {
  (void)base;
  (void)argc;
  return symbol_v(vm.currentNs);
}

static void def_native(Vm& vm, const char* name, NativeFn fn) {
  u32 r = vm.stack.len;
  Value f = make_native(vm, name, fn);
  vm.push(f);
  ns_define(vm, vm.intern.intern(name, (u32)strlen(name)), vm.stack[r], false, nil_v());
  vm.popTo(r);
}

void register_eval_natives(Vm& vm) {
  u32 saved = vm.currentNs;
  vm.currentNs = vm.syms.otiumCore_;
  def_native(vm, "signal", nat_signal);
  def_native(vm, "error", nat_error);
  def_native(vm, "invoke-restart", nat_invoke_restart);
  def_native(vm, "compute-restarts", nat_compute_restarts);
  def_native(vm, "find-restart", nat_find_restart);
  def_native(vm, "restart-name", nat_restart_name);
  def_native(vm, "restart-description", nat_restart_description);
  def_native(vm, "define-condition", nat_define_condition);
  def_native(vm, "condition-of-type?", nat_condition_of_type);
  def_native(vm, "gensym", nat_gensym);
  def_native(vm, "current-ns", nat_current_ns);
  def_native(vm, "*expander*", nat_expander);
  def_native(vm, "expander-lexical?", nat_expander_lexical);
  def_native(vm, "expander-macro-var", nat_expander_macro_var);
  vm.currentNs = saved;
}

}  // namespace ot
