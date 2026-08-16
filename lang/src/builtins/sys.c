// builtins/sys.c — type predicates (10.2), output (10.7), functions and
// evaluation (10.8), namespaces (10.10), plus def_native.
#include "../builtins.h"
#include "../state.h"
#include "../ns.h"
#include "../heap.h"
#include "../eval.h"  // FunctionData, eval_form, apply
#include "../reader.h"
#include "../printer.h"
#include "../intern.h"
#include "../sequence.h"

// ---------------------------------------------------------------------------
// def_native — wrap a NativeFn in a Function object, define it in currentNs.

void def_native(State* vm, const char* name, NativeFn f) {
  u32 sc = scope_begin(vm);
  Slot native = scope_push(vm, make_native(vm, name, f));
  ns_define(vm, intern_id(&vm->intern, name, (u32)strlen(name)), slot_get(native), false, nil_v());
  scope_pop_to(vm, sc);
}

// ---------------------------------------------------------------------------
// Predicates.

#define TAG_PRED(cname, lname, expr)                                                               \
  static Value cname(State* vm, u32 base, u32 argc) {                                              \
    OT_TRY(need_argc(vm, lname, argc, 1, 1));                                                      \
    Value v = ARG(0);                                                                              \
    (void)v;                                                                                       \
    return bool_v(expr);                                                                           \
  }

TAG_PRED(nat_nilp, "nil?", v.tag == Tag_Nil)
TAG_PRED(nat_nullp, "null?", v.tag == Tag_Null)
TAG_PRED(nat_booleanp, "boolean?", v.tag == Tag_True || v.tag == Tag_False)
TAG_PRED(nat_intp, "int?", v.tag == Tag_Int)
TAG_PRED(nat_floatp, "float?", v.tag == Tag_Float)
TAG_PRED(nat_numberp, "number?", v.tag == Tag_Int || v.tag == Tag_Float)
TAG_PRED(nat_symbolp, "symbol?", v.tag == Tag_Symbol)
TAG_PRED(nat_keywordp, "keyword?", v.tag == Tag_Keyword)
TAG_PRED(nat_stringp, "string?", v.tag == Tag_String)
TAG_PRED(nat_pairp, "pair?", v.tag == Tag_Pair)
TAG_PRED(nat_arrayp, "array?", v.tag == Tag_Array)
TAG_PRED(nat_tablep, "table?", v.tag == Tag_Table)
TAG_PRED(nat_bufferp, "buffer?", v.tag == Tag_Buffer)
TAG_PRED(nat_foreignp, "foreign?", v.tag == Tag_Foreign)
TAG_PRED(nat_macrop, "macro?", v.tag == Tag_Macro)
TAG_PRED(nat_procedurep, "procedure?", v.tag == Tag_Function)
TAG_PRED(nat_truep, "true?", v.tag == Tag_True)
TAG_PRED(nat_falsep, "false?", v.tag == Tag_False)

static Value nat_listp(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "list?", argc, 1, 1));
  Value v = ARG(0);
  while (v.tag == Tag_Pair) v = as_pair(v)->cdr;
  return bool_v(v.tag == Tag_Null);
}

static Value nat_not(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "not", argc, 1, 1));
  return bool_v(is_falsy(ARG(0)));
}

static Value nat_eqp(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "eq?", argc, 2, 2));
  return bool_v(val_eq(ARG(0), ARG(1)));
}

static Value nat_equalp(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "equal?", argc, 2, 2));
  return bool_v(val_equal(vm, ARG(0), ARG(1)));
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
  const char* n = type_name(ARG(0).tag);
  return keyword_v(intern_id(&vm->intern, n, (u32)strlen(n)));
}

// ---------------------------------------------------------------------------
// Output (10.7). All through vm->writeFn.

static void write_out(State* vm, Buf* b) {
  if (vm->writeFn && b->len) vm->writeFn(vm->writeUd, b->data, b->len);
}

static Value print_all(State* vm, u32 base, u32 argc, bool repr, bool nl) {
  Buf out = {0};
  for (u32 i = 0; i < argc; i++) {
    if (i) vec_push(&out, ' ');
    if (repr) print_repr(vm, ARG(i), &out);
    else print_display(vm, ARG(i), &out);
  }
  if (nl) vec_push(&out, '\n');
  write_out(vm, &out);
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
  Buf out = {0};
  vec_push(&out, '\n');
  write_out(vm, &out);
  buf_deinit(&out);
  return nil_v();
}

