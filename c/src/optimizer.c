#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* The C side of the optimizing compiler, which is written in Otium (see
 * docs/optimizer.md): global resolution and cell facts for its front end,
 * the installer for the VM units its backend emits, and the hook that offers
 * each top-level form to it. */

/* Opcode keywords: the enum names without OP_, lowercased, with - for _.
 * Keyword text includes the colon. */
static const char* const optimizer_opcode_names[OP_COUNT] = {
    [OP_MOVE] = ":move",
    [OP_IMM] = ":imm",
    [OP_CONST] = ":const",
    [OP_GLOBAL] = ":global",
    [OP_SET_GLOBAL] = ":set-global",
    [OP_DEFINE_GLOBAL] = ":define-global",
    [OP_UPVALUE] = ":upvalue",
    [OP_UPVALUE_BOX] = ":upvalue-box",
    [OP_SET_UPVALUE] = ":set-upvalue",
    [OP_BOX] = ":box",
    [OP_UNBOX] = ":unbox",
    [OP_SET_BOX] = ":set-box",
    [OP_CHECK] = ":check",
    [OP_CLEAR] = ":clear",
    [OP_CLOSURE] = ":closure",
    [OP_JUMP] = ":jump",
    [OP_JFALSE] = ":jfalse",
    [OP_JTRUE] = ":jtrue",
    [OP_JBOUND] = ":jbound",
    [OP_CALL] = ":call",
    [OP_TAIL_CALL] = ":tail-call",
    [OP_CALL_GLOBAL] = ":call-global",
    [OP_TAIL_CALL_GLOBAL] = ":tail-call-global",
    [OP_RETURN] = ":return",
    [OP_RETURN_IMM] = ":return-imm",
    [OP_TRY] = ":try",
    [OP_END_TRY] = ":end-try",
    [OP_RAISE] = ":raise",
    [OP_CLEANUP] = ":cleanup",
    [OP_RUN_CLEANUP] = ":run-cleanup",
    [OP_RESTARTS] = ":restarts",
    [OP_RESULT] = ":result",
    [OP_END_RESTARTS] = ":end-restarts",
    [OP_BIND_HANDLERS] = ":bind-handlers",
    [OP_UNBIND_HANDLERS] = ":unbind-handlers",
    [OP_MAKE_PARAM] = ":make-param",
    [OP_BIND_PARAM] = ":bind-param",
    [OP_UNBIND_PARAMS] = ":unbind-params",
    [OP_ADD] = ":add",
    [OP_SUB] = ":sub",
    [OP_MUL] = ":mul",
    [OP_ADDI] = ":addi",
    [OP_LT] = ":lt",
    [OP_LE] = ":le",
    [OP_GT] = ":gt",
    [OP_GE] = ":ge",
    [OP_NUMEQ] = ":numeq",
    [OP_EQ] = ":eq",
    [OP_CONS] = ":cons",
    [OP_CAR] = ":car",
    [OP_CDR] = ":cdr",
    [OP_NOT] = ":not",
    [OP_NULLP] = ":nullp",
    [OP_PAIRP] = ":pairp",
    [OP_BR_LT] = ":br-lt",
    [OP_BR_LE] = ":br-le",
    [OP_BR_GT] = ":br-gt",
    [OP_BR_GE] = ":br-ge",
    [OP_BR_NUMEQ] = ":br-numeq",
    [OP_BR_EQ] = ":br-eq",
    [OP_BR_LTI] = ":br-lti",
    [OP_BR_LEI] = ":br-lei",
    [OP_BR_GTI] = ":br-gti",
    [OP_BR_GEI] = ":br-gei",
    [OP_BR_NUMEQI] = ":br-numeqi",
    [OP_BR_NULL] = ":br-null",
    [OP_BR_PAIR] = ":br-pair",
    [OP_BR_EQK] = ":br-eqk",
};

static bool optimizer_arity(ot_state* rt, const char* who, size_t count, size_t arity) {
  return count == arity || oti_errorf(rt, "%s expects %zu arguments", who, arity);
}

/* The namespace a symbol names, without creating it. */
static bool optimizer_namespace(ot_state* rt, const char* who, ot_value symbol, uint32_t* space) {
  if (!oti_kind(symbol, K_SYMBOL)) return oti_type_error(rt, who, "a namespace symbol", symbol);
  size_t length = 0;
  const char* name = oti_text_bytes(symbol, &length);
  if (length == 3 && !memcmp(name, "ffi", 3)) {
    name = "otium.ffi";
    length = 9;
  }
  for (size_t i = 0; i < rt->namespace_count; i++)
    if (strlen(rt->namespaces[i]) == length && !memcmp(rt->namespaces[i], name, length)) {
      *space = (uint32_t)i;
      return true;
    }
  return oti_errorf(rt, "%s: unknown namespace %.*s", who, (int)length, name);
}

