// riscv.cpp - risc-v and opensbi specific functionality

#include "ot/kernel/kernel.hpp"

#define SCAUSE_ECALL 8
#define SSTATUS_SPP (1 << 8)

extern "C" char __bss[], __bss_end[], __stack_top[];

struct sbiret {
  long error;
  long value;
};

struct trap_frame {
  uint32_t ra;
  uint32_t gp;
  uint32_t tp;
  uint32_t t0;
  uint32_t t1;
  uint32_t t2;
  uint32_t t3;
  uint32_t t4;
  uint32_t t5;
  uint32_t t6;
  uint32_t a0;
  uint32_t a1;
  uint32_t a2;
  uint32_t a3;
  uint32_t a4;
  uint32_t a5;
  uint32_t a6;
  uint32_t a7;
  uint32_t s0;
  uint32_t s1;
  uint32_t s2;
  uint32_t s3;
  uint32_t s4;
  uint32_t s5;
  uint32_t s6;
  uint32_t s7;
  uint32_t s8;
  uint32_t s9;
  uint32_t s10;
  uint32_t s11;
  uint32_t sp;
} __attribute__((packed));

#define READ_CSR(reg)                                                          \
  ({                                                                           \
    unsigned long __tmp;                                                       \
    __asm__ __volatile__("csrr %0, " #reg : "=r"(__tmp));                      \
    __tmp;                                                                     \
  })

#define WRITE_CSR(reg, value)                                                  \
  do {                                                                         \
    uint32_t __tmp = (value);                                                  \
    __asm__ __volatile__("csrw " #reg ", %0" ::"r"(__tmp));                    \
  } while (0)

struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4,
                       long arg5, long fid, long eid) {
  register long a0 __asm__("a0") = arg0;
  register long a1 __asm__("a1") = arg1;
  register long a2 __asm__("a2") = arg2;
  register long a3 __asm__("a3") = arg3;
  register long a4 __asm__("a4") = arg4;
  register long a5 __asm__("a5") = arg5;
  register long a6 __asm__("a6") = fid;
  register long a7 __asm__("a7") = eid;

  __asm__ __volatile__("ecall"
                       : "=r"(a0), "=r"(a1)
                       : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5),
                         "r"(a6), "r"(a7)
                       : "memory");
  return (struct sbiret){.error = a0, .value = a1};
}

void oputchar(char ch) {
  sbi_call(ch, 0, 0, 0, 0, 0, 0, 1 /* Console Putchar */);
}

int ogetchar() {
  return (int)sbi_call(0, 0, 0, 0, 0, 0, 0, 2 /* Console Putchar */).error;
}

#define SBI_EXT_SRST 0x53525354 // "SRST"
#define SBI_SRST_SHUTDOWN 0
void kernel_exit(void) {
  sbi_call(0, 0, 0, 0, 0, 0, SBI_SRST_SHUTDOWN, SBI_EXT_SRST);
}

void wfi(void) {
  for (;;) {
    __asm__ __volatile__("wfi");
  }
}

void handle_syscall(struct trap_frame *f) {
  uint32_t sysno = f->a3;
  uint32_t arg0 = f->a0;
  // Not currently used
  // uint32_t arg1 = f->a1;
  // uint32_t arg2 = f->a2;

  f->a0 = 0;
  switch (sysno) {
  case OU_PUTCHAR:
    oputchar(arg0);
    break;
  case OU_YIELD:
    break;
  case OU_EXIT:
    if (current_proc) {
      oprintf("Process %s (pid=%d) exited\n", current_proc->name,
              current_proc->pid);
      current_proc->state = TERMINATED;
    }
    break;
  case OU_GETCHAR:
    f->a0 = ogetchar();
    break;
  case OU_ALLOC_PAGE: {
    if (!current_proc) {
      f->a0 = 0; // Return NULL if no current process
      break;
    }
    // Allocate physical page
    PageAddr paddr = page_allocate(current_proc->pid, 1);
    if (paddr.is_null()) {
      f->a0 = 0; // Return NULL on allocation failure
      break;
    }
    // Get virtual address for this allocation
    uintptr_t vaddr = current_proc->heap_next_vaddr;
    // Map the page into user space
    map_page(current_proc->page_table, vaddr, paddr,
             PAGE_U | PAGE_R | PAGE_W, current_proc->pid);
    // Update next heap address
    current_proc->heap_next_vaddr += OT_PAGE_SIZE;
    // Return virtual address to user
    f->a0 = vaddr;
    break;
  }
  default:
    PANIC("unexpected syscall sysno=%x\n", sysno);
  }
  yield();
}