// ---------------------------------------------------------------------------
// Functions and evaluation (10.8).

static Value nat_identity(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "identity", argc, 1, 1));
  return ARG(0);
}

static Value nat_quit(State* vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "quit", argc, 0, 0));
  return start_quit(vm);
}

static Value nat_apply(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "apply", argc, 2, UINT32_MAX));
  u32 sc = scope_begin(vm);
  Slot cursor = scope_push(vm, ARG(argc - 1));
  Slot item = scope_push(vm, nil_v());
  SeqIter iter;
  seq_iter_init(&iter, cursor);
  u32 argBase = vm->stack.len;
  for (u32 i = 1; i + 1 < argc; i++) state_push(vm, ARG(i));
  for (;;) {
    SeqStep step = seq_iter_next(&iter, item);
    if (step == SeqStep_End) break;
    if (step != SeqStep_Item) return scope_exit(vm, sc, sequence_error(vm, "apply", step));
    state_push(vm, slot_get(item));
  }
  return scope_exit(vm, sc, apply(vm, ARG(0), argBase, vm->stack.len - argBase));
}

static Value nat_for_each(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "for-each", argc, 2, 2));
  u32 sc = scope_begin(vm);
  Slot cursor = scope_push(vm, ARG(1));
  Slot item = scope_push(vm, nil_v());
  SeqIter iter;
  seq_iter_init(&iter, cursor);
  for (;;) {
    SeqStep step = seq_iter_next(&iter, item);
    if (step == SeqStep_End) return scope_exit(vm, sc, nil_v());
    if (step != SeqStep_Item) return scope_exit(vm, sc, sequence_error(vm, "for-each", step));
    Value result;
    {
      u32 call = scope_begin(vm);
      scope_push(vm, slot_get(item));
      result = apply(vm, ARG(0), call, 1);
      scope_pop_to(vm, call);
    }
    OT_TRYS(vm, sc, result);
  }
}

static Value nat_eval(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "eval", argc, 1, 1));
  return eval_form(vm, ARG(0));
}

static Value nat_read_string(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "read-string", argc, 1, 1));
  OT_TRY(need_string(vm, "read-string", ARG(0)));
  // Snapshot the source into a C-heap Buf: the Reader keeps a raw pointer to
  // the source for its whole (allocating) lifetime, so it must never point
  // into the GC heap.
  StringData* s = as_string(ARG(0));
  u32 srcNchars = s->nchars;
  Buf src = {0};
  buf_append(&src, string_data_bytes(s), s->len);
  Reader r;
  reader_init(&r, vm, src.data ? src.data : "", src.len, "<read-string>");
  Value form = reader_next(&r);
  if (form.tag == Tag_Unwind) {
    buf_deinit(&src);
    return form;
  }
  if (reader_at_eof(&r) && is_nil(form) && srcNchars == 0) {
    buf_deinit(&src);
    return raise_error(vm, "read-string: empty input");
  }
  // Reader returns nil for both EOF and a nil literal, so only a zero-byte
  // source can be identified as empty here.
  u32 sc = scope_begin(vm);
  Slot formS = scope_push(vm, form);
  Value trailing = reader_next(&r);
  if (trailing.tag == Tag_Unwind) {
    buf_deinit(&src);
    return scope_exit(vm, sc, trailing);
  }
  if (!reader_at_eof(&r)) {
    buf_deinit(&src);
    return scope_exit(vm, sc, raise_error(vm, "read-string: trailing input"));
  }
  buf_deinit(&src);
  return scope_exit(vm, sc, slot_get(formS));
}