/* Look up a global cell as code compiled in namespace space would, with
 * create as for oti_resolve_global (false), or as def assigns it (true). */
static ot_value optimizer_cell(ot_state* rt, const char* who, size_t first, size_t count,
                               bool definition) {
  if (!optimizer_arity(rt, who, count, 2)) return 0;
  uint32_t space;
  if (!optimizer_namespace(rt, who, rt->stack[first], &space)) return 0;
  ot_value symbol = rt->stack[first + 1];
  if (!oti_kind(symbol, K_SYMBOL)) return oti_type_error(rt, who, "a symbol", symbol), 0;
  size_t length = 0;
  const char* name = oti_text_bytes(symbol, &length);
  uint32_t saved = rt->current_namespace;
  rt->current_namespace = space;
  uint32_t cell = definition ? oti_global(rt, name, length) : oti_resolve_global(rt, name, length);
  rt->current_namespace = saved;
  return cell == UINT32_MAX ? 0 : ot_int(cell);
}

static ot_value optimizer_resolve_global(ot_state* rt, size_t first, size_t count) {
  return optimizer_cell(rt, "%resolve-global", first, count, false);
}

static ot_value optimizer_global_cell(ot_state* rt, size_t first, size_t count) {
  return optimizer_cell(rt, "%global-cell", first, count, true);
}

static bool optimizer_cell_id(ot_state* rt, const char* who, ot_value v, uint32_t* cell) {
  if (!ot_is_int(v) || ot_to_int(v) < 0 || (uintptr_t)ot_to_int(v) >= rt->global_count)
    return oti_type_error(rt, who, "a global cell id", v);
  *cell = (uint32_t)ot_to_int(v);
  return true;
}

/* Put key and slots[2] into the table in slots[0]; slots[1] holds the key. */
static bool optimizer_put(ot_state* rt, ot_value* slots, const char* key) {
  slots[1] = oti_intern(rt, K_KEYWORD, key, strlen(key));
  if (!slots[1]) return false;
  slots[0] = oti_table_set(rt, slots[0], slots[1], slots[2]);
  return slots[0] != 0;
}

static ot_value optimizer_symbol(ot_state* rt, const char* prefix, const char* name) {
  size_t a = prefix ? strlen(prefix) + 1 : 0, b = strlen(name);
  char* text = malloc(a + b + 1);
  if (!text) return oti_fail(rt, "out of host memory");
  if (prefix) {
    memcpy(text, prefix, a - 1);
    text[a - 1] = '/';
  }
  memcpy(text + a, name, b);
  ot_value symbol = oti_intern(rt, K_SYMBOL, text, a + b);
  free(text);
  return symbol;
}

