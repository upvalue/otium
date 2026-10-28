#include "internal.h"

/* Native code for x86-64. The compiler, written in Otium, hands over a unit:
 * an instruction stream close to sljit's own operations, with registers
 * already allocated, plus tables of literals, global cells, functions and
 * frame descriptors. This file turns units into machine code, keeps the code
 * objects, and provides what that code calls: the entry trampoline, stubs
 * for every callee that isn't a closure, the unwinder, and helpers for
 * allocation, slow paths and control forms. See
 * scratch/native-compiler-design.md, sections 6 to 9.
 *
 * Registers, in sljit's names: S0 (rbx) holds the state and S1 (r15) the
 * allocation pointer, heap.start + heap.used, which is stored back before
 * every call into C. Allocation compares a bumped S1 with heap.end. At a call R0 holds the argument count, R1 the callee and R2 to R6 the
 * first five arguments; the rest are in native.args from index 5. The result
 * comes back in R0. Every register but S0 and S1 is clobbered by a call.
 *
 * Frames are sljit's: the return address, any registers its prologue pushed,
 * then the locals, which hold spilled values and registers saved around
 * slow paths. Each call site has a descriptor, found by its return address,
 * that gives the frame size and the slots holding live values. */

#ifdef OTIUM_NATIVE

#include "sljit-config.h"
#include "sljitLir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* The stream format. Integers are unsigned or signed LEB128. */
enum {
  NS_END,
  NS_FUNC,     /* f */
  NS_ENTER,    /* scratches slots */
  NS_LABEL,    /* label */
  NS_MOV,      /* mode dst src */
  NS_OP2,      /* op flags dst a b */
  NS_OP2U,     /* op flags a b */
  NS_CMPJ,     /* cond a b label */
  NS_JUMP,     /* label */
  NS_JCOND,    /* cond label */
  NS_FLAGS,    /* op dst cond */
  NS_CALL,     /* tail src descriptor */
  NS_CCALL,    /* helper args descriptor */
  NS_RET,      /* src */
  NS_ADDR,     /* dst label */
  NS_SPBASE,   /* dst offset */
  NS_POLL,     /* label: jump there when the stack pointer is below native.limit */
  NS_COUNT
};

enum {
  NO_REG,     /* R n */
  NO_IMM,     /* signed value */
  NO_MEM,     /* base offset */
  NO_MEM2,    /* base index shift */
  NO_SLOT,    /* i */
  NO_CELL,    /* unit cell, field */
  NO_CONST,   /* unit literal */
  NO_STATE,   /* state field */
  NO_RT,
  NO_ALLOC,
  NO_CELL_INDEX, /* immediate: the cell number of a unit cell */
  NO_FIXNUM,     /* immediate: the tagged word of a fixnum */
};

/* State fields the stream can name. */
enum {
  SF_HEAP_END,
  SF_HEAP_START,
  SF_LIMIT,
  SF_ARGS,
  SF_CALL_OTHER,
  SF_NATIVE_SP,
  SF_CALLEE,
  SF_ARGC,
  SF_COUNT
};

static const sljit_s32 mov_ops[] = {SLJIT_MOV,     SLJIT_MOV_U8,  SLJIT_MOV_S8,  SLJIT_MOV_U16,
                                    SLJIT_MOV_S16, SLJIT_MOV_U32, SLJIT_MOV_S32, SLJIT_MOV32};
static const sljit_s32 op2_ops[] = {SLJIT_ADD, SLJIT_SUB,  SLJIT_MUL,  SLJIT_AND,
                                    SLJIT_OR,  SLJIT_XOR,  SLJIT_SHL,  SLJIT_LSHR,
                                    SLJIT_ASHR};

/* Registers R7 and up overlap sljit's saved registers, which a prologue
 * pushes when the function uses them. */
#define SCRATCH_REGISTERS 7

typedef struct {
  uint32_t ra;       /* return address, as an offset from the code start */
  uint32_t frame;    /* bytes from the frame's stack pointer to its return address */
  uint32_t offset;   /* source offset of the call, or UINT32_MAX */
  uint32_t function; /* the oti_function id, or UINT32_MAX for runtime stubs */
  uint32_t slots;    /* first word of the live slot bitmap in the code object */
  uint16_t words;    /* bitmap words */
  /* A call in tail position that keeps its frame, a builtin's: backtraces
   * leave it out when it is innermost, as the VM's do. */
  bool tail;
} oti_descriptor;

typedef struct oti_code {
  uintptr_t start, end;
  oti_descriptor* descriptors;
  size_t descriptor_count;
  uint64_t* bitmaps;
  /* Constant slots the code reads, and the functions it defines. */
  uint32_t *constants, *functions;
  size_t constant_count, function_count;
  bool marked;
} oti_code;

typedef struct oti_code_chunk {
  unsigned char* base;
  size_t used, size;
  struct oti_code_chunk* next;
} oti_code_chunk;

/* Runtime stubs, in a code object of their own. */
enum { STUB_CALL_OTHER, STUB_UNBOUND, STUB_EVALUATOR, STUB_COUNT };

struct oti_native_runtime {
  oti_code_chunk* chunks;
  oti_code** codes;
  size_t code_count, code_capacity;
  uintptr_t stubs[STUB_COUNT];
  unsigned char* stack;
  size_t stack_bytes;
  uintptr_t limit;
  FILE* perf_map;
  bool verbose;
};

/* The trampoline and resume routine, below. */
ot_value oti_native_enter(ot_state* rt, ot_value callee, uintptr_t entry, size_t argc);
_Noreturn void oti_native_resume(ot_state* rt, uintptr_t sp, uintptr_t pc, ot_value value);
extern char oti_native_return[], oti_native_fail[];

#define OFF_SP 0
#define OFF_ENTRY 8
#define OFF_LIMIT 16
#define OFF_TOP 24
#define OFF_ARGS 32
#define OFF_DEPTH 40
#define OFF_HEAP_START 88
#define OFF_HEAP_USED 112
#define STR_(x) #x
#define STR(x) STR_(x)

_Static_assert(offsetof(ot_state, native.sp) == OFF_SP, "trampoline offsets");
_Static_assert(offsetof(ot_state, native.entry) == OFF_ENTRY, "trampoline offsets");
_Static_assert(offsetof(ot_state, native.limit) == OFF_LIMIT, "trampoline offsets");
_Static_assert(offsetof(ot_state, native.stack_top) == OFF_TOP, "trampoline offsets");
_Static_assert(offsetof(ot_state, native.args) == OFF_ARGS, "trampoline offsets");
_Static_assert(offsetof(ot_state, native.depth) == OFF_DEPTH, "trampoline offsets");
_Static_assert(offsetof(ot_state, heap.used) == OFF_HEAP_USED, "trampoline offsets");
_Static_assert(offsetof(ot_state, heap.start) == OFF_HEAP_START, "trampoline offsets");

/* oti_native_enter(rt, callee, entry, argc): call native code from C with the
 * arguments in native.args. The outermost entry switches to the Otium stack;
 * a nested one is already on it, since C helpers run there. Either way it
 * leaves an entry record under the call (the previous native.sp, the
 * previous record and the stack pointer to return on) that frame walking
 * and unwinding use. The unwinder returns failure here through
 * oti_native_fail, with the stack pointer at the record. */
__asm__(".text\n"
        ".p2align 4\n"
        ".globl oti_native_enter\n"
        ".type oti_native_enter,@function\n"
        "oti_native_enter:\n"
        "  push %rbp\n"
        "  push %rbx\n"
        "  push %r12\n"
        "  push %r13\n"
        "  push %r14\n"
        "  push %r15\n"
        "  mov %rdi, %rbx\n"
        "  mov %rsp, %r12\n"
        "  cmpq $0, " STR(OFF_DEPTH) "(%rbx)\n"
        "  jne 1f\n"
        "  mov " STR(OFF_TOP) "(%rbx), %rsp\n"
        "1:\n"
        "  and $-16, %rsp\n"
        "  sub $32, %rsp\n"
        "  mov " STR(OFF_SP) "(%rbx), %rax\n"
        "  mov %rax, 0(%rsp)\n"
        "  mov " STR(OFF_ENTRY) "(%rbx), %rax\n"
        "  mov %rax, 8(%rsp)\n"
        "  mov %r12, 16(%rsp)\n"
        "  mov %rsp, " STR(OFF_ENTRY) "(%rbx)\n"
        "  movq $0, " STR(OFF_SP) "(%rbx)\n"
        "  incq " STR(OFF_DEPTH) "(%rbx)\n"
        "  mov " STR(OFF_HEAP_USED) "(%rbx), %r15\n"
        "  add " STR(OFF_HEAP_START) "(%rbx), %r15\n"
        "  mov %rdx, %r9\n"
        "  mov %rcx, %rax\n"
        "  mov " STR(OFF_ARGS) "(%rbx), %r10\n"
        "  mov 0(%r10), %rdi\n"
        "  mov 8(%r10), %rcx\n"
        "  mov 16(%r10), %r8\n"
        "  mov 24(%r10), %r11\n"
        "  mov 32(%r10), %r10\n"
        "  call *%r9\n"
        ".globl oti_native_return\n"
        "oti_native_return:\n"
        "  jmp 2f\n"
        ".globl oti_native_fail\n"
        "oti_native_fail:\n"
        "  xor %eax, %eax\n"
        "2:\n"
        "  sub " STR(OFF_HEAP_START) "(%rbx), %r15\n"
        "  mov %r15, " STR(OFF_HEAP_USED) "(%rbx)\n"
        "  decq " STR(OFF_DEPTH) "(%rbx)\n"
        "  mov 0(%rsp), %rdx\n"
        "  mov %rdx, " STR(OFF_SP) "(%rbx)\n"
        "  mov 8(%rsp), %rdx\n"
        "  mov %rdx, " STR(OFF_ENTRY) "(%rbx)\n"
        "  mov 16(%rsp), %rsp\n"
        "  pop %r15\n"
        "  pop %r14\n"
        "  pop %r13\n"
        "  pop %r12\n"
        "  pop %rbx\n"
        "  pop %rbp\n"
        "  ret\n"
        ".size oti_native_enter, .-oti_native_enter\n"
        ".p2align 4\n"
        ".globl oti_native_resume\n"
        ".type oti_native_resume,@function\n"
        "oti_native_resume:\n"
        "  mov %rdi, %rbx\n"
        "  mov %rsi, %rsp\n"
        "  mov " STR(OFF_HEAP_USED) "(%rbx), %r15\n"
        "  add " STR(OFF_HEAP_START) "(%rbx), %r15\n"
        "  mov %rcx, %rax\n"
        "  jmp *%rdx\n"
        ".size oti_native_resume, .-oti_native_resume\n");

