/* The arm64 half of native.c: frame layout, the machine code sljit can't
 * express, and the trampolines. sljit's registers here: S0 is x19, S1 x20,
 * R0 to R7 are x0 to x7 and R8 to R10 x11 to x13. x9 and x10 are sljit's
 * temporaries, free between its instructions; x21 and up are never used by
 * native code, which keeps only S0 and S1.
 *
 * A call leaves its return address in x30, which sljit's prologue stores at
 * sp + 8 in the new frame, under the locals (which start at sp + 16,
 * SLJIT_LOCALS_OFFSET). A frame's size (oti_descriptor.frame) is the whole
 * of it. The bridges store the return address of a call into C at sp[-1],
 * as an x86-64 call would leave it, so frame walking starts the same way. */

static uintptr_t* return_slot(uintptr_t sp, size_t frame) {
  (void)frame;
  return (uintptr_t*)sp + 1;
}

/* Bytes from a frame's stack pointer to its caller's. */
static size_t frame_span(size_t frame) { return frame; }

/* The stack pointer of code entered as if called by the frame at sp;
 * oti_native_resume loads the return address from sp[-1]. */
static uintptr_t call_sp(uintptr_t sp) { return sp; }

static uint32_t frame_size(struct sljit_compiler* c, sljit_s32 scratches) {
  (void)scratches;
  return (uint32_t)c->local_size;
}

static void instruction(struct sljit_compiler* c, uint32_t word) {
  sljit_emit_op_custom(c, &word, sizeof(word));
}

/* Encodings, with x31 meaning sp or xzr as the instruction has it. */
#define A64_LDR(rt, rn, offset) (0xf9400000u | ((uint32_t)(offset) / 8) << 10 | (rn) << 5 | (rt))
#define A64_SUB_IMM(rd, rn, imm, shift) \
  (0xd1000000u | (uint32_t)(shift) << 22 | (uint32_t)(imm) << 10 | (rn) << 5 | (rd))
#define A64_CMP(rn, rm) (0xeb00001fu | (rm) << 16 | (rn) << 5)
#define A64_CMP_SP(rm) (0xeb2063ffu | (rm) << 16)
#define A64_BCOND(words, cond) (0x54000000u | ((uint32_t)(words) & 0x7ffff) << 5 | (cond))
#define A64_MOVZ(rd, imm, hw) (0xd2800000u | (uint32_t)(hw) << 21 | ((uint32_t)(imm) & 0xffff) << 5 | (rd))
#define A64_MOVK(rd, imm, hw) (0xf2800000u | (uint32_t)(hw) << 21 | ((uint32_t)(imm) & 0xffff) << 5 | (rd))
#define A64_ADR(rd, bytes) (0x10000000u | ((uint32_t)(bytes) & 3) << 29 | ((uint32_t)(bytes) >> 2) << 5 | (rd))
#define A64_BR(rn) (0xd61f0000u | (rn) << 5)
#define A64_HS 2

_Static_assert(offsetof(ot_state, native.limit) % 8 == 0 &&
                   offsetof(ot_state, native.limit) < 32768 &&
                   offsetof(ot_state, native.base_limit) % 8 == 0 &&
                   offsetof(ot_state, native.base_limit) < 32768,
               "the checks load with a scaled 12-bit offset");

/* The poll at function entries and loop back edges: one compare of the stack
 * pointer with native.limit, which sljit can't name: ldr x9, [x19, limit];
 * cmp sp, x9. */
static struct sljit_jump* poll(struct sljit_compiler* c) {
  instruction(c, A64_LDR(9, 19, offsetof(ot_state, native.limit)));
  instruction(c, A64_CMP_SP(9));
  sljit_set_current_flags(c, SLJIT_CURRENT_FLAGS_SUB | SLJIT_CURRENT_FLAGS_COMPARE |
                                 SLJIT_SET_LESS);
  return sljit_emit_jump(c, SLJIT_LESS);
}

/* A frame too big for the check after the prologue checks before it, and
 * moves to a new segment before it is made: oti_segment_grow takes the size
 * in x9 and where to continue in x10, and leaves x30, the function's return
 * address, and the argument registers alone.
 *
 *   sub x9, sp, need       ldr x10, [x19, base_limit]   cmp x9, x10
 *   b.hs 1f                mov x9, need                  adr x10, 1f
 *   mov x16, oti_segment_grow                            br x16
 * 1: */
