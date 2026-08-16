// builtins/sys.cpp — type predicates (10.2), output (10.7), functions and
// evaluation (10.8), namespaces (10.10), plus def_native.
#include "../builtins.hpp"
#include "../state.hpp"
#include "../ns.hpp"
#include "../heap.hpp"
#include "../eval.hpp"  // FunctionData, eval_form, apply
#include "../reader.hpp"
#include "../printer.hpp"
#include "../intern.hpp"
#include "../sequence.hpp"

namespace ot {

// ---------------------------------------------------------------------------
// def_native — wrap a NativeFn in a Function object, define it in currentNs.

void def_native(State& vm, const char* name, NativeFn f) {
  Scope s(vm);
  Slot native = s.push(make_native(vm, name, f));
  ns_define(vm, vm.intern.intern(name, (u32)strlen(name)), native.get(), false, nil_v());
}

// ---------------------------------------------------------------------------
// Predicates.

#define TAG_PRED(cname, lname, expr)                                                               \
  static Value cname(State& vm, u32 base, u32 argc) {                                              \
    OT_TRY(need_argc(vm, lname, argc, 1, 1));                                                      \
    Value v = ARG(0);                                                                              \
    (void)v;                                                                                       \
    return bool_v(expr);                                                                           \
  }

TAG_PRED(nat_nilp, "nil?", v.tag == Tag::Nil)
TAG_PRED(nat_nullp, "null?", v.tag == Tag::Null)
TAG_PRED(nat_booleanp, "boolean?", v.tag == Tag::True || v.tag == Tag::False)
TAG_PRED(nat_intp, "int?", v.tag == Tag::Int)
TAG_PRED(nat_floatp, "float?", v.tag == Tag::Float)
TAG_PRED(nat_numberp, "number?", v.tag == Tag::Int || v.tag == Tag::Float)
TAG_PRED(nat_symbolp, "symbol?", v.tag == Tag::Symbol)
TAG_PRED(nat_keywordp, "keyword?", v.tag == Tag::Keyword)
TAG_PRED(nat_stringp, "string?", v.tag == Tag::String)
TAG_PRED(nat_pairp, "pair?", v.tag == Tag::Pair)
TAG_PRED(nat_arrayp, "array?", v.tag == Tag::Array)
TAG_PRED(nat_tablep, "table?", v.tag == Tag::Table)
TAG_PRED(nat_bufferp, "buffer?", v.tag == Tag::Buffer)
TAG_PRED(nat_foreignp, "foreign?", v.tag == Tag::Foreign)
TAG_PRED(nat_macrop, "macro?", v.tag == Tag::Macro)
TAG_PRED(nat_procedurep, "procedure?", v.tag == Tag::Function)
TAG_PRED(nat_truep, "true?", v.tag == Tag::True)
TAG_PRED(nat_falsep, "false?", v.tag == Tag::False)

static Value nat_listp(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "list?", argc, 1, 1));
  Value v = ARG(0);
  while (v.tag == Tag::Pair) v = as_pair(v)->cdr;
  return bool_v(v.tag == Tag::Null);
}

static Value nat_not(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "not", argc, 1, 1));
  return bool_v(is_falsy(ARG(0)));
}

static Value nat_eqp(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "eq?", argc, 2, 2));
  return bool_v(val_eq(ARG(0), ARG(1)));
}

static Value nat_equalp(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "equal?", argc, 2, 2));
  return bool_v(val_equal(vm, ARG(0), ARG(1)));
}

static const char* type_name(Tag t) {
  switch (t) {
    case Tag::Nil: return "nil";
    case Tag::Null: return "null";
    case Tag::False:
    case Tag::True: return "boolean";
    case Tag::Int: return "int";
    case Tag::Float: return "float";
    case Tag::Symbol: return "symbol";
    case Tag::Keyword: return "keyword";
    case Tag::String: return "string";
    case Tag::Pair: return "pair";
    case Tag::Array: return "array";
    case Tag::Table: return "table";
    case Tag::Buffer: return "buffer";
    case Tag::Code: return "code";
    case Tag::Function: return "function";
    case Tag::Macro: return "macro";
    case Tag::Param: return "param";
    case Tag::Restart: return "restart";
    case Tag::Foreign: return "foreign";
    default: return "unknown";
  }
}