/* --- Code space -------------------------------------------------------- */

#define CODE_CHUNK ((size_t)4 << 20)

static size_t page_size(void) {
  static size_t page;
  if (!page) page = (size_t)sysconf(_SC_PAGESIZE);
  return page;
}

/* Pages are writable only while sljit fills them, then executable. */
static void protect(void* start, size_t size, int prot) {
  uintptr_t page = page_size(), from = (uintptr_t)start & ~(page - 1);
  uintptr_t to = ((uintptr_t)start + size + page - 1) & ~(page - 1);
  if (mprotect((void*)from, to - from, prot)) {
    perror("otium: code space protection");
    abort();
  }
}

void* oti_code_space_alloc(void* space, size_t size) {
  oti_native_runtime* n = space;
  size = (size + 15) & ~(size_t)15;
  oti_code_chunk* c = n->chunks;
  if (!c || c->size - c->used < size) {
    size_t bytes = size > CODE_CHUNK ? (size + page_size() - 1) & ~(page_size() - 1) : CODE_CHUNK;
    void* base = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) return NULL;
    c = malloc(sizeof(*c));
    if (!c) {
      munmap(base, bytes);
      return NULL;
    }
    *c = (oti_code_chunk){base, 0, bytes, n->chunks};
    n->chunks = c;
  }
  void* p = c->base + c->used;
  c->used += size;
  protect(p, size, PROT_READ | PROT_WRITE);
  return p;
}

/* Code memory is kept for now; reclaimed code objects only free their
 * metadata. */
void oti_code_space_free(void* space, void* code) {
  (void)space;
  (void)code;
}

/* --- Code objects ------------------------------------------------------ */

static oti_code* code_at(const oti_native_runtime* n, uintptr_t address) {
  size_t low = 0, high = n->code_count;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    oti_code* c = n->codes[middle];
    if (address < c->start) high = middle;
    else if (address >= c->end) low = middle + 1;
    else return c;
  }
  return NULL;
}

static const oti_descriptor* descriptor_at(const oti_native_runtime* n, uintptr_t ra,
                                           const oti_code** owner) {
  const oti_code* c = code_at(n, ra);
  if (!c) return NULL;
  uint32_t offset = (uint32_t)(ra - c->start);
  size_t low = 0, high = c->descriptor_count;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    if (c->descriptors[middle].ra < offset) low = middle + 1;
    else high = middle;
  }
  if (low == c->descriptor_count || c->descriptors[low].ra != offset) return NULL;
  if (owner) *owner = c;
  return &c->descriptors[low];
}

static bool add_code(ot_state* rt, oti_code* code) {
  oti_native_runtime* n = rt->native_runtime;
  if (!oti_grow(rt, (void**)&n->codes, &n->code_capacity, n->code_count + 1, sizeof(oti_code*)))
    return false;
  size_t i = n->code_count;
  while (i && n->codes[i - 1]->start > code->start) {
    n->codes[i] = n->codes[i - 1];
    i--;
  }
  n->codes[i] = code;
  n->code_count++;
  return true;
}

static void free_code(oti_code* code) {
  free(code->descriptors);
  free(code->bitmaps);
  free(code->constants);
  free(code->functions);
  free(code);
}

/* --- The bridge -------------------------------------------------------- */

typedef struct {
  struct sljit_jump* jump;
  uint32_t label;
} oti_pending_jump;

typedef struct {
  struct sljit_label* label;
  uint32_t descriptor, frame, function;
} oti_site;

typedef struct {
  ot_state* rt;
  struct sljit_compiler* c;
  const unsigned char *p, *end;
  bool bad;
  const uint32_t *cells, *constants;
  size_t cell_count, constant_count;
  struct sljit_label** labels;
  size_t label_count;
  oti_pending_jump* jumps;
  size_t jump_count, jump_capacity;
  oti_site* sites;
  size_t site_count, site_capacity;
  struct sljit_label** entries;
  size_t function_count;
  uint32_t function, frame;
  const uint32_t* function_ids;
} oti_bridge;

static uint64_t read_unsigned(oti_bridge* b) {
  uint64_t value = 0;
  for (unsigned shift = 0; shift < 64; shift += 7) {
    if (b->p >= b->end) {
      b->bad = true;
      return 0;
    }
    unsigned char byte = *b->p++;
    value |= (uint64_t)(byte & 127) << shift;
    if (!(byte & 128)) return value;
  }
  b->bad = true;
  return 0;
}

/* Signed LEB128, which the Otium side writes without overflowing fixnums. */
static int64_t read_signed(oti_bridge* b) {
  uint64_t value = 0;
  for (unsigned shift = 0; shift < 70; shift += 7) {
    if (b->p >= b->end) {
      b->bad = true;
      return 0;
    }
    unsigned char byte = *b->p++;
    if (shift < 64) value |= (uint64_t)(byte & 127) << shift;
    if (!(byte & 128)) {
      if (shift + 7 < 64 && (byte & 64)) value |= ~(uint64_t)0 << (shift + 7);
      return (int64_t)value;
    }
  }
  b->bad = true;
  return 0;
}

static sljit_s32 register_operand(oti_bridge* b) {
  uint64_t r = read_unsigned(b);
  if (r <= 10) return SLJIT_R((sljit_s32)r);
  if (r == 11) return SLJIT_S0;
  if (r == 12) return SLJIT_S1;
  b->bad = true;
  return SLJIT_R0;
}

static sljit_sw state_offset(uint64_t field, bool* bad) {
  switch (field) {
    case SF_HEAP_END: return (sljit_sw)offsetof(ot_state, heap.end);
    case SF_HEAP_START: return (sljit_sw)offsetof(ot_state, heap.start);
    case SF_LIMIT: return (sljit_sw)offsetof(ot_state, native.limit);
    case SF_ARGS: return (sljit_sw)offsetof(ot_state, native.args);
    case SF_CALL_OTHER: return (sljit_sw)offsetof(ot_state, native_stubs[STUB_CALL_OTHER]);
    case SF_NATIVE_SP: return (sljit_sw)offsetof(ot_state, native.sp);
    case SF_CALLEE: return (sljit_sw)offsetof(ot_state, native.callee);
    case SF_ARGC: return (sljit_sw)offsetof(ot_state, native.argc);
    default: *bad = true; return 0;
  }
}

static void operand(oti_bridge* b, sljit_s32* op, sljit_sw* w) {
  uint64_t kind = read_unsigned(b), i;
  *w = 0;
  switch (kind) {
    case NO_REG: *op = register_operand(b); return;
    case NO_IMM:
      *op = SLJIT_IMM;
      *w = (sljit_sw)read_signed(b);
      return;
    case NO_MEM:
      *op = SLJIT_MEM1(register_operand(b));
      *w = (sljit_sw)read_signed(b);
      return;
    case NO_MEM2: {
      sljit_s32 base = register_operand(b), index = register_operand(b);
      *op = SLJIT_MEM2(base, index);
      *w = (sljit_sw)read_unsigned(b);
      if (*w > 3) b->bad = true;
      return;
    }
    case NO_SLOT:
      *op = SLJIT_MEM1(SLJIT_SP);
      *w = (sljit_sw)read_unsigned(b) * 8;
      return;
    case NO_CELL: {
      i = read_unsigned(b);
      uint64_t field = read_unsigned(b);
      if (i >= b->cell_count || field > 1) {
        b->bad = true;
        i = 0;
      }
      *op = SLJIT_MEM1(SLJIT_S0);
      *w = (sljit_sw)(OTI_CELLS_OFFSET + (b->cell_count ? b->cells[i] : 0) * sizeof(oti_cell) +
                      field * sizeof(ot_value));
      return;
    }
    case NO_CONST:
      i = read_unsigned(b);
      if (i >= b->constant_count) {
        b->bad = true;
        i = 0;
      }
      *op = SLJIT_MEM1(SLJIT_S0);
      *w = (sljit_sw)(OTI_CONSTANTS_OFFSET +
                      (b->constant_count ? b->constants[i] : 0) * sizeof(ot_value));
      return;
    case NO_STATE:
      *op = SLJIT_MEM1(SLJIT_S0);
      *w = state_offset(read_unsigned(b), &b->bad);
      return;
    case NO_RT: *op = SLJIT_S0; return;
    case NO_ALLOC: *op = SLJIT_S1; return;
    case NO_CELL_INDEX:
      i = read_unsigned(b);
      if (i >= b->cell_count) {
        b->bad = true;
        i = 0;
      }
      *op = SLJIT_IMM;
      *w = b->cell_count ? (sljit_sw)b->cells[i] : 0;
      return;
    case NO_FIXNUM:
      *op = SLJIT_IMM;
      *w = (sljit_sw)(((uint64_t)read_signed(b) << 1) | 1);
      return;
    default: b->bad = true; *op = SLJIT_IMM;
  }
}

static uint32_t read_label(oti_bridge* b) {
  uint64_t l = read_unsigned(b);
  if (l >= b->label_count) {
    b->bad = true;
    return 0;
  }
  return (uint32_t)l;
}

