// ns.c — namespaces, vars, resolution (spec 7 and 3.1).
#include "ns.h"
#include "state.h"

Value var_value(Value var) { return as_array(var)->items[VAR_VALUE]; }
void var_set(Value var, Value v) { as_array(var)->items[VAR_VALUE] = v; }
bool var_private(Value var) { return is_truthy(as_array(var)->items[VAR_PRIVATE]); }

Value ns_field(State* vm, Value nsRec, u32 kwId) { return table_get(vm, nsRec, keyword_v(kwId)); }

Value ns_lookup(State* vm, u32 nsName) { return table_get(vm, vm->nsRegistry, symbol_v(nsName)); }

// Each sub-structure is rooted before it is stored: table_put allocates, so a
// make_* result left in a raw local could be moved before the put reads it.
Value ns_get_or_create(State* vm, u32 nsName) {
  Value ns = ns_lookup(vm, nsName);
  if (!is_nil(ns)) return ns;
  OT_SCOPE(vm);
  Ref nsS = ref_push(vm, make_table(vm));
  Ref field = ref_push(vm, nil_v());
  table_put(vm, ref_get(vm, nsS), keyword_v(vm->syms.kwName), symbol_v(nsName));
  ref_set(vm, field, make_table(vm));
  table_put(vm, ref_get(vm, nsS), keyword_v(vm->syms.kwVars), ref_get(vm, field));
  ref_set(vm, field, make_table(vm));
  table_put(vm, ref_get(vm, nsS), keyword_v(vm->syms.kwAliases), ref_get(vm, field));
  ref_set(vm, field, make_table(vm));
  table_put(vm, ref_get(vm, nsS), keyword_v(vm->syms.kwRefers), ref_get(vm, field));
  ref_set(vm, field, make_array(vm, 8));
  table_put(vm, ref_get(vm, nsS), keyword_v(vm->syms.kwOrder), ref_get(vm, field));
  table_put(vm, vm->nsRegistry, symbol_v(nsName), ref_get(vm, nsS));

  // Auto-refer all public otium.core vars present at creation time (7.1).
  if (nsName != vm->syms.otiumCore_) {
    Ref core = ref_push(vm, ns_lookup(vm, vm->syms.otiumCore_));
    if (!is_nil(ref_get(vm, core))) {
      Ref cvars = ref_push(vm, ns_field(vm, ref_get(vm, core), vm->syms.kwVars));
      Ref order = ref_push(vm, ns_field(vm, ref_get(vm, core), vm->syms.kwOrder));
      Ref refers = ref_push(vm, ns_field(vm, ref_get(vm, nsS), vm->syms.kwRefers));
      Ref nameSym = ref_push(vm, nil_v());
      Ref var = ref_push(vm, nil_v());
      // The bound and the element are re-read through `order` every iteration.
      // Caching an ArrayData* across the table_put would strand it the moment
      // table storage lives on the GC heap and the put can collect.
      for (u32 i = 0; i < as_array(ref_get(vm, order))->len; i++) {
        ref_set(vm, nameSym, as_array(ref_get(vm, order))->items[i]);
        ref_set(vm, var, table_get(vm, ref_get(vm, cvars), ref_get(vm, nameSym)));
        if (!is_nil(ref_get(vm, var)) && !var_private(ref_get(vm, var)))
          table_put(vm, ref_get(vm, refers), ref_get(vm, nameSym), ref_get(vm, var));
      }
    }
  }
  return ref_get(vm, nsS);
}

