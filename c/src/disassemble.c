#include "internal.h"

/* Readable listings of compiled functions, for tests and for looking at what
 * the compiler emits. Set OTIUM_DUMP_CODE to print every top-level form's code
 * to stderr as it is compiled. */

static const char* const opcode_names[OP_COUNT] = {
    [OP_MOVE] = "MOVE",
    [OP_IMM] = "IMM",
    [OP_CONST] = "CONST",
    [OP_GLOBAL] = "GLOBAL",
    [OP_SET_GLOBAL] = "SET_GLOBAL",
    [OP_DEFINE_GLOBAL] = "DEFINE_GLOBAL",
    [OP_UPVALUE] = "UPVALUE",
    [OP_UPVALUE_BOX] = "UPVALUE_BOX",
    [OP_SET_UPVALUE] = "SET_UPVALUE",
    [OP_BOX] = "BOX",
    [OP_UNBOX] = "UNBOX",
    [OP_SET_BOX] = "SET_BOX",
    [OP_CHECK] = "CHECK",
    [OP_CLEAR] = "CLEAR",
    [OP_CLOSURE] = "CLOSURE",
    [OP_JUMP] = "JUMP",
    [OP_JFALSE] = "JFALSE",
    [OP_JTRUE] = "JTRUE",
    [OP_JBOUND] = "JBOUND",
    [OP_CALL] = "CALL",
    [OP_TAIL_CALL] = "TAIL_CALL",
    [OP_CALL_GLOBAL] = "CALL_GLOBAL",
    [OP_TAIL_CALL_GLOBAL] = "TAIL_CALL_GLOBAL",
    [OP_RETURN] = "RETURN",
    [OP_RETURN_IMM] = "RETURN_IMM",
    [OP_TRY] = "TRY",
    [OP_END_TRY] = "END_TRY",
    [OP_RAISE] = "RAISE",
    [OP_CLEANUP] = "CLEANUP",
    [OP_RUN_CLEANUP] = "RUN_CLEANUP",
    [OP_RESTARTS] = "RESTARTS",
    [OP_RESULT] = "RESULT",
    [OP_END_RESTARTS] = "END_RESTARTS",
    [OP_BIND_HANDLERS] = "BIND_HANDLERS",
    [OP_UNBIND_HANDLERS] = "UNBIND_HANDLERS",
    [OP_MAKE_PARAM] = "MAKE_PARAM",
    [OP_BIND_PARAM] = "BIND_PARAM",
    [OP_UNBIND_PARAMS] = "UNBIND_PARAMS",
    [OP_ADD] = "ADD",
    [OP_SUB] = "SUB",
    [OP_MUL] = "MUL",
    [OP_ADDI] = "ADDI",
    [OP_LT] = "LT",
    [OP_LE] = "LE",
    [OP_GT] = "GT",
    [OP_GE] = "GE",
    [OP_NUMEQ] = "NUMEQ",
    [OP_EQ] = "EQ",
    [OP_CONS] = "CONS",
    [OP_CAR] = "CAR",
    [OP_CDR] = "CDR",
    [OP_NOT] = "NOT",
    [OP_NULLP] = "NULLP",
    [OP_PAIRP] = "PAIRP",
    [OP_BR_LT] = "BR_LT",
    [OP_BR_LE] = "BR_LE",
    [OP_BR_GT] = "BR_GT",
    [OP_BR_GE] = "BR_GE",
    [OP_BR_NUMEQ] = "BR_NUMEQ",
    [OP_BR_EQ] = "BR_EQ",
    [OP_BR_LTI] = "BR_LTI",
    [OP_BR_LEI] = "BR_LEI",
    [OP_BR_GTI] = "BR_GTI",
    [OP_BR_GEI] = "BR_GEI",
    [OP_BR_NUMEQI] = "BR_NUMEQI",
    [OP_BR_NULL] = "BR_NULL",
    [OP_BR_PAIR] = "BR_PAIR",
    [OP_BR_EQK] = "BR_EQK",
};

const char* oti_opcode_name(unsigned op) { return op < OP_COUNT ? opcode_names[op] : "?"; }

static void print_immediate(ot_state* rt, FILE* out, uint32_t bits) {
  ot_value v = (ot_value)(intptr_t)(int32_t)bits;
  if (ot_is_int(v)) fprintf(out, " %ld", (long)ot_to_int(v));
  else if (v == V_UNBOUND) fputs(" unbound", out);
  else {
    fputc(' ', out);
    ot_write(rt, out, v);
  }
}