static void pend(oti_bridge* b, struct sljit_jump* jump, uint32_t label) {
  if (!jump) {
    b->bad = true;
    return;
  }
  if (!oti_grow(b->rt, (void**)&b->jumps, &b->jump_capacity, b->jump_count + 1,
                sizeof(oti_pending_jump))) {
    b->bad = true;
    return;
  }
  b->jumps[b->jump_count++] = (oti_pending_jump){jump, label};
}

static void site(oti_bridge* b, uint64_t descriptor) {
  if (!oti_grow(b->rt, (void**)&b->sites, &b->site_capacity, b->site_count + 1,
                sizeof(oti_site))) {
    b->bad = true;
    return;
  }
  b->sites[b->site_count++] =
      (oti_site){sljit_emit_label(b->c), (uint32_t)descriptor, b->frame, b->function};
}

static const void* helper_address(uint64_t id);

/* A call into C: S1 goes back to heap.used and the stack pointer to
 * native.sp first, so the collector sees both, and S1 is reloaded after. The
 * state goes in R0, ahead of the arguments the stream put in R1 up. */
static void c_call(struct sljit_compiler* c, const void* function, uint64_t args) {
  static const sljit_s32 types[] = {
      SLJIT_ARGS1(W, P), SLJIT_ARGS2(W, P, W), SLJIT_ARGS3(W, P, W, W), SLJIT_ARGS4(W, P, W, W, W)};
  sljit_emit_op2(c, SLJIT_SUB, SLJIT_MEM1(SLJIT_S0), (sljit_sw)offsetof(ot_state, heap.used),
                 SLJIT_S1, 0, SLJIT_MEM1(SLJIT_S0), (sljit_sw)offsetof(ot_state, heap.start));
  sljit_get_local_base(c, SLJIT_MEM1(SLJIT_S0), (sljit_sw)offsetof(ot_state, native.sp), 0);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_S0, 0);
  sljit_emit_icall(c, SLJIT_CALL, types[args], SLJIT_IMM, SLJIT_FUNC_ADDR(function));
}

static void after_c_call(struct sljit_compiler* c) {
  sljit_emit_op2(c, SLJIT_ADD, SLJIT_S1, 0, SLJIT_MEM1(SLJIT_S0),
                 (sljit_sw)offsetof(ot_state, heap.start), SLJIT_MEM1(SLJIT_S0),
                 (sljit_sw)offsetof(ot_state, heap.used));
}

/* The poll at function entries and loop back edges: one compare of the stack
 * pointer with native.limit, which sljit can't name, so it goes in as bytes:
 * cmp rsp, [rbx + limit]. */
static struct sljit_jump* poll(struct sljit_compiler* c) {
  _Static_assert(OFF_LIMIT < 128, "poll uses an 8-bit displacement");
  unsigned char cmp[] = {0x48, 0x3b, 0x63, OFF_LIMIT};
  sljit_emit_op_custom(c, cmp, sizeof(cmp));
  sljit_set_current_flags(c, SLJIT_CURRENT_FLAGS_SUB | SLJIT_CURRENT_FLAGS_COMPARE |
                                 SLJIT_SET_LESS);
  return sljit_emit_jump(c, SLJIT_LESS);
}

static void enter(struct sljit_compiler* c, sljit_s32 scratches, sljit_s32 slots, uint32_t* frame) {
  sljit_emit_enter(c, SLJIT_ENTER_REG_ARG | SLJIT_ENTER_KEEP(2), SLJIT_ARGS0(W), scratches, 2,
                   slots * 8);
  sljit_s32 pushed = scratches > SCRATCH_REGISTERS ? scratches - SCRATCH_REGISTERS : 0;
  *frame = (uint32_t)(c->local_size + pushed * 8);
}

static bool translate(oti_bridge* b) {
  struct sljit_compiler* c = b->c;
  sljit_s32 d, s1, s2;
  sljit_sw dw, s1w, s2w;
  for (;;) {
    if (b->bad) return false;
    uint64_t op = read_unsigned(b);
    switch (op) {
      case NS_END: return !b->bad;
      case NS_FUNC: {
        uint64_t f = read_unsigned(b);
        if (f >= b->function_count) return false;
        b->function = b->function_ids[f];
        b->entries[f] = sljit_emit_aligned_label(c, SLJIT_LABEL_ALIGN_16, NULL);
        break;
      }
      case NS_ENTER: {
        uint64_t scratches = read_unsigned(b), slots = read_unsigned(b);
        if (scratches < SCRATCH_REGISTERS || scratches > 11 || slots > 100000) return false;
        enter(c, (sljit_s32)scratches, (sljit_s32)slots, &b->frame);
        break;
      }
      case NS_LABEL: {
        uint32_t l = read_label(b);
        if (b->labels[l]) return false;
        b->labels[l] = sljit_emit_label(c);
        break;
      }
      case NS_MOV: {
        uint64_t mode = read_unsigned(b);
        if (mode >= sizeof(mov_ops) / sizeof(mov_ops[0])) return false;
        operand(b, &d, &dw);
        operand(b, &s1, &s1w);
        sljit_emit_op1(c, mov_ops[mode], d, dw, s1, s1w);
        break;
      }
      case NS_OP2:
      case NS_OP2U: {
        uint64_t code = read_unsigned(b), flags = read_unsigned(b);
        if (code >= sizeof(op2_ops) / sizeof(op2_ops[0])) return false;
        sljit_s32 o = op2_ops[code] | (flags == 1 ? SLJIT_SET_Z : flags ? SLJIT_SET((sljit_s32)(flags - 2)) : 0);
        if (op == NS_OP2) {
          operand(b, &d, &dw);
          operand(b, &s1, &s1w);
          operand(b, &s2, &s2w);
          sljit_emit_op2(c, o, d, dw, s1, s1w, s2, s2w);
        } else {
          operand(b, &s1, &s1w);
          operand(b, &s2, &s2w);
          sljit_emit_op2u(c, o, s1, s1w, s2, s2w);
        }
        break;
      }
      case NS_CMPJ: {
        uint64_t cond = read_unsigned(b);
        if (cond > SLJIT_SIG_LESS_EQUAL) return false;
        operand(b, &s1, &s1w);
        operand(b, &s2, &s2w);
        uint32_t l = read_label(b);
        pend(b, sljit_emit_cmp(c, (sljit_s32)cond, s1, s1w, s2, s2w), l);
        break;
      }
      case NS_JUMP: pend(b, sljit_emit_jump(c, SLJIT_JUMP), read_label(b)); break;
      case NS_JCOND: {
        uint64_t cond = read_unsigned(b);
        if (cond > SLJIT_NOT_CARRY) return false;
        pend(b, sljit_emit_jump(c, (sljit_s32)cond), read_label(b));
        break;
      }
      case NS_FLAGS: {
        uint64_t o = read_unsigned(b);
        operand(b, &d, &dw);
        uint64_t cond = read_unsigned(b);
        if (o > 2 || cond > SLJIT_NOT_CARRY) return false;
        static const sljit_s32 flag_ops[] = {SLJIT_MOV, SLJIT_OR, SLJIT_AND};
        sljit_emit_op_flags(c, flag_ops[o], d, dw, (sljit_s32)cond);
        break;
      }
      case NS_CALL: {
        uint64_t tail = read_unsigned(b);
        operand(b, &s1, &s1w);
        uint64_t descriptor = read_unsigned(b);
        sljit_emit_icall(c, SLJIT_CALL_REG_ARG | (tail ? SLJIT_CALL_RETURN : 0), SLJIT_ARGS0(W),
                         s1, s1w);
        if (!tail) site(b, descriptor);
        break;
      }
      case NS_CCALL: {
        uint64_t helper = read_unsigned(b), args = read_unsigned(b);
        uint64_t descriptor = read_unsigned(b);
        const void* address = helper_address(helper);
        if (!address || args > 3) return false;
        c_call(c, address, args);
        site(b, descriptor);
        after_c_call(c);
        break;
      }
      case NS_RET:
        operand(b, &s1, &s1w);
        sljit_emit_return(c, SLJIT_MOV, s1, s1w);
        break;
      case NS_ADDR: {
        operand(b, &d, &dw);
        pend(b, sljit_emit_op_addr(c, SLJIT_MOV_ADDR, d, dw), read_label(b));
        break;
      }
      case NS_SPBASE:
        operand(b, &d, &dw);
        sljit_get_local_base(c, d, dw, (sljit_sw)read_signed(b));
        break;
      case NS_POLL: pend(b, poll(c), read_label(b)); break;
      default: return false;
    }
    if (sljit_get_compiler_error(c)) return false;
  }
}

/* --- Function records -------------------------------------------------- */

static uint32_t register_function(ot_state* rt) {
  size_t id = rt->function_free;
  while (id < rt->function_count && rt->functions[id]) id++;
  if (id >= UINT32_MAX || !oti_grow(rt, (void**)&rt->functions, &rt->function_capacity, id + 1,
                                    sizeof(oti_function*))) {
    oti_error(rt, "too many functions");
    return UINT32_MAX;
  }
  oti_function* f = calloc(1, sizeof(*f));
  if (!f) {
    oti_error(rt, "out of host memory");
    return UINT32_MAX;
  }
  f->namespace = rt->current_namespace;
  f->source = rt->source;
  if (id == rt->function_count) rt->function_count++;
  rt->function_free = id + 1;
  rt->functions[id] = f;
  return (uint32_t)id;
}

static void release_function(ot_state* rt, uint32_t id) {
  if (id >= rt->function_count) return;
  oti_free_function(rt->functions[id]);
  rt->functions[id] = NULL;
  if (id < rt->function_free) rt->function_free = id;
}