static ot_value optimizer_cell_info(ot_state* rt, size_t first, size_t count) {
  if (!optimizer_arity(rt, "%cell-info", count, 1)) return 0;
  uint32_t cell = 0;
  if (!optimizer_cell_id(rt, "%cell-info", rt->stack[first], &cell)) return 0;
  ot_value slots[3] = {OT_NIL, OT_NIL, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, slots, 3);
  const char* space = rt->namespaces[rt->global_info[cell].space];
  ot_value value = rt->globals[cell];
  bool builtin = (value & 255) == TAG_BUILTIN && (value >> 8) < B_COUNT &&
                 rt->builtin_cells[value >> 8] == cell && rt->global_info[cell].space == 0;
  bool inlined = builtin && oti_inlined_builtin((unsigned)(value >> 8));
  bool ok = (slots[0] = oti_table_new(rt)) &&
            (slots[2] = optimizer_symbol(rt, space, rt->global_info[cell].name)) &&
            optimizer_put(rt, slots, ":name") && (slots[2] = optimizer_symbol(rt, NULL, space)) &&
            optimizer_put(rt, slots, ":ns");
  slots[2] = rt->globals[cell] != V_UNBOUND ? OT_TRUE : OT_FALSE;
  ok = ok && optimizer_put(rt, slots, ":defined");
  slots[2] = rt->global_info[cell].macro ? OT_TRUE : OT_FALSE;
  ok = ok && optimizer_put(rt, slots, ":macro");
  slots[2] = OT_NIL;
  ok = ok && (!builtin || (slots[2] = optimizer_symbol(rt, NULL, rt->global_info[cell].name))) &&
       optimizer_put(rt, slots, ":builtin");
  slots[2] = inlined ? OT_TRUE : OT_FALSE;
  ok = ok && optimizer_put(rt, slots, ":inlined");
  ot_value result = ok ? slots[0] : 0;
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

static ot_value optimizer_form_offset(ot_state* rt, size_t first, size_t count) {
  if (!optimizer_arity(rt, "%form-offset", count, 1)) return 0;
  size_t offset;
  if (!oti_pair_offset(rt->stack[first], &offset)) return OT_NIL;
  return ot_int((intptr_t)offset);
}

static ot_value optimizer_vm_opcodes(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!optimizer_arity(rt, "%vm-opcodes", count, 0)) return 0;
  ot_value slots[3] = {OT_NIL, OT_NIL, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, slots, 3);
  bool ok = (slots[0] = oti_table_new(rt)) != 0;
  for (unsigned op = 0; ok && op < OP_COUNT; op++) {
    slots[2] = ot_int(op);
    ok = optimizer_put(rt, slots, optimizer_opcode_names[op]);
  }
  ot_value result = ok ? slots[0] : 0;
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

static bool optimizer_immediate(ot_value v) {
  return !value_is_ptr(v) && (ot_value)(intptr_t)(int32_t)v == v;
}

static ot_value optimizer_immediate_bits(ot_state* rt, size_t first, size_t count) {
  if (!optimizer_arity(rt, "%immediate-bits", count, 1)) return 0;
  ot_value v = rt->stack[first];
  return optimizer_immediate(v) ? ot_int((int32_t)v) : OT_NIL;
}

/* Whether IMM may load these bits: a fixnum, one of the fixed immediates, a
 * character or a builtin. Anything else would be read as a heap pointer or an
 * unknown builtin. */
static bool optimizer_loadable(uint32_t bits) {
  ot_value v = (ot_value)(intptr_t)(int32_t)bits;
  if (ot_is_int(v)) return true;
  switch (v) {
    case OT_NIL:
    case OT_FALSE:
    case OT_TRUE:
    case OT_EMPTY:
    case OT_EOF:
    case V_UNBOUND: return true;
    default: break;
  }
  if ((v & 255) == TAG_CHAR) return (v >> 8) <= 0x10ffff;
  return (v & 255) == TAG_BUILTIN && (v >> 8) < B_COUNT;
}

/* Unit descriptor keys, interned once per install. */
enum {
  KEY_NAME,
  KEY_ARITY,
  KEY_OPTIONAL,
  KEY_REST,
  KEY_LOCALS,
  KEY_CODE,
  KEY_CONSTANTS,
  KEY_STATICS,
  KEY_CAPTURES,
  KEY_POINTS,
  KEY_COUNT
};

static const char* const install_keys[KEY_COUNT] = {
    ":name", ":arity",     ":optional", ":rest",     ":locals",
    ":code", ":constants", ":statics",  ":captures", ":points",
};

/* A unit function while it is installed. constants maps unit constant
 * indexes to runtime constants; statics are (constant index, function index)
 * pairs. */
typedef struct {
  oti_function* fn;
  uint32_t id;
  size_t constant_count, static_count;
  uint32_t* constants;
  uint32_t* statics;
} install_entry;

typedef struct {
  ot_state* rt;
  /* The unit array, then the keys. Rooted. */
  ot_value slots[1 + KEY_COUNT];
  install_entry* entries;
  size_t count;
  /* The function being read, for error messages. */
  size_t current;
} install_state;

/* Report "what detail" about the function being read. */
static bool install_fail(install_state* s, const char* what, const char* detail) {
  return oti_errorf(s->rt, "%%vm-install: function %zu: %s%s", s->current, what,
                    detail ? detail : "");
}

static ot_value install_field(install_state* s, ot_value table, unsigned key) {
  return oti_table_get(s->rt, table, s->slots[1 + key], OT_NIL);
}

/* An array's items and length. Items move with the array on allocation. */
static bool install_array(install_state* s, ot_value v, const char* what, ot_value** items,
                          size_t* length) {
  if (!oti_kind(v, K_ARRAY)) return install_fail(s, what, " must be an array");
  oti_array_obj* array = oti_as_array(v);
  *length = array->length;
  *items = array->length ? object_fields(array->data) : NULL;
  return true;
}

static bool install_int(install_state* s, ot_value v, const char* what, intptr_t low, intptr_t high,
                        intptr_t* out) {
  if (!ot_is_int(v) || ot_to_int(v) < low || ot_to_int(v) > high)
    return install_fail(s, what, " is not a fixnum in range");
  *out = ot_to_int(v);
  return true;
}

/* An optional array field: nil reads as empty. */
static bool install_list(install_state* s, ot_value table, unsigned key, ot_value** items,
                         size_t* length) {
  ot_value v = install_field(s, table, key);
  if (v == OT_NIL) {
    *items = NULL;
    *length = 0;
    return true;
  }
  return install_array(s, v, install_keys[key], items, length);
}

/* A two-element array of fixnums, or of a fixnum and a flag when flag. */
static bool install_pair(install_state* s, ot_value v, const char* what, intptr_t high, bool flag,
                         intptr_t* first, intptr_t* second) {
  ot_value* items = NULL;
  size_t length = 0;
  if (!install_array(s, v, what, &items, &length)) return false;
  if (length != 2) return install_fail(s, what, " entries must have two elements");
  if (!install_int(s, items[0], what, 0, high, first)) return false;
  if (flag) {
    *second = oti_truth(items[1]);
    return true;
  }
  return install_int(s, items[1], what, 0, INT32_MAX, second);
}

static bool install_read(install_state* s, ot_value table) {
  ot_state* rt = s->rt;
  install_entry* e = &s->entries[s->current];
  oti_function* fn = e->fn;
  if (!oti_kind(table, K_TABLE)) return install_fail(s, "descriptor", " must be a table");
  intptr_t n = 0;
  ot_value name = install_field(s, table, KEY_NAME);
  if (name != OT_NIL) {
    if (!oti_kind(name, K_STRING)) return install_fail(s, "name", " must be a string or nil");
    size_t length = 0;
    const char* text = oti_text_bytes(name, &length);
    fn->name = malloc(length + 1);
    if (!fn->name) return oti_error(rt, "out of host memory");
    memcpy(fn->name, text, length);
    fn->name[length] = 0;
  }
  if (!install_int(s, install_field(s, table, KEY_ARITY), "arity", 0, UINT16_MAX, &n)) return false;
  fn->arity = (size_t)n;
  ot_value optional = install_field(s, table, KEY_OPTIONAL);
  if (optional != OT_NIL && !install_int(s, optional, "optional", 0, UINT16_MAX, &n)) return false;
  fn->optional = optional == OT_NIL ? 0 : (size_t)n;
  fn->rest = oti_truth(install_field(s, table, KEY_REST));
  fn->variadic = fn->rest || fn->optional;
  if (fn->rest && fn->optional)
    return install_fail(s, "optional and rest parameters cannot combine", NULL);
  if (!install_int(s, install_field(s, table, KEY_LOCALS), "locals", 0, UINT16_MAX, &n))
    return false;
  fn->locals = (size_t)n;
  if (fn->locals < fn->arity + fn->optional + fn->rest)
    return install_fail(s, "locals must cover the parameters", NULL);

  ot_value* items = NULL;
  size_t length = 0;
  if (!install_array(s, install_field(s, table, KEY_CODE), "code", &items, &length)) return false;
  if (!length || length % 5) return install_fail(s, "code", " must be five fixnums per word");
  fn->count = fn->capacity = length / 5;
  fn->code = malloc(fn->count * sizeof(oti_instruction));
  if (!fn->code) return oti_error(rt, "out of host memory");
  for (size_t i = 0; i < fn->count; i++) {
    intptr_t f[5] = {0};
    if (!install_int(s, items[5 * i], "opcode", 0, OP_COUNT - 1, &f[0]) ||
        !install_int(s, items[5 * i + 1], "x operand", INT8_MIN, UINT8_MAX, &f[1]))
      return false;
    for (size_t k = 2; k < 5; k++)
      if (!install_int(s, items[5 * i + k], "operand", INT16_MIN, UINT16_MAX, &f[k])) return false;
    fn->code[i] = (oti_instruction){(uint8_t)f[0], (uint8_t)f[1], (uint16_t)f[2], (uint16_t)f[3],
                                    (uint16_t)f[4]};
  }

  if (!install_list(s, table, KEY_CONSTANTS, &items, &length)) return false;
  e->constant_count = length;
  e->constants = malloc((length ? length : 1) * sizeof(uint32_t));
  if (!e->constants) return oti_error(rt, "out of host memory");
  for (size_t i = 0; i < length; i++) e->constants[i] = UINT32_MAX;

  if (!install_list(s, table, KEY_STATICS, &items, &length)) return false;
  e->static_count = length;
  e->statics = malloc((length ? 2 * length : 1) * sizeof(uint32_t));
  if (!e->statics) return oti_error(rt, "out of host memory");
  for (size_t i = 0; i < length; i++) {
    intptr_t constant = 0, function = 0;
    if (!install_pair(s, items[i], "statics", INT32_MAX, false, &constant, &function)) return false;
    if ((size_t)constant >= e->constant_count)
      return install_fail(s, "static constant index out of range", NULL);
    if ((size_t)function >= s->count)
      return install_fail(s, "static function index out of range", NULL);
    if (e->constants[constant] != UINT32_MAX)
      return install_fail(s, "two statics share a constant index", NULL);
    /* Not mapped from the unit's value; install_link makes the closure. */
    e->constants[constant] = 0;
    e->statics[2 * i] = (uint32_t)constant;
    e->statics[2 * i + 1] = (uint32_t)function;
  }

  if (!install_list(s, table, KEY_CAPTURES, &items, &length)) return false;
  if (length > UINT16_MAX) return install_fail(s, "too many captures", NULL);
  fn->capture_count = fn->capture_capacity = length;
  fn->captures = malloc((length ? length : 1) * sizeof(oti_capture));
  if (!fn->captures) return oti_error(rt, "out of host memory");
  for (size_t i = 0; i < length; i++) {
    intptr_t index = 0, local = 0;
    if (!install_pair(s, items[i], "captures", UINT16_MAX, true, &index, &local)) return false;
    fn->captures[i] = (oti_capture){(uint32_t)index, local != 0, false};
  }

  if (!install_list(s, table, KEY_POINTS, &items, &length)) return false;
  fn->point_count = fn->point_capacity = length;
  fn->points = malloc((length ? length : 1) * sizeof(oti_source_point));
  if (!fn->points) return oti_error(rt, "out of host memory");
  for (size_t i = 0; i < length; i++) {
    intptr_t pc = 0, offset = 0;
    if (!install_pair(s, items[i], "points", INT32_MAX, false, &pc, &offset)) return false;
    if ((size_t)pc >= fn->count || (i && pc < fn->points[i - 1].pc))
      return install_fail(s, "points must be in pc order within the code", NULL);
    fn->points[i] = (oti_source_point){(uint32_t)pc, (uint32_t)offset};
  }

  if (!s->current && (fn->arity || fn->optional || fn->rest || fn->capture_count))
    return install_fail(s, "the entry takes no parameters and has no captures", NULL);
  return true;
}

static bool install_word_error(install_state* s, size_t pc, const char* problem) {
  oti_instruction in = s->entries[s->current].fn->code[pc];
  return oti_errorf(s->rt, "%%vm-install: function %zu, word %zu (%s): %s", s->current, pc,
                    in.op < OP_COUNT ? optimizer_opcode_names[in.op] + 1 : "?", problem);
}

/* Registers rA .. rA+count-1 lie in the frame. */
static bool install_registers(install_state* s, size_t pc, uint32_t first, size_t count) {
  if (first + count > 1 + s->entries[s->current].fn->locals)
    return install_word_error(s, pc, "register out of range");
  return true;
}

static bool install_register(install_state* s, size_t pc, uint32_t r) {
  return install_registers(s, pc, r, 1);
}

/* A register written by the instruction. Register 0 holds the closure. */
static bool install_target_register(install_state* s, size_t pc, uint32_t r) {
  if (!r) return install_word_error(s, pc, "register 0 holds the running closure");
  return install_register(s, pc, r);
}

static bool install_jump(install_state* s, size_t pc, uint32_t target) {
  if (target >= s->entries[s->current].fn->count)
    return install_word_error(s, pc, "jump target outside the function");
  return true;
}

/* A branch's target is the JUMP word after it. */
static bool install_branch(install_state* s, size_t pc) {
  oti_function* fn = s->entries[s->current].fn;
  if (pc + 1 >= fn->count || fn->code[pc + 1].op != OP_JUMP)
    return install_word_error(s, pc, "needs a jump word after it");
  return true;
}

static bool install_constant(install_state* s, size_t pc, uint32_t index) {
  if (index >= s->entries[s->current].constant_count)
    return install_word_error(s, pc, "constant index out of range");
  return true;
}

static bool install_global(install_state* s, size_t pc, uint32_t cell) {
  if (cell >= s->rt->global_count) return install_word_error(s, pc, "unknown global cell");
  return true;
}

/* Every capture of the child comes from a register or capture of fn. */
static bool install_closure(install_state* s, size_t pc, uint32_t child) {
  if (child >= s->count) return install_word_error(s, pc, "function index out of range");
  oti_function *fn = s->entries[s->current].fn, *made = s->entries[child].fn;
  for (size_t i = 0; i < made->capture_count; i++) {
    oti_capture c = made->captures[i];
    if (c.local ? c.index > fn->locals : c.index >= fn->capture_count)
      return install_word_error(s, pc, "the function's captures don't fit this frame");
  }
  return true;
}

static bool install_check_word(install_state* s, size_t pc) {
  oti_function* fn = s->entries[s->current].fn;
  oti_instruction in = fn->code[pc];
  uint32_t bc = oti_instruction_bc(in);
  switch ((oti_opcode)in.op) {
    case OP_MOVE:
    case OP_UNBOX:
    case OP_CAR:
    case OP_CDR:
    case OP_NOT:
    case OP_NULLP:
    case OP_PAIRP: return install_target_register(s, pc, in.a) && install_register(s, pc, in.b);
    case OP_IMM:
      if (!optimizer_loadable(bc)) return install_word_error(s, pc, "not an immediate value");
      return install_target_register(s, pc, in.a);
    case OP_RETURN_IMM:
      return optimizer_loadable(bc) || install_word_error(s, pc, "not an immediate value");
    case OP_CONST: return install_target_register(s, pc, in.a) && install_constant(s, pc, bc);
    case OP_GLOBAL: return install_target_register(s, pc, in.a) && install_global(s, pc, bc);
    case OP_SET_GLOBAL:
    case OP_DEFINE_GLOBAL: return install_register(s, pc, in.a) && install_global(s, pc, bc);
    case OP_UPVALUE:
    case OP_UPVALUE_BOX:
    case OP_SET_UPVALUE:
      if (in.b >= fn->capture_count) return install_word_error(s, pc, "capture index out of range");
      return in.op == OP_SET_UPVALUE ? install_register(s, pc, in.a)
                                     : install_target_register(s, pc, in.a);
    case OP_BOX:
    case OP_RESULT: return install_target_register(s, pc, in.a);
    case OP_CHECK:
    case OP_RAISE:
    case OP_RETURN:
    case OP_CLEANUP: return install_register(s, pc, in.a);
    case OP_SET_BOX:
    case OP_BIND_PARAM: return install_register(s, pc, in.a) && install_register(s, pc, in.b);
    case OP_CLEAR:
      return (!in.b || install_target_register(s, pc, in.a)) &&
             install_registers(s, pc, in.a, in.b);
    case OP_CLOSURE: return install_target_register(s, pc, in.a) && install_closure(s, pc, bc);
    case OP_JUMP: return install_jump(s, pc, bc);
    case OP_JFALSE:
    case OP_JTRUE:
    case OP_JBOUND: return install_register(s, pc, in.a) && install_jump(s, pc, bc);
    case OP_TRY: return install_target_register(s, pc, in.a) && install_jump(s, pc, bc);
    /* The callee and arguments are rA .. rA+n, and the result returns to rA. */
    case OP_CALL:
      return install_target_register(s, pc, in.a) && install_registers(s, pc, in.a, 1u + in.b);
    case OP_TAIL_CALL: return install_registers(s, pc, in.a, 1u + in.b);
    case OP_CALL_GLOBAL:
      return install_target_register(s, pc, in.a) && install_registers(s, pc, in.a, 1u + in.x) &&
             install_global(s, pc, bc);
    case OP_TAIL_CALL_GLOBAL:
      return install_registers(s, pc, in.a, 1u + in.x) && install_global(s, pc, bc);
    case OP_END_TRY:
    case OP_RUN_CLEANUP:
    case OP_END_RESTARTS:
    case OP_UNBIND_HANDLERS:
    case OP_UNBIND_PARAMS: return true;
    case OP_RESTARTS: return install_registers(s, pc, in.a, 3u * in.b) && install_branch(s, pc);
    case OP_BIND_HANDLERS: return install_registers(s, pc, in.a, 2u * in.b);
    case OP_MAKE_PARAM:
    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
    case OP_LT:
    case OP_LE:
    case OP_GT:
    case OP_GE:
    case OP_NUMEQ:
    case OP_EQ:
    case OP_CONS:
      return install_target_register(s, pc, in.a) && install_register(s, pc, in.b) &&
             install_register(s, pc, in.c);
    case OP_ADDI: return install_target_register(s, pc, in.a) && install_register(s, pc, in.b);
    case OP_BR_LT:
    case OP_BR_LE:
    case OP_BR_GT:
    case OP_BR_GE:
    case OP_BR_NUMEQ:
    case OP_BR_EQ:
      return install_register(s, pc, in.a) && install_register(s, pc, in.b) &&
             install_branch(s, pc);
    case OP_BR_LTI:
    case OP_BR_LEI:
    case OP_BR_GTI:
    case OP_BR_GEI:
    case OP_BR_NUMEQI:
    case OP_BR_NULL:
    case OP_BR_PAIR: return install_register(s, pc, in.a) && install_branch(s, pc);
    case OP_BR_EQK:
      return install_register(s, pc, in.a) && install_constant(s, pc, bc) && install_branch(s, pc);
    case OP_COUNT: break;
  }
  return install_word_error(s, pc, "unknown opcode");
}

static bool install_check(install_state* s) {
  oti_function* fn = s->entries[s->current].fn;
  for (size_t pc = 0; pc < fn->count; pc++)
    if (!install_check_word(s, pc)) return false;
  switch (fn->code[fn->count - 1].op) {
    case OP_JUMP:
    case OP_RETURN:
    case OP_RETURN_IMM:
    case OP_TAIL_CALL:
    case OP_TAIL_CALL_GLOBAL:
    case OP_RAISE: break;
    default: return install_fail(s, "code must not run past its last word", NULL);
  }
  install_entry* e = &s->entries[s->current];
  for (size_t i = 0; i < e->static_count; i++)
    if (s->entries[e->statics[2 * i + 1]].fn->capture_count)
      return install_fail(s, "a static closure's function has captures", NULL);
  return true;
}

static void install_free(install_state* s, bool installed) {
  for (size_t i = 0; i < s->count; i++) {
    install_entry* e = &s->entries[i];
    if (!installed && e->fn) {
      if (e->id != UINT32_MAX) {
        s->rt->functions[e->id] = NULL;
        if (e->id < s->rt->function_free) s->rt->function_free = e->id;
      }
      oti_free_function(e->fn);
    }
    free(e->constants);
    free(e->statics);
  }
  free(s->entries);
}

/* Give every function an id, map the constants and make the static closures,
 * then rewrite the unit's constant and function indexes. The closures are the
 * only allocation, so the unit is read before them. */
static bool install_link(install_state* s, uint32_t space) {
  ot_state* rt = s->rt;
  for (size_t i = 0; i < s->count; i++) {
    install_entry* e = &s->entries[i];
    e->fn->namespace = space;
    e->fn->source = rt->source;
    e->id = oti_add_function(rt, e->fn);
    if (e->id == UINT32_MAX) return false;
  }
  for (size_t i = 0; i < s->count; i++) {
    install_entry* e = &s->entries[i];
    s->current = i;
    ot_value* items = NULL;
    size_t length = 0;
    ot_value descriptor = object_fields(oti_as_array(s->slots[0])->data)[i];
    if (!install_list(s, descriptor, KEY_CONSTANTS, &items, &length)) return false;
    /* Statics' slots were marked 0 by install_read. */
    for (size_t k = 0; k < length; k++)
      if (e->constants[k] == UINT32_MAX &&
          (e->constants[k] = oti_constant(rt, items[k])) == UINT32_MAX)
        return false;
  }
  for (size_t i = 0; i < s->count; i++) {
    install_entry* e = &s->entries[i];
    for (size_t k = 0; k < e->static_count; k++) {
      uint32_t id = s->entries[e->statics[2 * k + 1]].id;
      ot_value closure = oti_alloc(rt, K_CLOSURE, 0, sizeof(id));
      if (!closure) return false;
      memcpy(object_bytes(closure), &id, sizeof(id));
      uint32_t constant = oti_constant(rt, closure);
      if (constant == UINT32_MAX) return false;
      e->constants[e->statics[2 * k]] = constant;
    }
    oti_function* fn = e->fn;
    for (size_t pc = 0; pc < fn->count; pc++) {
      oti_instruction* in = &fn->code[pc];
      uint32_t bc = oti_instruction_bc(*in);
      if (in->op == OP_CONST || in->op == OP_BR_EQK) bc = e->constants[bc];
      else if (in->op == OP_CLOSURE) bc = s->entries[bc].id;
      else continue;
      in->b = (uint16_t)(bc & 0xffff);
      in->c = (uint16_t)(bc >> 16);
    }
  }
  return true;
}

static ot_value optimizer_vm_install(ot_state* rt, size_t first, size_t count) {
  if (!optimizer_arity(rt, "%vm-install", count, 2)) return 0;
  uint32_t space;
  if (!optimizer_namespace(rt, "%vm-install", rt->stack[first + 1], &space)) return 0;
  if (!oti_kind(rt->stack[first], K_ARRAY) || !oti_as_array(rt->stack[first])->length)
    return oti_type_error(rt, "%vm-install", "a non-empty array of functions", rt->stack[first]), 0;
  install_state s = {.rt = rt};
  s.slots[0] = rt->stack[first];
  for (size_t i = 1; i <= KEY_COUNT; i++) s.slots[i] = OT_NIL;
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, s.slots, 1 + KEY_COUNT);
  bool ok = true;
  for (unsigned k = 0; ok && k < KEY_COUNT; k++)
    ok =
        (s.slots[1 + k] = oti_intern(rt, K_KEYWORD, install_keys[k], strlen(install_keys[k]))) != 0;
  /* From here until install_link makes the static closures, nothing allocates
   * on the managed heap. */
  if (ok) {
    s.count = oti_as_array(s.slots[0])->length;
    s.entries = calloc(s.count, sizeof(install_entry));
    if (!s.entries) ok = oti_error(rt, "out of host memory");
  }
  for (size_t i = 0; ok && i < s.count; i++) {
    s.entries[i].id = UINT32_MAX;
    s.entries[i].fn = calloc(1, sizeof(oti_function));
    if (!s.entries[i].fn) ok = oti_error(rt, "out of host memory");
  }
  for (s.current = 0; ok && s.current < s.count; s.current++)
    ok = install_read(&s, object_fields(oti_as_array(s.slots[0])->data)[s.current]);
  for (s.current = 0; ok && s.current < s.count; s.current++) ok = install_check(&s);
  ok = ok && install_link(&s, space);
  ot_value result = ok ? ot_int(s.entries[0].id) : 0;
  if (s.entries) install_free(&s, ok);
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

static ot_value optimizer_set(ot_state* rt, size_t first, size_t count) {
  if (!optimizer_arity(rt, "%set-optimizer!", count, 1)) return 0;
  ot_value f = rt->stack[first];
  if (f != OT_NIL && !oti_procedure(f))
    return oti_type_error(rt, "%set-optimizer!", "a procedure or nil", f), 0;
  rt->optimizer = f;
  return OT_NIL;
}

bool oti_optimize(ot_state* rt, const oti_ast* form, uint32_t* function, bool* installed) {
  *installed = false;
  if (rt->optimizer == OT_NIL || rt->optimizing) return true;
  ot_value slots[2] = {OT_NIL, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, slots, 2);
  const char* space = rt->namespaces[rt->current_namespace];
  bool ok = (slots[0] = oti_form_datum(rt, form)) &&
            (slots[1] = oti_intern(rt, K_SYMBOL, space, strlen(space))) &&
            (slots[1] = oti_cons(rt, slots[1], OT_EMPTY)) &&
            (slots[0] = oti_cons(rt, slots[0], slots[1]));
  ot_value result = 0;
  if (ok) {
    rt->optimizing = true;
    result = oti_call_list(rt, rt->optimizer, slots[0]);
    rt->optimizing = false;
  }
  oti_roots_pop(&rt->heap, &roots);
  if (!result) return false;
  if (result == OT_NIL) return true;
  /* An entry: no parameters and no captures, as %vm-install returns. */
  oti_function* fn = ot_is_int(result) && ot_to_int(result) >= 0 &&
                             (uintptr_t)ot_to_int(result) < rt->function_count
                         ? rt->functions[ot_to_int(result)]
                         : NULL;
  if (!fn || fn->arity || fn->variadic || fn->capture_count)
    return oti_type_error(rt, "optimizer", "nil or an installed entry function id", result);
  *function = (uint32_t)ot_to_int(result);
  *installed = true;
  return true;
}

bool ot_load_optimizer(ot_state* rt) {
  if (!oti_host_entry(rt)) return false;
  const char* setting = getenv("OTIUM_OPTIMIZE");
  if (!setting || !*setting || !strcmp(setting, "0")) return true;
  const char* enable = "(require otium.compiler.driver) (otium.compiler.driver/enable!)";
  uint32_t saved = rt->current_namespace, source = rt->source;
  rt->current_namespace = 0;
  rt->source = OTI_NO_SOURCE;
  ot_value result;
  bool ok = ot_eval(rt, enable, strlen(enable), &result);
  rt->current_namespace = saved;
  rt->source = source;
  return ok;
}

const oti_builtin_def oti_optimizer_builtins[] = {
    {B_RESOLVE_GLOBAL, "%resolve-global", optimizer_resolve_global},
    {B_GLOBAL_CELL, "%global-cell", optimizer_global_cell},
    {B_CELL_INFO, "%cell-info", optimizer_cell_info},
    {B_FORM_OFFSET, "%form-offset", optimizer_form_offset},
    {B_VM_OPCODES, "%vm-opcodes", optimizer_vm_opcodes},
    {B_IMMEDIATE_BITS, "%immediate-bits", optimizer_immediate_bits},
    {B_VM_INSTALL, "%vm-install", optimizer_vm_install},
    {B_SET_OPTIMIZER, "%set-optimizer!", optimizer_set},
    {0, NULL, NULL},
};
