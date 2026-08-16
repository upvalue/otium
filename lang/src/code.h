// code.h - bytecode objects, instruction metadata, verification, and dumps.
#pragma once
#include "common.h"
#include "slots.h"
#include "value.h"
#include "vec.h"

typedef struct State State;

typedef enum Operand : u8 { Operand_None, Operand_U8, Operand_U16, Operand_I32 } Operand;

// Keep this as the single opcode inventory. The enum, metadata table, and
// computed-goto labels are all generated from it so adding an instruction
// cannot silently put the two interpreters out of sync.
#define OT_OPCODE_LIST(X)                                                                          \
  X(Const, "const", U16)                                                                           \
  X(Nil, "nil", None)                                                                              \
  X(True, "true", None)                                                                            \
  X(False, "false", None)                                                                          \
  X(Null, "null", None)                                                                            \
  X(Int8, "int8", U8)                                                                              \
  X(Pop, "pop", None)                                                                              \
  X(GetLocal, "get-local", U16)                                                                    \
  X(SetLocal, "set-local", U16)                                                                    \
  X(GetBoxed, "get-boxed", U16)                                                                    \
  X(SetBoxed, "set-boxed", U16)                                                                    \
  X(MakeBox, "make-box", None)                                                                     \
  X(GetUpval, "get-upval", U16)                                                                    \
  X(SetUpval, "set-upval", U16)                                                                    \
  X(GetGlobal, "get-global", U16)                                                                  \
  X(SetGlobal, "set-global", U16)                                                                  \
  X(DefGlobal, "def-global", U16)                                                                  \
  X(Closure, "closure", U16)                                                                       \
  X(ToMacro, "to-macro", None)                                                                     \
  X(Call, "call", U16)                                                                             \
  X(TailCall, "tailcall", U16)                                                                     \
  X(Return, "return", None)                                                                        \
  X(Jump, "jump", I32)                                                                             \
  X(JumpFalse, "jump-false", I32)                                                                  \
  X(JumpFalsePeek, "jump-false-peek", I32)                                                         \
  X(JumpTruePeek, "jump-true-peek", I32)                                                           \
  X(Loop, "loop", I32)                                                                             \
  X(Cons, "cons", None)                                                                            \
  X(List, "list", U16)                                                                             \
  X(Append2, "append2", None)

typedef enum Op : u8 {
#define OT_ENUM(name, text, operand) Op_##name,
  OT_OPCODE_LIST(OT_ENUM)
#undef OT_ENUM
      Op_Count,
} Op;

typedef struct OpInfo {
  const char* name;
  Operand operand;
} OpInfo;

typedef struct CodeSpec {  // zero-init is the default spec: (CodeSpec){0}
  u32 nfixed;
  u32 hasRest;
  u32 nupvals;
  u32 nlocals;
  u32 maxStack;
  u32 name;
} CodeSpec;

const OpInfo* op_info(Op op);
u32 operand_width(Operand operand);

// constants must be an Array. It is rooted across allocation and copied into
// the Code object's pinned C-heap constant area.
Value make_code_ref(State* vm, Ref dst, const u8* bytes, u32 len, Ref constants,
                    const CodeSpec* spec);

u32 code_len_ref(State* vm, Ref code);
u32 code_const_count_ref(State* vm, Ref code);
u32 code_name_ref(State* vm, Ref code);
u8 code_byte_ref(State* vm, Ref code, u32 at);
void code_constant_ref(State* vm, Ref dst, Ref code, u32 index);

// Diagnostic helpers. code_print_ascii_ref uses femtolisp's byte+48
// convention, escaping bytes that are awkward in a quoted ASCII string.
bool code_verify_ref(State* vm, Ref code, Buf* error);
void code_print_ascii_ref(State* vm, Ref code, Buf* out);
void code_disassemble_ref(State* vm, Ref code, Buf* out);

static inline u16 code_read_u16(const u8* p) { return (u16)((u16)p[0] | ((u16)p[1] << 8)); }
static inline i32 code_read_i32(const u8* p) {
  u32 n = (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
  return (i32)n;
}