/* A constant slot nothing else shares, for a unit's static closures. */
static uint32_t fresh_constant(ot_state* rt, ot_value value) {
  size_t index = rt->constant_free;
  while (index < rt->constant_count && rt->constant_state[index]) index++;
  if (index >= OTI_CONSTANT_RESERVE ||
      !oti_grow(rt, (void**)&rt->constant_state, &rt->constant_state_capacity, index + 1, 1)) {
    oti_error(rt, "too many constants");
    return UINT32_MAX;
  }
  if (index == rt->constant_count) rt->constant_count++;
  rt->constant_state[index] = 1;
  rt->constants[index] = value;
  rt->constant_free = index + 1;
  rt->constant_roots.count = rt->constant_count;
  return (uint32_t)index;
}

oti_function* oti_closure_fn(ot_state* rt, ot_value closure) {
  if (!rt->native_mode) return rt->functions[oti_closure_function(closure)];
  uintptr_t entry = oti_as_closure(closure)->code;
  const oti_code* c = code_at(rt->native_runtime, entry);
  if (!c) return NULL;
  for (size_t i = 0; i < c->function_count; i++) {
    oti_function* f = rt->functions[c->functions[i]];
    if (f && f->entry == entry) return f;
  }
  return NULL;
}

/* --- Installing units -------------------------------------------------- */

static ot_value* array_items(ot_value array, size_t* count) {
  if (!oti_kind(array, K_ARRAY)) {
    *count = 0;
    return NULL;
  }
  *count = oti_as_array(array)->length;
  if (!*count) return NULL;
  return oti_as_array_data(oti_as_array(array)->data)->items;
}

static bool unit_int(ot_value v, uint32_t* out) {
  if (!ot_is_int(v) || ot_to_int(v) < 0 || ot_to_int(v) > (intptr_t)UINT32_MAX - 1) return false;
  *out = (uint32_t)ot_to_int(v);
  return true;
}

static int compare_descriptors(const void* a, const void* b) {
  const oti_descriptor *x = a, *y = b;
  return x->ra < y->ra ? -1 : x->ra > y->ra;
}

static void write_perf_map(ot_state* rt, oti_code* code) {
  oti_native_runtime* n = rt->native_runtime;
  if (!n->perf_map) return;
  for (size_t i = 0; i < code->function_count; i++) {
    oti_function* f = rt->functions[code->functions[i]];
    uintptr_t end = i + 1 < code->function_count ? rt->functions[code->functions[i + 1]]->entry
                                                 : code->end;
    fprintf(n->perf_map, "%lx %lx %s\n", (unsigned long)f->entry, (unsigned long)(end - f->entry),
            f->name ? f->name : "fn");
  }
  fflush(n->perf_map);
}

/* Install a unit: [stream literals cells functions descriptors statics
 * max-args].
 * functions are [name arity optional rest? offset]; descriptors are
 * [function live-slots offset]; statics are [function literal], closures
 * without captures that the unit reads as literals. Returns the static
 * closure of function 0, the unit's top level. */
ot_value oti_native_install(ot_state* rt, size_t slot) {
  if (getenv("OTIUM_DUMP_UNITS")) {
    ot_write(rt, stderr, rt->stack[slot]);
    fputc('\n', stderr);
  }
  size_t parts_count;
  ot_value* parts = array_items(rt->stack[slot], &parts_count);
  if (parts_count < 6 || !oti_kind(parts[0], K_BYTEVECTOR)) {
    oti_error(rt, "native unit: malformed");
    return 0;
  }
  size_t literal_count, cell_count, function_count, descriptor_count, static_count;
  array_items(parts[1], &literal_count);
  array_items(parts[2], &cell_count);
  array_items(parts[3], &function_count);
  array_items(parts[4], &descriptor_count);
  array_items(parts[5], &static_count);
  if (!function_count) {
    oti_error(rt, "native unit: no functions");
    return 0;
  }
  /* Calls store arguments past the fifth in native.args. */
  uint32_t max_args = 0;
  if (parts_count > 6 && unit_int(parts[6], &max_args) && !oti_native_args(rt, max_args)) return 0;
  oti_bridge b = {.rt = rt};
  oti_code* code = calloc(1, sizeof(*code));
  uint32_t* cells = calloc(cell_count + 1, sizeof(uint32_t));
  uint32_t* constants = calloc(literal_count + 1, sizeof(uint32_t));
  uint32_t* functions = calloc(function_count, sizeof(uint32_t));
  b.entries = calloc(function_count, sizeof(struct sljit_label*));
  bool ok = code && cells && constants && functions && b.entries;
  if (!ok) oti_error(rt, "out of host memory");
  size_t made_functions = 0;
  bool installed = false;
  /* Static closures get fresh slots, filled once their code exists. */
  for (size_t i = 0; ok && i < literal_count; i++) {
    ot_value* literals = array_items(parts[1], &literal_count);
    bool fixed = false;
    ot_value* statics = array_items(parts[5], &static_count);
    for (size_t s = 0; s < static_count; s++) {
      size_t n;
      ot_value* entry = array_items(statics[s], &n);
      uint32_t literal;
      if (n == 2 && unit_int(entry[1], &literal) && literal == i) fixed = true;
    }
    constants[i] = fixed ? fresh_constant(rt, OT_NIL) : oti_constant(rt, literals[i]);
    ok = constants[i] != UINT32_MAX;
  }
  for (size_t i = 0; ok && i < cell_count; i++) {
    ot_value* items = array_items(parts[2], &cell_count);
    ok = unit_int(items[i], &cells[i]) && cells[i] < rt->global_count;
    if (!ok) oti_error(rt, "native unit: bad cell");
  }
  for (size_t i = 0; ok && i < function_count; i++) {
    uint32_t id = register_function(rt);
    if (id == UINT32_MAX) {
      ok = false;
      break;
    }
    functions[made_functions++] = id;
    ot_value* items = array_items(parts[3], &function_count);
    size_t n;
    ot_value* info = array_items(items[i], &n);
    uint32_t arity, optional, offset = UINT32_MAX;
    if (n < 5 || !unit_int(info[1], &arity) || !unit_int(info[2], &optional)) {
      ok = oti_error(rt, "native unit: bad function");
      break;
    }
    oti_function* f = rt->functions[id];
    f->arity = arity;
    f->optional = optional;
    f->rest = oti_truth(info[3]);
    f->variadic = f->rest || f->optional;
    if (unit_int(info[4], &offset)) f->offset = offset;
    else f->offset = UINT32_MAX;
    if (oti_kind(info[0], K_SYMBOL) || oti_kind(info[0], K_STRING)) {
      size_t length;
      const char* text = oti_text_bytes(info[0], &length);
      f->name = malloc(length + 1);
      if (f->name) {
        memcpy(f->name, text, length);
        f->name[length] = 0;
      }
    }
  }
  /* Descriptors, with their bitmaps, in stream order. */
  oti_descriptor* descriptors = calloc(descriptor_count + 1, sizeof(oti_descriptor));
  uint64_t* bitmaps = NULL;
  size_t bitmap_count = 0, bitmap_capacity = 0;
  if (ok && !descriptors) ok = oti_error(rt, "out of host memory");
  for (size_t i = 0; ok && i < descriptor_count; i++) {
    ot_value* items = array_items(parts[4], &descriptor_count);
    size_t n, slot_count;
    ot_value* info = array_items(items[i], &n);
    uint32_t function = UINT32_MAX, offset = UINT32_MAX;
    if (n < 3 || (info[0] != OT_NIL && (!unit_int(info[0], &function) || function >= function_count))) {
      ok = oti_error(rt, "native unit: bad descriptor");
      break;
    }
    unit_int(info[2], &offset);
    ot_value* slots = array_items(info[1], &slot_count);
    uint32_t highest = 0;
    for (size_t s = 0; s < slot_count; s++) {
      uint32_t slot;
      if (!unit_int(slots[s], &slot)) ok = oti_error(rt, "native unit: bad slot");
      else if (slot + 1 > highest) highest = slot + 1;
    }
    uint32_t words = (highest + 63) / 64;
    if (!ok || !oti_grow(rt, (void**)&bitmaps, &bitmap_capacity, bitmap_count + words + 1,
                         sizeof(uint64_t)))
      break;
    memset(bitmaps + bitmap_count, 0, (words + 1) * sizeof(uint64_t));
    for (size_t s = 0; s < slot_count; s++) {
      uint32_t slot = (uint32_t)ot_to_int(slots[s]);
      bitmaps[bitmap_count + slot / 64] |= (uint64_t)1 << (slot % 64);
    }
    descriptors[i] = (oti_descriptor){0, 0, offset,
                                      function == UINT32_MAX ? UINT32_MAX : functions[function],
                                      (uint32_t)bitmap_count, (uint16_t)words,
                                      n > 3 && oti_truth(info[3])};
    bitmap_count += words;
  }
  /* The stream. */
  size_t stream_length = ok ? oti_bytes_length(parts[0]) : 0;
  unsigned char* stream = ok ? malloc(stream_length + 1) : NULL;
  if (ok && !stream) ok = oti_error(rt, "out of host memory");
  if (ok) memcpy(stream, oti_bytes_data(parts[0]), stream_length);
  void* machine = NULL;
  if (ok) {
    b.c = sljit_create_compiler(NULL);
    b.p = stream;
    b.end = stream + stream_length;
    b.cells = cells;
    b.cell_count = cell_count;
    b.constants = constants;
    b.constant_count = literal_count;
    b.function_count = function_count;
    b.function_ids = functions;
    /* Labels are numbered densely by the compiler; count them first. */
    uint64_t labels = read_unsigned(&b);
    b.label_count = (size_t)labels;
    b.labels = calloc(b.label_count + 1, sizeof(struct sljit_label*));
    ok = b.c && b.labels && !b.bad;
    if (ok && rt->native_runtime->verbose && rt->core_loaded) sljit_compiler_verbose(b.c, stderr);
    if (ok) ok = translate(&b);
    for (size_t i = 0; ok && i < b.jump_count; i++) {
      struct sljit_label* target = b.labels[b.jumps[i].label];
      if (!target) ok = false;
      else sljit_set_label(b.jumps[i].jump, target);
    }
    for (size_t i = 0; ok && i < function_count; i++) ok = b.entries[i] != NULL;
    for (size_t i = 0; ok && i < b.site_count; i++) ok = b.sites[i].descriptor < descriptor_count;
    if (ok) {
      machine = sljit_generate_code(b.c, 0, rt->native_runtime);
      ok = machine != NULL;
    }
    if (!ok && !oti_failed(rt))
      oti_errorf(rt, "native unit rejected (sljit error %d)", b.c ? sljit_get_compiler_error(b.c) : -1);
  }
  if (ok) {
    size_t size = sljit_get_generated_code_size(b.c);
    protect(machine, size, PROT_READ | PROT_EXEC);
    code->start = (uintptr_t)machine;
    code->end = code->start + size;
    for (size_t i = 0; i < b.site_count; i++) {
      oti_descriptor* d = &descriptors[b.sites[i].descriptor];
      d->ra = (uint32_t)(sljit_get_label_addr(b.sites[i].label) - code->start);
      d->frame = b.sites[i].frame;
    }
    for (size_t i = 0; i < function_count; i++)
      rt->functions[functions[i]]->entry = sljit_get_label_addr(b.entries[i]);
    qsort(descriptors, descriptor_count, sizeof(oti_descriptor), compare_descriptors);
    code->descriptors = descriptors;
    code->descriptor_count = descriptor_count;
    code->bitmaps = bitmaps;
    code->constants = constants;
    code->constant_count = literal_count;
    code->functions = functions;
    code->function_count = function_count;
    descriptors = NULL;
    bitmaps = NULL;
    ok = add_code(rt, code);
    if (ok) {
      installed = true;
      write_perf_map(rt, code);
    } else {
      code->descriptors = NULL;
      code->bitmaps = NULL;
    }
  }
  ot_value result = 0;
  uint32_t top = UINT32_MAX;
  for (size_t i = 0; ok && i < static_count; i++) {
    ot_value closure = oti_alloc_closure(rt, 0);
    if (!closure) {
      ok = false;
      break;
    }
    /* Allocation may have moved the unit. */
    parts = array_items(rt->stack[slot], &parts_count);
    size_t n;
    ot_value* entry = array_items(array_items(parts[5], &static_count)[i], &n);
    uint32_t function, literal;
    if (n != 2 || !unit_int(entry[0], &function) || !unit_int(entry[1], &literal) ||
        function >= function_count || literal >= literal_count) {
      ok = oti_error(rt, "native unit: bad static closure");
      break;
    }
    oti_as_closure(closure)->code = rt->functions[code->functions[function]]->entry;
    rt->constants[code->constants[literal]] = closure;
    if (function == 0) top = code->constants[literal];
  }
  /* Later allocations may have moved it; the constant slot is a root. */
  if (ok && top != UINT32_MAX) result = rt->constants[top];
  if (ok && !result) ok = oti_error(rt, "native unit: no static top level");
  if (b.c) sljit_free_compiler(b.c);
  free(b.labels);
  free(b.jumps);
  free(b.sites);
  free(b.entries);
  free(stream);
  free(cells);
  free(descriptors);
  free(bitmaps);
  if (installed) return ok ? result : 0;
  /* Nothing refers to a rejected unit's functions yet. Its constants are
   * left for oti_reclaim. */
  for (size_t i = 0; i < made_functions; i++) release_function(rt, functions[i]);
  free(functions);
  free(constants);
  free(code);
  return 0;
}

