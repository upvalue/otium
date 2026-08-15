// builtins/sys.cpp — type predicates (10.2), output (10.7), functions and
// evaluation (10.8), namespaces (10.10), plus def_native.
#include "../builtins.hpp"
#include "../vm.hpp"
#include "../ns.hpp"
#include "../heap.hpp"
#include "../eval.hpp"      // FunctionData, eval_form, apply
#include "../reader.hpp"
#include "../printer.hpp"
#include "../intern.hpp"

namespace ot {

#define ARG(n) vm.stack[base + (n)]

// ---------------------------------------------------------------------------
// def_native — wrap a NativeFn in a Function object, define it in otium.core.

void def_native(Vm& vm, const char* name, NativeFn f) {
  u32 id = vm.intern.intern(name, (u32)strlen(name));
  Obj* o = vm.heap.alloc(ObjType::Function, (u32)sizeof(FunctionData));
  FunctionData* fd = (FunctionData*)((char*)o + sizeof(Obj));
  fd->name = id;
  fd->params = nil_v();
  fd->body = nil_v();
  fd->env = nil_v();
  fd->nsName = symbol_v(vm.syms.otiumCore_);
  fd->native = f;
  fd->docstring = nil_v();
  ns_define(vm, id, obj_v(Tag::Function, o), false, nil_v());
}

// ---------------------------------------------------------------------------
// Predicates.

static Value one_arg(Vm& vm, const char* who, u32 argc) {
  if (argc != 1) return raise_error(vm, "%s: expected 1 argument", who);
  return nil_v();
}

#define TAG_PRED(cname, lname, expr)                            \
  static Value cname(Vm& vm, u32 base, u32 argc) {              \
    OT_TRY(one_arg(vm, lname, argc));                           \
    Value v = ARG(0); (void)v;                                  \
    return bool_v(expr);                                        \
  }

TAG_PRED(nat_nilp,      "nil?",      v.tag == Tag::Nil)
TAG_PRED(nat_nullp,     "null?",     v.tag == Tag::Null)
TAG_PRED(nat_booleanp,  "boolean?",  v.tag == Tag::True || v.tag == Tag::False)
TAG_PRED(nat_intp,      "int?",      v.tag == Tag::Int)
TAG_PRED(nat_floatp,    "float?",    v.tag == Tag::Float)
TAG_PRED(nat_numberp,   "number?",   v.tag == Tag::Int || v.tag == Tag::Float)
TAG_PRED(nat_symbolp,   "symbol?",   v.tag == Tag::Symbol)
TAG_PRED(nat_keywordp,  "keyword?",  v.tag == Tag::Keyword)
TAG_PRED(nat_stringp,   "string?",   v.tag == Tag::String)
TAG_PRED(nat_pairp,     "pair?",     v.tag == Tag::Pair)
TAG_PRED(nat_arrayp,    "array?",    v.tag == Tag::Array)
TAG_PRED(nat_tablep,    "table?",    v.tag == Tag::Table)
TAG_PRED(nat_bufferp,   "buffer?",   v.tag == Tag::Buffer)
TAG_PRED(nat_macrop,    "macro?",    v.tag == Tag::Macro)
TAG_PRED(nat_procedurep,"procedure?",v.tag == Tag::Function)
TAG_PRED(nat_truep,     "true?",     v.tag == Tag::True)
TAG_PRED(nat_falsep,    "false?",    v.tag == Tag::False)

static Value nat_listp(Vm& vm, u32 base, u32 argc) {
  OT_TRY(one_arg(vm, "list?", argc));
  Value v = ARG(0);
  while (v.tag == Tag::Pair) v = as_pair(v)->cdr;
  return bool_v(v.tag == Tag::Null);
}

static Value nat_not(Vm& vm, u32 base, u32 argc) {
  OT_TRY(one_arg(vm, "not", argc));
  return bool_v(is_falsy(ARG(0)));
}

static Value nat_emptyp(Vm& vm, u32 base, u32 argc) {
  OT_TRY(one_arg(vm, "empty?", argc));
  Value v = ARG(0);
  switch (v.tag) {
    case Tag::Nil: case Tag::Null: return bool_v(true);
    case Tag::Pair:   return bool_v(false);
    case Tag::Array:  return bool_v(as_array(v)->len == 0);
    case Tag::Table:  return bool_v(as_table(v)->count == 0);
    case Tag::String: return bool_v(as_string(v)->nchars == 0);
    case Tag::Buffer: return bool_v(as_buffer(v)->buf.len == 0);
    default: return raise_error(vm, "empty?: unsupported type");
  }
}

static Value nat_eqp(Vm& vm, u32 base, u32 argc) {
  if (argc != 2) return raise_error(vm, "eq?: expected 2 arguments");
  return bool_v(val_eq(ARG(0), ARG(1)));
}

static Value nat_equalp(Vm& vm, u32 base, u32 argc) {
  if (argc != 2) return raise_error(vm, "equal?: expected 2 arguments");
  return bool_v(val_equal(vm, ARG(0), ARG(1)));
}

static const char* type_name(Tag t) {
  switch (t) {
    case Tag::Nil: return "nil";
    case Tag::Null: return "null";
    case Tag::False: case Tag::True: return "boolean";
    case Tag::Int: return "int";
    case Tag::Float: return "float";
    case Tag::Symbol: return "symbol";
    case Tag::Keyword: return "keyword";
    case Tag::String: return "string";
    case Tag::Pair: return "pair";
    case Tag::Array: return "array";
    case Tag::Table: return "table";
    case Tag::Buffer: return "buffer";
    case Tag::Function: return "function";
    case Tag::Macro: return "macro";
    case Tag::Param: return "param";
    case Tag::Restart: return "restart";
    default: return "unknown";
  }
}

static Value nat_type(Vm& vm, u32 base, u32 argc) {
  OT_TRY(one_arg(vm, "type", argc));
  const char* n = type_name(ARG(0).tag);
  return keyword_v(vm.intern.intern(n, (u32)strlen(n)));
}

// ---------------------------------------------------------------------------
// Output (10.7). All through vm.writeFn.

static void write_out(Vm& vm, Buf& b) {
  if (vm.writeFn && b.len) vm.writeFn(vm.writeUd, b.data, b.len);
}

static Value print_all(Vm& vm, u32 base, u32 argc, bool repr, bool nl) {
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

static Value nat_display(Vm& vm, u32 base, u32 argc) { return print_all(vm, base, argc, false, false); }
static Value nat_write(Vm& vm, u32 base, u32 argc)   { return print_all(vm, base, argc, true, false); }
static Value nat_println(Vm& vm, u32 base, u32 argc) { return print_all(vm, base, argc, false, true); }

static Value nat_newline(Vm& vm, u32 base, u32 argc) {
  (void)base; (void)argc;
  Buf out; out.push('\n');
  write_out(vm, out);
  return nil_v();
}

// ---------------------------------------------------------------------------
// Functions and evaluation (10.8).

static Value nat_identity(Vm& vm, u32 base, u32 argc) {
  OT_TRY(one_arg(vm, "identity", argc));
  return ARG(0);
}

static Value nat_apply(Vm& vm, u32 base, u32 argc) {
  if (argc < 2) return raise_error(vm, "apply: expected at least 2 arguments");
  Value f = ARG(0);
  Value seq = ARG(argc - 1);
  u32 cbase = vm.stack.len;
  for (u32 i = 1; i + 1 < argc; i++) vm.push(ARG(i));
  if (seq.tag == Tag::Array) {
    ArrayData* a = as_array(seq);
    for (u32 i = 0; i < a->len; i++) vm.push(a->items[i]);
  } else if (seq.tag == Tag::Pair || seq.tag == Tag::Null || is_nil(seq)) {
    for (Value p = seq; p.tag == Tag::Pair; p = as_pair(p)->cdr) vm.push(as_pair(p)->car);
  } else {
    vm.popTo(cbase);
    return raise_error(vm, "apply: last argument must be a sequence");
  }
  Value r = apply(vm, f, cbase, vm.stack.len - cbase);
  vm.popTo(cbase);
  return r;
}

static Value nat_eval(Vm& vm, u32 base, u32 argc) {
  OT_TRY(one_arg(vm, "eval", argc));
  return eval_form(vm, ARG(0));
}

static Value nat_read_string(Vm& vm, u32 base, u32 argc) {
  OT_TRY(one_arg(vm, "read-string", argc));
  if (ARG(0).tag != Tag::String) return raise_error(vm, "read-string: expected string");
  StringData* s = as_string(ARG(0));
  Reader r(vm, (const char*)(s + 1), s->len, "<read-string>");
  Value form = r.next();
  OT_TRY(form);
  if (r.atEof() && is_nil(form) && s->nchars == 0)
    return raise_error(vm, "read-string: empty input");
  // INTEGRATION: Reader contract makes "no form at all" vs "read nil literal"
  // hard to distinguish; treating eof-with-nil as empty input.
  Value trailing = r.next();
  OT_TRY(trailing);
  if (!r.atEof()) return raise_error(vm, "read-string: trailing input");
  return form;
}

// If form is (sym args...) and sym resolves to a macro, expand once.
// Returns bool via *expanded; result (or original form) as return value.
static Value expand_once(Vm& vm, Value form, bool* expanded) {
  *expanded = false;
  if (form.tag != Tag::Pair) return form;
  Value head = as_pair(form)->car;
  if (head.tag != Tag::Symbol) return form;
  Value callee = ns_resolve(vm, head);
  if (callee.tag == Tag::Unwind) {
    // unresolvable head is not a macro call; swallow the unwind
    vm_cancel_unwind(vm);
    return form;
  }
  if (callee.tag != Tag::Macro) return form;
  // push unevaluated argument forms and call the macro
  u32 cbase = vm.stack.len;
  u32 n = 0;
  for (Value p = as_pair(form)->cdr; p.tag == Tag::Pair; p = as_pair(p)->cdr) {
    vm.push(as_pair(p)->car);
    n++;
  }
  Value r = apply(vm, callee, cbase, n);
  vm.popTo(cbase);
  if (r.tag == Tag::Unwind) return r;
  *expanded = true;
  return r;
}

static Value nat_macroexpand_1(Vm& vm, u32 base, u32 argc) {
  OT_TRY(one_arg(vm, "macroexpand-1", argc));
  bool e;
  return expand_once(vm, ARG(0), &e);
}

static Value nat_macroexpand(Vm& vm, u32 base, u32 argc) {
  OT_TRY(one_arg(vm, "macroexpand", argc));
  Value form = ARG(0);
  u32 root = vm.push(form);
  for (;;) {
    bool e;
    Value next = expand_once(vm, form, &e);
    if (next.tag == Tag::Unwind) { vm.popTo(root); return next; }
    if (!e) { vm.popTo(root); return next; }
    form = next;
    vm.stack[root] = form;
  }
}

// ---------------------------------------------------------------------------
// Namespaces (10.10).

static Value nat_in_ns(Vm& vm, u32 base, u32 argc) {
  OT_TRY(one_arg(vm, "in-ns", argc));
  if (ARG(0).tag != Tag::Symbol) return raise_error(vm, "in-ns: expected symbol");
  ns_switch(vm, ARG(0).id);
  return nil_v();
}

// "ns/name: docstring" for a resolvable var (spec 10.10), nil otherwise.
static Value nat_describe(Vm& vm, u32 base, u32 argc) {
  OT_TRY(one_arg(vm, "describe", argc));
  if (ARG(0).tag != Tag::Symbol) return raise_error(vm, "describe: expected symbol");
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
  return make_string(vm, out.data, out.len);
}

// ---------------------------------------------------------------------------

void register_sys(Vm& vm) {
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
  def_native(vm, "macro?", nat_macrop);
  def_native(vm, "procedure?", nat_procedurep);
  def_native(vm, "list?", nat_listp);
  def_native(vm, "true?", nat_truep);
  def_native(vm, "false?", nat_falsep);
  def_native(vm, "empty?", nat_emptyp);
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
  def_native(vm, "identity", nat_identity);
  def_native(vm, "eval", nat_eval);
  def_native(vm, "read-string", nat_read_string);
  // gensym and current-ns are registered by register_eval_natives (they use
  // the Vm's counter and current-ns field); don't shadow them here.
  def_native(vm, "macroexpand-1", nat_macroexpand_1);
  def_native(vm, "macroexpand", nat_macroexpand);
  def_native(vm, "in-ns", nat_in_ns);
  def_native(vm, "describe", nat_describe);
}

} // namespace ot