static Value nat_type(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "type", argc, 1, 1));
  const char* n = type_name(ARG(0).tag);
  return keyword_v(vm.intern.intern(n, (u32)strlen(n)));
}

// ---------------------------------------------------------------------------
// Output (10.7). All through vm.writeFn.

static void write_out(State& vm, Buf& b) {
  if (vm.writeFn && b.len) vm.writeFn(vm.writeUd, b.data, b.len);
}

static Value print_all(State& vm, u32 base, u32 argc, bool repr, bool nl) {
  Buf out;
  for (u32 i = 0; i < argc; i++) {
    if (i) out.push(' ');
    if (repr) print_repr(vm, ARG(i), out);
    else print_display(vm, ARG(i), out);
  }
  if (nl) out.push('\n');
  write_out(vm, out);
  return nil_v();
}

static Value nat_display(State& vm, u32 base, u32 argc) {
  return print_all(vm, base, argc, false, false);
}
static Value nat_write(State& vm, u32 base, u32 argc) {
  return print_all(vm, base, argc, true, false);
}
static Value nat_println(State& vm, u32 base, u32 argc) {
  return print_all(vm, base, argc, false, true);
}

static Value nat_newline(State& vm, u32 base, u32 argc) {
  (void)base;
  OT_TRY(need_argc(vm, "newline", argc, 0, 0));
  Buf out;
  out.push('\n');
  write_out(vm, out);
  return nil_v();
}

// ---------------------------------------------------------------------------
// Functions and evaluation (10.8).

static Value nat_identity(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "identity", argc, 1, 1));
  return ARG(0);
}

static Value nat_quit(State& vm, u32, u32 argc) {
  OT_TRY(need_argc(vm, "quit", argc, 0, 0));
  return start_quit(vm);
}

static Value nat_exit(State& vm, u32, u32 argc) {
  OT_TRY(need_argc(vm, "exit", argc, 0, 0));
  return start_quit(vm);
}

static Value nat_apply(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "apply", argc, 2, UINT32_MAX));
  Scope roots(vm);
  Slot cursor = roots.push(ARG(argc - 1));
  Slot item = roots.push();
  SeqIter iter(cursor);
  u32 argBase = vm.stack.len;
  for (u32 i = 1; i + 1 < argc; i++) vm.push(ARG(i));
  for (;;) {
    SeqStep step = iter.next(item);
    if (step == SeqStep::End) break;
    if (step != SeqStep::Item) return sequence_error(vm, "apply", step);
    vm.push(item.get());
  }
  return apply(vm, ARG(0), argBase, vm.stack.len - argBase);
}

static Value nat_for_each(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "for-each", argc, 2, 2));
  Scope roots(vm);
  Slot cursor = roots.push(ARG(1));
  Slot item = roots.push();
  SeqIter iter(cursor);
  for (;;) {
    SeqStep step = iter.next(item);
    if (step == SeqStep::End) return nil_v();
    if (step != SeqStep::Item) return sequence_error(vm, "for-each", step);
    Value result;
    {
      Scope call(vm);
      call.push(item.get());
      result = apply(vm, ARG(0), call.base, 1);
    }
    OT_TRY(result);
  }
}

static Value nat_eval(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "eval", argc, 1, 1));
  return eval_form(vm, ARG(0));
}

