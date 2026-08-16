// eval.c - top-level expansion/compilation, application, and control helpers.
#include "eval.h"
#include "builtins.h"
#include "compile.h"
#include "reader.h"

// ---------------------------------------------------------------- helpers

// Symbol/keyword id; strings are interned; unsupported values return 0.
static u32 name_id_of(State* vm, Ref value) { return ot_name_id(vm, value); }

// ---------------------------------------------------------------- require

static void unwrap_quote(State* vm, Ref dst, Ref value) {
  if (ot_tag(vm, value) != Tag_Pair) {
    ot_copy(vm, dst, value);
    return;
  }
  OT_SCOPE(vm);
  Ref part = ot_push(vm);
  ot_car(vm, part, value);
  if (ot_tag(vm, part) != Tag_Symbol || ot_id(vm, part) != ot_syms(vm)->quote_) {
    ot_copy(vm, dst, value);
    return;
  }
  ot_cdr(vm, part, value);
  if (ot_tag(vm, part) != Tag_Pair) {
    ot_copy(vm, dst, value);
    return;
  }
  ot_car(vm, dst, part);
}

static void strip_array_literal_head_ref(State* vm, Ref dst, Ref forms) {
  if (ot_tag(vm, forms) != Tag_Pair) {
    ot_copy(vm, dst, forms);
    return;
  }
  OT_SCOPE(vm);
  Ref head = ot_push(vm);
  ot_car(vm, head, forms);
  if (ot_tag(vm, head) == Tag_Symbol && ot_id(vm, head) == ot_syms(vm)->array_)
    ot_cdr(vm, dst, forms);
  else
    ot_copy(vm, dst, forms);
}

static Value require_spec(State* vm, Ref spec) {
  OT_SCOPE(vm);
  const Syms* syms = ot_syms(vm);
  Ref normalized = ot_push(vm);
  Ref name = ot_push(vm);
  Ref opts = ot_push(vm);
  Ref part = ot_push(vm);
  unwrap_quote(vm, normalized, spec);
  ot_copy(vm, name, normalized);
  ot_set_null(vm, opts);
  if (ot_tag(vm, normalized) == Tag_Pair) {
    ot_car(vm, part, normalized);
    unwrap_quote(vm, name, part);
    ot_cdr(vm, opts, normalized);
  }
  u32 target = name_id_of(vm, name);
  if (!target) return raise_error(vm, "require: bad namespace name");
  Value loaded = ot_require_load(vm, target);
  if (loaded.tag == Tag_Unwind) return loaded;
  Ref current = ot_push(vm);
  Ref names = ot_push(vm);
  Ref symbol = ot_push(vm);
  Ref var = ot_push(vm);
  Ref table = ot_push(vm);
  Ref targetNs = ot_push(vm);
  ot_ns_get_or_create(vm, current, ot_current_ns(vm));
  while (ot_tag(vm, opts) == Tag_Pair) {
    ot_car(vm, part, opts);
    bool isKeyword = ot_tag(vm, part) == Tag_Keyword;
    u32 option = isKeyword ? ot_id(vm, part) : 0;
    if (option == syms->kwAs) {
      ot_cdr(vm, opts, opts);
      if (ot_tag(vm, opts) != Tag_Pair) return raise_error(vm, "require: :as needs a name");
      ot_car(vm, part, opts);
      unwrap_quote(vm, symbol, part);
      ot_ns_field(vm, table, current, syms->kwAliases);
      ot_set_symbol(vm, var, target);
      ot_table_put(vm, table, symbol, var);
    } else if (option == syms->kwRefer) {
      ot_cdr(vm, opts, opts);
      if (ot_tag(vm, opts) != Tag_Pair) return raise_error(vm, "require: :refer needs a list");
      ot_car(vm, part, opts);
      unwrap_quote(vm, names, part);
      strip_array_literal_head_ref(vm, names, names);
      // The target namespace is re-looked-up each iteration rather than hoisted:
      // table_put allocates once table storage is GC-owned.
      while (ot_tag(vm, names) == Tag_Pair) {
        ot_car(vm, symbol, names);
        ot_ns_lookup(vm, targetNs, target);
        ot_ns_field(vm, table, targetNs, syms->kwVars);
        ot_table_get(vm, var, table, symbol);
        if (ot_nil(vm, var) || ot_var_private(vm, var))
          return raise_error_sym(vm, "cannot refer %.*s", ot_id(vm, symbol));
        ot_ns_field(vm, table, current, syms->kwRefers);
        ot_table_put(vm, table, symbol, var);
        ot_cdr(vm, names, names);
      }
    }  // :reload and unknown options tolerated / ignored in stage 0
    ot_cdr(vm, opts, opts);
  }
  return nil_v();
}

