// builtins/expand.c — stage-0 macro expansion and expander oracles.
#include "../builtins.h"
#include <stdio.h>

static Value nat_gensym(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "gensym", argc, 0, 1));
  char buf[128];
  const char* prefix = "G";
  u32 plen = 1;
  if (argc >= 1) {
    u32 id = ot_name_id(vm, ARG(0));
    if (id) prefix = ot_intern_name(vm, id, &plen);
  }
  int n = snprintf(buf, sizeof buf, "%.*s__%llu", (int)plen, prefix,
                   (unsigned long long)ot_next_gensym(vm));
  return symbol_v(ot_intern(vm, buf, (u32)n));
}

// Stage-0 expander: expand macro calls in head position, recursively, and
// recurse into subforms (skipping quote). Identity for everything else.
// Both helpers write their result into dst and return nil or an unwind.
static Value expand0(State* vm, Ref dst, Ref form);

static Value expand0_list(State* vm, Ref dst, Ref l) {
  if (ot_tag(vm, l) != Tag_Pair) {
    ot_copy(vm, dst, l);
    return nil_v();
  }
  OT_SCOPE(vm);
  Ref lS = ot_push_copy(vm, l);  // dst may alias l
  Ref head = ot_push(vm);
  ot_car(vm, head, lS);
  Ref headX = ot_push(vm);
  OT_TRY(expand0(vm, headX, head));
  Ref tail = ot_push(vm);
  ot_cdr(vm, tail, lS);
  Ref tailX = ot_push(vm);
  OT_TRY(expand0_list(vm, tailX, tail));
  ot_cons(vm, dst, headX, tailX);
  return nil_v();
}

// Applies the macro in `macroVal` to the unevaluated argument forms of
// `form`, into dst. Its own function so the scope guard does not nest inside
// expand0's loop.
static Value apply_macro(State* vm, Ref dst, Ref macroVal, Ref form) {
  OT_SCOPE(vm);
  Ref result = ot_push(vm);
  Ref cursor = ot_push(vm);
  ot_cdr(vm, cursor, form);
  u32 argBase = ot_top(vm);
  u32 argc = 0;
  while (ot_tag(vm, cursor) == Tag_Pair) {
    Ref arg = ot_push(vm);
    ot_car(vm, arg, cursor);
    ot_cdr(vm, cursor, cursor);
    argc++;
  }
  OT_TRY(ot_apply(vm, result, macroVal, argBase, argc));
  ot_copy(vm, dst, result);
  return nil_v();
}

static Value expand0(State* vm, Ref dst, Ref form) {
  OT_SCOPE(vm);
  Ref cur = ot_push_copy(vm, form);
  Ref head = ot_push(vm);
  Ref var = ot_push(vm);
  Ref val = ot_push(vm);
  for (u32 guard = 0; guard < 1000; guard++) {
    if (ot_tag(vm, cur) != Tag_Pair) {
      ot_copy(vm, dst, cur);
      return nil_v();
    }
    ot_car(vm, head, cur);
    if (ot_tag(vm, head) != Tag_Symbol) break;
    if (ot_id(vm, head) == ot_syms(vm)->quote_) {
      ot_copy(vm, dst, cur);
      return nil_v();
    }
    if (!ot_resolve_var(vm, var, head)) break;
    ot_array_get(vm, val, var, OT_VAR_VALUE);
    if (ot_tag(vm, val) != Tag_Macro) break;
    OT_TRY(apply_macro(vm, cur, val, cur));  // re-expand the replacement
  }
  // recurse into subforms
  return expand0_list(vm, dst, cur);
}

static Value nat_expander(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "*expander*", argc, 1, 1));
  OT_SCOPE(vm);
  Ref out = ot_push(vm);
  OT_TRY(expand0(vm, out, ARG(0)));
  return ot_ret(vm, out);
}

static Value nat_expander_lexical(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "expander-lexical?", argc, 2, 2));
  OT_SCOPE(vm);
  Ref env = ot_push_copy(vm, ARG(0));
  Ref frame = ot_push(vm);
  Ref hit = ot_push(vm);
  while (ot_tag(vm, env) == Tag_Pair) {
    ot_car(vm, frame, env);
    ot_table_get(vm, hit, frame, ARG(1));
    if (!ot_nil(vm, hit)) return bool_v(true);
    ot_cdr(vm, env, env);
  }
  return bool_v(false);
}

static Value nat_expander_macro_var(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "expander-macro-var", argc, 1, 1));
  if (ot_tag(vm, ARG(0)) != Tag_Symbol) return nil_v();
  // Resolve in the namespace of the form being expanded, not in whatever
  // namespace the expander closure itself was defined in.
  u32 savedNs = ot_current_ns(vm);
  if (ot_expand_ns(vm)) ot_set_current_ns(vm, ot_expand_ns(vm));
  OT_SCOPE(vm);
  Ref var = ot_push(vm);
  bool found = ot_resolve_var(vm, var, ARG(0));
  ot_set_current_ns(vm, savedNs);
  if (!found) return nil_v();
  Ref val = ot_push(vm);
  ot_array_get(vm, val, var, OT_VAR_VALUE);
  if (ot_tag(vm, val) != Tag_Macro) return nil_v();
  return ot_ret(vm, val);
}

static Value nat_current_ns(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "current-ns", argc, 0, 0));
  return symbol_v(ot_current_ns(vm));
}

void register_expand(State* vm) {
  u32 saved = ot_current_ns(vm);
  ot_set_current_ns(vm, ot_syms(vm)->otiumCore_);
  ot_def_native(vm, "gensym", nat_gensym);
  ot_def_native(vm, "current-ns", nat_current_ns);
  ot_def_native(vm, "*expander*", nat_expander);
  ot_def_native(vm, "expander-lexical?", nat_expander_lexical);
  ot_def_native(vm, "expander-macro-var", nat_expander_macro_var);
  ot_set_current_ns(vm, saved);
}