static void check_big_frame(struct sljit_compiler* c, size_t need) {
  _Static_assert((SMALL_FRAME + 100000 * 8) >> 24 == 0, "frame sizes fit two 12-bit immediates");
  uint64_t grow = (uint64_t)(uintptr_t)oti_segment_grow;
  instruction(c, A64_SUB_IMM(9, 31, need >> 12, 1));
  instruction(c, A64_SUB_IMM(9, 9, need & 0xfff, 0));
  instruction(c, A64_LDR(10, 19, offsetof(ot_state, native.base_limit)));
  instruction(c, A64_CMP(9, 10));
  instruction(c, A64_BCOND(9, A64_HS));
  instruction(c, A64_MOVZ(9, need, 0));
  instruction(c, A64_MOVK(9, need >> 16, 1));
  instruction(c, A64_ADR(10, 6 * 4));
  for (int i = 0; i < 4; i++)
    instruction(c, (i ? A64_MOVK(16, grow >> (16 * i), i) : A64_MOVZ(16, grow, 0)));
  instruction(c, A64_BR(16));
}

#ifdef __APPLE__
#define ASM_ADDRESS(reg, name) \
  "  adrp " reg ", " ASM_SYM(name) "@PAGE\n  add " reg ", " reg ", " ASM_SYM(name) "@PAGEOFF\n"
#else
#define ASM_ADDRESS(reg, name) \
  "  adrp " reg ", " ASM_SYM(name) "\n  add " reg ", " reg ", :lo12:" ASM_SYM(name) "\n"
#endif

/* Store new at [x19 + OFF_LIMIT] if it still holds old, with x14 to x16. */
#define ASM_SWAP_LIMIT(old, new)       \
  "  add x16, x19, #OFF_LIMIT\n"       \
  "8:\n"                               \
  "  ldxr x14, [x16]\n"                \
  "  cmp x14, " old "\n"               \
  "  b.ne 9f\n"                        \
  "  stxr w15, " new ", [x16]\n"       \
  "  cbnz w15, 8b\n"                   \
  "9:\n"                               \
  "  clrex\n"

_Static_assert(STACK_MAX % 65536 == 0 && STACK_MAX >> 32 == 0, "oti_poll loads it with one movz");

/* oti_native_enter(rt, callee, entry, argc): call native code from C with the
 * arguments in native.args. Native code runs on the thread's Otium stack:
 * the outermost entry starts at its top, a nested one 16 bytes below the
 * frame that called C, leaving that call's return address in place. The
 * entry leaves a record under the call (the previous native.sp, the
 * previous record, the C stack pointer to return on and the previous
 * native_c_sp) that frame walking and unwinding use, and points
 * native_c_sp at its own C frame, so C called from here doesn't overwrite
 * it. The unwinder returns failure here through oti_native_fail, with the
 * stack pointer at the record. It saves every register C expects kept,
 * d8 to d15 too: unwinding returns here past C frames that may hold
 * values in them. */
