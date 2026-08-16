// builtins/cond.c — conditions and restarts (spec 8).
#include "../builtins.h"

static Value build_condition(State* vm, const char* who, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, who, argc, 1, UINT32_MAX));
  Value first = vm->stack.data[base];
  if (first.tag != Tag_String) {
    if (argc > 1) return raise_error(vm, "extra arguments after a non-string condition");
    return first;
  }
  OT_SCOPE(vm);
  Ref c = ref_push(vm, make_table(vm));
  table_put(vm, ref_get(vm, c), keyword_v(vm->syms.kwType), symbol_v(vm->syms.error_));
  table_put(vm, ref_get(vm, c), keyword_v(vm->syms.kwMessage), vm->stack.data[base]);
  if (argc > 1) {
    Ref data = ref_push(vm, make_array(vm, argc - 1));
    for (u32 i = 1; i < argc; i++)
      array_push(vm, ref_get(vm, data), vm->stack.data[base + i]);
    table_put(vm, ref_get(vm, c), keyword_v(vm->syms.kwData), ref_get(vm, data));
  }
  return ref_get(vm, c);
}

static Value nat_signal(State* vm, u32 base, u32 argc) {
  Value c = build_condition(vm, "signal", base, argc);
  OT_TRY(c);
  return signal_value(vm, c, false);
}

static Value nat_error(State* vm, u32 base, u32 argc) {
  Value c = build_condition(vm, "error", base, argc);
  OT_TRY(c);
  return signal_value(vm, c, true);
}

static Value nat_invoke_restart(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "invoke-restart", argc, 1, UINT32_MAX));
  Value which = vm->stack.data[base];
  Value target = nil_v();
  if (which.tag == Tag_Restart) {
    for (u32 i = vm->restarts.len; i-- > 0;)
      if (vm->restarts.data[i].restart.obj == which.obj) {
        target = which;
        break;
      }
    if (is_nil(target)) return raise_error(vm, "invoke-restart: restart is no longer active");
  } else {
    u32 nid = name_id_of(vm, which);
    if (!nid) return raise_error(vm, "invoke-restart: bad restart name");
    for (u32 i = vm->restarts.len; i-- > 0;) {
      if (as_restart(vm->restarts.data[i].restart)->name == nid) {
        target = vm->restarts.data[i].restart;
        break;
      }
    }
    if (is_nil(target)) return raise_error_sym(vm, "no active restart named %.*s", nid);
  }
  // Read the id before list_from_stack allocates — `target` is a raw local
  // and would dangle across the collect.
  u64 rid = as_restart(target)->restartId;
  vm->unwindRestartArgs = list_from_stack(vm, base + 1, argc - 1);
  vm->unwindRestartId = rid;
  vm->unwindCondition = nil_v();
  vm->unwindKind = UnwindKind_Restart;
  return unwind_v();
}

static Value nat_compute_restarts(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "compute-restarts", argc, 0, 0));
  OT_SCOPE(vm);
  Ref arr = ref_push(vm, make_array(vm, vm->restarts.len));
  for (u32 i = vm->restarts.len; i-- > 0;)                        // innermost first
    array_push(vm, ref_get(vm, arr), vm->restarts.data[i].restart);
  return ref_get(vm, arr);
}

static Value nat_find_restart(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "find-restart", argc, 1, UINT32_MAX));
  u32 nid = name_id_of(vm, vm->stack.data[base]);
  if (!nid) return raise_error(vm, "find-restart: bad name");
  for (u32 i = vm->restarts.len; i-- > 0;)
    if (as_restart(vm->restarts.data[i].restart)->name == nid) return vm->restarts.data[i].restart;
  return nil_v();
}

static Value nat_restart_name(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "restart-name", argc, 1, 1));
  OT_TRY(need_restart(vm, "restart-name", vm->stack.data[base]));
  return symbol_v(as_restart(vm->stack.data[base])->name);
}

static Value nat_restart_description(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "restart-description", argc, 1, 1));
  OT_TRY(need_restart(vm, "restart-description", vm->stack.data[base]));
  return as_restart(vm->stack.data[base])->description;
}

static Value nat_define_condition(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "define-condition", argc, 2, 2));
  u32 tid = name_id_of(vm, vm->stack.data[base]);
  if (!tid) return raise_error(vm, "define-condition: bad type");
  Value parent = vm->stack.data[base + 1];
  if (is_nil(parent)) {
    table_put(vm, vm->typeParents, symbol_v(tid), nil_v());  // delete = root
    return symbol_v(tid);
  }
  u32 pid = name_id_of(vm, parent);
  if (!pid) return raise_error(vm, "define-condition: bad parent");
  // cycle check: would tid become its own ancestor?
  u32 cur = pid;
  for (u32 guard = 0; guard < 1000; guard++) {
    if (cur == tid) return raise_error(vm, "define-condition: cycle");
    Value nxt = table_get(vm, vm->typeParents, symbol_v(cur));
    if (nxt.tag != Tag_Symbol) break;
    cur = nxt.id;
  }
  table_put(vm, vm->typeParents, symbol_v(tid), symbol_v(pid));
  return symbol_v(tid);
}

static Value nat_condition_of_type(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "condition-of-type?", argc, 2, 2));
  Value c = vm->stack.data[base];
  u32 tid = name_id_of(vm, vm->stack.data[base + 1]);
  if (c.tag != Tag_Table) return bool_v(false);
  Value ct = table_get(vm, c, keyword_v(vm->syms.kwType));
  if (ct.tag != Tag_Symbol && ct.tag != Tag_Keyword) return bool_v(false);
  u32 cur = ct.id;
  for (u32 guard = 0; guard < 1000; guard++) {
    if (cur == tid) return bool_v(true);
    Value nxt = table_get(vm, vm->typeParents, symbol_v(cur));
    if (nxt.tag != Tag_Symbol) return bool_v(false);
    cur = nxt.id;
  }
  return bool_v(false);
}

void register_cond(State* vm) {
  u32 saved = vm->currentNs;
  vm->currentNs = vm->syms.otiumCore_;
  def_native(vm, "signal", nat_signal);
  def_native(vm, "error", nat_error);
  def_native(vm, "invoke-restart", nat_invoke_restart);
  def_native(vm, "compute-restarts", nat_compute_restarts);
  def_native(vm, "find-restart", nat_find_restart);
  def_native(vm, "restart-name", nat_restart_name);
  def_native(vm, "restart-description", nat_restart_description);
  def_native(vm, "define-condition", nat_define_condition);
  def_native(vm, "condition-of-type?", nat_condition_of_type);
  vm->currentNs = saved;
}
