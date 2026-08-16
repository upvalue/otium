#include "code.h"
#include "heap.h"
#include "printer.h"
#include "state.h"

static const OpInfo kOpInfo[] = {
#define OT_INFO(name, text, operand) {text, Operand_##operand},
    OT_OPCODE_LIST(OT_INFO)
#undef OT_INFO
};
static_assert(sizeof(kOpInfo) / sizeof(kOpInfo[0]) == (u32)Op_Count);

const OpInfo* op_info(Op op) {
  OT_ASSERT((u8)op < (u8)Op_Count);
  return &kOpInfo[(u8)op];
}

u32 operand_width(Operand operand) {
  switch (operand) {
    case Operand_None: return 0;
    case Operand_U8: return 1;
    case Operand_U16: return 2;
    case Operand_I32: return 4;
  }
  return 0;
}

Value make_code(State* vm, const u8* bytes, u32 len, Value constants, const CodeSpec* spec) {
  if (constants.tag != Tag_Array) return raise_error(vm, "code constants must be an array");
  u32 sc = scope_begin(vm);
  Slot constantsRoot = scope_push(vm, constants);
  Obj* obj = heap_alloc(&vm->heap, ObjType_Code, sizeof(CodeData));
  CodeData* code = as_code(obj_v(Tag_Code, obj));
  code->len = len;
  code->constCount = as_array(slot_get(constantsRoot))->len;
  code->nfixed = spec->nfixed;
  code->hasRest = spec->hasRest;
  code->nupvals = spec->nupvals;
  code->nlocals = spec->nlocals;
  code->maxStack = spec->maxStack;
  code->name = spec->name;

  if (len) {
    code->bytes = (u8*)ot_alloc(len);
    if (!code->bytes) ot_fatal("code: cannot allocate bytecode");
    memcpy(code->bytes, bytes, len);
  }
  if (code->constCount) {
    if (code->constCount > UINT32_MAX / sizeof(Value)) ot_fatal("code: constant pool overflow");
    code->consts = (Value*)ot_alloc((size_t)code->constCount * sizeof(Value));
    if (!code->consts) ot_fatal("code: cannot allocate constant pool");
    ArrayData* source = as_array(slot_get(constantsRoot));
    memcpy(code->consts, source->items, (size_t)code->constCount * sizeof(Value));
  }
  return scope_exit(vm, sc, obj_v(Tag_Code, obj));
}

static bool fail(Buf* error, const char* fmt, u32 at) {
  if (error) buf_printf(error, fmt, at);
  return false;
}

bool code_verify(Value value, Buf* error) {
  if (value.tag != Tag_Code) {
    if (error) buf_append_cstr(error, "not a code object");
    return false;
  }
  CodeData* code = as_code(value);
  if (!code->len) {
    if (error) buf_append_cstr(error, "empty bytecode");
    return false;
  }

  // ot_alloc does not promise zeroed memory (see common.h); the boundary table
  // is read before every slot is written, so zero it explicitly.
  u8* boundaries = (u8*)ot_alloc((size_t)code->len + 1u);
  if (!boundaries) ot_fatal("code verifier: out of memory");
  memset(boundaries, 0, (size_t)code->len + 1u);
  u32 ip = 0;
  bool ok = true;
  while (ip < code->len) {
    u32 at = ip;
    boundaries[at] = 1;
    u8 raw = code->bytes[ip++];
    if (raw >= (u8)Op_Count) {
      ok = fail(error, "invalid opcode at %u", at);
      break;
    }
    Op op = (Op)raw;
    u32 width = operand_width(op_info(op)->operand);
    if (width > code->len - ip) {
      ok = fail(error, "truncated operand at %u", at);
      break;
    }
    if ((op == Op_Const || op == Op_GetGlobal || op == Op_SetGlobal || op == Op_DefGlobal ||
         op == Op_Closure) &&
        code_read_u16(code->bytes + ip) >= code->constCount) {
      ok = fail(error, "constant index out of range at %u", at);
      break;
    }
    ip += width;
  }
  if (ok) boundaries[code->len] = 1;

  if (ok) {
    ip = 0;
    while (ip < code->len) {
      u32 at = ip;
      Op op = (Op)code->bytes[ip++];
      u32 width = operand_width(op_info(op)->operand);
      if (op == Op_Jump || op == Op_JumpFalse || op == Op_JumpFalsePeek || op == Op_JumpTruePeek ||
          op == Op_Loop) {
        i32 rel = code_read_i32(code->bytes + ip);
        i64 target = (i64)ip + 4 + rel;
        if (target < 0 || target >= code->len || !boundaries[(u32)target]) {
          ok = fail(error, "jump target is not an instruction at %u", at);
          break;
        }
      }
      ip += width;
    }
  }
  ot_free(boundaries);
  return ok;
}

static void append_hex_escape(Buf* out, u8 byte) {
  static const char hex[] = "0123456789abcdef";
  buf_append_cstr(out, "\\x");
  vec_push(out, hex[byte >> 4]);
  vec_push(out, hex[byte & 15]);
}

void code_print_ascii(Value value, Buf* out) {
  OT_ASSERT(value.tag == Tag_Code);
  CodeData* code = as_code(value);
  vec_push(out, '"');
  for (u32 i = 0; i < code->len; i++) {
    u8 shifted = (u8)(code->bytes[i] + (u8)'0');
    if (shifted >= 0x20 && shifted <= 0x7e && shifted != '"' && shifted != '\\')
      vec_push(out, (char)shifted);
    else append_hex_escape(out, shifted);
  }
  vec_push(out, '"');
}

void code_disassemble(State* vm, Value value, Buf* out) {
  OT_ASSERT(value.tag == Tag_Code);
  u32 sc = scope_begin(vm);
  Slot codeRoot = scope_push(vm, value);
  u32 ip = 0;
  while (ip < as_code(slot_get(codeRoot))->len) {
    CodeData* code = as_code(slot_get(codeRoot));
    u32 at = ip;
    u8 raw = code->bytes[ip++];
    buf_printf(out, "%04x  ", at);
    if (raw >= (u8)Op_Count) {
      buf_printf(out, "<invalid %u>\n", raw);
      scope_pop_to(vm, sc);
      return;
    }
    Op op = (Op)raw;
    const OpInfo* info = op_info(op);
    buf_append_cstr(out, info->name);
    switch (info->operand) {
      case Operand_None: break;
      case Operand_U8: buf_printf(out, " %u", code->bytes[ip]); break;
      case Operand_U16: {
        u16 operand = code_read_u16(code->bytes + ip);
        buf_printf(out, " %u", operand);
        if ((op == Op_Const || op == Op_GetGlobal || op == Op_SetGlobal || op == Op_DefGlobal ||
             op == Op_Closure) &&
            operand < code->constCount) {
          buf_append_cstr(out, " ; ");
          print_repr(vm, code->consts[operand], out);
        }
        break;
      }
      case Operand_I32: buf_printf(out, " %d", code_read_i32(code->bytes + ip)); break;
    }
    ip += operand_width(info->operand);
    vec_push(out, '\n');
  }
  scope_pop_to(vm, sc);
}