/* --- Calling native code from C ---------------------------------------- */

bool oti_native_args(ot_state* rt, size_t count) {
  if (count + 8 <= rt->native.args_capacity) return true;
  size_t old = rt->native.args_capacity;
  if (!oti_grow(rt, (void**)&rt->native.args, &rt->native.args_capacity, count + 8,
                sizeof(ot_value)))
    return false;
  for (size_t i = old; i < rt->native.args_capacity; i++) rt->native.args[i] = OT_NIL;
  return true;
}

static uintptr_t entry_of(ot_state* rt, ot_value callee) {
  return oti_kind(callee, K_CLOSURE) ? oti_as_closure(callee)->code
                                     : rt->native_stubs[STUB_CALL_OTHER];
}

/* Call callee with argc arguments already in native.args. 0 on failure, with
 * the error recorded, as oti_call_list. */
ot_value oti_native_invoke(ot_state* rt, ot_value callee, size_t argc) {
  if (!oti_host_entry(rt)) return 0;
  size_t base = rt->sp, tries = rt->try_count, params = rt->param_count;
  size_t handlers = rt->handler_count, handler_top = rt->handler_top,
         restarts = rt->restart_scope_count, cleanups = rt->cleanup_count;
  rt->frame_count++;
  ot_value result = oti_native_enter(rt, callee, entry_of(rt, callee), argc);
  rt->frame_count--;
  rt->try_count = tries;
  oti_restore_restarts(rt, restarts);
  oti_restore_cleanups(rt, cleanups);
  oti_restore_params(rt, params);
  oti_restore_handlers(rt, handlers, handler_top);
  rt->sp = rt->stack_roots.count = base;
  if (!result && !oti_failed(rt)) oti_error(rt, "native call failed");
  return result;
}

ot_value oti_native_invoke_list(ot_state* rt, ot_value function, ot_value args) {
  size_t n = 0;
  for (ot_value p = args; oti_kind(p, K_PAIR); p = oti_cdr(p)) n++;
  ot_value live[] = {function, args};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 2);
  bool ok = oti_native_args(rt, n);
  oti_roots_pop(&rt->heap, &roots);
  if (!ok) return 0;
  size_t i = 0;
  for (ot_value p = live[1]; oti_kind(p, K_PAIR); p = oti_cdr(p)) rt->native.args[i++] = oti_car(p);
  return oti_native_invoke(rt, live[0], n);
}

/* --- Frame walking ------------------------------------------------------ */

/* Visit the live slots of every native frame, run by run, from the frame
 * that last called C out to the outermost entry record. */
static void visit_frames(ot_state* rt, oti_root_visitor visit, void* context) {
  oti_native_runtime* n = rt->native_runtime;
  uintptr_t sp = rt->native.sp;
  while (sp) {
    uintptr_t ra = ((uintptr_t*)sp)[-1];
    if (ra == (uintptr_t)oti_native_return) {
      sp = ((uintptr_t*)sp)[0];
      continue;
    }
    const oti_code* c;
    const oti_descriptor* d = descriptor_at(n, ra, &c);
    if (!d) {
      fprintf(stderr, "otium: no frame descriptor for return address %#lx\n", (unsigned long)ra);
      abort();
    }
    const uint64_t* bits = c->bitmaps + d->slots;
    for (uint32_t w = 0; w < d->words; w++)
      for (uint64_t word = bits[w]; word; word &= word - 1)
        visit(context, (ot_value*)sp + w * 64 + (uint32_t)__builtin_ctzll(word));
    sp += d->frame + 8;
  }
}

/* native.args is not a root: values pass through it between a call and the
 * callee's entry, or into C that copies them before it can collect. */
void oti_native_visit_roots(ot_state* rt, oti_root_visitor visit, void* context) {
  visit(context, &rt->native.callee);
  visit_frames(rt, visit, context);
}

/* The frames of an uncaught error, innermost first, as oti_trace_error does
 * for VM frames. */
#define TRACE_LIMIT 200

static char* copy_name(const char* s) {
  if (!s) return NULL;
  size_t n = strlen(s) + 1;
  char* copy = malloc(n);
  if (copy) memcpy(copy, s, n);
  return copy;
}

void oti_native_trace(ot_state* rt) {
  oti_error_record* e = &rt->error;
  if (!e->set || e->traced || rt->try_count || rt->interrupted || rt->exit_pending ||
      rt->restart_pending)
    return;
  e->traced = true;
  oti_error_place* places = calloc(TRACE_LIMIT, sizeof(oti_error_place));
  if (!places) return;
  size_t count = 0;
  bool seen = false;
  uintptr_t sp = rt->native.sp;
  while (sp && count < TRACE_LIMIT) {
    uintptr_t ra = ((uintptr_t*)sp)[-1];
    if (ra == (uintptr_t)oti_native_return) {
      sp = ((uintptr_t*)sp)[0];
      continue;
    }
    const oti_descriptor* d = descriptor_at(rt->native_runtime, ra, NULL);
    if (!d) break;
    sp += d->frame + 8;
    if (d->function == UINT32_MAX || d->offset == UINT32_MAX) continue;
    const oti_function* fn = rt->functions[d->function];
    if (!fn || fn->source == OTI_NO_SOURCE) continue;
    bool innermost = !seen;
    seen = true;
    if (innermost && d->tail && strcmp(e->code, "E0201")) continue;
    /* Recursion through one call site shows once. */
    if (count && places[count - 1].source == fn->source && places[count - 1].offset == d->offset)
      continue;
    places[count++] = (oti_error_place){fn->source, d->offset, copy_name(fn->name)};
  }
  if (!count) {
    free(places);
    return;
  }
  e->frames = places;
  e->frame_count = count;
}

/* The source offset of the innermost native call site with one. */
static bool innermost_offset(ot_state* rt, uint32_t* source, uint32_t* offset) {
  uintptr_t sp = rt->native.sp;
  while (sp) {
    uintptr_t ra = ((uintptr_t*)sp)[-1];
    if (ra == (uintptr_t)oti_native_return) return false;
    const oti_descriptor* d = descriptor_at(rt->native_runtime, ra, NULL);
    if (!d) return false;
    if (d->function != UINT32_MAX && d->offset != UINT32_MAX && rt->functions[d->function]) {
      *source = rt->functions[d->function]->source;
      *offset = d->offset;
      return true;
    }
    sp += d->frame + 8;
  }
  return false;
}

