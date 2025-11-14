#include "ot/core/kernel.hpp"

extern "C" char __bss[], __bss_end[], __stack_top[];

// Forward declarations for test programs (defined in kernel-prog.cpp)
extern "C" void proc_hello_world(void);
extern "C" void proc_mem_test(void);
extern "C" void proc_alternate_a(void);
extern "C" void proc_alternate_b(void);
extern const char mem_test_image[];
void get_process_pages(uint32_t pid, uintptr_t *pages, uint32_t *count);

// Common kernel initialization - sets up idle process
void kernel_common(void) {
#ifndef OT_ARCH_WASM
  omemset(__bss, 0, (size_t)__bss_end - (size_t)__bss);
#endif
  TRACE(LSOFT, "hello from kernel_common");

  idle_proc = process_create("idle", nullptr, nullptr);
  current_proc = idle_proc;
  TRACE(LSOFT, "created idle proc with name %s and pid %d", idle_proc->name,
        idle_proc->pid);

#if defined(OT_ARCH_RISCV) && !defined(OT_POSIX)
  // TODO: Enable MMU
  // Problem: Kernel code is mapped with PAGE_U so user mode can access it
  // But supervisor mode can't fetch instructions from PAGE_U pages (even with SUM)
  // Solutions to explore:
  // 1. Compile kernel as position-independent code
  // 2. Use a small trampoline section without PAGE_U for MMU switching
  // 3. Map kernel code at non-PAGE_U address for supervisor, PAGE_U address for user
  //    (dual mapping - partially implemented above)
  // For now: MMU disabled, physical addressing, no process isolation but user mode still works
  TRACE(LSOFT, "MMU disabled - TODO: solve PAGE_U instruction fetch issue");
#endif
}
