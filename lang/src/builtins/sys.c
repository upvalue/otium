// builtins/sys.c — type predicates (10.2), output (10.7), functions and
// evaluation (10.8), namespaces (10.10).
#include "../builtins.h"

// ---------------------------------------------------------------------------
// Predicates.

#define TAG_PRED(cname, lname, expr)                                                               \
  static Value cname(State* vm, u32 base, u32 argc) {                                              \
    OT_TRY(need_argc(vm, lname, argc, 1, 1));                                                      \
    Tag t = ot_tag(vm, ARG(0));                                                                    \
    (void)t;                                                                                       \
    return bool_v(expr);                                                                           \
  }

TAG_PRED(nat_nilp, "nil?", t == Tag_Nil)
TAG_PRED(nat_nullp, "null?", t == Tag_Null)
TAG_PRED(nat_booleanp, "boolean?", t == Tag_True || t == Tag_False)
TAG_PRED(nat_intp, "int?", t == Tag_Int)
TAG_PRED(nat_floatp, "float?", t == Tag_Float)
TAG_PRED(nat_numberp, "number?", t == Tag_Int || t == Tag_Float)
TAG_PRED(nat_symbolp, "symbol?", t == Tag_Symbol)
TAG_PRED(nat_keywordp, "keyword?", t == Tag_Keyword)
TAG_PRED(nat_stringp, "string?", t == Tag_String)
TAG_PRED(nat_pairp, "pair?", t == Tag_Pair)
TAG_PRED(nat_arrayp, "array?", t == Tag_Array)
TAG_PRED(nat_tablep, "table?", t == Tag_Table)
TAG_PRED(nat_bufferp, "buffer?", t == Tag_Buffer)
TAG_PRED(nat_foreignp, "foreign?", t == Tag_Foreign)
TAG_PRED(nat_macrop, "macro?", t == Tag_Macro)
TAG_PRED(nat_procedurep, "procedure?", t == Tag_Function)
TAG_PRED(nat_truep, "true?", t == Tag_True)
TAG_PRED(nat_falsep, "false?", t == Tag_False)

static Value nat_listp(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "list?", argc, 1, 1));
  OT_SCOPE(vm);
  Ref p = ot_push_copy(vm, ARG(0));
  while (ot_tag(vm, p) == Tag_Pair) ot_cdr(vm, p, p);
  return bool_v(ot_tag(vm, p) == Tag_Null);
}

static Value nat_not(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "not", argc, 1, 1));
  return bool_v(!ot_truthy(vm, ARG(0)));
}

static Value nat_eqp(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "eq?", argc, 2, 2));
  return bool_v(ot_eq(vm, ARG(0), ARG(1)));
}

static Value nat_equalp(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "equal?", argc, 2, 2));
  return bool_v(ot_equal(vm, ARG(0), ARG(1)));
}

static const char* type_name(Tag t) {
  switch (t) {
    case Tag_Nil: return "nil";
    case Tag_Null: return "null";
    case Tag_False:
    case Tag_True: return "boolean";
    case Tag_Int: return "int";
    case Tag_Float: return "float";
    case Tag_Symbol: return "symbol";
    case Tag_Keyword: return "keyword";
    case Tag_String: return "string";
    case Tag_Pair: return "pair";
    case Tag_Array: return "array";
    case Tag_Table: return "table";
    case Tag_Buffer: return "buffer";
    case Tag_Code: return "code";
    case Tag_Function: return "function";
    case Tag_Macro: return "macro";
    case Tag_Param: return "param";
    case Tag_Restart: return "restart";
    case Tag_Foreign: return "foreign";
    default: return "unknown";
  }
}

static Value nat_type(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "type", argc, 1, 1));
  const char* n = type_name(ot_tag(vm, ARG(0)));
  return keyword_v(ot_intern(vm, n, (u32)strlen(n)));
}

// ---------------------------------------------------------------------------
// Output (10.7). All through the host's write seam.

static Value print_all(State* vm, u32 base, u32 argc, bool repr, bool nl) {
  Buf out = {0};
  for (u32 i = 0; i < argc; i++) {
    if (i) vec_push(&out, ' ');
    if (repr) ot_repr(vm, ARG(i), &out);
    else ot_display(vm, ARG(i), &out);
  }
  if (nl) vec_push(&out, '\n');
  ot_write_out(vm, out.data, out.len);
  buf_deinit(&out);
  return nil_v();
}