/* --- Unwinding ---------------------------------------------------------- */

static _Noreturn void fail_to_entry(ot_state* rt) {
  oti_native_resume(rt, rt->native.entry, (uintptr_t)oti_native_fail, 0);
}

static bool resume_restart(ot_state* rt, size_t stop) {
  size_t index = rt->restart_count;
  while (index && rt->restarts[index - 1] != rt->restart_transfer[0]) index--;
  if (!index) {
    rt->restart_pending = false;
    return oti_error(rt, "restart is no longer active");
  }
  size_t scope_index = rt->restart_scope_count;
  while (scope_index && rt->restart_scopes[scope_index - 1].first >= index) scope_index--;
  oti_restart_scope scope = rt->restart_scopes[scope_index - 1];
  if (rt->cleanup_count > scope.state.cleanups) {
    if (rt->cleanup_states[rt->cleanup_count - 1].state.frames <= stop) fail_to_entry(rt);
    oti_run_cleanup(rt, true);
    return true;
  }
  if (scope.state.frames <= stop) fail_to_entry(rt);
  oti_restore_restarts(rt, scope_index);
  rt->try_count = scope.tries;
  oti_restore_params(rt, scope.state.params);
  oti_restore_handlers(rt, scope.state.handlers, scope.state.handler_top);
  rt->sp = rt->stack_roots.count = scope.state.sp;
  rt->restart_pending = false;
  oti_clear_error(rt);
  rt->raised = rt->signaled = false;
  rt->condition_values[0] = OT_NIL;
  /* The target calls the restart's handler with the arguments. */
  ot_value transfer = oti_alloc(rt, K_PAIR, 2, 0);
  if (!transfer) return true;
  oti_as_pair(transfer)->car = oti_as_restart(rt->restart_transfer[0])->handler;
  oti_as_pair(transfer)->cdr = rt->restart_transfer[1];
  rt->restart_transfer[0] = rt->restart_transfer[1] = OT_NIL;
  oti_native_resume(rt, scope.state.slot, scope.state.target, transfer);
}

/* Unwind from the native frame that called this C function, after an error
 * has been recorded: run handlers and cleanups as oti_drive does, then jump
 * to the innermost try or restart in this run of native frames, or return
 * failure to the trampoline that started the run. */
_Noreturn void oti_native_raise(ot_state* rt) {
  size_t stop = rt->frame_count - 1;
  if (!oti_failed(rt)) oti_error(rt, "native code failed");
  {
    uint32_t source, offset;
    oti_error_record* e = &rt->error;
    if (e->set && !e->located && innermost_offset(rt, &source, &offset) &&
        source != OTI_NO_SOURCE) {
      e->offset = offset;
      e->source = source;
      e->located = true;
    }
  }
  for (;;) {
    if (!rt->try_count) oti_native_trace(rt);
    if (!rt->interrupted && !rt->exit_pending && !rt->restart_pending && !rt->signaled)
      oti_signal_error(rt);
    if (rt->restart_pending) {
      resume_restart(rt, stop);
      continue;
    }
    size_t target_cleanups = !rt->interrupted && !rt->exit_pending && rt->try_count
                                 ? rt->tries[rt->try_count - 1].cleanups
                                 : 0;
    if (rt->cleanup_count > target_cleanups) {
      if (rt->cleanup_states[rt->cleanup_count - 1].state.frames <= stop) fail_to_entry(rt);
      oti_run_cleanup(rt, true);
      continue;
    }
    if (rt->interrupted || rt->exit_pending || !rt->try_count ||
        rt->tries[rt->try_count - 1].frames <= stop)
      fail_to_entry(rt);
    oti_try_frame handler = rt->tries[--rt->try_count];
    oti_restore_restarts(rt, handler.restarts);
    oti_restore_params(rt, handler.params);
    oti_restore_handlers(rt, handler.handlers, handler.handler_top);
    rt->sp = rt->stack_roots.count = handler.sp;
    ot_value condition = oti_error_condition(rt);
    if (!condition) continue;
    oti_clear_error(rt);
    rt->raised = false;
    rt->signaled = false;
    rt->condition_values[0] = OT_NIL;
    oti_native_resume(rt, handler.slot, handler.target, condition);
  }
}

/* --- Helpers ------------------------------------------------------------ */

typedef uintptr_t W;

static W h_alloc(ot_state* rt, W kind, W fields, W bytes) {
  ot_value v = oti_alloc_slow(rt, (uint32_t)kind, (uint32_t)fields, (size_t)bytes);
  if (!v) oti_native_raise(rt);
  return v;
}

static ot_value builtin_on_stack(ot_state* rt, unsigned id, ot_value a, ot_value b, size_t n) {
  size_t first = rt->sp;
  if (!oti_stack(rt, first + 2)) oti_native_raise(rt);
  rt->stack[first] = a;
  rt->stack[first + 1] = b;
  rt->sp = rt->stack_roots.count = first + n;
  ot_value result = oti_builtin(rt, id, first, n);
  rt->sp = rt->stack_roots.count = first;
  if (!result || oti_failed(rt)) oti_native_raise(rt);
  return result;
}

static W h_prim1(ot_state* rt, W id, W a) {
  return builtin_on_stack(rt, (unsigned)id, a, OT_NIL, 1);
}

static W h_prim2(ot_state* rt, W id, W a, W b) {
  return builtin_on_stack(rt, (unsigned)id, a, b, 2);
}

static W h_arity(ot_state* rt, W closure, W argc) {
  const oti_function* fn = oti_closure_fn(rt, closure);
  if (fn) oti_arity_error(rt, fn, argc);
  else oti_coded_error(rt, "E0301", "wrong number of arguments");
  oti_native_raise(rt);
}

static W h_unbound(ot_state* rt, W marker) {
  oti_unbound_error(rt, (uint32_t)(marker >> 8));
  oti_native_raise(rt);
}

static W h_uninitialized(ot_state* rt, W captured) {
  oti_error(rt, captured ? "uninitialized captured binding" : "uninitialized local");
  oti_native_raise(rt);
}

static bool inlined_builtin_cell(ot_state* rt, uint32_t index) {
  ot_value old = oti_global_value(rt, index);
  if ((old & 255) != TAG_BUILTIN) return false;
  unsigned id = (unsigned)(old >> 8);
  if (!oti_inlined_builtin(id) || rt->builtin_cells[id] != index) return false;
  oti_errorf(rt, "cannot redefine builtin: %s", rt->global_info[index].name);
  return true;
}

static W h_define(ot_state* rt, W cell, W value) {
  if (inlined_builtin_cell(rt, (uint32_t)cell)) oti_native_raise(rt);
  rt->global_info[cell].defined = 1;
  rt->global_info[cell].macro = oti_kind(value, K_MACRO);
  oti_set_global(rt, (uint32_t)cell, value);
  return value;
}

static W h_set_global(ot_state* rt, W cell, W value) {
  if (oti_unbound(oti_global_value(rt, (uint32_t)cell))) {
    oti_coded_error(rt, "E0201", "cannot set an unbound global");
    oti_native_raise(rt);
  }
  if (inlined_builtin_cell(rt, (uint32_t)cell)) oti_native_raise(rt);
  rt->global_info[cell].macro = oti_kind(value, K_MACRO);
  oti_set_global(rt, (uint32_t)cell, value);
  return OT_NIL;
}

/* The stack check at function entries and loop back edges failed: the stack
 * really is too deep, or an interrupt was requested. */
static W h_poll(ot_state* rt) {
  oti_native_runtime* n = rt->native_runtime;
  if (rt->native.limit != n->limit) {
    rt->native.limit = n->limit;
    if (!oti_interrupt_point(rt)) oti_native_raise(rt);
    return OT_NIL;
  }
  if (rt->native.sp <= n->limit) {
    oti_error(rt, "VM stack limit exceeded");
    oti_native_raise(rt);
  }
  return OT_NIL;
}

/* The rest list of a variadic call, from native.args[fixed, argc). */
static W h_rest(ot_state* rt, W fixed) {
  ot_value rest = OT_EMPTY;
  oti_roots root, args;
  oti_roots_push(&rt->heap, &root, &rest, 1);
  oti_roots_push(&rt->heap, &args, rt->native.args, rt->native.argc);
  for (size_t i = rt->native.argc; i > fixed; i--) {
    ot_value pair = oti_alloc(rt, K_PAIR, 2, 0);
    if (!pair) {
      oti_roots_pop(&rt->heap, &args);
      oti_roots_pop(&rt->heap, &root);
      oti_native_raise(rt);
    }
    oti_as_pair(pair)->car = rt->native.args[i - 1];
    oti_as_pair(pair)->cdr = rest;
    rest = pair;
  }
  oti_roots_pop(&rt->heap, &args);
  oti_roots_pop(&rt->heap, &root);
  return rest;
}

static W h_try(ot_state* rt, W pc, W sp) {
  if (!oti_grow(rt, (void**)&rt->tries, &rt->try_capacity, rt->try_count + 1,
                sizeof(oti_try_frame)))
    oti_native_raise(rt);
  rt->tries[rt->try_count++] =
      (oti_try_frame){rt->frame_count,     rt->sp,        pc, rt->param_count, rt->handler_count,
                      rt->handler_top,     rt->restart_scope_count, rt->cleanup_count, sp};
  return OT_NIL;
}

static W h_end_try(ot_state* rt) {
  if (rt->try_count) rt->try_count--;
  return OT_NIL;
}

static W h_raise(ot_state* rt, W condition) {
  rt->condition_values[0] = condition;
  rt->raised = true;
  oti_error(rt, "uncaught condition");
  rt->signaled = true;
  oti_native_raise(rt);
}

