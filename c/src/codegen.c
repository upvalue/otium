#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* Builtins the native compiler (lib/otium/compiler) uses to resolve names
 * and source positions the way compiler.c does, and to install the units it
 * produces. All but %native-install work in the VM too, which runs the
 * compiler to build boot files. */

static ot_value codegen_arity(ot_state* rt, size_t count, size_t low, size_t high) {
  if (count < low || count > high) return oti_fail(rt, "wrong number of arguments");
  return OT_TRUE;
}

static bool cell_arg(ot_state* rt, ot_value v, uint32_t* cell) {
  if (!ot_is_int(v) || ot_to_int(v) < 0 || (size_t)ot_to_int(v) >= rt->global_count)
    return oti_error(rt, "expected a global cell number");
  *cell = (uint32_t)ot_to_int(v);
  return true;
}

static ot_value codegen_install(ot_state* rt, size_t first, size_t count) {
  if (!codegen_arity(rt, count, 1, 1)) return 0;
#ifdef OTIUM_NATIVE
  if (rt->native_mode) return oti_native_install(rt, first);
#else
  (void)first;
#endif
  return oti_fail(rt, "%native-install needs a native process");
}

/* The cell a global name resolves to here, created in the current namespace
 * when nothing else matches, as compiler.c resolves names. */
static ot_value codegen_cell(ot_state* rt, size_t first, size_t count) {
  if (!codegen_arity(rt, count, 1, 1)) return 0;
  ot_value name = rt->stack[first];
  if (!oti_kind(name, K_SYMBOL)) return oti_fail(rt, "%native-cell expects a symbol");
  size_t length;
  const char* text = oti_text_bytes(name, &length);
  uint32_t cell = oti_resolve_global(rt, text, length);
  return cell == UINT32_MAX ? 0 : ot_int((intptr_t)cell);
}

/* The name of the builtin a cell holds, when it is that builtin's own cell,
 * else #f. Inlined builtins' own cells can't be redefined. */
static ot_value codegen_cell_builtin(ot_state* rt, size_t first, size_t count) {
  uint32_t cell = 0;
  if (!codegen_arity(rt, count, 1, 1) || !cell_arg(rt, rt->stack[first], &cell)) return 0;
  ot_value v = oti_global_value(rt, cell);
  if ((v & 255) != TAG_BUILTIN) return OT_FALSE;
  unsigned id = (unsigned)(v >> 8);
  if (id >= B_COUNT || rt->builtin_cells[id] != cell) return OT_FALSE;
  const char* name = rt->global_info[cell].name;
  return oti_intern(rt, K_SYMBOL, name, strlen(name));
}

/* (namespace . name) of a cell, as strings, for code written to files. */
static ot_value codegen_cell_name(ot_state* rt, size_t first, size_t count) {
  uint32_t cell = 0;
  if (!codegen_arity(rt, count, 1, 1) || !cell_arg(rt, rt->stack[first], &cell)) return 0;
  const oti_global_info* info = &rt->global_info[cell];
  ot_value live[] = {OT_NIL, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 2);
  const char* space = rt->namespaces[info->space];
  live[0] = oti_text(rt, K_STRING, space, strlen(space));
  if (live[0]) live[1] = oti_text(rt, K_STRING, info->name, strlen(info->name));
  ot_value result = live[0] && live[1] ? oti_cons(rt, live[0], live[1]) : 0;
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

static ot_value codegen_namespace(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!codegen_arity(rt, count, 0, 0)) return 0;
  return ot_int((intptr_t)rt->current_namespace);
}

static ot_value codegen_native_p(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!codegen_arity(rt, count, 0, 0)) return 0;
  return ot_bool(rt->native_mode);
}

/* The source offset a located list carries, or nil. */
static ot_value codegen_source_offset(ot_state* rt, size_t first, size_t count) {
  if (!codegen_arity(rt, count, 1, 1)) return 0;
  size_t offset;
  return oti_pair_offset(rt->stack[first], &offset) ? ot_int((intptr_t)offset) : OT_NIL;
}

/* (%compile-error message offset [code]): fail with a compile error located
 * at offset, when it is an integer. code is a diagnostic code string, or the
 * symbol invalid-declaration for a tagged declaration error. */
