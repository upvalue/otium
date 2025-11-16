#include "ot/core/kernel.hpp"

extern "C" char __bss[], __bss_end[], __stack_top[];

// Forward declarations for test programs (defined in main.cpp)
void proc_hello_world(void);
void proc_mem_test(void);
void proc_alternate_a(void);
void proc_alternate_b(void);
void proc_userspace_demo(void);
extern const char mem_test_image[];
void get_process_pages(uint32_t pid, uintptr_t *pages, uint32_t *count);

// Common kernel initialization - sets up idle process
void kernel_common(void) {
#ifndef OT_ARCH_WASM
  omemset(__bss, 0, (size_t)__bss_end - (size_t)__bss);
#endif
  TRACE(LSOFT, "hello from kernel_common");

  idle_proc = process_create("idle", nullptr, nullptr, true); // kernel mode
  current_proc = idle_proc;
  TRACE(LSOFT, "created idle proc with name %s and pid %d", idle_proc->name,
        idle_proc->pid);

#if defined(OT_ARCH_RISCV) && !defined(OT_POSIX)
  // Using physical memory only (no MMU/virtual addressing)
  // This is simpler and compatible with systems without MMU (like RP2350)
  // User mode execution still provides fault isolation even without memory protection
  TRACE(LSOFT, "Physical memory mode - no MMU");
#endif
}
