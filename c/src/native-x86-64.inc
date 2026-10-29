/* The x86-64 half of native.c: frame layout, the machine code sljit can't
 * express, and the trampolines. sljit's registers here: S0 is rbx, S1 r15,
 * R0 rax, R1 rsi and R2 to R6 rdi, rcx, r8, r11 and r10.
 *
 * A call pushes its return address, so a frame's own return address sits
 * just above it, and a frame's size (oti_descriptor.frame) doesn't count
 * it. */

/* Registers R7 and up overlap sljit's saved registers, which a prologue
 * pushes when the function uses them. */
#define SCRATCH_REGISTERS 7

static uintptr_t* return_slot(uintptr_t sp, size_t frame) { return (uintptr_t*)(sp + frame); }

/* Bytes from a frame's stack pointer to its caller's. */
static size_t frame_span(size_t frame) { return frame + 8; }

/* The stack pointer of code entered as if called by the frame at sp, whose
 * return address is at sp[-1]. */
static uintptr_t call_sp(uintptr_t sp) { return sp - 8; }

static uint32_t frame_size(struct sljit_compiler* c, sljit_s32 scratches) {
  sljit_s32 pushed = scratches > SCRATCH_REGISTERS ? scratches - SCRATCH_REGISTERS : 0;
  return (uint32_t)(c->local_size + pushed * 8);
}

#define OFF_LIMIT 0
_Static_assert(offsetof(ot_state, poll_limit) == OFF_LIMIT, "the poll's displacement");

/* The poll at function entries and loop back edges: one compare of the stack
 * pointer with poll_limit, which sljit can't name, so it goes in as bytes:
 * cmp rsp, [rbx + limit]. */
static struct sljit_jump* poll(struct sljit_compiler* c) {
  _Static_assert(OFF_LIMIT < 128, "poll uses an 8-bit displacement");
  unsigned char cmp[] = {0x48, 0x3b, 0x63, OFF_LIMIT};
  sljit_emit_op_custom(c, cmp, sizeof(cmp));
  sljit_set_current_flags(c, SLJIT_CURRENT_FLAGS_SUB | SLJIT_CURRENT_FLAGS_COMPARE |
                                 SLJIT_SET_LESS);
  return sljit_emit_jump(c, SLJIT_LESS);
}

/* A frame too big for the check after the prologue checks before it, with
 * r9 and rdx, which carry no arguments, and moves to a new segment before
 * it is made. */
static void check_big_frame(struct sljit_compiler* c, size_t need) {
  int32_t down = -(int32_t)need, limit = (int32_t)offsetof(ot_state, native.base_limit);
  uint64_t grow = (uint64_t)(uintptr_t)oti_segment_grow;
  unsigned char lea[8] = {0x4c, 0x8d, 0x8c, 0x24}, cmp[7] = {0x4c, 0x3b, 0x8b},
                jae[2] = {0x73, 18}, size[5] = {0xba}, target[10] = {0x49, 0xb9},
                call[3] = {0x41, 0xff, 0xd1};
  memcpy(lea + 4, &down, 4);
  memcpy(cmp + 3, &limit, 4);
  uint32_t bytes = (uint32_t)need;
  memcpy(size + 1, &bytes, 4);
  memcpy(target + 2, &grow, 8);
  sljit_emit_op_custom(c, lea, sizeof(lea));
  sljit_emit_op_custom(c, cmp, sizeof(cmp));
  sljit_emit_op_custom(c, jae, sizeof(jae));
  sljit_emit_op_custom(c, size, sizeof(size));
  sljit_emit_op_custom(c, target, sizeof(target));
  sljit_emit_op_custom(c, call, sizeof(call));
}

/* oti_native_enter(rt, callee, entry, argc): call native code from C with the
 * arguments in native.args. Native code runs on the thread's Otium stack:
 * the outermost entry starts at its top, a nested one just below the frame
 * that called C, leaving that call's return address in place. The entry
 * leaves a record under the call (the previous native.sp, the previous
 * record, the C stack pointer to return on and the previous native_c_sp)
 * that frame walking and unwinding use, and points native_c_sp below its
 * own C frame, so C called from here doesn't overwrite it. The unwinder
 * returns failure here through oti_native_fail, with the stack pointer at
 * the record. */