static ot_value codegen_compile_error(ot_state* rt, size_t first, size_t count) {
  if (!codegen_arity(rt, count, 2, 3)) return 0;
  ot_value message = rt->stack[first], offset = rt->stack[first + 1];
  if (!oti_kind(message, K_STRING)) return oti_fail(rt, "%compile-error expects a message");
  size_t length;
  const char* text = oti_text_bytes(message, &length);
  char code[32] = "";
  /* A declaration error is tagged, as compiler.c's declaration_error. */
  if (count == 3 && oti_kind(rt->stack[first + 2], K_SYMBOL)) {
    bool fresh = !oti_failed(rt);
    char* copy = malloc(length + 1);
    if (!copy) return oti_fail(rt, "out of host memory");
    memcpy(copy, text, length);
    copy[length] = 0;
    oti_tagged_error(rt, "declaration", "invalid-declaration", copy, 0, NULL);
    free(copy);
    if (fresh && ot_is_int(offset) && ot_to_int(offset) >= 0)
      oti_error_at(rt, (size_t)ot_to_int(offset));
    return 0;
  }
  if (count == 3 && oti_kind(rt->stack[first + 2], K_STRING)) {
    size_t n;
    const char* c = oti_text_bytes(rt->stack[first + 2], &n);
    if (n < sizeof(code)) {
      memcpy(code, c, n);
      code[n] = 0;
    }
  }
  if (code[0]) oti_coded_errorf(rt, code, "%.*s", (int)length, text);
  else oti_errorf(rt, "%.*s", (int)length, text);
  if (ot_is_int(offset) && ot_to_int(offset) >= 0) oti_error_at(rt, (size_t)ot_to_int(offset));
  return 0;
}

/* The cell a definition of name binds: in the current namespace, an error
 * when an import has the name, as oti_global. */
static ot_value codegen_cell_define(ot_state* rt, size_t first, size_t count) {
  if (!codegen_arity(rt, count, 1, 1)) return 0;
  ot_value name = rt->stack[first];
  if (!oti_kind(name, K_SYMBOL)) return oti_fail(rt, "%native-cell-define expects a symbol");
  size_t length;
  const char* text = oti_text_bytes(name, &length);
  uint32_t cell = oti_global(rt, text, length);
  return cell == UINT32_MAX ? 0 : ot_int((intptr_t)cell);
}

static ot_value codegen_core_cell(ot_state* rt, size_t first, size_t count) {
  uint32_t cell = 0;
  if (!codegen_arity(rt, count, 1, 1) || !cell_arg(rt, rt->stack[first], &cell)) return 0;
  return ot_bool(rt->global_info[cell].space == 0);
}

/* The machine word of a builtin, by name; "qq-append" is the compiler's
 * unnamed append for quasiquote. */
static ot_value codegen_builtin(ot_state* rt, size_t first, size_t count) {
  if (!codegen_arity(rt, count, 1, 1)) return 0;
  if (!oti_kind(rt->stack[first], K_STRING)) return oti_fail(rt, "%native-builtin expects a name");
  size_t length;
  const char* text = oti_text_bytes(rt->stack[first], &length);
  int id = length == 9 && !memcmp(text, "qq-append", 9) ? B_QQ_APPEND : oti_builtin_index(text);
  if (id < 0) return oti_fail(rt, "%native-builtin: no such builtin");
  return ot_int((intptr_t)(((ot_value)id << 8) | TAG_BUILTIN));
}

static ot_value codegen_object_class(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!codegen_arity(rt, count, 0, 0) || !oti_classes_init(rt)) return 0;
  return rt->classes[C_OBJECT];
}

/* Every form in a string, as located data, the way the evaluator reads a
 * file. */