static Value nat_display(State* vm, u32 base, u32 argc) {
  return print_all(vm, base, argc, false, false);
}
static Value nat_write(State* vm, u32 base, u32 argc) {
  return print_all(vm, base, argc, true, false);
}
static Value nat_println(State* vm, u32 base, u32 argc) {
  return print_all(vm, base, argc, false, true);
}

static Value nat_newline(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "newline", argc, 0, 0));
  ot_write_out(vm, "\n", 1);
  return nil_v();
}

// ---------------------------------------------------------------------------
// Functions and evaluation (10.8).

static Value nat_identity(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "identity", argc, 1, 1));
  return ot_ret(vm, ARG(0));
}

static Value nat_quit(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "quit", argc, 0, 0));
  return ot_start_quit(vm);
}

static Value nat_apply(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "apply", argc, 2, UINT32_MAX));
  OT_SCOPE(vm);
  Ref result = ot_push(vm);
  Ref cursor = ot_push_copy(vm, ARG(argc - 1));
  Ref item = ot_push(vm);
  SeqIter iter;
  seq_iter_init(&iter, vm, cursor);
  u32 argBase = ot_top(vm);
  for (u32 i = 1; i + 1 < argc; i++) ot_push_copy(vm, ARG(i));
  for (;;) {
    SeqStep step = seq_iter_next(&iter, item);
    if (step == SeqStep_End) break;
    if (step != SeqStep_Item) return sequence_error(vm, "apply", step);
    ot_push_copy(vm, item);
  }
  OT_TRY(ot_apply(vm, result, ARG(0), argBase, ot_top(vm) - argBase));
  return ot_ret(vm, result);
}

// One-argument callback in its own scope: a second OT_SCOPE inside nat_for_each
// would shadow the first.
static Value call_with_item(State* vm, Ref fn, Ref item) {
  OT_SCOPE(vm);
  Ref result = ot_push(vm);
  u32 argBase = ot_top(vm);
  ot_push_copy(vm, item);
  return ot_apply(vm, result, fn, argBase, 1);
}

static Value nat_for_each(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "for-each", argc, 2, 2));
  OT_SCOPE(vm);
  Ref cursor = ot_push_copy(vm, ARG(1));
  Ref item = ot_push(vm);
  SeqIter iter;
  seq_iter_init(&iter, vm, cursor);
  for (;;) {
    SeqStep step = seq_iter_next(&iter, item);
    if (step == SeqStep_End) return nil_v();
    if (step != SeqStep_Item) return sequence_error(vm, "for-each", step);
    OT_TRY(call_with_item(vm, ARG(0), item));
  }
}

static Value nat_eval(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "eval", argc, 1, 1));
  OT_SCOPE(vm);
  Ref result = ot_push(vm);
  OT_TRY(ot_eval(vm, result, ARG(0)));
  return ot_ret(vm, result);
}

static Value nat_read_string(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "read-string", argc, 1, 1));
  OT_TRY(need_string(vm, "read-string", ARG(0)));
  OT_SCOPE(vm);
  Ref form = ot_push(vm);
  u32 outcome = 0;
  OT_TRY(ot_read_string(vm, form, ARG(0), &outcome));
  if (outcome == 1) return raise_error(vm, "read-string: empty input");
  if (outcome == 2) return raise_error(vm, "read-string: trailing input");
  return ot_ret(vm, form);
}

// If form is (sym args...) and sym resolves to a macro, expand once into dst.
// Returns bool via *expanded; nil or an unwind as the return value.
static Value expand_once(State* vm, Ref dst, Ref form, bool* expanded) {
  *expanded = false;
  if (ot_tag(vm, form) != Tag_Pair) {
    ot_copy(vm, dst, form);
    return nil_v();
  }
  OT_SCOPE(vm);
  Ref formS = ot_push_copy(vm, form);  // dst may alias form
  Ref head = ot_push(vm);
  ot_car(vm, head, formS);
  if (ot_tag(vm, head) != Tag_Symbol) {
    ot_copy(vm, dst, formS);
    return nil_v();
  }
  Ref callee = ot_push(vm);
  Value resolved = ot_resolve(vm, callee, head);
  if (resolved.tag == Tag_Unwind) {
    // unresolvable head is not a macro call; swallow the unwind
    ot_cancel_unwind(vm);
    ot_copy(vm, dst, formS);
    return nil_v();
  }
  if (ot_tag(vm, callee) != Tag_Macro) {
    ot_copy(vm, dst, formS);
    return nil_v();
  }
  // push unevaluated argument forms and call the macro
  Ref result = ot_push(vm);
  Ref cursor = ot_push(vm);
  ot_cdr(vm, cursor, formS);
  u32 cbase = ot_top(vm);
  u32 n = 0;
  while (ot_tag(vm, cursor) == Tag_Pair) {
    Ref arg = ot_push(vm);
    ot_car(vm, arg, cursor);
    ot_cdr(vm, cursor, cursor);
    n++;
  }
  OT_TRY(ot_apply(vm, result, callee, cbase, n));
  *expanded = true;
  ot_copy(vm, dst, result);
  return nil_v();
}