// If form is (sym args...) and sym resolves to a macro, expand once.
// Returns bool via *expanded; result (or original form) as return value.
static Value expand_once(State* vm, Value form, bool* expanded) {
  *expanded = false;
  if (form.tag != Tag_Pair) return form;
  Value head = as_pair(form)->car;
  if (head.tag != Tag_Symbol) return form;
  u32 sc = scope_begin(vm);
  Slot formS = scope_push(vm, form);  // ns_resolve allocates
  Value callee = ns_resolve(vm, head);
  if (callee.tag == Tag_Unwind) {
    // unresolvable head is not a macro call; swallow the unwind
    state_cancel_unwind(vm);
    return scope_exit(vm, sc, slot_get(formS));
  }
  if (callee.tag != Tag_Macro) return scope_exit(vm, sc, slot_get(formS));
  // push unevaluated argument forms and call the macro (state_push doesn't
  // allocate on the GC heap, so walking the form while pushing is safe)
  Slot calleeS = scope_push(vm, callee);
  u32 cbase = vm->stack.len;
  u32 n = 0;
  for (Value p = as_pair(slot_get(formS))->cdr; p.tag == Tag_Pair; p = as_pair(p)->cdr) {
    state_push(vm, as_pair(p)->car);
    n++;
  }
  Value r = apply(vm, slot_get(calleeS), cbase, n);
  if (r.tag == Tag_Unwind) return scope_exit(vm, sc, r);
  *expanded = true;
  return scope_exit(vm, sc, r);
}

static Value nat_macroexpand_1(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "macroexpand-1", argc, 1, 1));
  bool e;
  return expand_once(vm, ARG(0), &e);
}

static Value nat_macroexpand(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "macroexpand", argc, 1, 1));
  u32 sc = scope_begin(vm);
  Slot formS = scope_push(vm, ARG(0));
  for (;;) {
    bool e;
    Value next = expand_once(vm, slot_get(formS), &e);
    if (next.tag == Tag_Unwind || !e) return scope_exit(vm, sc, next);
    slot_set(formS, next);
  }
}

// "ns/name: docstring" for a resolvable var (spec 10.10), nil otherwise.
static Value nat_describe(State* vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "describe", argc, 1, 1));
  OT_TRY(need_symbol(vm, "describe", ARG(0)));
  Value var = ns_resolve_var(vm, ARG(0));
  if (is_nil(var)) return nil_v();
  Buf out = {0};
  print_display(vm, array_get(var, VAR_NS), &out);
  vec_push(&out, '/');
  print_display(vm, array_get(var, VAR_NAME), &out);
  Value doc = array_get(var, VAR_DOC);
  if (!is_nil(doc)) {
    buf_append_cstr(&out, ": ");
    print_display(vm, doc, &out);
  }
  Value result = make_string_buf(vm, &out);
  buf_deinit(&out);
  return result;
}

// ---------------------------------------------------------------------------

void register_sys(State* vm) {
  def_native(vm, "nil?", nat_nilp);
  def_native(vm, "null?", nat_nullp);
  def_native(vm, "boolean?", nat_booleanp);
  def_native(vm, "int?", nat_intp);
  def_native(vm, "float?", nat_floatp);
  def_native(vm, "number?", nat_numberp);
  def_native(vm, "symbol?", nat_symbolp);
  def_native(vm, "keyword?", nat_keywordp);
  def_native(vm, "string?", nat_stringp);
  def_native(vm, "pair?", nat_pairp);
  def_native(vm, "array?", nat_arrayp);
  def_native(vm, "table?", nat_tablep);
  def_native(vm, "buffer?", nat_bufferp);
  def_native(vm, "foreign?", nat_foreignp);
  def_native(vm, "macro?", nat_macrop);
  def_native(vm, "procedure?", nat_procedurep);
  def_native(vm, "list?", nat_listp);
  def_native(vm, "true?", nat_truep);
  def_native(vm, "false?", nat_falsep);
  def_native(vm, "not", nat_not);
  def_native(vm, "eq?", nat_eqp);
  def_native(vm, "equal?", nat_equalp);
  def_native(vm, "type", nat_type);
  def_native(vm, "display", nat_display);
  def_native(vm, "print", nat_display);
  def_native(vm, "write", nat_write);
  def_native(vm, "println", nat_println);
  def_native(vm, "newline", nat_newline);
  def_native(vm, "apply", nat_apply);
  def_native(vm, "for-each", nat_for_each);
  def_native(vm, "identity", nat_identity);
  def_native(vm, "quit", nat_quit);
  def_native(vm, "exit", nat_quit);
  def_native(vm, "eval", nat_eval);
  def_native(vm, "read-string", nat_read_string);
  // gensym and current-ns are registered by register_expand (they use
  // the State's counter and current-ns field); don't shadow them here.
  def_native(vm, "macroexpand-1", nat_macroexpand_1);
  def_native(vm, "macroexpand", nat_macroexpand);
  def_native(vm, "describe", nat_describe);
}
