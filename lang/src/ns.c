// ns.c — namespaces, vars, resolution (spec 7 and 3.1).
//
// Namespace records and var cells live on the moving heap, so this file only
// handles them through rooted slots.
#include "slots.h"

void ot_var_value(State* vm, Ref dst, Ref var) { ot_array_get(vm, dst, var, OT_VAR_VALUE); }

void ot_var_set(State* vm, Ref var, Ref value) {
  OT_ASSERT(ot_array_set(vm, var, OT_VAR_VALUE, value));
}

bool ot_var_private(State* vm, Ref var) {
  OT_SCOPE(vm);
  Ref flag = ot_push(vm);
  ot_array_get(vm, flag, var, OT_VAR_PRIVATE);
  return ot_truthy(vm, flag);
}

void ot_ns_field(State* vm, Ref dst, Ref nsRecord, u32 kwId) {
  ot_table_get_im(vm, dst, nsRecord, keyword_v(kwId));
}

bool ot_ns_lookup(State* vm, Ref dst, u32 nsName) {
  // The registry is permanently rooted in stack[0] (see state_create).
  ot_table_get_im(vm, dst, (Ref){0}, symbol_v(nsName));
  return !ot_nil(vm, dst);
}

void ot_ns_get_or_create(State* vm, Ref dst, u32 nsName) {
  if (ot_ns_lookup(vm, dst, nsName)) return;

  OT_SCOPE(vm);
  const Syms* syms = ot_syms(vm);
  Ref ns = ot_push(vm);
  Ref field = ot_push(vm);
  ot_make_table(vm, ns);
  ot_table_put_im2(vm, ns, keyword_v(syms->kwName), symbol_v(nsName));
  ot_make_table(vm, field);
  ot_table_put_im(vm, ns, keyword_v(syms->kwVars), field);
  ot_make_table(vm, field);
  ot_table_put_im(vm, ns, keyword_v(syms->kwAliases), field);
  ot_make_table(vm, field);
  ot_table_put_im(vm, ns, keyword_v(syms->kwRefers), field);
  ot_make_array(vm, field, 8);
  ot_table_put_im(vm, ns, keyword_v(syms->kwOrder), field);
  ot_table_put_im(vm, (Ref){0}, symbol_v(nsName), ns);

  // Auto-refer all public otium.core vars present at creation time (7.1).
  if (nsName != syms->otiumCore_) {
    Ref core = ot_push(vm);
    if (ot_ns_lookup(vm, core, syms->otiumCore_)) {
      Ref coreVars = ot_push(vm);
      Ref order = ot_push(vm);
      Ref refers = ot_push(vm);
      Ref name = ot_push(vm);
      Ref var = ot_push(vm);
      ot_ns_field(vm, coreVars, core, syms->kwVars);
      ot_ns_field(vm, order, core, syms->kwOrder);
      ot_ns_field(vm, refers, ns, syms->kwRefers);
      for (u32 i = 0; i < ot_array_len(vm, order); i++) {
        ot_array_get(vm, name, order, i);
        ot_table_get(vm, var, coreVars, name);
        if (!ot_nil(vm, var) && !ot_var_private(vm, var)) ot_table_put(vm, refers, name, var);
      }
    }
  }

  ot_copy(vm, dst, ns);
}

void ot_define(State* vm, Ref dst, u32 name, Ref value, bool isPrivate, Ref docstring) {
  OT_SCOPE(vm);
  const Syms* syms = ot_syms(vm);
  u32 currentNs = ot_current_ns(vm);
  Ref ns = ot_push(vm);
  Ref vars = ot_push(vm);
  Ref var = ot_push(vm);
  ot_ns_get_or_create(vm, ns, currentNs);
  ot_ns_field(vm, vars, ns, syms->kwVars);
  ot_table_get_im(vm, var, vars, symbol_v(name));

  if (ot_nil(vm, var)) {
    ot_make_array(vm, var, OT_VAR_PRIVATE + 1);
    ot_array_push(vm, var, value);
    ot_array_push_im(vm, var, symbol_v(name));
    ot_array_push_im(vm, var, symbol_v(currentNs));
    ot_array_push(vm, var, docstring);
    ot_array_push_im(vm, var, bool_v(isPrivate));
    ot_table_put_im(vm, vars, symbol_v(name), var);

    Ref order = ot_push(vm);
    ot_ns_field(vm, order, ns, syms->kwOrder);
    ot_array_push_im(vm, order, symbol_v(name));
  } else {
    OT_ASSERT(ot_array_set(vm, var, OT_VAR_VALUE, value));
    OT_ASSERT(ot_array_set(vm, var, OT_VAR_DOC, docstring));
    Ref flag = ot_push(vm);
    ot_set_bool(vm, flag, isPrivate);
    OT_ASSERT(ot_array_set(vm, var, OT_VAR_PRIVATE, flag));
  }

  ot_copy(vm, dst, value);
}