static Value nat_macroexpand_1(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "macroexpand-1", argc, 1, 1));
  bool e;
  OT_SCOPE(vm);
  Ref out = ot_push(vm);
  OT_TRY(expand_once(vm, out, ARG(0), &e));
  return ot_ret(vm, out);
}

static Value nat_macroexpand(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "macroexpand", argc, 1, 1));
  OT_SCOPE(vm);
  Ref form = ot_push_copy(vm, ARG(0));
  for (;;) {
    bool e;
    OT_TRY(expand_once(vm, form, form, &e));
    if (!e) return ot_ret(vm, form);
  }
}

// "ns/name: docstring" for a resolvable var (spec 10.10), nil otherwise.
static Value nat_describe(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "describe", argc, 1, 1));
  OT_TRY(need_symbol(vm, "describe", ARG(0)));
  OT_SCOPE(vm);
  Ref var = ot_push(vm);
  if (!ot_resolve_var(vm, var, ARG(0))) return nil_v();
  Ref field = ot_push(vm);
  Buf out = {0};
  ot_array_get(vm, field, var, OT_VAR_NS);
  ot_display(vm, field, &out);
  vec_push(&out, '/');
  ot_array_get(vm, field, var, OT_VAR_NAME);
  ot_display(vm, field, &out);
  ot_array_get(vm, field, var, OT_VAR_DOC);
  if (!ot_nil(vm, field)) {
    buf_append_cstr(&out, ": ");
    ot_display(vm, field, &out);
  }
  Ref result = ot_push(vm);
  ot_make_string_buf(vm, result, &out);
  buf_deinit(&out);
  return ot_ret(vm, result);
}

// ---------------------------------------------------------------------------

void register_sys(State* vm) {
  ot_def_native(vm, "nil?", nat_nilp);
  ot_def_native(vm, "null?", nat_nullp);
  ot_def_native(vm, "boolean?", nat_booleanp);
  ot_def_native(vm, "int?", nat_intp);
  ot_def_native(vm, "float?", nat_floatp);
  ot_def_native(vm, "number?", nat_numberp);
  ot_def_native(vm, "symbol?", nat_symbolp);
  ot_def_native(vm, "keyword?", nat_keywordp);
  ot_def_native(vm, "string?", nat_stringp);
  ot_def_native(vm, "pair?", nat_pairp);
  ot_def_native(vm, "array?", nat_arrayp);
  ot_def_native(vm, "table?", nat_tablep);
  ot_def_native(vm, "buffer?", nat_bufferp);
  ot_def_native(vm, "foreign?", nat_foreignp);
  ot_def_native(vm, "macro?", nat_macrop);
  ot_def_native(vm, "procedure?", nat_procedurep);
  ot_def_native(vm, "list?", nat_listp);
  ot_def_native(vm, "true?", nat_truep);
  ot_def_native(vm, "false?", nat_falsep);
  ot_def_native(vm, "not", nat_not);
  ot_def_native(vm, "eq?", nat_eqp);
  ot_def_native(vm, "equal?", nat_equalp);
  ot_def_native(vm, "type", nat_type);
  ot_def_native(vm, "display", nat_display);
  ot_def_native(vm, "print", nat_display);
  ot_def_native(vm, "write", nat_write);
  ot_def_native(vm, "println", nat_println);
  ot_def_native(vm, "newline", nat_newline);
  ot_def_native(vm, "apply", nat_apply);
  ot_def_native(vm, "for-each", nat_for_each);
  ot_def_native(vm, "identity", nat_identity);
  ot_def_native(vm, "quit", nat_quit);
  ot_def_native(vm, "exit", nat_quit);
  ot_def_native(vm, "eval", nat_eval);
  ot_def_native(vm, "read-string", nat_read_string);
  // gensym and current-ns are registered by register_expand (they use
  // the State's counter and current-ns field); don't shadow them here.
  ot_def_native(vm, "macroexpand-1", nat_macroexpand_1);
  ot_def_native(vm, "macroexpand", nat_macroexpand);
  ot_def_native(vm, "describe", nat_describe);
}