Value ns_define(State* vm, u32 name, Value v, bool isPrivate, Value docstring) {
  OT_SCOPE(vm);
  Ref vS = ref_push(vm, v);
  Ref dS = ref_push(vm, docstring);
  Ref nsS = ref_push(vm, ns_get_or_create(vm, vm->currentNs));
  Ref var = ref_push(vm, table_get(vm, ns_field(vm, ref_get(vm, nsS), vm->syms.kwVars),
                                   symbol_v(name)));
  if (is_nil(ref_get(vm, var))) {
    Ref varS = ref_push(vm, make_array(vm, VAR_SLOTS));
    array_push(vm, ref_get(vm, varS), ref_get(vm, vS));  // value
    array_push(vm, ref_get(vm, varS), symbol_v(name));
    array_push(vm, ref_get(vm, varS), symbol_v(vm->currentNs));
    array_push(vm, ref_get(vm, varS), ref_get(vm, dS));  // docstring
    array_push(vm, ref_get(vm, varS), bool_v(isPrivate));
    // ns_field is re-read after each mutation rather than hoisted: array_push
    // and table_put both allocate.
    table_put(vm, ns_field(vm, ref_get(vm, nsS), vm->syms.kwVars), symbol_v(name),
              ref_get(vm, varS));
    array_push(vm, ns_field(vm, ref_get(vm, nsS), vm->syms.kwOrder), symbol_v(name));
  } else {
    // No allocation between these reads and writes, so the interior pointer is
    // safe for the length of the block.
    ArrayData* a = as_array(ref_get(vm, var));
    a->items[VAR_VALUE] = ref_get(vm, vS);
    a->items[VAR_DOC] = ref_get(vm, dS);
    a->items[VAR_PRIVATE] = bool_v(isPrivate);
  }
  return ref_get(vm, vS);
}

bool sym_qualified(State* vm, u32 symId) {
  u32 len;
  const char* s = intern_name(&vm->intern, symId, &len);
  for (u32 i = 1; i + 1 < len; i++)
    if (s[i] == '/') return true;
  return false;
}

// Resolve a var cell; nil if not found. When raiseErr, a miss also raises a
// condition (raise_error stores it in vm->unwindCondition; its return value
// is always the immediate Unwind sentinel, so callers just return unwind_v()).
static Value resolve_var_impl(State* vm, Value sym, bool raiseErr) {
  u32 len;
  const char* s = intern_name(&vm->intern, sym.id, &len);
  u32 slash = 0;
  for (u32 i = 1; i + 1 < len; i++)
    if (s[i] == '/') {
      slash = i;
      break;
    }

  if (slash) {  // qualified p/n
    u32 p = intern_id(&vm->intern, s, slash);
    u32 n = intern_id(&vm->intern, s + slash + 1, len - slash - 1);
    Value cur = ns_lookup(vm, vm->currentNs);
    u32 nsName = p;
    if (!is_nil(cur)) {
      Value alias = table_get(vm, ns_field(vm, cur, vm->syms.kwAliases), symbol_v(p));
      if (alias.tag == Tag_Symbol) nsName = alias.id;
    }
    Value target = ns_lookup(vm, nsName);
    if (is_nil(target)) {
      if (raiseErr) raise_error(vm, "no such namespace: %.*s", (int)slash, s);
      return nil_v();
    }
    Value var = table_get(vm, ns_field(vm, target, vm->syms.kwVars), symbol_v(n));
    if (is_nil(var)) {
      if (raiseErr) raise_error_sym(vm, "no such var: %.*s", sym.id);
      return nil_v();
    }
    if (var_private(var) && nsName != vm->currentNs) {
      if (raiseErr) raise_error_sym(vm, "var is private: %.*s", sym.id);
      return nil_v();
    }
    return var;
  }

  // unqualified: own vars -> refers
  Value cur = ns_lookup(vm, vm->currentNs);
  if (!is_nil(cur)) {
    Value var = table_get(vm, ns_field(vm, cur, vm->syms.kwVars), sym);
    if (!is_nil(var)) return var;
    var = table_get(vm, ns_field(vm, cur, vm->syms.kwRefers), sym);
    if (!is_nil(var)) return var;
  }
  if (raiseErr) raise_error_sym(vm, "unresolved symbol: %.*s", sym.id);
  return nil_v();
}

Value ns_resolve_var(State* vm, Value symbol) { return resolve_var_impl(vm, symbol, false); }

Value ns_resolve(State* vm, Value symbol) {
  Value var = resolve_var_impl(vm, symbol, true);
  if (is_nil(var)) return unwind_v();
  return var_value(var);
}

void ns_switch(State* vm, u32 nsName) {
  ns_get_or_create(vm, nsName);
  vm->currentNs = nsName;
}
