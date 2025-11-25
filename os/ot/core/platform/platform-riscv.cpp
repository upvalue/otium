// riscv.cpp - risc-v and opensbi specific functionality

#include "ot/core/kernel.hpp"
#include "ot/lib/messages.hpp"
#include "ot/lib/mpack/mpack-reader.hpp"
#include "ot/user/local-storage.hpp"

#define SCAUSE_ECALL 8
#define SSTATUS_SPP (1 << 8)
#define SSTATUS_SUM (1 << 18) // Permit Supervisor User Memory access

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

#define READ_CSR(reg)                                                                                                  \
  ({                                                                                                                   \
    unsigned long __tmp;                                                                                               \
    __asm__ __volatile__("csrr %0, " #reg : "=r"(__tmp));                                                              \
    __tmp;                                                                                                             \
  })

#define WRITE_CSR(reg, value)                                                                                          \
  do {                                                                                                                 \
    uint32_t __tmp = (value);                                                                                          \
    __asm__ __volatile__("csrw " #reg ", %0" ::"r"(__tmp));                                                            \
  } while (0)

struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long fid, long eid) {
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
                       : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
                       : "memory");
  return (struct sbiret){.error = a0, .value = a1};
}

extern "C" {

int oputchar(char ch) {
  sbi_call(ch, 0, 0, 0, 0, 0, 0, 1 /* Console Putchar */);
  return 1;
}

int ogetchar() { return (int)sbi_call(0, 0, 0, 0, 0, 0, 0, 2 /* Console Putchar */).error; }

} // extern "C"

#define SBI_EXT_SRST 0x53525354 // "SRST"
#define SBI_SRST_SHUTDOWN 0
void kernel_exit(void) { sbi_call(0, 0, 0, 0, 0, 0, SBI_SRST_SHUTDOWN, SBI_EXT_SRST); }

void wfi(void) {
  for (;;) {
    __asm__ __volatile__("wfi");
  }
}

