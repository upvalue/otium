// builtins/expand.c — stage-0 macro expansion and expander oracles.
#include "../builtins.h"
#include "../eval.h"
#include "../form.h"
#include "../ns.h"

static Value nat_gensym(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "gensym", argc, 0, 1));
  char buf[128];
  const char* prefix = "G";
  u32 plen = 1;
  if (argc >= 1) {
    u32 id = name_id_of(vm, vm->stack.data[base]);
    if (id) prefix = intern_name(&vm->intern, id, &plen);
  }
  int n = snprintf(buf, sizeof buf, "%.*s__%llu", (int)plen, prefix,
                   (unsigned long long)++vm->gensymCounter);
  return symbol_v(intern_id(&vm->intern, buf, (u32)n));
}

// Stage-0 expander: expand macro calls in head position, recursively, and
// recurse into subforms (skipping quote). Identity for everything else.
static Value expand0(State* vm, Value form);

static Value expand0_list(State* vm, Value l) {
  if (!pairp(l)) return l;
  OT_SCOPE(vm);
  Ref lS = ref_push(vm, l);  // expand0 allocates; keep the cursor rooted
  Value h = expand0(vm, car_(ref_get(vm, lS)));
  if (h.tag == Tag_Unwind) return h;
  Ref head = ref_push(vm, h);
  Value t = expand0_list(vm, cdr_(ref_get(vm, lS)));
  if (t.tag == Tag_Unwind) return t;
  Ref tail = ref_push(vm, t);
  return make_pair(vm, ref_get(vm, head), ref_get(vm, tail));
}

// Applies `macro` to the unevaluated argument forms of `form`. Its own function
// so the scope guard does not nest inside expand0's loop.
static Value apply_macro(State* vm, Value macro, Ref form) {
  OT_SCOPE(vm);
  Ref mslot = ref_push(vm, macro);
  u32 base = vm->stack.len;
  u32 argc = 0;
  // state_push does not allocate, so walking the form while pushing is safe.
  for (Value a = cdr_(ref_get(vm, form)); pairp(a); a = cdr_(a)) {
    state_push(vm, car_(a));
    argc++;
  }
  return apply(vm, ref_get(vm, mslot), base, argc);
}

static Value expand0(State* vm, Value form) {
  OT_SCOPE(vm);
  Ref cur = ref_push(vm, form);
  for (u32 guard = 0; guard < 1000; guard++) {
    if (!pairp(ref_get(vm, cur))) return ref_get(vm, cur);
    Value h = car_(ref_get(vm, cur));
    if (h.tag != Tag_Symbol) break;
    if (h.id == vm->syms.quote_) return ref_get(vm, cur);
    Value var = ns_resolve_var(vm, h);
    if (is_nil(var) || var_value(var).tag != Tag_Macro) break;
    Value r = apply_macro(vm, var_value(var), cur);
    OT_TRY(r);
    ref_set(vm, cur, r);  // re-expand the replacement
  }
  // recurse into subforms
  return expand0_list(vm, ref_get(vm, cur));
}

static Value nat_expander(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "*expander*", argc, 1, 1));
  return expand0(vm, vm->stack.data[base]);
}

static Value nat_expander_lexical(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "expander-lexical?", argc, 2, 2));
  Value env = vm->stack.data[base];
  Value sym = vm->stack.data[base + 1];
  while (pairp(env)) {
    if (!is_nil(table_get(vm, car_(env), sym))) return bool_v(true);
    env = cdr_(env);
  }
  return bool_v(false);
}

static Value nat_expander_macro_var(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "expander-macro-var", argc, 1, 1));
  if (vm->stack.data[base].tag != Tag_Symbol) return nil_v();
  // Resolve in the namespace of the form being expanded, not in whatever
  // namespace the expander closure itself was defined in.
  u32 savedNs = vm->currentNs;
  if (vm->expandNs) vm->currentNs = vm->expandNs;
  Value var = ns_resolve_var(vm, vm->stack.data[base]);
  vm->currentNs = savedNs;
  if (is_nil(var)) return nil_v();
  Value v = var_value(var);
  return (v.tag == Tag_Macro) ? v : nil_v();
}

static Value nat_current_ns(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "current-ns", argc, 0, 0));
  return symbol_v(vm->currentNs);
}

void register_expand(State* vm) {
  u32 saved = vm->currentNs;
  vm->currentNs = vm->syms.otiumCore_;
  def_native(vm, "gensym", nat_gensym);
  def_native(vm, "current-ns", nat_current_ns);
  def_native(vm, "*expander*", nat_expander);
  def_native(vm, "expander-lexical?", nat_expander_lexical);
  def_native(vm, "expander-macro-var", nat_expander_macro_var);
  vm->currentNs = saved;
}