static Value nat_read_string(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "read-string", argc, 1, 1));
  OT_TRY(need_string(vm, "read-string", ARG(0)));
  // Snapshot the source into a C-heap Buf: the Reader keeps a raw pointer to
  // the source for its whole (allocating) lifetime, so it must never point
  // into the GC heap.
  StringData* s = as_string(ARG(0));
  u32 srcNchars = s->nchars;
  Buf src;
  src.append(string_bytes(s), s->len);
  Reader r(vm, src.data ? src.data : "", src.len, "<read-string>");
  Value form = r.next();
  OT_TRY(form);
  if (r.atEof() && is_nil(form) && srcNchars == 0)
    return raise_error(vm, "read-string: empty input");
  // Reader returns nil for both EOF and a nil literal, so only a zero-byte
  // source can be identified as empty here.
  Scope sc(vm);
  Slot formS = sc.push(form);
  Value trailing = r.next();
  OT_TRY(trailing);
  if (!r.atEof()) return raise_error(vm, "read-string: trailing input");
  return formS.get();
}

// If form is (sym args...) and sym resolves to a macro, expand once.
// Returns bool via *expanded; result (or original form) as return value.
static Value expand_once(State& vm, Value form, bool* expanded) {
  *expanded = false;
  if (form.tag != Tag::Pair) return form;
  Value head = as_pair(form)->car;
  if (head.tag != Tag::Symbol) return form;
  Scope s(vm);
  Slot formS = s.push(form);  // ns_resolve allocates
  Value callee = ns_resolve(vm, head);
  if (callee.tag == Tag::Unwind) {
    // unresolvable head is not a macro call; swallow the unwind
    state_cancel_unwind(vm);
    return formS.get();
  }
  if (callee.tag != Tag::Macro) return formS.get();
  // push unevaluated argument forms and call the macro (vm.push doesn't
  // allocate on the GC heap, so walking the form while pushing is safe)
  Slot calleeS = s.push(callee);
  u32 cbase = vm.stack.len;
  u32 n = 0;
  for (Value p = as_pair(formS.get())->cdr; p.tag == Tag::Pair; p = as_pair(p)->cdr) {
    vm.push(as_pair(p)->car);
    n++;
  }
  Value r = apply(vm, calleeS.get(), cbase, n);
  if (r.tag == Tag::Unwind) return r;
  *expanded = true;
  return r;
}

static Value nat_macroexpand_1(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "macroexpand-1", argc, 1, 1));
  bool e;
  return expand_once(vm, ARG(0), &e);
}

static Value nat_macroexpand(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "macroexpand", argc, 1, 1));
  Scope s(vm);
  Slot formS = s.push(ARG(0));
  for (;;) {
    bool e;
    Value next = expand_once(vm, formS.get(), &e);
    if (next.tag == Tag::Unwind || !e) return next;
    formS.set(next);
  }
}

// "ns/name: docstring" for a resolvable var (spec 10.10), nil otherwise.
static Value nat_describe(State& vm, u32 base, u32 argc) {
  OT_TRY(need_argc(vm, "describe", argc, 1, 1));
  OT_TRY(need_symbol(vm, "describe", ARG(0)));
  Value var = ns_resolve_var(vm, ARG(0));
  if (is_nil(var)) return nil_v();
  Buf out;
  print_display(vm, array_get(var, VAR_NS), out);
  out.push('/');
  print_display(vm, array_get(var, VAR_NAME), out);
  Value doc = array_get(var, VAR_DOC);
  if (!is_nil(doc)) {
    out.appendCstr(": ");
    print_display(vm, doc, out);
  }
  return make_string(vm, out);
}

// ---------------------------------------------------------------------------

void register_sys(State& vm) {
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
  def_native(vm, "exit", nat_exit);
  def_native(vm, "eval", nat_eval);
  def_native(vm, "read-string", nat_read_string);
  // gensym and current-ns are registered by register_expand (they use
  // the State's counter and current-ns field); don't shadow them here.
  def_native(vm, "macroexpand-1", nat_macroexpand_1);
  def_native(vm, "macroexpand", nat_macroexpand);
  def_native(vm, "describe", nat_describe);
}

}  // namespace ot