void handle_syscall(struct trap_frame *f) {
  uint32_t sysno = f->a3;
  uint32_t arg0 = f->a0;
  // Not currently used
  uint32_t arg1 = f->a1;
  // uint32_t arg2 = f->a2;

  f->a0 = 0;
  switch (sysno) {
  case OU_PUTCHAR:
    f->a0 = oputchar(arg0);
    break;
  case OU_YIELD:
    yield();
    break;
  case OU_EXIT:
    if (current_proc) {
      oprintf("Process %s (pidx=%d, pid=%lu) exited\n", current_proc->name, current_proc->pidx, current_proc->pid);
      current_proc->state = TERMINATED;
      yield(); // Switch to another process - this never returns
    }
    break;
  case OU_GETCHAR:
    f->a0 = ogetchar();
    break;
  case OU_ALLOC_PAGE: {
    TRACE(LLOUD, "OU_ALLOC_PAGE syscall");
    PageAddr result = process_alloc_mapped_page(current_proc, true, true, false);
    TRACE(LLOUD, "allocated page: %x", result.raw());
    f->a0 = result.raw();
    break;
  }
  case OU_GET_SYS_PAGE: {
    PageAddr page;
    if (arg0 == OU_SYS_PAGE_ARG) {
      page = process_get_arg_page();
    } else if (arg0 == OU_SYS_PAGE_COMM) {
      page = process_get_comm_page();
    } else if (arg0 == OU_SYS_PAGE_STORAGE) {
      page = process_get_storage_page();
    }
    f->a0 = page.raw();
    break;
  }
  case OU_IO_PUTS: {
    PageAddr comm_page = process_get_comm_page();
    if (comm_page.is_null()) {
      oprintf("Failed to get comm page\n");
      f->a0 = 0;
      break;
    }
    MsgString msg(comm_page.as<char>(), OT_PAGE_SIZE);
    StringView sv;
    MsgSerializationError error = msg.deserialize(sv);
    if (error != MSG_SERIALIZATION_OK) {
      oprintf("Failed to serialize string: %d\n", error);
      f->a0 = 0;
    }
    for (size_t i = 0; i < sv.len; i++) {
      oputchar(sv[i]);
    }
    break;
  }
  case OU_PROC_LOOKUP: {
    PageAddr comm_page = process_get_comm_page();
    if (comm_page.is_null()) {
      return;
    }

    MPackReader reader(comm_page.as<char>(), OT_PAGE_SIZE);
    StringView str;
    if (!reader.read_string(str)) {
      f->a0 = 0;
    }
    // process_lookup now returns Pid
    Pid pid = process_lookup(str);

    f->a0 = pid.raw();
    break;
  }
  case OU_IPC_SEND: {
    // RISC-V: a0=target_pid, a1=method_and_flags, a2=arg0, a4=arg1, a5=arg2
    Pid target_pid = Pid(arg0);
    uintptr_t method_and_flags = arg1;
    intptr_t arg_0 = f->a2;
    intptr_t arg_1 = f->a4;
    intptr_t arg_2 = f->a5;

    // Unpack method and flags
    intptr_t method = IPC_UNPACK_METHOD(method_and_flags);
    uintptr_t flags = IPC_UNPACK_FLAGS(method_and_flags);

    TRACE_IPC(LLOUD, "IPC send from pidx %d (pid %lu) to pid %lu, method=%d, flags=%x", current_proc->pidx.raw(),
              current_proc->pid.raw(), target_pid.raw(), method, flags);

    // Look up target by pid
    Pidx target_pidx = process_lookup_by_pid(target_pid);
    if (target_pidx == PIDX_INVALID) {
      TRACE_IPC(LSOFT, "IPC send failed: target pid %lu not found", target_pid.raw());
      f->a0 = IPC__PID_NOT_FOUND;
      f->a1 = 0;
      f->a2 = 0;
      break;
    }

    Process *target = process_lookup_by_pidx(target_pidx);

    // Handle comm page transfer if requested
    if (flags & IPC_FLAG_HAS_COMM_DATA) {
      if (!current_proc->comm_page.is_null() && !target->comm_page.is_null()) {
        TRACE_IPC(LLOUD, "IPC: copying comm page from pidx %d to pidx %d", current_proc->pidx.raw(), target_pidx.raw());
        memcpy(target->comm_page.as_ptr(), current_proc->comm_page.as_ptr(), OT_PAGE_SIZE);
      }
    }

    // Set up message
    target->pending_message.sender_pid = current_proc->pid; // Fill in sender's globally unique PID
    target->pending_message.method_and_flags = method_and_flags;
    target->pending_message.args[0] = arg_0;
    target->pending_message.args[1] = arg_1;
    target->pending_message.args[2] = arg_2;
    target->has_pending_message = true;
    target->blocked_sender = current_proc;

    TRACE_IPC(LLOUD, "IPC: switching to target process pidx %d (pid %lu)", target_pidx.raw(), target_pid.raw());

    // If target is waiting, wake it and switch to it immediately
    if (target->state == IPC_WAIT) {
      target->state = RUNNABLE;
      process_switch_to(target); // Direct context switch - receiver will process and reply
      // After this returns, we're back in our own context with our stack and trap frame valid
    } else {
      TRACE_IPC(LLOUD, "IPC: target not in IPC_WAIT, yielding normally");
      yield();
    }

    // When we get here, receiver has replied and switched back to us
    // Response is in our (sender's) pending_response
    // Our trap frame 'f' should still be valid since we're back on our own stack
    TRACE_IPC(LLOUD, "IPC send returning: error=%d, values=[%d, %d, %d]", current_proc->pending_response.error_code,
              current_proc->pending_response.values[0], current_proc->pending_response.values[1],
              current_proc->pending_response.values[2]);
    f->a0 = current_proc->pending_response.error_code;
    f->a1 = current_proc->pending_response.values[0];
    f->a2 = current_proc->pending_response.values[1];
    f->a4 = current_proc->pending_response.values[2];
    break;
  }
  case OU_IPC_RECV: {
    if (current_proc->has_pending_message) {
      TRACE_IPC(LLOUD, "Process pidx %d (pid %lu) receiving pending message from pid %lu", current_proc->pidx.raw(),
                current_proc->pid.raw(), current_proc->pending_message.sender_pid.raw());
      f->a0 = current_proc->pending_message.sender_pid.raw();
      f->a1 = current_proc->pending_message.method_and_flags;
      f->a2 = current_proc->pending_message.args[0];
      f->a4 = current_proc->pending_message.args[1];
      f->a5 = current_proc->pending_message.args[2];
      current_proc->has_pending_message = false;
    } else {
      TRACE_IPC(LLOUD, "Process pidx %d (pid %lu) entering IPC_WAIT", current_proc->pidx.raw(),
                current_proc->pid.raw());
      current_proc->state = IPC_WAIT;
      yield();
      // Will resume here when message arrives
      TRACE_IPC(LLOUD, "Process pidx %d (pid %lu) woken from IPC_WAIT with message from pid %lu",
                current_proc->pidx.raw(), current_proc->pid.raw(), current_proc->pending_message.sender_pid.raw());
      f->a0 = current_proc->pending_message.sender_pid.raw();
      f->a1 = current_proc->pending_message.method_and_flags;
      f->a2 = current_proc->pending_message.args[0];
      f->a4 = current_proc->pending_message.args[1];
      f->a5 = current_proc->pending_message.args[2];
      current_proc->has_pending_message = false;
    }
    break;
  }
  case OU_IPC_REPLY: {
    // RISC-V: a0=error_code, a1=values[0], a2=values[1], a4=values[2]
    TRACE_IPC(LLOUD, "Process pidx %d (pid %lu) replying: error=%d, values=[%d, %d, %d]", current_proc->pidx.raw(),
              current_proc->pid.raw(), arg0, arg1, f->a2, f->a4);

    if (current_proc->blocked_sender) {
      Process *sender = current_proc->blocked_sender;

      // Store response in SENDER's pending_response field (they will read it)
      sender->pending_response.error_code = (ErrorCode)arg0;
      sender->pending_response.values[0] = arg1;
      sender->pending_response.values[1] = f->a2;
      sender->pending_response.values[2] = f->a4;

      // Handle comm page transfer back to sender if IPC_FLAG_RECV_COMM_DATA was set
      uintptr_t request_flags = IPC_UNPACK_FLAGS(current_proc->pending_message.method_and_flags);
      if (request_flags & IPC_FLAG_RECV_COMM_DATA) {
        if (!current_proc->comm_page.is_null() && !sender->comm_page.is_null()) {
          TRACE_IPC(LSOFT, "IPC reply: copying comm page from server pidx %d back to client pidx %d",
                    current_proc->pidx.raw(), sender->pidx.raw());
          memcpy(sender->comm_page.as_ptr(), current_proc->comm_page.as_ptr(), OT_PAGE_SIZE);
        }
      }

      current_proc->blocked_sender = nullptr;
      TRACE_IPC(LLOUD, "IPC reply sent, immediately switching back to sender pidx %d (pid %lu)", sender->pidx,
                sender->pid);
      // Switch back to sender immediately - receiver will resume when scheduled again
      process_switch_to(sender);
      // After this returns (when we're scheduled again), continue normally
    } else {
      TRACE_IPC(LSOFT, "IPC reply called but no blocked sender");
    }
    break;
  }
  case OU_SHUTDOWN:
    oprintf("Shutdown syscall invoked by process %s (pidx=%d, pid=%lu)\n", current_proc->name, current_proc->pidx,
            current_proc->pid);
    shutdown_all_processes();
    // shutdown_all_processes calls kernel_exit() and never returns
    break;
  case OU_LOCK_KNOWN_MEMORY: {
    KnownMemory km = (KnownMemory)arg0;
    size_t page_count = arg1;
    PageAddr result = known_memory_lock(km, page_count, current_proc->pidx);
    f->a0 = result.raw();
    break;
  }
  default:
    PANIC("unexpected syscall sysno=%x\n", sysno);
  }
  // If uncommented, yields after every syscall
  // yield();
}