void oti_native_trampolines(void);
void oti_native_trampolines(void) {
  __asm__(ASM_TEXT
          ".p2align 4\n"
          ASM_FUNC(oti_native_enter)
          "  stp x29, x30, [sp, #-160]!\n"
          "  stp x19, x20, [sp, #16]\n"
          "  stp x21, x22, [sp, #32]\n"
          "  stp x23, x24, [sp, #48]\n"
          "  stp x25, x26, [sp, #64]\n"
          "  stp x27, x28, [sp, #80]\n"
          "  stp d8, d9, [sp, #96]\n"
          "  stp d10, d11, [sp, #112]\n"
          "  stp d12, d13, [sp, #128]\n"
          "  stp d14, d15, [sp, #144]\n"
          "  mov x19, x0\n"
          "  mov x21, sp\n"
          "  ldr x9, [x19, #OFF_DEPTH]\n"
          "  cbnz x9, 1f\n"
          "  ldr x9, [x19, #OFF_TOP]\n"
          "  b 3f\n"
          "1:\n"
          "  ldr x9, [x19, #OFF_SP]\n"
          "  sub x9, x9, #16\n"
          "3:\n"
          "  and x9, x9, #-16\n"
          "  sub x9, x9, #32\n"
          "  mov sp, x9\n"
          "  ldr x10, [x19, #OFF_SP]\n"
          "  ldr x11, [x19, #OFF_ENTRY]\n"
          "  stp x10, x11, [sp]\n"
          "  ldr x10, [x19, #OFF_CSP]\n"
          "  stp x21, x10, [sp, #16]\n"
          "  str x21, [x19, #OFF_CSP]\n"
          "  ldr x10, [x19, #OFF_CBASE]\n"
          "  cbnz x10, 4f\n"
          "  str x21, [x19, #OFF_CBASE]\n"
          "4:\n"
          "  str x9, [x19, #OFF_ENTRY]\n"
          "  str xzr, [x19, #OFF_SP]\n"
          "  ldr x10, [x19, #OFF_DEPTH]\n"
          "  add x10, x10, #1\n"
          "  str x10, [x19, #OFF_DEPTH]\n"
          "  ldr x20, [x19, #OFF_HEAP_USED]\n"
          "  ldr x10, [x19, #OFF_HEAP_START]\n"
          "  add x20, x20, x10\n"
          "  mov x9, x2\n"
          "  mov x0, x3\n"
          "  ldr x10, [x19, #OFF_ARGS]\n"
          "  ldp x2, x3, [x10]\n"
          "  ldp x4, x5, [x10, #16]\n"
          "  ldr x6, [x10, #32]\n"
          "  blr x9\n"
          ASM_INNER(oti_native_return)
          "  b 2f\n"
          ASM_INNER(oti_native_fail)
          "  mov x0, #0\n"
          "2:\n"
          "  ldr x10, [x19, #OFF_HEAP_START]\n"
          "  sub x20, x20, x10\n"
          "  str x20, [x19, #OFF_HEAP_USED]\n"
          "  ldr x10, [x19, #OFF_DEPTH]\n"
          "  sub x10, x10, #1\n"
          "  str x10, [x19, #OFF_DEPTH]\n"
          "  ldp x10, x11, [sp]\n"
          "  str x10, [x19, #OFF_SP]\n"
          "  str x11, [x19, #OFF_ENTRY]\n"
          "  ldp x9, x10, [sp, #16]\n"
          "  str x10, [x19, #OFF_CSP]\n"
          "  cbnz x10, 5f\n"
          "  str xzr, [x19, #OFF_CBASE]\n"
          "5:\n"
          "  mov sp, x9\n"
          "  ldp x19, x20, [sp, #16]\n"
          "  ldp x21, x22, [sp, #32]\n"
          "  ldp x23, x24, [sp, #48]\n"
          "  ldp x25, x26, [sp, #64]\n"
          "  ldp x27, x28, [sp, #80]\n"
          "  ldp d8, d9, [sp, #96]\n"
          "  ldp d10, d11, [sp, #112]\n"
          "  ldp d12, d13, [sp, #128]\n"
          "  ldp d14, d15, [sp, #144]\n"
          "  ldp x29, x30, [sp], #160\n"
          "  ret\n"
          ASM_END(oti_native_enter)
          ".p2align 4\n"
          /* oti_thread_begin: a green thread's first instructions, on its
           * own Otium stack, with the state in x19, the entry address in x0
           * and the callee, count and arguments in native.callee,
           * native.argc and native.args. It leaves an entry record as
           * oti_native_enter does, over a save area whose return path lands
           * in oti_thread_end with the state in x19, which hands the result
           * (0 after failure) to oti_thread_finished on the C stack. The
           * callee is entered with oti_native_return as its return address,
           * which frame walking looks for. */
          ASM_FUNC(oti_thread_begin)
          "  mov x9, x0\n"
          "  sub sp, sp, #160\n"
          ASM_ADDRESS("x10", oti_thread_end)
          "  stp xzr, x10, [sp]\n"
          "  stp x19, xzr, [sp, #16]\n"
          "  mov x10, #32\n"
          "6:\n"
          "  str xzr, [sp, x10]\n"
          "  add x10, x10, #8\n"
          "  cmp x10, #160\n"
          "  b.lo 6b\n"
          "  mov x21, sp\n"
          "  sub sp, sp, #32\n"
          "  stp xzr, xzr, [sp]\n"
          "  ldr x10, [x19, #OFF_CSP]\n"
          "  stp x21, x10, [sp, #16]\n"
          "  mov x10, sp\n"
          "  str x10, [x19, #OFF_ENTRY]\n"
          "  str xzr, [x19, #OFF_SP]\n"
          "  ldr x10, [x19, #OFF_DEPTH]\n"
          "  add x10, x10, #1\n"
          "  str x10, [x19, #OFF_DEPTH]\n"
          "  ldr x1, [x19, #OFF_CALLEE]\n"
          "  ldr x0, [x19, #OFF_ARGC]\n"
          "  ldr x10, [x19, #OFF_ARGS]\n"
          "  ldp x2, x3, [x10]\n"
          "  ldp x4, x5, [x10, #16]\n"
          "  ldr x6, [x10, #32]\n"
          ASM_ADDRESS("x30", oti_native_return)
          "  br x9\n"
          ASM_END(oti_thread_begin)
          /* The C stack position the record restored is from when the
           * thread began; the main thread may have moved on since. A thread
           * ends with no C frames of its own, at the current base. */
          ASM_FUNC(oti_thread_end)
          "  ldr x9, [x19, #OFF_CBASE]\n"
          "  mov sp, x9\n"
          "  str x9, [x19, #OFF_CSP]\n"
          "  mov x1, x0\n"
          "  mov x0, x19\n"
          "  bl " ASM_SYM(oti_thread_finished) "\n"
          "  brk #1\n"
          ASM_END(oti_thread_end)
          /* oti_segment_return: the return address of a function at the top
           * of a segment. The link record is at the stack pointer; the
           * result in x0 goes back to the caller on the segment below, which
           * becomes the one in use (drop_segment and use_segment). Only x0,
           * x19 and x20 matter to native code here. */
          ASM_FUNC(oti_segment_return)
          "  ldr x9, [x19, #OFF_SEGMENT]\n"
          "  ldr x10, [x9, #SEGMENT_SIZE]\n"
          "  ldr x11, [x19, #OFF_STACK_BYTES]\n"
          "  sub x11, x11, x10\n"
          "  str x11, [x19, #OFF_STACK_BYTES]\n"
          "  ldr x9, [x9, #SEGMENT_PREV]\n"
          "  str x9, [x19, #OFF_SEGMENT]\n"
          "  ldr x10, [x9, #SEGMENT_SIZE]\n"
          "  sub x10, x9, x10\n"
          "  add x10, x10, #SEGMENT_LIMIT\n"
          "  ldr x11, [x19, #OFF_BASE_LIMIT]\n"
          "  str x10, [x19, #OFF_BASE_LIMIT]\n"
          ASM_SWAP_LIMIT("x11", "x10")
          "  ldp x10, x30, [sp]\n"
          "  mov sp, x10\n"
          "  ret\n"
          ASM_END(oti_segment_return)
          /* oti_poll: the entry check or loop check failed. A function past
           * its segment's limit with no interrupt pending, whose frame size
           * is cached, moves as grow_frame would move it when a segment is
           * kept above; everything else goes to h_poll. The return address
           * into the function is in x30, the function's frame at the stack
           * pointer. Native code expects only x19 and x20 kept here. */
          ASM_FUNC(oti_poll)
          "  mov x9, sp\n"
          "  ldr x10, [x19, #OFF_BASE_LIMIT]\n"
          "  cmp x9, x10\n"
          "  b.hs 1f\n"
          "  ldr x11, [x19, #OFF_LIMIT]\n"
          "  cmp x11, x10\n"
          "  b.ne 1f\n"
          "  ldr x11, [x19, #OFF_SEGMENT]\n"
          "  ldr x11, [x11, #SEGMENT_NEXT]\n"
          "  cbz x11, 1f\n"
          "  lsr x12, x30, #3\n"
          "  and x12, x12, #FRAME_MASK\n"
          "  ldr x13, [x19, #OFF_RUNTIME]\n"
          "  add x13, x13, x12, lsl #4\n"
          "  add x13, x13, #OFF_FRAMES\n"
          "  ldp x14, x12, [x13]\n"
          "  cmp x14, x30\n"
          "  b.ne 1f\n"
          "  ldr x13, [x11, #SEGMENT_SIZE]\n"
          "  ldr x14, [x19, #OFF_STACK_BYTES]\n"
          "  add x14, x14, x13\n"
          "  movz x15, #SEGMENTS_MAX_HIGH, lsl #16\n"
          "  cmp x14, x15\n"
          "  b.hi 1f\n"
          "  str x14, [x19, #OFF_STACK_BYTES]\n"
          "  str x11, [x19, #OFF_SEGMENT]\n"
          "  sub x13, x11, x13\n"
          "  add x13, x13, #SEGMENT_LIMIT\n"
          "  str x13, [x19, #OFF_BASE_LIMIT]\n"
          ASM_SWAP_LIMIT("x10", "x13")
          /* link_segment, then the frame copied under it. */
          "  add x14, x9, x12\n"
          "  ldr x15, [x9, #8]\n"
          "  stp x14, x15, [x11, #-16]\n"
          "  sub x16, x11, #16\n"
          "  sub x16, x16, x12\n"
          "  mov x13, x16\n"
          "  add x17, x9, x12\n"
          "2:\n"
          "  ldp x14, x15, [x9], #16\n"
          "  stp x14, x15, [x13], #16\n"
          "  cmp x9, x17\n"
          "  b.lo 2b\n"
          ASM_ADDRESS("x14", oti_segment_return)
          "  str x14, [x16, #8]\n"
          "  mov sp, x16\n"
          "  ret\n"
          "1:\n"
          "  b " ASM_SYM(oti_bridge_h_poll) "\n"
          ASM_END(oti_poll)
          /* oti_segment_grow: reached from a big frame's check before its
           * prologue (check_big_frame), with the frame's size in x9, where
           * to continue in x10 and the arguments still in their registers.
           * It saves them as oti_segment_grow_entry expects, with the
           * function's return address at the caller's sp[-1], gets a new
           * segment's link record on the C stack, and continues the
           * function under it with oti_segment_return as return address. */
          ASM_FUNC(oti_segment_grow)
          "  sub sp, sp, #80\n"
          "  stp x0, x1, [sp]\n"
          "  stp x2, x3, [sp, #16]\n"
          "  stp x4, x5, [sp, #32]\n"
          "  stp x6, x10, [sp, #48]\n"
          "  str x30, [sp, #72]\n"
          "  mov x21, sp\n"
          "  mov x0, x19\n"
          "  mov x1, x9\n"
          "  mov x2, x21\n"
          "  ldr x9, [x19, #OFF_CSP]\n"
          "  mov sp, x9\n"
          "  bl " ASM_SYM(oti_segment_grow_entry) "\n"
          "  mov sp, x0\n"
          "  ldp x0, x1, [x21]\n"
          "  ldp x2, x3, [x21, #16]\n"
          "  ldp x4, x5, [x21, #32]\n"
          "  ldp x6, x10, [x21, #48]\n"
          ASM_ADDRESS("x30", oti_segment_return)
          "  br x10\n"
          ASM_END(oti_segment_grow)
          /* Where a thread that stopped in a C call (see BRIDGE) resumes,
           * with the call's value in x0. */
          ASM_FUNC(oti_c_resume)
          "  ldr x9, [x19, #OFF_SP]\n"
          "  ldur x30, [x9, #-8]\n"
          "  mov sp, x9\n"
          "  ret\n"
          ASM_END(oti_c_resume)
          /* oti_native_resume(rt, sp, pc, value), with the return address
           * of code entered as if called (call_sp) taken from sp[-1]. */
          ASM_FUNC(oti_native_resume)
          "  mov x19, x0\n"
          "  mov sp, x1\n"
          "  ldur x30, [x1, #-8]\n"
          "  ldr x20, [x19, #OFF_HEAP_USED]\n"
          "  ldr x9, [x19, #OFF_HEAP_START]\n"
          "  add x20, x20, x9\n"
          "  mov x0, x3\n"
          "  br x2\n"
          ASM_END(oti_native_resume)
          ASM_TEXT_END);
}

/* A bridge per C function native code calls: it records the calling frame
 * and the call's return address at its sp[-1], where frame walking reads it,
 * and calls the function on the C stack. Each has its own direct call,
 * which predicts far better than one shared indirect call. The stack
 * pointer moves to the C stack before anything is stored under the Otium
 * one. A thread that stopped in one resumes at oti_c_resume with the call's
 * value in x0. */
#define BRIDGE(fn)                                \
  __asm__ volatile(ASM_TEXT                       \
                   ".p2align 4\n"                 \
                   ASM_FUNC(oti_bridge_##fn)      \
                   "  mov x9, sp\n"               \
                   "  str x9, [x19, #OFF_SP]\n"   \
                   "  ldr x10, [x19, #OFF_CSP]\n" \
                   "  mov sp, x10\n"              \
                   "  stur x30, [x9, #-8]\n"      \
                   "  bl %c0\n"                   \
                   "  ldr x9, [x19, #OFF_SP]\n"   \
                   "  ldur x30, [x9, #-8]\n"      \
                   "  mov sp, x9\n"               \
                   "  ret\n"                      \
                   ASM_TEXT_END                   \
                   :                              \
                   : "S"(fn))
