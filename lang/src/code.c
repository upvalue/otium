#include "code.h"

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

static bool fail(Buf* error, const char* fmt, u32 at) {
  if (error) buf_printf(error, fmt, at);
  return false;
}

static u16 read_u16(State* vm, Ref code, u32 at) {
  return (u16)((u16)code_byte_ref(vm, code, at) |
               ((u16)code_byte_ref(vm, code, at + 1) << 8));
}

static i32 read_i32(State* vm, Ref code, u32 at) {
  u32 n = (u32)code_byte_ref(vm, code, at) |
          ((u32)code_byte_ref(vm, code, at + 1) << 8) |
          ((u32)code_byte_ref(vm, code, at + 2) << 16) |
          ((u32)code_byte_ref(vm, code, at + 3) << 24);
  return (i32)n;
}

bool code_verify_ref(State* vm, Ref code, Buf* error) {
  if (ot_tag(vm, code) != Tag_Code) {
    if (error) buf_append_cstr(error, "not a code object");
    return false;
  }
  u32 len = code_len_ref(vm, code);
  if (!len) {
    if (error) buf_append_cstr(error, "empty bytecode");
    return false;
  }

  u8* boundaries = (u8*)ot_alloc((size_t)len + 1u);
  if (!boundaries) ot_fatal("code verifier: out of memory");
  memset(boundaries, 0, (size_t)len + 1u);
  u32 ip = 0;
  bool ok = true;
  while (ip < len) {
    u32 at = ip;
    boundaries[at] = 1;
    u8 raw = code_byte_ref(vm, code, ip++);
    if (raw >= (u8)Op_Count) {
      ok = fail(error, "invalid opcode at %u", at);
      break;
    }
    Op op = (Op)raw;
    u32 width = operand_width(op_info(op)->operand);
    if (width > len - ip) {
      ok = fail(error, "truncated operand at %u", at);
      break;
    }
    if ((op == Op_Const || op == Op_GetGlobal || op == Op_SetGlobal || op == Op_DefGlobal ||
         op == Op_Closure) &&
        read_u16(vm, code, ip) >= code_const_count_ref(vm, code)) {
      ok = fail(error, "constant index out of range at %u", at);
      break;
    }
    ip += width;
  }
  if (ok) boundaries[len] = 1;

  if (ok) {
    ip = 0;
    while (ip < len) {
      u32 at = ip;
      Op op = (Op)code_byte_ref(vm, code, ip++);
      u32 width = operand_width(op_info(op)->operand);
      if (op == Op_Jump || op == Op_JumpFalse || op == Op_JumpFalsePeek ||
          op == Op_JumpTruePeek || op == Op_Loop) {
        i64 target = (i64)ip + 4 + read_i32(vm, code, ip);
        if (target < 0 || target >= len || !boundaries[(u32)target]) {
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

void code_print_ascii_ref(State* vm, Ref code, Buf* out) {
  OT_ASSERT(ot_tag(vm, code) == Tag_Code);
  vec_push(out, '"');
  for (u32 i = 0; i < code_len_ref(vm, code); i++) {
    u8 shifted = (u8)(code_byte_ref(vm, code, i) + (u8)'0');
    if (shifted >= 0x20 && shifted <= 0x7e && shifted != '"' && shifted != '\\')
      vec_push(out, (char)shifted);
    else
      append_hex_escape(out, shifted);
  }
  vec_push(out, '"');
}

void code_disassemble_ref(State* vm, Ref code, Buf* out) {
  OT_ASSERT(ot_tag(vm, code) == Tag_Code);
  OT_SCOPE(vm);
  Ref constant = ot_push(vm);
  u32 ip = 0;
  while (ip < code_len_ref(vm, code)) {
    u32 at = ip;
    u8 raw = code_byte_ref(vm, code, ip++);
    buf_printf(out, "%04x  ", at);
    if (raw >= (u8)Op_Count) {
      buf_printf(out, "<invalid %u>\n", raw);
      return;
    }
    Op op = (Op)raw;
    const OpInfo* info = op_info(op);
    buf_append_cstr(out, info->name);
    switch (info->operand) {
      case Operand_None: break;
      case Operand_U8: buf_printf(out, " %u", code_byte_ref(vm, code, ip)); break;
      case Operand_U16: {
        u16 operand = read_u16(vm, code, ip);
        buf_printf(out, " %u", operand);
        if ((op == Op_Const || op == Op_GetGlobal || op == Op_SetGlobal || op == Op_DefGlobal ||
             op == Op_Closure) &&
            operand < code_const_count_ref(vm, code)) {
          buf_append_cstr(out, " ; ");
          code_constant_ref(vm, constant, code, operand);
          ot_repr(vm, constant, out);
        }
        break;
      }
      case Operand_I32: buf_printf(out, " %d", read_i32(vm, code, ip)); break;
    }
    ip += operand_width(info->operand);
    vec_push(out, '\n');
  }
}