extern "C" void handle_trap(struct trap_frame *f) {
  uint32_t scause = READ_CSR(scause);
  uint32_t stval = READ_CSR(stval);
  uint32_t user_pc = READ_CSR(sepc);
  uint32_t sstatus = READ_CSR(sstatus);

  if (scause == SCAUSE_ECALL) {
    // Check if this is an SBI call (a7 contains extension ID) or kernel syscall (a3 contains sysno)
    // SBI calls should be forwarded to firmware, kernel syscalls handled by handle_syscall
    if (f->a7 != 0) {
      // This is an SBI call - forward to SBI firmware by making the call in supervisor mode
      struct sbiret result = sbi_call(f->a0, f->a1, f->a2, f->a3, f->a4, f->a5, f->a6, f->a7);
      f->a0 = result.error;
      f->a1 = result.value;
      // Advance past ecall instruction
      WRITE_CSR(sepc, user_pc + 4);
    } else {
      // This is a kernel syscall
      // Save the user PC before yielding (will be restored in yield)
      if (current_proc) {
        current_proc->user_pc = user_pc + 4; // Skip past the ecall instruction
      }
      handle_syscall(f);
      // update sepc to advance past syscall
      WRITE_CSR(sepc, current_proc->user_pc);
    }
  } else {
    // Check if trap came from user mode
    bool from_user = !(sstatus & SSTATUS_SPP);

    if (from_user && current_proc) {
      oprintf("Process %s (pidx=%d, pid=%lu) crashed: scause=%x, stval=%x, sepc=%x\n", current_proc->name,
              current_proc->pidx, current_proc->pid, scause, stval, user_pc);
      current_proc->state = TERMINATED;
      yield();
    } else {
      PANIC("unexpected trap in kernel stval=%x, sepc=%x\n", scause, stval, user_pc);
    }
  }
}

