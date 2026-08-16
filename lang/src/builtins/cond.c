// builtins/cond.c — conditions and restarts (spec 8).
#include "../builtins.h"

// Build the condition for signal/error into dst: a bare non-string value
// passes through; a string becomes {:type 'error :message s :data [rest]}.
static Value build_condition(State* vm, const char* who, u32 base, u32 argc, Ref dst) {
  OT_TRY(need_argc(vm, who, argc, 1, UINT32_MAX));
  if (ot_tag(vm, ARG(0)) != Tag_String) {
    if (argc > 1) return raise_error(vm, "extra arguments after a non-string condition");
    ot_copy(vm, dst, ARG(0));
    return nil_v();
  }
  const Syms* syms = ot_syms(vm);
  ot_make_table(vm, dst);
  ot_table_put_im2(vm, dst, keyword_v(syms->kwType), symbol_v(syms->error_));
  ot_table_put_im(vm, dst, keyword_v(syms->kwMessage), ARG(0));
  if (argc > 1) {
    OT_SCOPE(vm);
    Ref data = ot_push(vm);
    ot_make_array(vm, data, argc - 1);
    for (u32 i = 1; i < argc; i++) ot_array_push(vm, data, ARG(i));
    ot_table_put_im(vm, dst, keyword_v(syms->kwData), data);
  }
  return nil_v();
}

static Value nat_signal(State* vm, u32 base, u32 argc) {
  OT_SCOPE(vm);
  Ref c = ot_push(vm);
  OT_TRY(build_condition(vm, "signal", base, argc, c));
  return ot_signal(vm, c, false);
}

static Value nat_error(State* vm, u32 base, u32 argc) {
  OT_SCOPE(vm);
  Ref c = ot_push(vm);
  OT_TRY(build_condition(vm, "error", base, argc, c));
  return ot_signal(vm, c, true);
}

// Find the innermost active restart named `nid` into dst; false on miss.
static bool find_restart_named(State* vm, Ref dst, u32 nid) {
  for (u32 i = 0; i < ot_restart_count(vm); i++) {
    ot_restart_at(vm, dst, i);
    if (ot_restart_name(vm, dst) == nid) return true;
  }
  return false;
}

static Value nat_invoke_restart(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "invoke-restart", argc, 1, UINT32_MAX));
  OT_SCOPE(vm);
  Ref target = ot_push(vm);
  if (ot_tag(vm, ARG(0)) == Tag_Restart) {
    if (!ot_restart_active(vm, ARG(0)))
      return raise_error(vm, "invoke-restart: restart is no longer active");
    ot_copy(vm, target, ARG(0));
  } else {
    u32 nid = ot_name_id(vm, ARG(0));
    if (!nid) return raise_error(vm, "invoke-restart: bad restart name");
    if (!find_restart_named(vm, target, nid))
      return raise_error_sym(vm, "no active restart named %.*s", nid);
  }
  return ot_invoke_restart(vm, target, base + 1, argc - 1);
}

static Value nat_compute_restarts(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "compute-restarts", argc, 0, 0));
  OT_SCOPE(vm);
  Ref arr = ot_push(vm);
  ot_make_array(vm, arr, ot_restart_count(vm));
  Ref r = ot_push(vm);
  for (u32 i = 0; i < ot_restart_count(vm); i++) {  // innermost first
    ot_restart_at(vm, r, i);
    ot_array_push(vm, arr, r);
  }
  return ot_ret(vm, arr);
}

static Value nat_find_restart(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "find-restart", argc, 1, UINT32_MAX));
  u32 nid = ot_name_id(vm, ARG(0));
  if (!nid) return raise_error(vm, "find-restart: bad name");
  OT_SCOPE(vm);
  Ref r = ot_push(vm);
  if (!find_restart_named(vm, r, nid)) return nil_v();
  return ot_ret(vm, r);
}

static Value nat_restart_name(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "restart-name", argc, 1, 1));
  OT_TRY(need_restart(vm, "restart-name", ARG(0)));
  return symbol_v(ot_restart_name(vm, ARG(0)));
}

static Value nat_restart_description(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "restart-description", argc, 1, 1));
  OT_TRY(need_restart(vm, "restart-description", ARG(0)));
  ot_restart_description(vm, ARG(0), ARG(0));
  return ot_ret(vm, ARG(0));
}

static Value nat_define_condition(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "define-condition", argc, 2, 2));
  u32 tid = ot_name_id(vm, ARG(0));
  if (!tid) return raise_error(vm, "define-condition: bad type");
  Ref parents = ot_type_parents(vm);
  if (ot_nil(vm, ARG(1))) {
    ot_table_put_im2(vm, parents, symbol_v(tid), nil_v());  // delete = root
    return symbol_v(tid);
  }
  u32 pid = ot_name_id(vm, ARG(1));
  if (!pid) return raise_error(vm, "define-condition: bad parent");
  // cycle check: would tid become its own ancestor?
  OT_SCOPE(vm);
  Ref next = ot_push(vm);
  u32 cur = pid;
  for (u32 guard = 0; guard < 1000; guard++) {
    if (cur == tid) return raise_error(vm, "define-condition: cycle");
    ot_table_get_im(vm, next, parents, symbol_v(cur));
    if (ot_tag(vm, next) != Tag_Symbol) break;
    cur = ot_id(vm, next);
  }
  ot_table_put_im2(vm, parents, symbol_v(tid), symbol_v(pid));
  return symbol_v(tid);
}

static Value nat_condition_of_type(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "condition-of-type?", argc, 2, 2));
  u32 tid = ot_name_id(vm, ARG(1));
  if (ot_tag(vm, ARG(0)) != Tag_Table) return bool_v(false);
  OT_SCOPE(vm);
  Ref ct = ot_push(vm);
  ot_table_get_im(vm, ct, ARG(0), keyword_v(ot_syms(vm)->kwType));
  Tag t = ot_tag(vm, ct);
  if (t != Tag_Symbol && t != Tag_Keyword) return bool_v(false);
  Ref parents = ot_type_parents(vm);
  u32 cur = ot_id(vm, ct);
  Ref next = ot_push(vm);
  for (u32 guard = 0; guard < 1000; guard++) {
    if (cur == tid) return bool_v(true);
    ot_table_get_im(vm, next, parents, symbol_v(cur));
    if (ot_tag(vm, next) != Tag_Symbol) return bool_v(false);
    cur = ot_id(vm, next);
  }
  return bool_v(false);
}

void register_cond(State* vm) {
  u32 saved = ot_current_ns(vm);
  ot_set_current_ns(vm, ot_syms(vm)->otiumCore_);
  ot_def_native(vm, "signal", nat_signal);
  ot_def_native(vm, "error", nat_error);
  ot_def_native(vm, "invoke-restart", nat_invoke_restart);
  ot_def_native(vm, "compute-restarts", nat_compute_restarts);
  ot_def_native(vm, "find-restart", nat_find_restart);
  ot_def_native(vm, "restart-name", nat_restart_name);
  ot_def_native(vm, "restart-description", nat_restart_description);
  ot_def_native(vm, "define-condition", nat_define_condition);
  ot_def_native(vm, "condition-of-type?", nat_condition_of_type);
  ot_set_current_ns(vm, saved);
}