Value vm_control_defparam(State* vm, u32 base, u32 argc) {
  Ref name = {base};
  if (argc != 3 || ot_tag(vm, name) != Tag_Symbol)
    return raise_error(vm, "defparam: bad compiled form");
  u32 paramName = ot_id(vm, name);
  OT_SCOPE(vm);
  Ref paramRoot = ot_push(vm);
  Ref doc = ot_push_copy(vm, (Ref){base + 1});
  ot_make_param(vm, paramRoot, paramName, (Ref){base + 2});
  ot_define(vm, paramRoot, paramName, paramRoot, false, doc);
  return ot_ret(vm, paramRoot);
}

Value vm_control_ns(State* vm, u32 base, u32 argc) {
  Ref name = {base};
  if (argc == 0 || ot_tag(vm, name) != Tag_Symbol)
    return raise_error(vm, "ns: bad compiled form");
  ot_switch_ns(vm, ot_id(vm, name));
  OT_SCOPE(vm);
  Ref specs = ot_push(vm);
  Ref part = ot_push(vm);
  u32 requireId = ot_syms(vm)->kwRequire;
  for (u32 i = 1; i < argc; i++) {
    Ref clause = {base + i};
    if (ot_tag(vm, clause) != Tag_Pair) continue;
    ot_car(vm, part, clause);
    if (ot_tag(vm, part) != Tag_Keyword || ot_id(vm, part) != requireId)
      continue;
    ot_cdr(vm, specs, clause);
    while (ot_tag(vm, specs) == Tag_Pair) {
      ot_car(vm, part, specs);
      Value result = require_spec(vm, part);
      if (result.tag == Tag_Unwind) return result;
      ot_cdr(vm, specs, specs);
    }
  }
  return nil_v();
}

Value vm_control_in_ns(State* vm, u32 base, u32 argc) {
  if (argc != 1) return raise_error(vm, "in-ns: one argument");
  OT_SCOPE(vm);
  Ref normalized = ot_push(vm);
  unwrap_quote(vm, normalized, (Ref){base});
  u32 name = name_id_of(vm, normalized);
  if (!name) return raise_error(vm, "in-ns: bad namespace name");
  ot_switch_ns(vm, name);
  return nil_v();
}

Value vm_control_require(State* vm, u32 base, u32 argc) {
  for (u32 i = 0; i < argc; i++) {
    Value result = require_spec(vm, (Ref){base + i});
    if (result.tag == Tag_Unwind) return result;
  }
  return nil_v();
}

// Compile and execute one expanded top-level form.
Value eval_form_ref(State* vm, Ref dst, Ref form) {
  OT_SCOPE(vm);
  const Syms* syms = ot_syms(vm);
  Ref formRoot = ot_push_copy(vm, form);
  // Route through the expansion hook *expander* in otium.core.
  Ref core = ot_push(vm);
  if (ot_ns_lookup(vm, core, syms->otiumCore_)) {
    Ref vars = ot_push(vm);
    Ref var = ot_push(vm);
    Ref expander = ot_push(vm);
    ot_ns_field(vm, vars, core, syms->kwVars);
    ot_table_get_im(vm, var, vars, symbol_v(syms->expander_));
    if (!ot_nil(vm, var)) {
      ot_var_value(vm, expander, var);
      if (ot_tag(vm, expander) == Tag_Function) {
        u32 savedExpandNs = ot_expand_ns(vm);
        ot_set_expand_ns(vm, ot_current_ns(vm));
        u32 argBase = ot_top(vm);
        ot_push_copy(vm, formRoot);
        Value expanded = ot_apply(vm, formRoot, expander, argBase, 1);
        ot_set_expand_ns(vm, savedExpandNs);
        if (expanded.tag == Tag_Unwind) return expanded;
      }
    }
  }
  Ref code = ot_push(vm);
  OT_TRY(compile_form_ref(vm, code, formRoot));
  return ot_execute_code(vm, dst, code);
}

Value eval_source_policy(State* vm, const char* src, u32 len, const char* name,
                         const EvalSourcePolicy* policy) {
  EvalSourceState localState;
  EvalSourceState* state = policy->state ? policy->state : &localState;
  *state = (EvalSourceState){0};

  Reader reader;
  reader_init(&reader, vm, src, len, name);
  OT_SCOPE(vm);
  Ref last = ot_push(vm);
  Ref form = ot_push(vm);
  for (;;) {
    Value status = reader_next_ref(&reader, form);
    if (status.tag == Tag_Unwind) {
      state->readError = true;
      state->incomplete = reader_incomplete(&reader);
      return status;
    }
    if (reader_at_eof(&reader)) {
      state->consumed = len;
      return ot_ret(vm, last);
    }
    if (policy->eval) {
      Value result = policy->eval(vm, ot_ret(vm, form), policy->data);
      if (result.tag == Tag_Unwind) return result;
      ot_set_return(vm, last, result);
    } else {
      OT_TRY(eval_form_ref(vm, last, form));
    }
    state->consumed = reader_position(&reader);
    if (policy->afterEval)
      policy->afterEval(vm, ot_ret(vm, last), state->consumed, policy->data);
  }
}

Value eval_source(State* vm, const char* src, u32 len, const char* name) {
  EvalSourcePolicy policy = {0};
  return eval_source_policy(vm, src, len, name, &policy);
}
