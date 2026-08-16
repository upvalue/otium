#include "code.hpp"
#include "heap.hpp"
#include "printer.hpp"
#include "state.hpp"

namespace ot {

static constexpr OpInfo kOpInfo[] = {
#define OT_INFO(name, text, operand) {text, Operand::operand},
    OT_OPCODE_LIST(OT_INFO)
#undef OT_INFO
};
static_assert(sizeof(kOpInfo) / sizeof(kOpInfo[0]) == (u32)Op::Count);

const OpInfo& op_info(Op op) {
  OT_ASSERT((u8)op < (u8)Op::Count);
  return kOpInfo[(u8)op];
}

u32 operand_width(Operand operand) {
  switch (operand) {
    case Operand::None: return 0;
    case Operand::U8: return 1;
    case Operand::U16: return 2;
    case Operand::I32: return 4;
  }
  return 0;
}

Value make_code(State& state, const u8* bytes, u32 len, Value constants, const CodeSpec& spec) {
  if (constants.tag != Tag::Array) return raise_error(state, "code constants must be an array");
  Scope roots(state);
  Slot constantsRoot = roots.push(constants);
  Obj* obj = state.heap.alloc(ObjType::Code, sizeof(CodeData));
  CodeData* code = as_code(obj_v(Tag::Code, obj));
  code->len = len;
  code->constCount = as_array(constantsRoot.get())->len;
  code->nfixed = spec.nfixed;
  code->hasRest = spec.hasRest;
  code->nupvals = spec.nupvals;
  code->nlocals = spec.nlocals;
  code->maxStack = spec.maxStack;
  code->name = spec.name;

  if (len) {
    code->bytes = (u8*)malloc(len);
    if (!code->bytes) ot_fatal("code: cannot allocate bytecode");
    memcpy(code->bytes, bytes, len);
  }
  if (code->constCount) {
    if (code->constCount > UINT32_MAX / sizeof(Value)) ot_fatal("code: constant pool overflow");
    code->consts = (Value*)malloc((size_t)code->constCount * sizeof(Value));
    if (!code->consts) ot_fatal("code: cannot allocate constant pool");
    ArrayData* source = as_array(constantsRoot.get());
    memcpy(code->consts, source->items, (size_t)code->constCount * sizeof(Value));
  }
  return obj_v(Tag::Code, obj);
}

static bool fail(Buf* error, const char* fmt, u32 at) {
  if (error) error->printf(fmt, at);
  return false;
}

bool code_verify(Value value, Buf* error) {
  if (value.tag != Tag::Code) {
    if (error) error->appendCstr("not a code object");
    return false;
  }
  CodeData* code = as_code(value);
  if (!code->len) {
    if (error) error->appendCstr("empty bytecode");
    return false;
  }

  u8* boundaries = (u8*)calloc(code->len + 1u, 1);
  if (!boundaries) ot_fatal("code verifier: out of memory");
  u32 ip = 0;
  bool ok = true;
  while (ip < code->len) {
    u32 at = ip;
    boundaries[at] = 1;
    u8 raw = code->bytes[ip++];
    if (raw >= (u8)Op::Count) {
      ok = fail(error, "invalid opcode at %u", at);
      break;
    }
    Op op = (Op)raw;
    u32 width = operand_width(op_info(op).operand);
    if (width > code->len - ip) {
      ok = fail(error, "truncated operand at %u", at);
      break;
    }
    if ((op == Op::Const || op == Op::GetGlobal || op == Op::SetGlobal || op == Op::DefGlobal ||
         op == Op::Closure) &&
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
      u32 width = operand_width(op_info(op).operand);
      if (op == Op::Jump || op == Op::JumpFalse || op == Op::JumpFalsePeek ||
          op == Op::JumpTruePeek || op == Op::Loop) {
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
  free(boundaries);
  return ok;
}

static void append_hex_escape(Buf& out, u8 byte) {
  static const char hex[] = "0123456789abcdef";
  out.appendCstr("\\x");
  out.push(hex[byte >> 4]);
  out.push(hex[byte & 15]);
}

void code_print_ascii(Value value, Buf& out) {
  OT_ASSERT(value.tag == Tag::Code);
  CodeData* code = as_code(value);
  out.push('"');
  for (u32 i = 0; i < code->len; i++) {
    u8 shifted = (u8)(code->bytes[i] + (u8)'0');
    if (shifted >= 0x20 && shifted <= 0x7e && shifted != '"' && shifted != '\\')
      out.push((char)shifted);
    else append_hex_escape(out, shifted);
  }
  out.push('"');
}

void code_disassemble(State& state, Value value, Buf& out) {
  OT_ASSERT(value.tag == Tag::Code);
  Scope roots(state);
  Slot codeRoot = roots.push(value);
  u32 ip = 0;
  while (ip < as_code(codeRoot.get())->len) {
    CodeData* code = as_code(codeRoot.get());
    u32 at = ip;
    u8 raw = code->bytes[ip++];
    out.printf("%04x  ", at);
    if (raw >= (u8)Op::Count) {
      out.printf("<invalid %u>\n", raw);
      return;
    }
    Op op = (Op)raw;
    const OpInfo& info = op_info(op);
    out.appendCstr(info.name);
    switch (info.operand) {
      case Operand::None: break;
      case Operand::U8: out.printf(" %u", code->bytes[ip]); break;
      case Operand::U16: {
        u16 operand = code_read_u16(code->bytes + ip);
        out.printf(" %u", operand);
        if ((op == Op::Const || op == Op::GetGlobal || op == Op::SetGlobal || op == Op::DefGlobal ||
             op == Op::Closure) &&
            operand < code->constCount) {
          out.appendCstr(" ; ");
          print_repr(state, code->consts[operand], out);
        }
        break;
      }
      case Operand::I32: out.printf(" %d", code_read_i32(code->bytes + ip)); break;
    }
    ip += operand_width(info.operand);
    out.push('\n');
  }
}

}  // namespace ot