static W h_cleanup(ot_state* rt, W closure, W outcome) {
  size_t top = rt->sp;
  if (!oti_stack(rt, top + 1)) oti_native_raise(rt);
  rt->stack[top] = closure;
  rt->sp = rt->stack_roots.count = top + 1;
  bool ok = oti_install_cleanup(rt, outcome != OT_FALSE);
  rt->sp = rt->stack_roots.count = top;
  if (!ok) oti_native_raise(rt);
  return OT_NIL;
}

static W h_run_cleanup(ot_state* rt) {
  if (!oti_run_cleanup(rt, false)) oti_native_raise(rt);
  return OT_NIL;
}

/* Values for handlers and restarts arrive in native.args. */
static size_t args_to_stack(ot_state* rt, size_t count) {
  size_t top = rt->sp;
  if (!oti_stack(rt, top + count)) oti_native_raise(rt);
  memcpy(rt->stack + top, rt->native.args, count * sizeof(ot_value));
  rt->sp = rt->stack_roots.count = top + count;
  return top;
}

static W h_handlers(ot_state* rt, W count) {
  size_t top = args_to_stack(rt, 2 * count);
  bool ok = oti_install_handlers(rt, top, count);
  rt->sp = rt->stack_roots.count = top;
  if (!ok) oti_native_raise(rt);
  return OT_NIL;
}

static W h_unhandlers(ot_state* rt, W count) {
  if (count > rt->handler_count) {
    oti_error(rt, "invalid handler stack");
    oti_native_raise(rt);
  }
  oti_remove_handlers(rt, count);
  return OT_NIL;
}

static W h_bind_param(ot_state* rt, W param, W value) {
  if (!oti_bind_param(rt, param, value)) oti_native_raise(rt);
  return OT_NIL;
}

static W h_unbind_params(ot_state* rt, W count) {
  if (count > rt->param_count) {
    oti_error(rt, "invalid parameter stack");
    oti_native_raise(rt);
  }
  oti_restore_params(rt, rt->param_count - count);
  return OT_NIL;
}

static W h_make_param(ot_state* rt, W name, W value) {
  ot_value live[] = {name, value};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 2);
  ot_value v = oti_alloc(rt, K_PARAM, 2, 0);
  oti_roots_pop(&rt->heap, &roots);
  if (!v) oti_native_raise(rt);
  oti_as_param(v)->name = live[0];
  oti_as_param(v)->value = live[1];
  return v;
}

/* Restart triples arrive in native.args; they stay on rt->stack, where the
 * scope's sp points, until the scope ends. */
static W h_restarts(ot_state* rt, W count, W pc, W sp) {
  size_t top = args_to_stack(rt, 3 * count);
  bool ok = oti_install_restarts(rt, top, count, pc);
  rt->sp = rt->stack_roots.count = top;
  if (!ok) oti_native_raise(rt);
  rt->restart_scopes[rt->restart_scope_count - 1].state.slot = sp;
  return OT_NIL;
}

static W h_end_restarts(ot_state* rt) {
  if (!rt->restart_scope_count) {
    oti_error(rt, "invalid restart scope");
    oti_native_raise(rt);
  }
  oti_restore_restarts(rt, rt->restart_scope_count - 1);
  return OT_NIL;
}

static W h_restart_apply(ot_state* rt, W transfer) {
  ot_value result = oti_native_invoke_list(rt, oti_car(transfer), oti_cdr(transfer));
  if (!result) oti_native_raise(rt);
  return result;
}

/* The closure eval reads as, for the namespace named by a symbol. */
static W h_evaluator(ot_state* rt, W name) {
  size_t length;
  const char* text = oti_text_bytes(name, &length);
  uint32_t space = oti_namespace(rt, text, length);
  if (space == UINT32_MAX) oti_native_raise(rt);
  ot_value v = oti_evaluator(rt, space);
  if (!v) oti_native_raise(rt);
  return v;
}

enum {
  H_ALLOC,
  H_PRIM1,
  H_PRIM2,
  H_ARITY,
  H_UNBOUND,
  H_UNINITIALIZED,
  H_DEFINE,
  H_SET_GLOBAL,
  H_POLL,
  H_REST,
  H_TRY,
  H_END_TRY,
  H_RAISE,
  H_CLEANUP,
  H_RUN_CLEANUP,
  H_HANDLERS,
  H_UNHANDLERS,
  H_BIND_PARAM,
  H_UNBIND_PARAMS,
  H_MAKE_PARAM,
  H_RESTARTS,
  H_END_RESTARTS,
  H_RESTART_APPLY,
  H_EVALUATOR,
  H_COUNT
};

static const void* helper_address(uint64_t id) {
  static const void* const helpers[H_COUNT] = {
      [H_ALLOC] = (const void*)h_alloc,
      [H_PRIM1] = (const void*)h_prim1,
      [H_PRIM2] = (const void*)h_prim2,
      [H_ARITY] = (const void*)h_arity,
      [H_UNBOUND] = (const void*)h_unbound,
      [H_UNINITIALIZED] = (const void*)h_uninitialized,
      [H_DEFINE] = (const void*)h_define,
      [H_SET_GLOBAL] = (const void*)h_set_global,
      [H_POLL] = (const void*)h_poll,
      [H_REST] = (const void*)h_rest,
      [H_TRY] = (const void*)h_try,
      [H_END_TRY] = (const void*)h_end_try,
      [H_RAISE] = (const void*)h_raise,
      [H_CLEANUP] = (const void*)h_cleanup,
      [H_RUN_CLEANUP] = (const void*)h_run_cleanup,
      [H_HANDLERS] = (const void*)h_handlers,
      [H_UNHANDLERS] = (const void*)h_unhandlers,
      [H_BIND_PARAM] = (const void*)h_bind_param,
      [H_UNBIND_PARAMS] = (const void*)h_unbind_params,
      [H_MAKE_PARAM] = (const void*)h_make_param,
      [H_RESTARTS] = (const void*)h_restarts,
      [H_END_RESTARTS] = (const void*)h_end_restarts,
      [H_RESTART_APPLY] = (const void*)h_restart_apply,
      [H_EVALUATOR] = (const void*)h_evaluator,
  };
  return id < H_COUNT ? helpers[id] : NULL;
}

/* --- Calls to everything but closures ----------------------------------- */

#define APPLY_VALUE (((ot_value)B_APPLY << 8) | TAG_BUILTIN)

/* Spread apply's final list into the call at rt->stack[call]. */
static bool spread(ot_state* rt, size_t call, size_t* argc) {
  if (*argc < 2) return oti_error(rt, "apply needs a callable and a final argument sequence");
  ot_value list = rt->stack[call + *argc];
  bool array = oti_kind(list, K_ARRAY);
  ot_value cursor = array || list == OT_NIL ? OT_EMPTY : list, slow = cursor;
  size_t length = array ? oti_as_array(list)->length : 0, prefix = *argc - 2;
  while (oti_kind(cursor, K_PAIR)) {
    if (length >= rt->stack_limit) return oti_error(rt, "VM stack limit exceeded");
    length++;
    cursor = oti_cdr(cursor);
    if (!(length & 1)) slow = oti_cdr(slow);
    if (cursor != OT_EMPTY && cursor == slow)
      return oti_error(rt, "apply needs an acyclic proper list");
  }
  if (cursor != OT_EMPTY) return oti_error(rt, "apply needs a proper list");
  size_t count = prefix + length;
  if (count > rt->stack_limit || call + 1 + count > rt->stack_limit)
    return oti_error(rt, "VM stack limit exceeded");
  if (!oti_stack(rt, call + 1 + count)) return false;
  memmove(rt->stack + call, rt->stack + call + 1, (prefix + 1) * sizeof(ot_value));
  if (array && length)
    memcpy(rt->stack + call + 1 + prefix, oti_as_array_data(oti_as_array(list)->data)->items,
           length * sizeof(ot_value));
  else
    for (size_t i = 0; i < length; i++) {
      rt->stack[call + 1 + prefix + i] = oti_car(list);
      list = oti_cdr(list);
    }
  *argc = count;
  rt->sp = rt->stack_roots.count = call + 1 + count;
  return true;
}

/* The C half of the call_other stub: native.callee applied to native.argc
 * arguments in native.args. Returns the result, or 0 after leaving a closure
 * and its arguments in native.callee, native.argc and native.args for the
 * stub to enter by tail call. */
static W dispatch(ot_state* rt) {
  size_t argc = rt->native.argc, base = rt->sp;
  if (argc + 1 > rt->stack_limit || base + 1 + argc > rt->stack_limit ||
      !oti_stack(rt, base + 1 + argc)) {
    if (!oti_failed(rt)) oti_error(rt, "VM stack limit exceeded");
    oti_native_raise(rt);
  }
  rt->stack[base] = rt->native.callee;
  memcpy(rt->stack + base + 1, rt->native.args, argc * sizeof(ot_value));
  rt->sp = rt->stack_roots.count = base + 1 + argc;
  for (;;) {
    ot_value f = rt->stack[base];
    if (f == APPLY_VALUE) {
      if (!spread(rt, base, &argc)) oti_native_raise(rt);
      continue;
    }
    if (oti_kind(f, K_MACRO)) {
      rt->stack[base] = oti_as_macro(f)->expander;
      continue;
    }
    if (oti_kind(f, K_GENERIC) || oti_kind(f, K_NEXT)) {
      if (!oti_generic_prepare(rt, base, &argc)) oti_native_raise(rt);
      continue;
    }
    if (oti_kind(f, K_CLOSURE)) {
      if (!oti_native_args(rt, argc)) oti_native_raise(rt);
      memcpy(rt->native.args, rt->stack + base + 1, argc * sizeof(ot_value));
      rt->native.callee = rt->stack[base];
      rt->native.argc = argc;
      rt->sp = rt->stack_roots.count = base;
      return 0;
    }
    if (oti_primitive_value(f)) {
      ot_value result = oti_call_primitive(rt, base, argc);
      rt->sp = rt->stack_roots.count = base;
      if (!result || oti_failed(rt)) oti_native_raise(rt);
      return result;
    }
    oti_callable_error(rt, f);
    oti_native_raise(rt);
  }
}

