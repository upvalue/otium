// ns.cpp — namespaces, vars, resolution (spec 7 and 3.1).
#include "ns.hpp"
#include "vm.hpp"

namespace ot {

Value var_value(Value var) { return as_array(var)->items[VAR_VALUE]; }
void var_set(Value var, Value v) { as_array(var)->items[VAR_VALUE] = v; }
bool var_private(Value var) { return is_truthy(as_array(var)->items[VAR_PRIVATE]); }

Value ns_field(Vm& vm, Value nsRec, u32 kwId) { return table_get(vm, nsRec, keyword_v(kwId)); }

Value ns_lookup(Vm& vm, u32 nsName) { return table_get(vm, vm.nsRegistry, symbol_v(nsName)); }

Value ns_get_or_create(Vm& vm, u32 nsName) {
  Value ns = ns_lookup(vm, nsName);
  if (!is_nil(ns)) return ns;
  // Allocate each sub-structure BEFORE reading the ns slot: a make_* in
  // argument position would move the table out from under table_put.
  Scope sc(vm);
  Slot nsS = sc.push(make_table(vm));
  table_put(vm, nsS.get(), keyword_v(vm.syms.kwName), symbol_v(nsName));
  {
    Value t = make_table(vm);
    table_put(vm, nsS.get(), keyword_v(vm.syms.kwVars), t);
  }
  {
    Value t = make_table(vm);
    table_put(vm, nsS.get(), keyword_v(vm.syms.kwAliases), t);
  }
  {
    Value t = make_table(vm);
    table_put(vm, nsS.get(), keyword_v(vm.syms.kwRefers), t);
  }
  {
    Value a = make_array(vm, 8);
    table_put(vm, nsS.get(), keyword_v(vm.syms.kwOrder), a);
  }
  table_put(vm, vm.nsRegistry, symbol_v(nsName), nsS.get());

  // Auto-refer all public otium.core vars present at creation time (7.1).
  // table_get/table_put do not allocate on the GC heap, so these raw locals
  // remain valid for the whole walk.
  if (nsName != vm.syms.otiumCore_) {
    Value core = ns_lookup(vm, vm.syms.otiumCore_);
    if (!is_nil(core)) {
      Value cvars = ns_field(vm, core, vm.syms.kwVars);
      Value order = ns_field(vm, core, vm.syms.kwOrder);
      Value refers = ns_field(vm, nsS.get(), vm.syms.kwRefers);
      ArrayData* od = as_array(order);
      for (u32 i = 0; i < od->len; i++) {
        Value nameSym = od->items[i];
        Value var = table_get(vm, cvars, nameSym);
        if (!is_nil(var) && !var_private(var)) table_put(vm, refers, nameSym, var);
      }
    }
  }
  return nsS.get();
}

Value ns_define(Vm& vm, u32 name, Value v, bool isPrivate, Value docstring) {
  Scope sc(vm);
  Slot vS = sc.push(v);
  Slot dS = sc.push(docstring);
  Slot nsS = sc.push(ns_get_or_create(vm, vm.currentNs));
  Value vars = ns_field(vm, nsS.get(), vm.syms.kwVars);
  Value var = table_get(vm, vars, symbol_v(name));
  if (is_nil(var)) {
    Slot varS = sc.push(make_array(vm, VAR_SLOTS));
    // array_push/table_put are alloc-free; the var cell re-reads are cheap
    array_push(vm, varS.get(), vS.get());  // value
    array_push(vm, varS.get(), symbol_v(name));
    array_push(vm, varS.get(), symbol_v(vm.currentNs));
    array_push(vm, varS.get(), dS.get());  // docstring
    array_push(vm, varS.get(), bool_v(isPrivate));
    table_put(vm, ns_field(vm, nsS.get(), vm.syms.kwVars), symbol_v(name), varS.get());
    array_push(vm, ns_field(vm, nsS.get(), vm.syms.kwOrder), symbol_v(name));
  } else {
    ArrayData* a = as_array(var);
    a->items[VAR_VALUE] = vS.get();
    a->items[VAR_DOC] = dS.get();
    a->items[VAR_PRIVATE] = bool_v(isPrivate);
  }
  return vS.get();
}

bool sym_qualified(Vm& vm, u32 symId) {
  u32 len;
  const char* s = vm.intern.name(symId, &len);
  for (u32 i = 1; i + 1 < len; i++)
    if (s[i] == '/') return true;
  return false;
}

// Resolve a var cell; nil if not found. When raiseErr, a miss also raises a
// condition (raise_error stores it in vm.unwindCondition; its return value
// is always the immediate Unwind sentinel, so callers just return unwind_v()).
static Value resolve_var_impl(Vm& vm, Value sym, bool raiseErr) {
  u32 len;
  const char* s = vm.intern.name(sym.id, &len);
  u32 slash = 0;
  for (u32 i = 1; i + 1 < len; i++)
    if (s[i] == '/') {
      slash = i;
      break;
    }

  if (slash) {  // qualified p/n
    u32 p = vm.intern.intern(s, slash);
    u32 n = vm.intern.intern(s + slash + 1, len - slash - 1);
    Value cur = ns_lookup(vm, vm.currentNs);
    u32 nsName = p;
    if (!is_nil(cur)) {
      Value alias = table_get(vm, ns_field(vm, cur, vm.syms.kwAliases), symbol_v(p));
      if (alias.tag == Tag::Symbol) nsName = alias.id;
    }
    Value target = ns_lookup(vm, nsName);
    if (is_nil(target)) {
      if (raiseErr) raise_error(vm, "no such namespace: %.*s", (int)slash, s);
      return nil_v();
    }
    Value var = table_get(vm, ns_field(vm, target, vm.syms.kwVars), symbol_v(n));
    if (is_nil(var)) {
      if (raiseErr) raise_error_sym(vm, "no such var: %.*s", sym.id);
      return nil_v();
    }
    if (var_private(var) && nsName != vm.currentNs) {
      if (raiseErr) raise_error_sym(vm, "var is private: %.*s", sym.id);
      return nil_v();
    }
    return var;
  }

  // unqualified: own vars -> refers
  Value cur = ns_lookup(vm, vm.currentNs);
  if (!is_nil(cur)) {
    Value var = table_get(vm, ns_field(vm, cur, vm.syms.kwVars), sym);
    if (!is_nil(var)) return var;
    var = table_get(vm, ns_field(vm, cur, vm.syms.kwRefers), sym);
    if (!is_nil(var)) return var;
  }
  if (raiseErr) raise_error_sym(vm, "unresolved symbol: %.*s", sym.id);
  return nil_v();
}

Value ns_resolve_var(Vm& vm, Value symbol) { return resolve_var_impl(vm, symbol, false); }

Value ns_resolve(Vm& vm, Value symbol) {
  Value var = resolve_var_impl(vm, symbol, true);
  if (is_nil(var)) return unwind_v();
  return var_value(var);
}

void ns_switch(Vm& vm, u32 nsName) {
  ns_get_or_create(vm, nsName);
  vm.currentNs = nsName;
}

}  // namespace ot