void oti_native_trampolines(void);
void oti_native_trampolines(void) {
  __asm__(ASM_TEXT
          ".p2align 4\n"
          ASM_FUNC(oti_native_enter)
          "  push %rbp\n"
          "  push %rbx\n"
          "  push %r12\n"
          "  push %r13\n"
          "  push %r14\n"
          "  push %r15\n"
          "  mov %rdi, %rbx\n"
          "  mov %rsp, %r12\n"
          "  cmpq $0, OFF_DEPTH(%rbx)\n"
          "  jne 1f\n"
          "  mov OFF_TOP(%rbx), %rsp\n"
          "  jmp 3f\n"
          "1:\n"
          "  mov OFF_SP(%rbx), %rsp\n"
          "  sub $16, %rsp\n"
          "3:\n"
          "  and $-16, %rsp\n"
          "  sub $32, %rsp\n"
          "  mov OFF_SP(%rbx), %rax\n"
          "  mov %rax, 0(%rsp)\n"
          "  mov OFF_ENTRY(%rbx), %rax\n"
          "  mov %rax, 8(%rsp)\n"
          "  mov %r12, 16(%rsp)\n"
          "  mov OFF_CSP(%rbx), %rax\n"
          "  mov %rax, 24(%rsp)\n"
          "  mov %r12, %rax\n"
          "  and $-16, %rax\n"
          "  mov %rax, OFF_CSP(%rbx)\n"
          "  cmpq $0, OFF_CBASE(%rbx)\n"
          "  jne 4f\n"
          "  mov %rax, OFF_CBASE(%rbx)\n"
          "4:\n"
          "  mov %rsp, OFF_ENTRY(%rbx)\n"
          "  movq $0, OFF_SP(%rbx)\n"
          "  incq OFF_DEPTH(%rbx)\n"
          "  mov OFF_HEAP_USED(%rbx), %r15\n"
          "  add OFF_HEAP_START(%rbx), %r15\n"
          "  mov %rdx, %r9\n"
          "  mov %rcx, %rax\n"
          "  mov OFF_ARGS(%rbx), %r10\n"
          "  mov 0(%r10), %rdi\n"
          "  mov 8(%r10), %rcx\n"
          "  mov 16(%r10), %r8\n"
          "  mov 24(%r10), %r11\n"
          "  mov 32(%r10), %r10\n"
          "  call *%r9\n"
          ASM_INNER(oti_native_return)
          "  jmp 2f\n"
          ASM_INNER(oti_native_fail)
          "  xor %eax, %eax\n"
          "2:\n"
          "  sub OFF_HEAP_START(%rbx), %r15\n"
          "  mov %r15, OFF_HEAP_USED(%rbx)\n"
          "  decq OFF_DEPTH(%rbx)\n"
          "  mov 0(%rsp), %rdx\n"
          "  mov %rdx, OFF_SP(%rbx)\n"
          "  mov 8(%rsp), %rdx\n"
          "  mov %rdx, OFF_ENTRY(%rbx)\n"
          "  mov 24(%rsp), %rdx\n"
          "  mov %rdx, OFF_CSP(%rbx)\n"
          "  test %rdx, %rdx\n"
          "  jne 5f\n"
          "  movq $0, OFF_CBASE(%rbx)\n"
          "5:\n"
          "  mov 16(%rsp), %rsp\n"
          "  pop %r15\n"
          "  pop %r14\n"
          "  pop %r13\n"
          "  pop %r12\n"
          "  pop %rbx\n"
          "  pop %rbp\n"
          "  ret\n"
          ASM_END(oti_native_enter)
          ".p2align 4\n"
          /* oti_thread_begin: a green thread's first instructions, on its
           * own Otium stack, with the state in rbx, the entry address in rax
           * and the callee, count and arguments in native.callee,
           * native.argc and native.args. It leaves an entry record as
           * oti_native_enter does, but whose return path pops a frame built
           * here and lands in oti_thread_end, which hands the result (0
           * after failure) to oti_thread_finished on the C stack. The callee
           * is entered with oti_native_return as its return address, which
           * frame walking looks for. */
          ASM_FUNC(oti_thread_begin)
          "  lea " ASM_SYM(oti_thread_end) "(%rip), %rcx\n"
          "  push %rcx\n"
          "  push $0\n"
          "  push %rbx\n"
          "  push $0\n"
          "  push $0\n"
          "  push $0\n"
          "  push $0\n"
          "  mov %rsp, %r12\n"
          "  mov %rax, %r9\n"
          "  and $-16, %rsp\n"
          "  sub $32, %rsp\n"
          "  movq $0, 0(%rsp)\n"
          "  movq $0, 8(%rsp)\n"
          "  mov %r12, 16(%rsp)\n"
          "  mov OFF_CSP(%rbx), %rax\n"
          "  mov %rax, 24(%rsp)\n"
          "  mov %rsp, OFF_ENTRY(%rbx)\n"
          "  movq $0, OFF_SP(%rbx)\n"
          "  incq OFF_DEPTH(%rbx)\n"
          "  mov OFF_CALLEE(%rbx), %rsi\n"
          "  mov OFF_ARGC(%rbx), %rax\n"
          "  mov OFF_ARGS(%rbx), %r10\n"
          "  mov 0(%r10), %rdi\n"
          "  mov 8(%r10), %rcx\n"
          "  mov 16(%r10), %r8\n"
          "  mov 24(%r10), %r11\n"
          "  mov 32(%r10), %r10\n"
          "  lea " ASM_SYM(oti_native_return) "(%rip), %rdx\n"
          "  push %rdx\n"
          "  jmp *%r9\n"
          ASM_END(oti_thread_begin)
          /* The C stack position the record restored is from when the
           * thread began; the main thread may have moved on since. A thread
           * ends with no C frames of its own, at the current base. */
          ASM_FUNC(oti_thread_end)
          "  mov OFF_CBASE(%rbx), %rsp\n"
          "  mov %rsp, OFF_CSP(%rbx)\n"
          "  mov %rbx, %rdi\n"
          "  mov %rax, %rsi\n"
          "  call " ASM_SYM(oti_thread_finished) "\n"
          "  ud2\n"
          ASM_END(oti_thread_end)
          /* oti_segment_return: the return address of a function at the top
           * of a segment. The link record is at the stack pointer; the
           * result in rax goes back to the caller on the segment below,
           * which becomes the one in use (drop_segment and use_segment).
           * Only rax, rbx and r15 matter to native code here, and the
           * callee-saved registers hold the caller's values, so it uses
           * scratch ones. */
          ASM_FUNC(oti_segment_return)
          "  mov OFF_SEGMENT(%rbx), %rdi\n"
          "  mov SEGMENT_SIZE(%rdi), %rcx\n"
          "  sub %rcx, OFF_STACK_BYTES(%rbx)\n"
          "  mov SEGMENT_PREV(%rdi), %rsi\n"
          "  mov %rsi, OFF_SEGMENT(%rbx)\n"
          "  mov SEGMENT_SIZE(%rsi), %rcx\n"
          "  neg %rcx\n"
          "  lea SEGMENT_LIMIT(%rsi,%rcx), %rcx\n"
          "  mov %rax, %r8\n"
          "  mov OFF_BASE_LIMIT(%rbx), %rax\n"
          "  mov %rcx, OFF_BASE_LIMIT(%rbx)\n"
          "  cmpxchg %rcx, OFF_LIMIT(%rbx)\n"
          "  mov %r8, %rax\n"
          "  mov 8(%rsp), %rdx\n"
          "  mov (%rsp), %rsp\n"
          "  jmp *%rdx\n"
          ASM_END(oti_segment_return)
          /* oti_poll: the entry check or loop check failed. A function past
           * its segment's limit with no interrupt pending, whose frame size
           * is cached, moves as grow_frame would move it when a segment is
           * kept above; everything else goes to h_poll. The return address
           * into the function is at the stack pointer, its frame above it.
           * Native code expects only callee-saved registers kept here. */
          ASM_FUNC(oti_poll)
          "  lea 8(%rsp), %rsi\n"
          "  mov OFF_BASE_LIMIT(%rbx), %rax\n"
          "  cmp %rax, %rsi\n"
          "  jae 1f\n"
          "  cmp %rax, OFF_LIMIT(%rbx)\n"
          "  jne 1f\n"
          "  mov OFF_SEGMENT(%rbx), %r10\n"
          "  mov SEGMENT_NEXT(%r10), %r10\n"
          "  test %r10, %r10\n"
          "  jz 1f\n"
          "  mov (%rsp), %rdx\n"
          "  mov %rdx, %rcx\n"
          "  shr $3, %rcx\n"
          "  and $FRAME_MASK, %rcx\n"
          "  shl $4, %rcx\n"
          "  mov OFF_RUNTIME(%rbx), %r8\n"
          "  cmp OFF_FRAMES(%r8,%rcx), %rdx\n"
          "  jne 1f\n"
          "  mov OFF_FRAMES+8(%r8,%rcx), %rcx\n"
          "  mov SEGMENT_SIZE(%r10), %r8\n"
          "  mov OFF_STACK_BYTES(%rbx), %r9\n"
          "  add %r8, %r9\n"
          "  cmp $SEGMENTS_MAX, %r9\n"
          "  ja 1f\n"
          "  mov %r9, OFF_STACK_BYTES(%rbx)\n"
          "  mov %r10, OFF_SEGMENT(%rbx)\n"
          "  neg %r8\n"
          "  lea SEGMENT_LIMIT(%r10,%r8), %r9\n"
          "  mov %r9, OFF_BASE_LIMIT(%rbx)\n"
          "  cmpxchg %r9, OFF_LIMIT(%rbx)\n"
          /* link_segment, then the frame copied under it. */
          "  lea 8(%rsi,%rcx), %r9\n"
          "  mov %r9, -16(%r10)\n"
          "  mov (%rsi,%rcx), %r9\n"
          "  mov %r9, -8(%r10)\n"
          "  lea " ASM_SYM(oti_segment_return) "(%rip), %r9\n"
          "  mov %r9, -24(%r10)\n"
          "  sub $24, %r10\n"
          "  sub %rcx, %r10\n"
          "  test %rcx, %rcx\n"
          "  jz 3f\n"
          "2:\n"
          "  mov -8(%rsi,%rcx), %r9\n"
          "  mov %r9, -8(%r10,%rcx)\n"
          "  sub $8, %rcx\n"
          "  jnz 2b\n"
          "3:\n"
          "  mov %rdx, -8(%r10)\n"
          "  lea -8(%r10), %rsp\n"
          "  ret\n"
          "1:\n"
          "  jmp " ASM_SYM(oti_bridge_h_poll) "\n"
          ASM_END(oti_poll)
          /* oti_segment_grow: called from a big frame's check before its
           * prologue, with the frame's size in rdx and the arguments still
           * in their registers. It saves them, gets a new segment's link
           * record from oti_segment_grow_entry on the C stack, and continues
           * the function under it, as if called with oti_segment_return as
           * return address. */
          ASM_FUNC(oti_segment_grow)
          "  push %r12\n"
          "  push %rax\n"
          "  push %rsi\n"
          "  push %rdi\n"
          "  push %rcx\n"
          "  push %r8\n"
          "  push %r11\n"
          "  push %r10\n"
          "  mov %rsp, %r12\n"
          "  mov %rdx, %rsi\n"
          "  mov %r12, %rdx\n"
          "  mov %rbx, %rdi\n"
          "  mov OFF_CSP(%rbx), %rsp\n"
          "  call " ASM_SYM(oti_segment_grow_entry) "\n"
          "  lea -8(%rax), %r9\n"
          "  lea " ASM_SYM(oti_segment_return) "(%rip), %rdx\n"
          "  mov %rdx, (%r9)\n"
          "  mov 0(%r12), %r10\n"
          "  mov 8(%r12), %r11\n"
          "  mov 16(%r12), %r8\n"
          "  mov 24(%r12), %rcx\n"
          "  mov 32(%r12), %rdi\n"
          "  mov 40(%r12), %rsi\n"
          "  mov 48(%r12), %rax\n"
          "  mov 64(%r12), %rdx\n"
          "  mov 56(%r12), %r12\n"
          "  mov %r9, %rsp\n"
          "  jmp *%rdx\n"
          ASM_END(oti_segment_grow)
          /* Where a thread that stopped in a C call (see BRIDGE) resumes,
           * with the call's value in rax. */
          ASM_FUNC(oti_c_resume)
          "  mov OFF_SP(%rbx), %rsp\n"
          "  sub $8, %rsp\n"
          "  ret\n"
          ASM_END(oti_c_resume)
          ASM_FUNC(oti_native_resume)
          "  mov %rdi, %rbx\n"
          "  mov %rsi, %rsp\n"
          "  mov OFF_HEAP_USED(%rbx), %r15\n"
          "  add OFF_HEAP_START(%rbx), %r15\n"
          "  mov %rcx, %rax\n"
          "  jmp *%rdx\n"
          ASM_END(oti_native_resume)
          ASM_TEXT_END);
}

/* A bridge per C function native code calls: it records the calling frame
 * (whose return address stays on the Otium stack, where frame walking reads
 * it) and calls the function on the C stack. Each has its own direct call,
 * which predicts far better than one shared indirect call. A thread that
 * stopped in one resumes at oti_c_resume with the call's value in rax. */
#define BRIDGE(fn)                                       \
  __asm__ volatile(ASM_TEXT                              \
                   ".p2align 4\n"                        \
                   ASM_FUNC(oti_bridge_##fn)             \
                   "  lea 8(%%rsp), %%rax\n"             \
                   "  mov %%rax, OFF_SP(%%rbx)\n"        \
                   "  mov OFF_CSP(%%rbx), %%rsp\n"       \
                   "  call %P0\n"                        \
                   "  mov OFF_SP(%%rbx), %%rsp\n"        \
                   "  sub $8, %%rsp\n"                   \
                   "  ret\n"                             \
                   ASM_TEXT_END                          \
                   :                                     \
                   : "i"(fn))