bool ot_sym_qualified(State* vm, u32 symId) {
  u32 len = 0;
  const char* s = ot_intern_name(vm, symId, &len);
  for (u32 i = 1; i + 1 < len; i++)
    if (s[i] == '/') return true;
  return false;
}

// Resolve a var cell into dst. When raiseErr, a miss also starts a condition
// unwind; false is still returned so the caller can propagate unwind_v().
static bool resolve_var_impl(State* vm, Ref dst, Ref symbol, bool raiseErr) {
  OT_SCOPE(vm);
  const Syms* syms = ot_syms(vm);
  u32 symbolId = ot_id(vm, symbol);
  u32 len = 0;
  const char* s = ot_intern_name(vm, symbolId, &len);
  u32 slash = 0;
  for (u32 i = 1; i + 1 < len; i++)
    if (s[i] == '/') {
      slash = i;
      break;
    }

  Ref ns = ot_push(vm);
  Ref field = ot_push(vm);
  Ref key = ot_push(vm);
  Ref var = ot_push(vm);

  if (slash) {  // qualified p/n
    u32 prefix = ot_intern(vm, s, slash);
    u32 name = ot_intern(vm, s + slash + 1, len - slash - 1);
    u32 nsName = prefix;
    if (ot_ns_lookup(vm, ns, ot_current_ns(vm))) {
      ot_ns_field(vm, field, ns, syms->kwAliases);
      ot_set_symbol(vm, key, prefix);
      ot_table_get(vm, var, field, key);
      if (ot_tag(vm, var) == Tag_Symbol) nsName = ot_id(vm, var);
    }

    if (!ot_ns_lookup(vm, ns, nsName)) {
      if (raiseErr) raise_error(vm, "no such namespace: %.*s", (int)slash, s);
      ot_set_nil(vm, dst);
      return false;
    }
    ot_ns_field(vm, field, ns, syms->kwVars);
    ot_set_symbol(vm, key, name);
    ot_table_get(vm, var, field, key);
    if (ot_nil(vm, var)) {
      if (raiseErr) raise_error_sym(vm, "no such var: %.*s", symbolId);
      ot_set_nil(vm, dst);
      return false;
    }
    if (ot_var_private(vm, var) && nsName != ot_current_ns(vm)) {
      if (raiseErr) raise_error_sym(vm, "var is private: %.*s", symbolId);
      ot_set_nil(vm, dst);
      return false;
    }
    ot_copy(vm, dst, var);
    return true;
  }

  // unqualified: own vars -> refers
  if (ot_ns_lookup(vm, ns, ot_current_ns(vm))) {
    ot_ns_field(vm, field, ns, syms->kwVars);
    ot_table_get(vm, var, field, symbol);
    if (!ot_nil(vm, var)) {
      ot_copy(vm, dst, var);
      return true;
    }
    ot_ns_field(vm, field, ns, syms->kwRefers);
    ot_table_get(vm, var, field, symbol);
    if (!ot_nil(vm, var)) {
      ot_copy(vm, dst, var);
      return true;
    }
  }

  if (raiseErr) raise_error_sym(vm, "unresolved symbol: %.*s", symbolId);
  ot_set_nil(vm, dst);
  return false;
}

bool ot_resolve_var(State* vm, Ref dst, Ref symbol) {
  return resolve_var_impl(vm, dst, symbol, false);
}

Value ot_resolve(State* vm, Ref dst, Ref symbol) {
  if (!resolve_var_impl(vm, dst, symbol, true)) return unwind_v();
  ot_var_value(vm, dst, dst);
  return nil_v();
}

void ot_switch_ns(State* vm, u32 nsName) {
  OT_SCOPE(vm);
  Ref ns = ot_push(vm);
  ot_ns_get_or_create(vm, ns, nsName);
  ot_set_current_ns(vm, nsName);
}