__attribute__((naked)) __attribute__((aligned(4))) extern "C" void kernel_entry(void) {
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

__attribute__((naked)) extern "C" void switch_context(uint32_t *prev_sp, uint32_t *next_sp) {
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
      "addi sp, sp, 13 * 4\n" // We've popped 13 4-byte registers from the
                              // stack
      "ret\n");
}

extern "C" void user_entry(void) {
  // Simple user mode entry - physical addressing only
  // Switch to user-mode stack and drop to user privilege level
  uint32_t status = READ_CSR(sstatus);
  status &= ~SSTATUS_SPP; // Clear SPP to enter user mode
  status |= SSTATUS_SPIE; // Set SPIE to enable interrupts after sret

  // Get user stack pointer (top of user stack page) - physical address
  uintptr_t user_sp = current_proc->user_stack.raw() + OT_PAGE_SIZE;

  TRACE_PROC(LLOUD, "user_entry: sepc=%x, user_sp=%x, sstatus=%x", READ_CSR(sepc), user_sp, status);

  __asm__ __volatile__("mv sp, %[user_sp]\n" // Switch to user stack
                       "csrw sstatus, %[sstatus]\n"
                       "sret\n"
                       :
                       : [user_sp] "r"(user_sp), [sstatus] "r"(status)
                       : "sp");
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

  TRACE_PROC(LLOUD, "switching to process %s (pidx=%d, pid=%lu)", next->name, next->pidx, next->pid);

  // Use process_switch_to for all context switching - it handles:
  // - Updating current_proc
  // - Updating local_storage
  // - Setting sscratch and sepc registers
  // - Calling switch_context
  process_switch_to(next);

  TRACE_PROC(LLOUD, "returned from switch_context, current=%s", current_proc->name);
}

extern "C" void kernel_main(void) {
  WRITE_CSR(stvec, (uintptr_t)kernel_entry);
  // Physical addressing only - no need for SUM bit or page table setup
  kernel_start();
}

__attribute__((section(".text.boot"))) __attribute__((naked)) extern "C" void boot(void) {
  __asm__ __volatile__("mv sp, %[stack_top]\n" // Set the stack pointer
                       "j kernel_main\n"       // Jump to the kernel main function
                       :
                       : [stack_top] "r"(__stack_top) // Pass the stack top address as %[stack_top]
  );
}