static void disassemble_function(ot_state* rt, uint32_t id, FILE* out) {
  oti_function* f = rt->functions[id];
  fprintf(out, "function %u: arity %zu", id, f->arity);
  if (f->optional) fprintf(out, " optional %zu", f->optional);
  if (f->rest) fputs(" rest", out);
  fprintf(out, " locals %zu captures %zu\n", f->locals, f->capture_count);
  for (size_t i = 0; i < f->count; i++) {
    oti_instruction in = f->code[i];
    uint32_t bc = oti_instruction_bc(in);
    fprintf(out, "%5zu  %s", i, oti_opcode_name(in.op));
    switch (in.op) {
      case OP_IMM:
        fprintf(out, " r%u", in.a);
        print_immediate(rt, out, bc);
        break;
      case OP_RETURN_IMM: print_immediate(rt, out, bc); break;
      case OP_CONST:
      case OP_GLOBAL:
      case OP_SET_GLOBAL:
      case OP_DEFINE_GLOBAL:
      case OP_CLOSURE:
      case OP_TRY: fprintf(out, " r%u %u", in.a, bc); break;
      case OP_JUMP: fprintf(out, " -> %u", bc); break;
      case OP_JFALSE:
      case OP_JTRUE:
      case OP_JBOUND: fprintf(out, " r%u -> %u", in.a, bc); break;
      case OP_CALL:
      case OP_TAIL_CALL: fprintf(out, " r%u %u", in.a, in.b); break;
      case OP_CALL_GLOBAL:
      case OP_TAIL_CALL_GLOBAL: fprintf(out, " r%u %u %u", in.a, in.x, bc); break;
      case OP_RETURN:
      case OP_BOX:
      case OP_CHECK:
      case OP_RAISE:
      case OP_RESULT: fprintf(out, " r%u", in.a); break;
      case OP_END_TRY:
      case OP_RUN_CLEANUP:
      case OP_END_RESTARTS: break;
      case OP_CLEANUP: fprintf(out, " r%u %u", in.a, in.x); break;
      case OP_UNBIND_HANDLERS:
      case OP_UNBIND_PARAMS: fprintf(out, " %u", in.b); break;
      case OP_RESTARTS:
        fprintf(out, " r%u %u", in.a, in.b);
        if (i + 1 < f->count) fprintf(out, " -> %u", oti_instruction_bc(f->code[++i]));
        break;
      case OP_ADDI: fprintf(out, " r%u r%u %d", in.a, in.b, (int16_t)in.c); break;
      case OP_BR_LTI:
      case OP_BR_LEI:
      case OP_BR_GTI:
      case OP_BR_GEI:
      case OP_BR_NUMEQI: fprintf(out, " r%u %d", in.a, (int16_t)in.c); goto target;
      case OP_BR_LT:
      case OP_BR_LE:
      case OP_BR_GT:
      case OP_BR_GE:
      case OP_BR_NUMEQ:
      case OP_BR_EQ: fprintf(out, " r%u r%u", in.a, in.b); goto target;
      case OP_BR_NULL:
      case OP_BR_PAIR: fprintf(out, " r%u", in.a); goto target;
      case OP_BR_EQK:
        fprintf(out, " r%u K%u", in.a, bc);
      target:
        fprintf(out, " is %s", in.x ? "true" : "false");
        if (i + 1 < f->count) fprintf(out, " -> %u", oti_instruction_bc(f->code[++i]));
        break;
      case OP_UPVALUE:
      case OP_UPVALUE_BOX:
      case OP_SET_UPVALUE:
      case OP_CLEAR:
      case OP_BIND_HANDLERS: fprintf(out, " r%u %u", in.a, in.b); break;
      case OP_UNBOX:
      case OP_SET_BOX:
      case OP_BIND_PARAM:
      case OP_MOVE:
      case OP_CAR:
      case OP_CDR:
      case OP_NOT:
      case OP_NULLP:
      case OP_PAIRP: fprintf(out, " r%u r%u", in.a, in.b); break;
      default: fprintf(out, " r%u r%u r%u", in.a, in.b, in.c); break;
    }
    fputc('\n', out);
  }
  for (size_t i = 0; i < f->count; i++)
    if (f->code[i].op == OP_CLOSURE) {
      uint32_t child = oti_instruction_bc(f->code[i]);
      if (child < rt->function_count && rt->functions[child]) {
        fputc('\n', out);
        disassemble_function(rt, child, out);
      }
    }
}

void oti_disassemble(ot_state* rt, uint32_t id, FILE* out) {
  if (id < rt->function_count && rt->functions[id]) disassemble_function(rt, id, out);
}
