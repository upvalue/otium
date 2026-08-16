// builtins/expand.cpp — stage-0 macro expansion and expander oracles.
#include "../builtins.hpp"
#include "../eval.hpp"
#include "../form.hpp"
#include "../ns.hpp"
#include "../state.hpp"

namespace ot {

static Value nat_gensym(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "gensym", argc, 0, 1));
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
static Value expand0(State& vm, Value form);

static Value expand0_list(State& vm, Value l) {
  if (!pairp(l)) return l;
  Scope s(vm);
  Slot lS = s.push(l);  // expand0 allocates; keep the cursor rooted
  Value h = expand0(vm, car_(lS.get()));
  OT_TRY(h);
  Slot r = s.push(h);
  Value t = expand0_list(vm, cdr_(lS.get()));
  OT_TRY(t);
  return make_pair(vm, r.get(), t);
}

static Value expand0(State& vm, Value form) {
  for (u32 guard = 0; guard < 1000; guard++) {
    if (!pairp(form)) return form;
    Value h = car_(form);
    if (h.tag != Tag::Symbol) break;
    if (h.id == vm.syms.quote_) return form;
    Value var = ns_resolve_var(vm, h);
    if (is_nil(var) || var_value(var).tag != Tag::Macro) break;
    // call the macro on the unevaluated argument forms (vm.push doesn't
    // allocate on the GC heap, so walking the form while pushing is safe)
    Value r;
    {
      Scope s(vm);
      Slot mslot = s.push(var_value(var));
      u32 base = vm.stack.len;
      u32 argc = 0;
      for (Value a = cdr_(form); pairp(a); a = cdr_(a)) {
        vm.push(car_(a));
        argc++;
      }
      r = apply(vm, mslot.get(), base, argc);
    }
    OT_TRY(r);
    form = r;  // re-expand the replacement
  }
  // recurse into subforms
  return expand0_list(vm, form);
}

static Value nat_expander(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "*expander*", argc, 1, 1));
  return expand0(vm, vm.stack[base]);
}

static Value nat_expander_lexical(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "expander-lexical?", argc, 2, 2));
  Value env = vm.stack[base];
  Value sym = vm.stack[base + 1];
  while (pairp(env)) {
    if (!is_nil(table_get(vm, car_(env), sym))) return bool_v(true);
    env = cdr_(env);
  }
  return bool_v(false);
}

static Value nat_expander_macro_var(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "expander-macro-var", argc, 1, 1));
  if (vm.stack[base].tag != Tag::Symbol) return nil_v();
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

static Value nat_current_ns(State& vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "current-ns", argc, 0, 0));
  return symbol_v(vm.currentNs);
}

void register_expand(State& vm) {
  u32 saved = vm.currentNs;
  vm.currentNs = vm.syms.otiumCore_;
  def_native(vm, "gensym", nat_gensym);
  def_native(vm, "current-ns", nat_current_ns);
  def_native(vm, "*expander*", nat_expander);
  def_native(vm, "expander-lexical?", nat_expander_lexical);
  def_native(vm, "expander-macro-var", nat_expander_macro_var);
  vm.currentNs = saved;
}

}  // namespace ot