extern "C" void handle_trap(struct trap_frame *f) {
  uint32_t scause = READ_CSR(scause);
  uint32_t stval = READ_CSR(stval);
  uint32_t user_pc = READ_CSR(sepc);
  uint32_t sstatus = READ_CSR(sstatus);

  if (scause == SCAUSE_ECALL) {
    // Save the user PC before yielding (will be restored in yield)
    if (current_proc) {
      current_proc->user_pc = user_pc + 4; // Skip past the ecall instruction
    }
    handle_syscall(f);
    // user_pc will be restored by yield(), don't write it back here
  } else {
    // Check if trap came from user mode
    bool from_user = !(sstatus & SSTATUS_SPP);

    if (from_user && current_proc) {
      oprintf("Process %s (pid=%d) crashed: scause=%x, stval=%x, sepc=%x\n",
              current_proc->name, current_proc->pid, scause, stval, user_pc);
      current_proc->state = TERMINATED;
      yield();
    } else {
      PANIC("unexpected trap in kernel stval=%x, sepc=%x\n", scause, stval,
            user_pc);
    }
  }
}

__attribute__((naked)) __attribute__((aligned(4))) extern "C" void
kernel_entry(void) {
  __asm__ __volatile__("csrrw sp, sscratch, sp\n"
                       "addi sp, sp, -4 * 31\n"
                       "sw ra,  4 * 0(sp)\n"
                       "sw gp,  4 * 1(sp)\n"
                       "sw tp,  4 * 2(sp)\n"
                       "sw t0,  4 * 3(sp)\n"
                       "sw t1,  4 * 4(sp)\n"
                       "sw t2,  4 * 5(sp)\n"
                       "sw t3,  4 * 6(sp)\n"
                       "sw t4,  4 * 7(sp)\n"
                       "sw t5,  4 * 8(sp)\n"
                       "sw t6,  4 * 9(sp)\n"
                       "sw a0,  4 * 10(sp)\n"
                       "sw a1,  4 * 11(sp)\n"
                       "sw a2,  4 * 12(sp)\n"
                       "sw a3,  4 * 13(sp)\n"
                       "sw a4,  4 * 14(sp)\n"
                       "sw a5,  4 * 15(sp)\n"
                       "sw a6,  4 * 16(sp)\n"
                       "sw a7,  4 * 17(sp)\n"
                       "sw s0,  4 * 18(sp)\n"
                       "sw s1,  4 * 19(sp)\n"
                       "sw s2,  4 * 20(sp)\n"
                       "sw s3,  4 * 21(sp)\n"
                       "sw s4,  4 * 22(sp)\n"
                       "sw s5,  4 * 23(sp)\n"
                       "sw s6,  4 * 24(sp)\n"
                       "sw s7,  4 * 25(sp)\n"
                       "sw s8,  4 * 26(sp)\n"
                       "sw s9,  4 * 27(sp)\n"
                       "sw s10, 4 * 28(sp)\n"
                       "sw s11, 4 * 29(sp)\n"

                       "csrr a0, sscratch\n"
                       "sw a0, 4 * 30(sp)\n"

                       // Reset the kernel stack.
                       "addi a0, sp, 4 * 31\n"
                       "csrw sscratch, a0\n"

                       "mv a0, sp\n"
                       "call handle_trap\n"

                       "lw ra,  4 * 0(sp)\n"
                       "lw gp,  4 * 1(sp)\n"
                       "lw tp,  4 * 2(sp)\n"
                       "lw t0,  4 * 3(sp)\n"
                       "lw t1,  4 * 4(sp)\n"
                       "lw t2,  4 * 5(sp)\n"
                       "lw t3,  4 * 6(sp)\n"
                       "lw t4,  4 * 7(sp)\n"
                       "lw t5,  4 * 8(sp)\n"
                       "lw t6,  4 * 9(sp)\n"
                       "lw a0,  4 * 10(sp)\n"
                       "lw a1,  4 * 11(sp)\n"
                       "lw a2,  4 * 12(sp)\n"
                       "lw a3,  4 * 13(sp)\n"
                       "lw a4,  4 * 14(sp)\n"
                       "lw a5,  4 * 15(sp)\n"
                       "lw a6,  4 * 16(sp)\n"
                       "lw a7,  4 * 17(sp)\n"
                       "lw s0,  4 * 18(sp)\n"
                       "lw s1,  4 * 19(sp)\n"
                       "lw s2,  4 * 20(sp)\n"
                       "lw s3,  4 * 21(sp)\n"
                       "lw s4,  4 * 22(sp)\n"
                       "lw s5,  4 * 23(sp)\n"
                       "lw s6,  4 * 24(sp)\n"
                       "lw s7,  4 * 25(sp)\n"
                       "lw s8,  4 * 26(sp)\n"
                       "lw s9,  4 * 27(sp)\n"
                       "lw s10, 4 * 28(sp)\n"
                       "lw s11, 4 * 29(sp)\n"
                       "lw sp,  4 * 30(sp)\n"
                       "sret\n");
}