static ot_value codegen_read_forms(ot_state* rt, size_t first, size_t count) {
  if (!codegen_arity(rt, count, 1, 1)) return 0;
  if (!oti_kind(rt->stack[first], K_STRING)) return oti_fail(rt, "%read-forms expects a string");
  size_t length;
  const char* text = oti_text_bytes(rt->stack[first], &length);
  char* copy = malloc(length + 1);
  if (!copy) return oti_fail(rt, "out of host memory");
  memcpy(copy, text, length);
  copy[length] = 0;
  oti_reader reader;
  oti_reader_init(&reader, copy, length, OTI_SOURCE_LIMIT);
  size_t base = rt->sp;
  bool ok = true;
  oti_ast* form;
  while (ok && (form = oti_reader_next(&reader))) {
    ot_value v = oti_ast_datum(rt, form, true);
    if (!v || !oti_stack(rt, rt->sp + 1)) ok = false;
    else {
      rt->stack[rt->sp] = v;
      rt->sp = rt->sp + 1;
    }
  }
  if (ok && reader.error) ok = oti_reader_error(rt, &reader);
  oti_reader_destroy(&reader);
  free(copy);
  ot_value result = ok ? oti_array_from(rt, base, rt->sp - base) : 0;
  rt->sp = base;
  return result;
}

/* Make a namespace, by name, the current one. */
static ot_value codegen_set_namespace(ot_state* rt, size_t first, size_t count) {
  if (!codegen_arity(rt, count, 1, 1)) return 0;
  ot_value name = rt->stack[first];
  if (!oti_kind(name, K_SYMBOL) && !oti_kind(name, K_STRING))
    return oti_fail(rt, "%set-namespace expects a name");
  size_t length;
  const char* text = oti_text_bytes(name, &length);
  uint32_t space = oti_namespace(rt, text, length);
  if (space == UINT32_MAX) return 0;
  rt->current_namespace = space;
  return OT_NIL;
}

/* A symbol's gensym serial, or 0 for an interned symbol. */
static ot_value codegen_symbol_serial(ot_state* rt, size_t first, size_t count) {
  if (!codegen_arity(rt, count, 1, 1)) return 0;
  if (!oti_kind(rt->stack[first], K_SYMBOL)) return oti_fail(rt, "%symbol-serial expects a symbol");
  return ot_int((intptr_t)oti_symbol_id(rt->stack[first]));
}

/* Evaluate a form in the current namespace, as at top level. */
static ot_value codegen_evaluate(ot_state* rt, size_t first, size_t count) {
  if (!codegen_arity(rt, count, 1, 1)) return 0;
  return oti_eval_value(rt, rt->stack[first]);
}

static ot_value codegen_cell_bound(ot_state* rt, size_t first, size_t count) {
  uint32_t cell = 0;
  if (!codegen_arity(rt, count, 1, 1) || !cell_arg(rt, rt->stack[first], &cell)) return 0;
  return ot_bool(!oti_unbound(oti_global_value(rt, cell)));
}

const oti_builtin_def oti_codegen_builtins[] = {
    {B_NATIVE_CELL_BOUND, "%native-cell-bound?", codegen_cell_bound},
    {B_EVALUATE, "%native-evaluate", codegen_evaluate},
    {B_READ_FORMS, "%read-forms", codegen_read_forms},
    {B_SET_NAMESPACE, "%set-namespace", codegen_set_namespace},
    {B_SYMBOL_SERIAL, "%symbol-serial", codegen_symbol_serial},
    {B_NATIVE_CELL_DEFINE, "%native-cell-define", codegen_cell_define},
    {B_NATIVE_CORE_CELL, "%native-core-cell?", codegen_core_cell},
    {B_NATIVE_BUILTIN, "%native-builtin", codegen_builtin},
    {B_NATIVE_OBJECT_CLASS, "%native-object-class", codegen_object_class},
    {B_NATIVE_INSTALL, "%native-install", codegen_install},
    {B_NATIVE_CELL, "%native-cell", codegen_cell},
    {B_NATIVE_CELL_BUILTIN, "%native-cell-builtin", codegen_cell_builtin},
    {B_NATIVE_CELL_NAME, "%native-cell-name", codegen_cell_name},
    {B_NATIVE_NAMESPACE, "%native-namespace", codegen_namespace},
    {B_NATIVE_P, "%native?", codegen_native_p},
    {B_SOURCE_OFFSET, "%source-offset", codegen_source_offset},
    {B_COMPILE_ERROR, "%compile-error", codegen_compile_error},
    {0, NULL, NULL},
};