/* --- Stubs -------------------------------------------------------------- */

typedef struct {
  struct sljit_label* label;
  uint32_t frame;
} oti_stub_site;

/* The stubs are hand-written sljit: call_other, the entry of every callee
 * that isn't a closure; unbound, the entry of unbound cells; and the
 * evaluator, the code of the closures eval reads as. */
static bool build_stubs(ot_state* rt) {
  oti_native_runtime* n = rt->native_runtime;
  struct sljit_compiler* c = sljit_create_compiler(NULL);
  if (!c) return false;
  oti_stub_site sites[2];
  struct sljit_label* entries[STUB_COUNT];
  uint32_t frame;
  sljit_sw args = (sljit_sw)offsetof(ot_state, native.args);
  sljit_sw callee = (sljit_sw)offsetof(ot_state, native.callee);
  sljit_sw argc = (sljit_sw)offsetof(ot_state, native.argc);

  entries[STUB_CALL_OTHER] = sljit_emit_aligned_label(c, SLJIT_LABEL_ALIGN_16, NULL);
  enter(c, 8, 0, &frame);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R7, 0, SLJIT_MEM1(SLJIT_S0), args);
  for (int i = 0; i < 5; i++)
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM1(SLJIT_R7), i * 8, SLJIT_R(2 + i), 0);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0), callee, SLJIT_R1, 0);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM1(SLJIT_S0), argc, SLJIT_R0, 0);
  c_call(c, (const void*)dispatch, 0);
  sites[0] = (oti_stub_site){sljit_emit_label(c), frame};
  after_c_call(c);
  struct sljit_jump* done = sljit_emit_cmp(c, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R7, 0, SLJIT_MEM1(SLJIT_S0), args);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(SLJIT_S0), callee);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S0), argc);
  for (int i = 0; i < 5; i++)
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_R(2 + i), 0, SLJIT_MEM1(SLJIT_R7), i * 8);
  sljit_emit_icall(c, SLJIT_CALL_REG_ARG | SLJIT_CALL_RETURN, SLJIT_ARGS0(W), SLJIT_MEM1(SLJIT_R1),
                   (sljit_sw)offsetof(oti_closure_obj, code));
  sljit_set_label(done, sljit_emit_label(c));
  sljit_emit_return(c, SLJIT_MOV, SLJIT_R0, 0);

  entries[STUB_UNBOUND] = sljit_emit_aligned_label(c, SLJIT_LABEL_ALIGN_16, NULL);
  enter(c, 7, 0, &frame);
  c_call(c, (const void*)h_unbound, 1);
  sites[1] = (oti_stub_site){sljit_emit_label(c), frame};
  sljit_emit_return(c, SLJIT_MOV, SLJIT_R0, 0);

  /* (fn (form) (%eval-in-namespace namespace form)), namespace captured. */
  entries[STUB_EVALUATOR] = sljit_emit_aligned_label(c, SLJIT_LABEL_ALIGN_16, NULL);
  enter(c, 7, 0, &frame);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R3, 0, SLJIT_R2, 0);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_R1),
                 (sljit_sw)offsetof(oti_closure_obj, captures));
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM,
                 (sljit_sw)(((ot_value)B_EVAL_IN_NAMESPACE << 8) | TAG_BUILTIN));
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, 2);
  sljit_emit_icall(c, SLJIT_CALL_REG_ARG | SLJIT_CALL_RETURN, SLJIT_ARGS0(W), SLJIT_MEM1(SLJIT_S0),
                   (sljit_sw)offsetof(ot_state, native_stubs[STUB_CALL_OTHER]));

  void* machine = sljit_generate_code(c, 0, n);
  if (!machine) {
    sljit_free_compiler(c);
    return false;
  }
  size_t size = sljit_get_generated_code_size(c);
  protect(machine, size, PROT_READ | PROT_EXEC);
  oti_code* code = calloc(1, sizeof(*code));
  if (!code) {
    sljit_free_compiler(c);
    return false;
  }
  code->start = (uintptr_t)machine;
  code->end = code->start + size;
  code->descriptors = calloc(2, sizeof(oti_descriptor));
  code->bitmaps = calloc(1, sizeof(uint64_t));
  if (!code->descriptors || !code->bitmaps) {
    sljit_free_compiler(c);
    free_code(code);
    return false;
  }
  for (int i = 0; i < 2; i++)
    code->descriptors[i] =
        (oti_descriptor){(uint32_t)(sljit_get_label_addr(sites[i].label) - code->start),
                         sites[i].frame, UINT32_MAX, UINT32_MAX, 0, 0, false};
  code->descriptor_count = 2;
  qsort(code->descriptors, 2, sizeof(oti_descriptor), compare_descriptors);
  for (int i = 0; i < STUB_COUNT; i++) {
    n->stubs[i] = sljit_get_label_addr(entries[i]);
    rt->native_stubs[i] = n->stubs[i];
  }
  sljit_free_compiler(c);
  return add_code(rt, code);
}

uintptr_t oti_native_cell_entry(ot_state* rt, ot_value value) {
  if (oti_kind(value, K_CLOSURE)) return oti_as_closure(value)->code;
  if (oti_unbound(value)) return rt->native_stubs[STUB_UNBOUND];
  return rt->native_stubs[STUB_CALL_OTHER];
}

bool oti_native_evaluator(ot_state* rt, ot_value v, uint32_t space) {
  return oti_kind(v, K_CLOSURE) && oti_as_closure(v)->code == rt->native_stubs[STUB_EVALUATOR] &&
         oti_as_closure(v)->header.fields == 1 &&
         oti_as_closure(v)->captures[0] == ot_int((intptr_t)space);
}

ot_value oti_native_make_evaluator(ot_state* rt, uint32_t space) {
  ot_value v = oti_alloc_closure(rt, 1);
  if (!v) return 0;
  oti_as_closure(v)->code = rt->native_stubs[STUB_EVALUATOR];
  oti_as_closure(v)->captures[0] = ot_int((intptr_t)space);
  return v;
}

/* --- Reclamation -------------------------------------------------------- */

/* Mark the code a closure runs, and so the constants it reads. Returns the
 * constant slots to mark through mark, once per code object. */
bool oti_native_mark_closure(ot_state* rt, ot_value closure, bool (*mark)(void*, uint32_t),
                             void* context) {
  oti_code* c = code_at(rt->native_runtime, oti_as_closure(closure)->code);
  if (!c || c->marked) return true;
  c->marked = true;
  for (size_t i = 0; i < c->constant_count; i++)
    if (!mark(context, c->constants[i])) return false;
  return true;
}

/* After marking: free code objects nothing reaches, and their functions. */
void oti_native_sweep(ot_state* rt) {
  oti_native_runtime* n = rt->native_runtime;
  size_t kept = 0;
  for (size_t i = 0; i < n->code_count; i++) {
    oti_code* c = n->codes[i];
    /* Runtime stubs have no functions and are always kept. */
    if (c->marked || !c->function_count) {
      c->marked = false;
      n->codes[kept++] = c;
      continue;
    }
    for (size_t f = 0; f < c->function_count; f++) release_function(rt, c->functions[f]);
    free_code(c);
  }
  n->code_count = kept;
}

/* --- Setup -------------------------------------------------------------- */

#define STACK_BYTES ((size_t)256 << 20)
/* Room below the limit for C helpers and nested entries. */
#define STACK_MARGIN ((size_t)2 << 20)

bool oti_native_init(ot_state* rt) {
  oti_native_runtime* n = calloc(1, sizeof(*n));
  if (!n) return oti_error(rt, "out of host memory");
  rt->native_runtime = n;
  n->verbose = getenv("OTIUM_NATIVE_VERBOSE") != NULL;
  const char* perf = getenv("OTIUM_PERF_MAP");
  if (perf && *perf && strcmp(perf, "0")) {
    char path[64];
    snprintf(path, sizeof(path), "/tmp/perf-%ld.map", (long)getpid());
    n->perf_map = fopen(path, "w");
  }
  n->stack_bytes = STACK_BYTES;
  void* stack = mmap(NULL, n->stack_bytes, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (stack == MAP_FAILED) return oti_error(rt, "cannot reserve the native stack");
  n->stack = stack;
  /* A guard page at the bottom catches C helpers that run past the margin. */
  mprotect(stack, page_size(), PROT_NONE);
  rt->native.stack_top = ((uintptr_t)stack + n->stack_bytes) & ~(uintptr_t)15;
  n->limit = (uintptr_t)stack + page_size() + STACK_MARGIN;
  rt->native.limit = n->limit;
  if (!oti_native_args(rt, 16)) return false;
  rt->native.callee = OT_NIL;
  if (!build_stubs(rt)) return oti_error(rt, "cannot build native stubs");
  rt->native_mode = true;
  /* Cells defined so far get their entries. */
  for (size_t i = 0; i < rt->global_count; i++)
    rt->globals[i].entry = oti_native_cell_entry(rt, rt->globals[i].value);
  return true;
}

void oti_native_destroy(ot_state* rt) {
  oti_native_runtime* n = rt->native_runtime;
  if (!n) return;
  for (size_t i = 0; i < n->code_count; i++) free_code(n->codes[i]);
  free(n->codes);
  for (oti_code_chunk* c = n->chunks; c;) {
    oti_code_chunk* next = c->next;
    munmap(c->base, c->size);
    free(c);
    c = next;
  }
  if (n->stack) munmap(n->stack, n->stack_bytes);
  if (n->perf_map) fclose(n->perf_map);
  free(n);
  free(rt->native.args);
  rt->native_runtime = NULL;
}

#endif