__attribute__((naked)) extern "C" void switch_context(uint32_t *prev_sp,
                                                      uint32_t *next_sp) {
  // TODO: Handle stack overflows
  __asm__ __volatile__(
      // Save callee-saved registers onto the current process's stack.
      "addi sp, sp, -13 * 4\n" // Allocate stack space for 13 4-byte registers
      "sw ra,  0  * 4(sp)\n"   // Save callee-saved registers only
      "sw s0,  1  * 4(sp)\n"
      "sw s1,  2  * 4(sp)\n"
      "sw s2,  3  * 4(sp)\n"
      "sw s3,  4  * 4(sp)\n"
      "sw s4,  5  * 4(sp)\n"
      "sw s5,  6  * 4(sp)\n"
      "sw s6,  7  * 4(sp)\n"
      "sw s7,  8  * 4(sp)\n"
      "sw s8,  9  * 4(sp)\n"
      "sw s9,  10 * 4(sp)\n"
      "sw s10, 11 * 4(sp)\n"
      "sw s11, 12 * 4(sp)\n"

      // Switch the stack pointer.
      "sw sp, (a0)\n" // *prev_sp = sp;
      "lw sp, (a1)\n" // Switch stack pointer (sp) here

      // Restore callee-saved registers from the next process's stack.
      "lw ra,  0  * 4(sp)\n" // Restore callee-saved registers only
      "lw s0,  1  * 4(sp)\n"
      "lw s1,  2  * 4(sp)\n"
      "lw s2,  3  * 4(sp)\n"
      "lw s3,  4  * 4(sp)\n"
      "lw s4,  5  * 4(sp)\n"
      "lw s5,  6  * 4(sp)\n"
      "lw s6,  7  * 4(sp)\n"
      "lw s7,  8  * 4(sp)\n"
      "lw s8,  9  * 4(sp)\n"
      "lw s9,  10 * 4(sp)\n"
      "lw s10, 11 * 4(sp)\n"
      "lw s11, 12 * 4(sp)\n"
      "addi sp, sp, 13 * 4\n" // We've popped 13 4-byte registers from the stack
      "ret\n");
}

__attribute__((naked)) extern "C" void user_entry(void) {
  __asm__ __volatile__("csrw sepc, %[sepc]        \n"
                       "csrw sstatus, %[sstatus]  \n"
                       "sret                      \n"
                       :
                       : [sepc] "r"(USER_BASE), [sstatus] "r"(SSTATUS_SPIE));
}

void yield(void) {
  if (!current_proc || !idle_proc) {
    PANIC("current_proc or idle_proc is null");
  }

  Process *next = process_next_runnable();

  // No runnable process other than the current one
  if (next == current_proc) {
    // Still need to update sepc to advance past the syscall
    WRITE_CSR(sepc, current_proc->user_pc);
    return;
  }

  __asm__ __volatile__(
      "sfence.vma\n"
      "csrw satp, %[satp]\n"
      "sfence.vma\n"
      "csrw sscratch, %[sscratch]\n"
      "csrw sepc, %[sepc]\n" // Restore the next process's PC
      :
      // Don't forget the trailing comma!
      : [satp] "r"(SATP_SV32 | ((uintptr_t)next->page_table / OT_PAGE_SIZE)),
        [sscratch] "r"((uintptr_t)&next->stack[sizeof(next->stack)]),
        [sepc] "r"(next->user_pc));

  Process *prev = current_proc;
  current_proc = next;
  switch_context(&prev->stack_ptr, &current_proc->stack_ptr);
}

extern "C" void kernel_main(void) {
  WRITE_CSR(stvec, (uintptr_t)kernel_entry);
  kernel_start();
}

__attribute__((section(".text.boot"))) __attribute__((naked)) extern "C" void
boot(void) {
  __asm__ __volatile__(
      "mv sp, %[stack_top]\n" // Set the stack pointer
      "j kernel_main\n"       // Jump to the kernel main function
      :
      : [stack_top] "r"(
          __stack_top) // Pass the stack top address as %[stack_top]
  );
}
